/*
 * Sensor gateway - main process (Step 7: integration and controlled shutdown)
 *
 *   ./server <base port P> <HTTP port>
 *
 * Signals: SIGINT/SIGTERM are BLOCKED in every thread and received only by
 * main through sigtimedwait(). No signal handler, so no shared variable is
 * written asynchronously (clean for helgrind).
 *
 * Stop order: join connmgr (no more writer) -> wake readers -> join them
 *             -> free sbuffer -> close FIFO -> waitpid log process
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "connmgr.h"
#include "datamgr.h"
#include "log_process.h"
#include "logger.h"
#include "sbuffer.h"
#include "storagemgr.h"

#define MAIN_POLL_MS 200

static int parse_port(const char *text, int *port)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value <= 0 || value > 65535)
    {
        return -1;
    }
    *port = (int)value;
    return 0;
}

/* Fork the log process. Done before any thread exists. */
static pid_t start_log_process(void)
{
    pid_t pid;

    if (mkfifo(LOG_FIFO_NAME, 0666) != 0 && errno != EEXIST)
    {
        perror("mkfifo " LOG_FIFO_NAME);
        return -1;
    }

    fflush(stdout);
    fflush(stderr);

    pid = fork();
    if (pid == 0)
    {
        run_log_process();
        _exit(EXIT_SUCCESS);
    }
    if (pid < 0)
    {
        perror("fork");
    }
    return pid;
}

/* Wait for SIGINT/SIGTERM or a fatal error in a thread (no busy loop).
 * Return the signal number, or 0 if a thread asked to stop. */
static int wait_for_stop(const sigset_t *signals)
{
    struct timespec timeout = { 0, MAIN_POLL_MS * 1000000L };

    while (!gateway_stopping())
    {
        int sig = sigtimedwait(signals, NULL, &timeout);

        if (sig > 0)
        {
            gateway_request_stop(sig == SIGINT ? "received SIGINT"
                                               : "received SIGTERM");
            return sig;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    pthread_t conn_tid;
    pthread_t data_tid;
    pthread_t stor_tid;
    bool conn_ok = false;
    bool data_ok = false;
    bool stor_ok = false;
    sbuffer_t *buffer = NULL;
    sigset_t signals;
    pid_t log_pid;
    char msg[160];
    const char *reason;
    int status = EXIT_SUCCESS;
    int sig;

    if (argc != 3 ||
        parse_port(argv[1], &g_gateway.gateway_port) != 0 ||
        parse_port(argv[2], &g_gateway.http_port) != 0 ||
        g_gateway.gateway_port > 65535 - 3)
    {
        fprintf(stderr, "Usage: %s <base port P (1..65532)> <HTTP port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Block SIGINT/SIGTERM BEFORE creating any thread or process:
       every thread inherits the mask, only main receives them. */
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, NULL);

    /* writing to a closed socket / FIFO must return EPIPE, not kill us */
    signal(SIGPIPE, SIG_IGN);

    /* 1. Log process + FIFO */
    log_pid = start_log_process();
    if (log_pid < 0)
    {
        return EXIT_FAILURE;
    }

    if (logger_init() != 0)
    {
        kill(log_pid, SIGKILL);
        waitpid(log_pid, NULL, 0);
        unlink(LOG_FIFO_NAME);
        return EXIT_FAILURE;
    }

    snprintf(msg, sizeof(msg), "Gateway started (TCP base port %d, HTTP port %d)",
             g_gateway.gateway_port, g_gateway.http_port);
    logger_log("MAIN", msg);

    printf("=====================================\n");
    printf("Sensor Gateway Started (PID %d)\n", (int)getpid());
    printf("TBP %d | HLP %d | EVP %d | SGP %d | HTTP %d\n",
           g_gateway.gateway_port, g_gateway.gateway_port + 1,
           g_gateway.gateway_port + 2, g_gateway.gateway_port + 3,
           g_gateway.http_port);
    printf("=====================================\n");
    fflush(stdout);

    /* 2. sbuffer + 3 threads (from here on, use console_printf only) */
    if (sbuffer_init(&buffer) != SBUFFER_SUCCESS)
    {
        gateway_request_stop("cannot create sbuffer");
    }
    else
    {
        conn_ok = pthread_create(&conn_tid, NULL, connmgr_thread, buffer) == 0;
        data_ok = pthread_create(&data_tid, NULL, datamgr_thread, buffer) == 0;
        stor_ok = pthread_create(&stor_tid, NULL, storagemgr_thread, buffer) == 0;

        if (!conn_ok || !data_ok || !stor_ok)
        {
            gateway_request_stop("cannot create threads");
        }
    }

    /* 3. Run until a signal or a fatal error */
    sig = wait_for_stop(&signals);
    if (sig == 0)
    {
        status = EXIT_FAILURE;      /* stopped because of an error */
    }

    reason = gateway_stop_reason();
    snprintf(msg, sizeof(msg), "Gateway shutting down (%s)",
             reason != NULL ? reason : "unknown reason");
    logger_log("MAIN", msg);
    console_printf("\n%s\n", msg);

    /* 4. Stop the writer first: after this join, nobody inserts any more */
    if (conn_ok)
    {
        pthread_join(conn_tid, NULL);
    }

    /* 5. Wake the readers; they drain what is left, then return */
    if (buffer != NULL)
    {
        sbuffer_shutdown(buffer);
    }
    if (data_ok)
    {
        pthread_join(data_tid, NULL);
    }
    if (stor_ok)
    {
        pthread_join(stor_tid, NULL);
    }

    /* All threads are joined: plain printf is safe again */
    if (buffer != NULL)
    {
        size_t left = sbuffer_size(buffer);

        if (left > 0)
        {
            printf("%zu measurement(s) not stored were discarded\n", left);
        }
        sbuffer_free(&buffer);
    }

    /* 6. Close the FIFO: the log process reads EOF, writes the last lines
          and exits. waitpid() so that no zombie is left. */
    logger_close();

    if (waitpid(log_pid, NULL, 0) < 0)
    {
        perror("waitpid");
    }
    unlink(LOG_FIFO_NAME);

    printf("Gateway terminated\n");
    return status;
}
