/*
 * sensor_node - sensor simulator (section 3.7)
 *
 * ./sensor_node <type> <id> <host> <base port P> <period ms> [options]
 *   type:    temperature | humidity | motion | door | smoke_gas
 *   options: --fragment  split each frame into several send() with pauses
 *            --glue      put several frames into one send()
 *            --corrupt   sometimes send a broken frame
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#define MAX_FRAME       80
#define GLUE_COUNT      3
#define CORRUPT_RATE    5       /* 1 broken frame out of 5 on average */
#define SGP_ACK         0x06
#define SGP_NAK         0x15
#define SGP_TIMEOUT_MS  1000
#define SGP_MAX_RETRY   3

typedef enum { K_TEMP, K_HUM, K_MOTION, K_DOOR, K_GAS } kind_t;

static const struct
{
    const char *name;
    int port_offset;
} KINDS[] =
{
    [K_TEMP]   = { "temperature", 0 },
    [K_HUM]    = { "humidity",    1 },
    [K_MOTION] = { "motion",      2 },
    [K_DOOR]   = { "door",        2 },
    [K_GAS]    = { "smoke_gas",   3 },
};

typedef struct
{
    uint8_t bytes[MAX_FRAME];
    size_t len;
} frame_t;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* ---------- byte helpers ---------- */

static void put_le16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
}

static void put_le64(uint8_t *b, uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        b[i] = (uint8_t)(v >> (8 * i));
    }
}

static void put_be16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)v;
}

static void put_be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)(data[i] << 8);
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }

    return crc;
}

static double rand_range(double lo, double hi)
{
    return lo + (hi - lo) * ((double)rand() / RAND_MAX);
}

static void now_str(char *buf, size_t cap)
{
    time_t t = time(NULL);
    struct tm tm;

    localtime_r(&t, &tm);
    strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tm);
}

/* ---------- frame builders (section 3.4) ---------- */

static void build_tbp(frame_t *f, uint16_t id, double value, int64_t ts)
{
    uint64_t bits;

    memcpy(&bits, &value, sizeof(bits));
    put_le16(f->bytes, id);
    put_le64(f->bytes + 2, bits);
    put_le64(f->bytes + 10, (uint64_t)ts);
    f->len = 18;
}

static void build_hlp(frame_t *f, uint16_t id, double value, int64_t ts)
{
    int n = snprintf((char *)f->bytes, sizeof(f->bytes), "HUM %u %.1f %lld\n",
                     id, value, (long long)ts);
    f->len = (size_t)n;
}

static void build_evp_motion(frame_t *f, uint16_t id, uint32_t ts)
{
    f->bytes[0] = 0x01;
    f->bytes[1] = 6;
    put_be16(f->bytes + 2, id);
    put_be32(f->bytes + 4, ts);
    f->len = 8;
}

static void build_evp_door(frame_t *f, uint16_t id, uint8_t state, uint32_t ts)
{
    f->bytes[0] = 0x02;
    f->bytes[1] = 7;
    put_be16(f->bytes + 2, id);
    f->bytes[4] = state;
    put_be32(f->bytes + 5, ts);
    f->len = 9;
}

static void build_sgp(frame_t *f, uint16_t id, uint16_t ppm, uint32_t ts)
{
    f->bytes[0] = 0x7E;
    put_be16(f->bytes + 1, id);
    put_be16(f->bytes + 3, ppm);
    put_be32(f->bytes + 5, ts);
    put_be16(f->bytes + 9, crc16_ccitt_false(f->bytes + 1, 8));
    f->len = 11;
}

/* ---------- simulated sensor state ---------- */

typedef struct
{
    kind_t kind;
    uint16_t id;
    double value;
    uint8_t door_state;
} sensor_t;

