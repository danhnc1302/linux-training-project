#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "log_process.h"
#include "logger.h"

static void write_entry(FILE *log, unsigned long seq, const char *line)
{
    char when[32];
    time_t now = time(NULL);
    struct tm tm;

    localtime_r(&now, &tm);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(log, "%lu %s %s\n", seq, when, line);
    fflush(log);                    /* visible right away (web reads it) */
}

void run_log_process(void)
{
    char buf[4096];
    char line[512];
    size_t line_len = 0;
    unsigned long seq = 0;
    FILE *log;
    int fd;

    /* Ctrl+C reaches the whole process group: the log process ignores it
       and stops only when the main process closes the FIFO (EOF), so the
       last log-events ("Gateway shutting down ...") are not lost. */
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    log = fopen(LOG_FILE_NAME, "w");
    if (log == NULL)
    {
        perror("fopen " LOG_FILE_NAME);
        return;
    }

    /* The FIFO may not exist yet if this process starts first */
    if (mkfifo(LOG_FIFO_NAME, 0666) != 0 && access(LOG_FIFO_NAME, F_OK) != 0)
    {
        perror("mkfifo " LOG_FIFO_NAME);
        fclose(log);
        return;
    }

    fd = open(LOG_FIFO_NAME, O_RDONLY);     /* blocks until a writer opens */
    if (fd < 0)
    {
        perror("open " LOG_FIFO_NAME);
        fclose(log);
        return;
    }

    for (;;)
    {
        ssize_t n = read(fd, buf, sizeof(buf));   /* blocked, no CPU used */

        if (n == 0)
        {
            break;                  /* EOF: every writer closed the FIFO */
        }
        if (n < 0)
        {
            continue;               /* EINTR */
        }

        for (ssize_t i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                line[line_len] = '\0';
                if (line_len > 0)
                {
                    write_entry(log, ++seq, line);
                }
                line_len = 0;
            }
            else if (line_len < sizeof(line) - 1)
            {
                line[line_len++] = buf[i];
            }
        }
    }

    close(fd);
    fclose(log);
}
