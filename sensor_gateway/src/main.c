#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "logger.h"
#include "log_process.h"

#include "common.h"
#include "connmgr.h"
#include "datamgr.h"
#include "storagemgr.h"
#include "sbuffer.h"

gateway_t g_gateway = {0};

static void signal_handler(int sig)
{
    (void)sig;

    g_gateway.shutdown = 1;
}

int main(int argc, char *argv[])
{
    pthread_t conn_tid;
    pthread_t data_tid;
    pthread_t store_tid;

    if (argc != 3)
    {
        fprintf(stderr,
                "Usage: %s <gateway_port> <http_port>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    g_gateway.gateway_port = atoi(argv[1]);
    g_gateway.http_port    = atoi(argv[2]);

    if (g_gateway.gateway_port <= 0 ||
        g_gateway.http_port <= 0)
    {
        fprintf(stderr, "Invalid port number\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    logger_log("MAIN", "Gateway started");
    printf("=====================================\n");
    printf("Sensor Gateway Started\n");
    printf("Gateway Port : %d\n", g_gateway.gateway_port);
    printf("HTTP Port    : %d\n", g_gateway.http_port);
    printf("PID          : %d\n", getpid());
    printf("=====================================\n");

    if (access("logFifo", F_OK) != 0)
    {
        mkfifo("logFifo", 0666);
    }

    pid_t log_pid = fork();

    if (log_pid < 0)
    {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (log_pid == 0)
    {
        run_log_process();
        return 0;
    }

    if (logger_init() != 0)
    {
        return EXIT_FAILURE;
    }

    sbuffer_t *buffer = NULL;  
    if (sbuffer_init(&buffer) != SBUFFER_SUCCESS)
    {
        fprintf(stderr, "Failed to create sbuffer\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&conn_tid,
                       NULL,
                       connmgr_thread,
                       buffer) != 0)
    {
        perror("pthread_create connmgr");
        return EXIT_FAILURE;
    }

    if (pthread_create(&data_tid,
                       NULL,
                       datamgr_thread,
                       buffer) != 0)
    {
        perror("pthread_create datamgr");
        return EXIT_FAILURE;
    }

    if (pthread_create(&store_tid,
                       NULL,
                       storagemgr_thread,
                       buffer) != 0)
    {
        perror("pthread_create storagemgr");
        return EXIT_FAILURE;
    }

    while (!g_gateway.shutdown)
    {          
        sleep(1);
    }

    sbuffer_shutdown(buffer);

    printf("\nShutdown signal received...\n");

    pthread_join(conn_tid, NULL);
    pthread_join(data_tid, NULL);
    pthread_join(store_tid, NULL);

    printf("All threads stopped\n");
    printf("Gateway terminated\n");
    logger_log("MAIN", "Gateway stopped");
    logger_close();

    return EXIT_SUCCESS;
}