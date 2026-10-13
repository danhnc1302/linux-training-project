#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "common.h"
#include "connmgr.h"
#include "logger.h"
#include "sensor_types.h"
#include "sbuffer.h"

static void *client_thread(void *arg)
{
    client_ctx_t *ctx = (client_ctx_t *)arg;

    int client_fd = ctx->client_fd;
    sbuffer_t *buffer = ctx->buffer;

    tbp_frame_t frame;
    
    uint16_t sensor_id = 0;
    int first_frame = 1;

    while (!g_gateway.shutdown)
    {
        ssize_t bytes =
            recv(client_fd,
                 &frame,
                 sizeof(frame),
                 MSG_WAITALL);

        if (bytes == 0)
        {
            break;
        }

        if (bytes < 0)
        {
            perror("recv");
            break;
        }

        sensor_data_t data;

        data.seq = ++g_seq;
        data.sensor_id = frame.sensor_id;
        data.value = frame.value;
        data.timestamp = frame.timestamp;

        if (first_frame)
        {
            sensor_id = frame.sensor_id;

            char msg[128];

            snprintf(msg,
                     sizeof(msg),
                     "A sensor node with %u has opened a new connection",
                     sensor_id);

            logger_log("CONN", msg);

            first_frame = 0;
        }

        sbuffer_insert(buffer, &data);
    }

    if (sensor_id != 0)
    {
        char msg[128];

        snprintf(msg,
                 sizeof(msg),
                 "The sensor node with %u has closed the connection",
                 sensor_id);

        logger_log("CONN", msg);
    }

    close(client_fd);
    free(ctx);

    return NULL;
}

void *connmgr_thread(void *arg)
{
    sbuffer_t *buffer = (sbuffer_t *)arg;
    struct timeval tv;
    int server_fd;
    int client_fd;

    struct sockaddr_in addr;

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    logger_log("CONN", "Thread started");

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    setsockopt(server_fd,
           SOL_SOCKET,
           SO_RCVTIMEO,
           &tv,
           sizeof(tv));

    if (server_fd < 0)
    {
        perror("socket");
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_gateway.gateway_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd,
             (struct sockaddr *)&addr,
             sizeof(addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        close(server_fd);
        return NULL;
    }

    printf("[CONNMGR] Listening on port %d\n",
           g_gateway.gateway_port);

    while (!g_gateway.shutdown)
    {
        client_fd =
            accept(server_fd,
                NULL,
                NULL);

        if (client_fd < 0)
        {
            if (g_gateway.shutdown)
            {
                break;
            }

            continue;
        }

        printf("[CONNMGR] Client connected\n");

        client_ctx_t *ctx =
            malloc(sizeof(client_ctx_t));

        if (ctx == NULL)
        {
            close(client_fd);
            continue;
        }

        ctx->client_fd = client_fd;
        ctx->buffer = buffer;

        pthread_t tid;

        if (pthread_create(&tid,
                        NULL,
                        client_thread,
                        ctx) != 0)
        {
            perror("pthread_create");

            close(client_fd);
            free(ctx);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);

    printf("[CONNMGR] Exit\n");

    logger_log("CONN", "Thread stopped");

    return NULL;
}