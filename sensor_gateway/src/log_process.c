#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#define LOG_FIFO "logFifo"
#define LOG_FILE "gateway.log"

void run_log_process(void)
{
    int fifo_fd;

    char buffer[1024];

    if (access(LOG_FIFO, F_OK) != 0)
    {
        mkfifo(LOG_FIFO, 0666);
    }

    fifo_fd = open(LOG_FIFO, O_RDONLY);

    if (fifo_fd < 0)
    {
        perror("open fifo");
        return;
    }

    FILE *fp = fopen(LOG_FILE, "a");

    if (fp == NULL)
    {
        perror("gateway.log");
        close(fifo_fd);
        return;
    }

    unsigned long sequence = 1;

    while (1)
    {
        ssize_t bytes =
            read(fifo_fd,
                 buffer,
                 sizeof(buffer) - 1);

        if (bytes <= 0)
        {
            continue;
        }

        buffer[bytes] = '\0';

        char *saveptr = NULL;
        char *line = strtok_r(buffer, "\n", &saveptr);

        while (line != NULL)
        {
            fprintf(fp,
                    "%lu %s\n",
                    sequence++,
                    line);

            fflush(fp);

            line = strtok_r(NULL,
                            "\n",
                            &saveptr);
        }
    }
}