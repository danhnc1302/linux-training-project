#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "logger.h"

/* Own mutex, never taken together with the sbuffer lock (no lock cycle) */
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_fifo_fd = -1;

int logger_init(void)
{
    int fd = open(LOG_FIFO_NAME, O_WRONLY);   /* waits for the reader */

    if (fd < 0)
    {
        perror("open " LOG_FIFO_NAME);
        return -1;
    }

    pthread_mutex_lock(&g_log_mutex);
    g_fifo_fd = fd;
    pthread_mutex_unlock(&g_log_mutex);

    return 0;
}

void logger_log(const char *source, const char *message)
{
    char line[16 + LOG_MSG_MAX + 2];
    int len;

    _Static_assert(sizeof(line) <= PIPE_BUF, "log line must fit in PIPE_BUF");

    len = snprintf(line, sizeof(line), "%s %.*s", source, LOG_MSG_MAX, message);
    if (len < 0)
    {
        return;
    }
    if ((size_t)len > sizeof(line) - 2)
    {
        len = (int)sizeof(line) - 2;
    }

    /* one log-event = one line: replace any newline inside the message */
    for (int i = 0; i < len; i++)
    {
        if (line[i] == '\n' || line[i] == '\r')
        {
            line[i] = ' ';
        }
    }
    line[len++] = '\n';

    pthread_mutex_lock(&g_log_mutex);
    if (g_fifo_fd >= 0)
    {
        ssize_t n;

        do
        {
            n = write(g_fifo_fd, line, (size_t)len);   /* single write() */
        } while (n < 0 && errno == EINTR);
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_close(void)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_fifo_fd >= 0)
    {
        close(g_fifo_fd);
        g_fifo_fd = -1;
    }
    pthread_mutex_unlock(&g_log_mutex);
}
