#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "common.h"
#include "connmgr.h"
#include "logger.h"
#include "sbuffer.h"
#include "sensor_types.h"

#include "protocol_adapter.h"
#include "tbp_adapter.h"
#include "hlp_adapter.h"
#include "evp_adapter.h"
#include "sgp_adapter.h"

#define RX_BUF_SIZE             256   /* per connection (section 3.9)       */
#define MAX_CONSECUTIVE_ERRORS  5     /* close connection after this many   */
#define SELECT_TIMEOUT_SEC      1     /* re-check shutdown flag every 1 s   */

/* ADAPTERS table (section 3.5): add a new protocol = add one line here */
static const protocol_adapter_t *const ADAPTERS[] =
{
    &g_tbp_adapter,     /* P     */
    &g_hlp_adapter,     /* P + 1 */
    &g_evp_adapter,     /* P + 2 */
    &g_sgp_adapter      /* P + 3 */
};

#define ADAPTER_COUNT (sizeof(ADAPTERS) / sizeof(ADAPTERS[0]))

typedef struct
{
    int port;
    sbuffer_t *buffer;
    const protocol_adapter_t *adapter;
} listener_t;

typedef struct
{
    int fd;
    const protocol_adapter_t *adapter;
    sbuffer_t *buffer;

    uint8_t rx[RX_BUF_SIZE];          /* receive buffer of this connection */
    size_t rx_len;

    bool identified;                  /* sensor ID known                   */
    sensor_id_t sensor_id;

    int consecutive_errors;

    bool has_last;                    /* SGP duplicate detection           */
    sensor_id_t last_id;
    time_t last_ts;
    double last_value;
} connection_t;

static pthread_mutex_t g_seq_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_seq = 0;

static pthread_mutex_t g_conn_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_conn_cond = PTHREAD_COND_INITIALIZER;
static int g_conn_count = 0;

static uint64_t next_seq(void)
{
    uint64_t seq;

    pthread_mutex_lock(&g_seq_mutex);
    seq = ++g_seq;
    pthread_mutex_unlock(&g_seq_mutex);

    return seq;
}

static void log_sensor(const char *format, sensor_id_t id)
{
    char msg[160];

    snprintf(msg, sizeof(msg), format, (unsigned)id);
    logger_log("CONN", msg);
}

static void log_port(const char *format, const char *protocol,
                     const char *reason)
{
    char msg[256];

    snprintf(msg, sizeof(msg), format, protocol, reason);
    logger_log("CONN", msg);
}

/* Map a parse error to one of the fixed <reason> values of section 3.8 */
static const char *error_reason(const connection_t *c)
{
    if (c->adapter == &g_sgp_adapter &&
        c->rx_len >= SGP_FRAME_SIZE &&
        c->rx[0] == SGP_MAGIC)
    {
        uint16_t crc_rx = (uint16_t)(((uint16_t)c->rx[9] << 8) | c->rx[10]);

        if (crc16_ccitt_false(c->rx + 1, 8) != crc_rx)
        {
            return "bad CRC";
        }
    }

    return "bad length";
}

static void consume(connection_t *c, size_t n)
{
    if (n == 0)
    {
        n = 1;                      /* always make progress */
    }
    if (n > c->rx_len)
    {
        n = c->rx_len;
    }

    memmove(c->rx, c->rx + n, c->rx_len - n);
    c->rx_len -= n;
}

static void send_reply(const connection_t *c, parse_result_t result)
{
    uint8_t reply[8];
    size_t len = c->adapter->build_reply(result, reply, sizeof(reply));

    if (len > 0)
    {
        send(c->fd, reply, len, MSG_NOSIGNAL);
    }
}

/* SGP retransmission: a resent frame is identical to the previous one
 * (same id, ts and ppm). Policy: ignore the duplicate, but still ACK it so
 * the sensor stops resending. ppm is compared too because ts has 1 s
 * resolution: two real measurements in the same second are not duplicates. */
static bool is_duplicate(connection_t *c, const sensor_data_t *data)
{
    if (c->adapter != &g_sgp_adapter)
    {
        return false;
    }

    if (c->has_last &&
        c->last_id == data->sensor_id &&
        c->last_ts == data->timestamp &&
        c->last_value == data->value)
    {
        return true;
    }

    c->has_last = true;
    c->last_id = data->sensor_id;
    c->last_ts = data->timestamp;
    c->last_value = data->value;

    return false;
}

/* Parse all complete frames in rx. Return -1 if the connection must close. */
static int process_rx(connection_t *c)
{
    for (;;)
    {
        sensor_data_t data;
        size_t consumed = 0;
        parse_result_t result;

        if (c->rx_len == 0)
        {
            return 0;
        }

        memset(&data, 0, sizeof(data));
        result = c->adapter->parse(c->rx, c->rx_len, &data, &consumed);

        if (result == PARSE_NEED_MORE)
        {
            return 0;
        }

        if (result == PARSE_OK)
        {
            c->consecutive_errors = 0;

            if (!c->identified)
            {
                c->identified = true;
                c->sensor_id = data.sensor_id;
                log_sensor("A sensor node with %u has opened a new connection",
                           c->sensor_id);
            }

            if (!is_duplicate(c, &data))
            {
                data.seq = next_seq();
                data.protocol = c->adapter->name;

                /* sbuffer lock is held only inside sbuffer_insert() */
                sbuffer_insert(c->buffer, &data);
            }

            consume(c, consumed);
            send_reply(c, PARSE_OK);
            continue;
        }

        /* PARSE_ERROR */
        send_reply(c, PARSE_ERROR);

        if (!c->identified)
        {
            /* First frame is broken: sensor ID unknown, reject the node */
            log_port("Connection attempt failed on %s port (%s)",
                     c->adapter->name, "malformed first frame");
            return -1;
        }

        log_port("Received malformed frame on %s port (%s)",
                 c->adapter->name, error_reason(c));

        consume(c, consumed);

        c->consecutive_errors++;
        if (c->consecutive_errors >= MAX_CONSECUTIVE_ERRORS)
        {
            return -1;
        }
    }
}

