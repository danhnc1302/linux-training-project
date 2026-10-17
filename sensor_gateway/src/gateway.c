/*
 * Shared gateway state.
 * The stop flag is protected by a mutex (not atomics) so that helgrind
 * can see the synchronization: atomics are invisible to helgrind.
 */
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "common.h"

gateway_t g_gateway = { 0 };

static pthread_mutex_t g_stop_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_console_mutex = PTHREAD_MUTEX_INITIALIZER;

void gateway_request_stop(const char *reason)
{
    pthread_mutex_lock(&g_stop_mutex);
    if (g_gateway.stop_reason == NULL)
    {
        g_gateway.stop_reason = reason;
    }
    g_gateway.shutdown = 1;
    pthread_mutex_unlock(&g_stop_mutex);
}

bool gateway_stopping(void)
{
    bool stop;

    pthread_mutex_lock(&g_stop_mutex);
    stop = (g_gateway.shutdown != 0);
    pthread_mutex_unlock(&g_stop_mutex);

    return stop;
}

const char *gateway_stop_reason(void)
{
    const char *reason;

    pthread_mutex_lock(&g_stop_mutex);
    reason = g_gateway.stop_reason;
    pthread_mutex_unlock(&g_stop_mutex);

    return reason;
}

void console_printf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    pthread_mutex_lock(&g_console_mutex);
    vprintf(format, args);
    fflush(stdout);
    pthread_mutex_unlock(&g_console_mutex);
    va_end(args);
}
