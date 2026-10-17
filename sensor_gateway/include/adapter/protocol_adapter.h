#ifndef PROTOCOL_ADAPTER_H
#define PROTOCOL_ADAPTER_H

/* protocol_adapter.h - interface from section 3.5 */
#include <stddef.h>
#include <stdint.h>

#include "sensor_types.h"

typedef enum
{
    PARSE_ERROR     = -1,   /* broken frame: bad magic, length, CRC, value */
    PARSE_NEED_MORE =  0,   /* not enough bytes for one frame yet          */
    PARSE_OK        =  1    /* exactly one frame decoded                   */
} parse_result_t;

typedef struct
{
    const char *name;         /* "TBP", "HLP", "EVP", "SGP"               */
    uint16_t    port_offset;  /* listen port = base port + port_offset    */

    /* Decode at most one frame at the start of buf.                       */
    /*  PARSE_OK:        fill *out, *consumed = bytes used.                */
    /*  PARSE_NEED_MORE: keep bytes, wait for more data.                   */
    /*  PARSE_ERROR:     *consumed = bytes to drop to resynchronize.       */
    parse_result_t (*parse)(const uint8_t *buf, size_t len,
                            sensor_data_t *out, size_t *consumed);

    /* Build reply for the sensor; return 0 if protocol has no reply.      */
    size_t (*build_reply)(parse_result_t result, uint8_t *reply, size_t cap);
} protocol_adapter_t;

#endif
