#ifndef CONNMGR_H
#define CONNMGR_H
#include "sbuffer.h"
#include "adapter/protocol_adapter.h"

typedef struct
{
    uint16_t sensor_id;
    double value;
    uint64_t timestamp;
} __attribute__((packed)) tbp_frame_t;

typedef struct
{
    int client_fd;
    protocol_adapter_t *adapter;
    sbuffer_t *buffer;
} client_ctx_t;

void *connmgr_thread(void *arg);

#endif