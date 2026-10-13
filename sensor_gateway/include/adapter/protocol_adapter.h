#ifndef PROTOCOL_ADAPTER_H
#define PROTOCOL_ADAPTER_H

#include <stddef.h>
#include "sensor_types.h"

typedef enum
{
    PARSE_OK,
    PARSE_NEED_MORE,
    PARSE_ERROR
} parse_result_t;

typedef struct
{
    parse_result_t (*parse)(
        const uint8_t *buf,
        size_t len,
        size_t *consumed,
        sensor_data_t *data);

    int (*build_reply)(
        uint8_t *buf,
        size_t *len,
        int ack);
} protocol_adapter_t;

#endif