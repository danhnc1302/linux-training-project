#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "logger.h"

#define LOG_FIFO "logFifo"

static int g_fifo_fd = -1;

static pthread_mutex_t g_log_mutex =
    PTHREAD_MUTEX_INITIALIZER;

int logger_init(void)
{
    g_fifo_fd = open(LOG_FIFO, O_WRONLY);

    if (g_fifo_fd < 0)
    {
        perror("open logFifo");
        return -1;
    }

    return 0;
}

int logger_log(const char *sender,
               const char *message)
{
    char buffer[512];

    pthread_mutex_lock(&g_log_mutex);

    snprintf(buffer,
             sizeof(buffer),
             "%s|%s\n",
             sender,
             message);

    write(g_fifo_fd,
          buffer,
          strlen(buffer));

    pthread_mutex_unlock(&g_log_mutex);

    return 0;
}

void logger_close(void)
{
    if (g_fifo_fd >= 0)
    {
        close(g_fifo_fd);
    }
}