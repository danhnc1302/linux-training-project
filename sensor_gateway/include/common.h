#ifndef COMMON_H
#define COMMON_H

#include <signal.h>

typedef struct
{
    int gateway_port;
    int http_port;

    volatile sig_atomic_t shutdown;

} gateway_t;

extern gateway_t g_gateway;

#endif