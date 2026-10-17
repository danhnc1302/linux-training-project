#ifndef COMMON_H
#define COMMON_H

#include <signal.h>
#include <stdbool.h>

typedef struct
{
    volatile sig_atomic_t shutdown;     /* stop flag, protected by a mutex */
    int gateway_port;                   /* base port P                     */
    int http_port;
    const char *stop_reason;            /* first reason wins               */
} gateway_t;

extern gateway_t g_gateway;

/* Ask the whole gateway to stop. Thread-safe, keeps the first reason. */
void gateway_request_stop(const char *reason);

/* True when the gateway is stopping. Thread-safe. */
bool gateway_stopping(void);

/* Reason given to gateway_request_stop(), or NULL. Thread-safe. */
const char *gateway_stop_reason(void);

/* Thread-safe printf for console output (one mutex for all threads) */
void console_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

#endif