/* Build one valid frame and advance the simulated value */
static void next_frame(sensor_t *s, frame_t *f)
{
    time_t ts = time(NULL);

    switch (s->kind)
    {
    case K_TEMP:
        s->value += rand_range(-0.8, 0.8);
        if (s->value < 10.0) s->value = 10.0;
        if (s->value > 26.0) s->value = 26.0;
        build_tbp(f, s->id, s->value, (int64_t)ts);
        break;

    case K_HUM:
        s->value += rand_range(-3.0, 3.0);
        if (s->value < 20.0) s->value = 20.0;
        if (s->value > 80.0) s->value = 80.0;
        build_hlp(f, s->id, s->value, (int64_t)ts);
        break;

    case K_MOTION:
        build_evp_motion(f, s->id, (uint32_t)ts);
        break;

    case K_DOOR:
        s->door_state ^= 1U;            /* only send on state change */
        build_evp_door(f, s->id, s->door_state, (uint32_t)ts);
        break;

    case K_GAS:
        s->value += rand_range(-40.0, 40.0);
        if (s->value < 0.0)   s->value = 0.0;
        if (s->value > 600.0) s->value = 600.0;
        build_sgp(f, s->id, (uint16_t)s->value, (uint32_t)ts);
        break;
    }
}

/* Turn a valid frame into a broken one that the parser must reject */
static void corrupt_frame(const sensor_t *s, frame_t *f)
{
    switch (s->kind)
    {
    case K_TEMP:                        /* value out of range */
    {
        double bad = 999.0;
        uint64_t bits;

        memcpy(&bits, &bad, sizeof(bits));
        put_le64(f->bytes + 2, bits);
        break;
    }

    case K_HUM:
        if (rand() % 2)                 /* bad field */
        {
            f->len = (size_t)snprintf((char *)f->bytes, sizeof(f->bytes),
                                      "HUM %u abc %ld\n", s->id, (long)time(NULL));
        }
        else                            /* line too long (> 64 bytes) */
        {
            memset(f->bytes, 'X', 70);
            f->bytes[70] = '\n';
            f->len = 71;
        }
        break;

    case K_MOTION:
    case K_DOOR:                        /* door frame with invalid state */
        build_evp_door(f, s->id, 7, (uint32_t)time(NULL));
        break;

    case K_GAS:
        if (rand() % 2)
        {
            f->bytes[10] ^= 0xFF;       /* bad CRC */
        }
        else
        {
            f->bytes[0] = 0x00;         /* bad magic */
        }
        break;
    }
}

/* ---------- network ---------- */

static int connect_to(const char *host, int port)
{
    struct addrinfo hints;
    struct addrinfo *res;
    char port_str[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
    {
        errno = EHOSTUNREACH;
        return -1;
    }

    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) < 0)
    {
        int saved = errno;

        close(fd);
        fd = -1;
        errno = saved;
    }

    freeaddrinfo(res);
    return fd;
}

static int send_all(int fd, const uint8_t *buf, size_t len)
{
    while (len > 0)
    {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);

        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }

    return 0;
}

/* --fragment: send in pieces of 1..5 bytes with a short pause */
static int send_bytes(int fd, const uint8_t *buf, size_t len, bool fragment)
{
    if (!fragment)
    {
        return send_all(fd, buf, len);
    }

    while (len > 0)
    {
        size_t piece = (size_t)(1 + rand() % 5);

        if (piece > len)
        {
            piece = len;
        }
        if (send_all(fd, buf, piece) != 0)
        {
            return -1;
        }
        buf += piece;
        len -= piece;
        usleep(20000);
    }

    return 0;
}

/* Wait for 1 reply byte. Return byte, 0 on timeout, -1 if connection closed */
static int wait_reply(int fd, int timeout_ms)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    uint8_t byte;
    int ready = poll(&pfd, 1, timeout_ms);

    if (ready <= 0)
    {
        return 0;
    }
    if (recv(fd, &byte, 1, 0) != 1)
    {
        return -1;
    }

    return byte;
}

/* Drop extra replies left from earlier frames */
static void drain_replies(int fd)
{
    uint8_t tmp[64];

    while (recv(fd, tmp, sizeof(tmp), MSG_DONTWAIT) > 0)
    {
    }
}

/* SGP: resend a frame (the valid version) until ACK, at most 3 times */
static int sgp_confirm(int fd, const frame_t *valid, bool fragment, uint16_t id)
{
    for (int retry = 0; retry <= SGP_MAX_RETRY; retry++)
    {
        int reply;

        if (retry > 0)
        {
            printf("sensor %u: no ACK, resend %d/%d\n", id, retry, SGP_MAX_RETRY);
            if (send_bytes(fd, valid->bytes, valid->len, fragment) != 0)
            {
                return -1;
            }
        }

        reply = wait_reply(fd, SGP_TIMEOUT_MS);
        if (reply < 0)
        {
            return -1;
        }
        if (reply == SGP_ACK)
        {
            return 0;
        }
    }

    printf("sensor %u: frame dropped after %d retries\n", id, SGP_MAX_RETRY);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <type> <id> <host> <base port P> <period ms> "
            "[--fragment] [--glue] [--corrupt]\n"
            "  type: temperature | humidity | motion | door | smoke_gas\n",
            prog);
}