static void *connection_thread(void *arg)
{
    connection_t *c = (connection_t *)arg;

    while (!gateway_stopping())
    {
        fd_set rfds;
        struct timeval tv;
        ssize_t n;
        int ready;

        FD_ZERO(&rfds);
        FD_SET(c->fd, &rfds);
        tv.tv_sec = SELECT_TIMEOUT_SEC;
        tv.tv_usec = 0;

        ready = select(c->fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (ready == 0)
        {
            continue;               /* timeout: re-check shutdown */
        }

        n = recv(c->fd, c->rx + c->rx_len, sizeof(c->rx) - c->rx_len, 0);
        if (n == 0)
        {
            break;                  /* peer closed */
        }
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            break;
        }

        c->rx_len += (size_t)n;

        if (process_rx(c) != 0)
        {
            break;
        }

        if (c->rx_len == sizeof(c->rx))
        {
            /* Cannot happen with the 4 protocols (max frame 64 B) */
            log_port("Received malformed frame on %s port (%s)",
                     c->adapter->name, "bad length");
            break;
        }
    }

    if (c->identified)
    {
        log_sensor("The sensor node with %u has closed the connection",
                   c->sensor_id);
    }

    close(c->fd);
    free(c);

    pthread_mutex_lock(&g_conn_mutex);
    g_conn_count--;
    pthread_cond_broadcast(&g_conn_cond);
    pthread_mutex_unlock(&g_conn_mutex);

    return NULL;
}

static int open_listen_socket(const listener_t *l)
{
    struct sockaddr_in addr;
    int opt = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)l->port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, SOMAXCONN) < 0)
    {
        log_port("Connection attempt failed on %s port (%s)",
                 l->adapter->name, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void *listener_thread(void *arg)
{
    listener_t *l = (listener_t *)arg;
    int server_fd = open_listen_socket(l);

    if (server_fd < 0)
    {
        return NULL;
    }

    console_printf("[CONNMGR] %s listening on port %d\n", l->adapter->name, l->port);

    while (!gateway_stopping())
    {
        fd_set rfds;
        struct timeval tv;
        connection_t *c;
        pthread_t tid;
        int client_fd;

        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        tv.tv_sec = SELECT_TIMEOUT_SEC;
        tv.tv_usec = 0;

        if (select(server_fd + 1, &rfds, NULL, NULL, &tv) <= 0)
        {
            continue;               /* timeout or EINTR */
        }

        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
        {
            log_port("Connection attempt failed on %s port (%s)",
                     l->adapter->name, "accept error");
            continue;
        }

        c = calloc(1, sizeof(*c));
        if (c == NULL)
        {
            close(client_fd);
            continue;
        }

        c->fd = client_fd;
        c->adapter = l->adapter;
        c->buffer = l->buffer;

        pthread_mutex_lock(&g_conn_mutex);
        g_conn_count++;
        pthread_mutex_unlock(&g_conn_mutex);

        if (pthread_create(&tid, NULL, connection_thread, c) != 0)
        {
            pthread_mutex_lock(&g_conn_mutex);
            g_conn_count--;
            pthread_mutex_unlock(&g_conn_mutex);

            close(client_fd);
            free(c);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);
    return NULL;
}

void *connmgr_thread(void *arg)
{
    sbuffer_t *buffer = (sbuffer_t *)arg;
    listener_t listeners[ADAPTER_COUNT];
    pthread_t tids[ADAPTER_COUNT];
    bool created[ADAPTER_COUNT];

    logger_log("CONN", "Thread started");

    for (size_t i = 0; i < ADAPTER_COUNT; i++)
    {
        listeners[i].port = g_gateway.gateway_port + ADAPTERS[i]->port_offset;
        listeners[i].buffer = buffer;
        listeners[i].adapter = ADAPTERS[i];

        created[i] = (pthread_create(&tids[i], NULL, listener_thread,
                                     &listeners[i]) == 0);
    }

    for (size_t i = 0; i < ADAPTER_COUNT; i++)
    {
        if (created[i])
        {
            pthread_join(tids[i], NULL);
        }
    }

    /* Wait for all connection threads: nobody writes to sbuffer after this */
    pthread_mutex_lock(&g_conn_mutex);
    while (g_conn_count > 0)
    {
        pthread_cond_wait(&g_conn_cond, &g_conn_mutex);
    }
    pthread_mutex_unlock(&g_conn_mutex);

    console_printf("[CONNMGR] Exit\n");
    logger_log("CONN", "Thread stopped");

    return NULL;
}