int main(int argc, char *argv[])
{
    sensor_t s;
    bool fragment = false;
    bool glue = false;
    bool corrupt = false;
    int base_port;
    int period_ms;
    int port;
    int fd;
    long sent = 0;
    char when[32];
    int k;

    if (argc < 6)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    for (k = 0; k < (int)(sizeof(KINDS) / sizeof(KINDS[0])); k++)
    {
        if (strcmp(argv[1], KINDS[k].name) == 0)
        {
            break;
        }
    }
    if (k == (int)(sizeof(KINDS) / sizeof(KINDS[0])))
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    memset(&s, 0, sizeof(s));
    s.kind = (kind_t)k;
    s.id = (uint16_t)atoi(argv[2]);
    base_port = atoi(argv[4]);
    period_ms = atoi(argv[5]);

    if (base_port <= 0 || period_ms <= 0)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    for (int i = 6; i < argc; i++)
    {
        if (strcmp(argv[i], "--fragment") == 0)     fragment = true;
        else if (strcmp(argv[i], "--glue") == 0)    glue = true;
        else if (strcmp(argv[i], "--corrupt") == 0) corrupt = true;
        else
        {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    srand((unsigned)(time(NULL) ^ (getpid() << 8) ^ s.id));
    s.value = (s.kind == K_TEMP) ? 18.0 : (s.kind == K_HUM) ? 50.0 : 150.0;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    port = base_port + KINDS[s.kind].port_offset;
    fd = connect_to(argv[3], port);
    now_str(when, sizeof(when));

    if (fd < 0)
    {
        printf("%s sensor %u %s connect failed (%s)\n",
               when, s.id, KINDS[s.kind].name, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("%s sensor %u %s connected to %s:%d\n",
           when, s.id, KINDS[s.kind].name, argv[3], port);
    fflush(stdout);

    while (!g_stop)
    {
        frame_t valid[GLUE_COUNT];
        uint8_t out[GLUE_COUNT * MAX_FRAME];
        size_t out_len = 0;
        int count = glue ? GLUE_COUNT : 1;
        int sleep_ms;

        /* Build 1 frame (or GLUE_COUNT frames for --glue) */
        for (int i = 0; i < count; i++)
        {
            frame_t f;

            next_frame(&s, &f);
            valid[i] = f;

            /* never corrupt the first frame: the gateway rejects
               a node whose first frame is malformed */
            if (corrupt && sent > 0 && rand() % CORRUPT_RATE == 0)
            {
                corrupt_frame(&s, &f);
                printf("sensor %u: sending corrupted frame\n", s.id);
            }

            memcpy(out + out_len, f.bytes, f.len);
            out_len += f.len;
            sent++;
        }

        if (s.kind == K_GAS)
        {
            drain_replies(fd);
        }

        if (send_bytes(fd, out, out_len, fragment) != 0)
        {
            break;
        }

        if (s.kind == K_GAS)
        {
            bool closed = false;

            for (int i = 0; i < count && !closed; i++)
            {
                closed = (sgp_confirm(fd, &valid[i], fragment, s.id) != 0);
            }
            if (closed)
            {
                break;
            }
        }

        fflush(stdout);

        /* motion/door: random interval around the average period */
        if (s.kind == K_MOTION || s.kind == K_DOOR)
        {
            sleep_ms = (int)rand_range(period_ms * 0.5, period_ms * 1.5);
        }
        else
        {
            sleep_ms = period_ms;
        }

        for (int waited = 0; waited < sleep_ms && !g_stop; waited += 50)
        {
            usleep(50000);
        }
    }

    now_str(when, sizeof(when));
    printf("%s sensor %u %s disconnected\n", when, s.id, KINDS[s.kind].name);

    close(fd);
    return EXIT_SUCCESS;
}
