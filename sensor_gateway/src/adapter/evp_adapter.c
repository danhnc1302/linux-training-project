#include "evp_adapter.h"

#define EVP_TYPE_MOTION     0x01U
#define EVP_TYPE_DOOR       0x02U
#define EVP_LEN_MOTION      6U      /* id(2) + ts(4)            */
#define EVP_LEN_DOOR        7U      /* id(2) + state(1) + ts(4) */
#define EVP_HEADER_SIZE     2U      /* type(1) + length(1)      */

static uint16_t read_be16(const uint8_t *b)
{
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}

static uint32_t read_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |
            (uint32_t)b[3];
}

static parse_result_t evp_parse(const uint8_t *buf, size_t len,
                                sensor_data_t *out, size_t *consumed)
{
    uint8_t type;
    uint8_t length;
    size_t frame_size;

    *consumed = 0;

    if (len < EVP_HEADER_SIZE)
    {
        return PARSE_NEED_MORE;
    }

    type   = buf[0];
    length = buf[1];

    if (!((type == EVP_TYPE_MOTION && length == EVP_LEN_MOTION) ||
          (type == EVP_TYPE_DOOR   && length == EVP_LEN_DOOR)))
    {
        /* Unknown type/length: frame boundary unknown, drop 1 byte */
        *consumed = 1;
        return PARSE_ERROR;
    }

    frame_size = EVP_HEADER_SIZE + length;

    if (len < frame_size)
    {
        return PARSE_NEED_MORE;
    }

    *consumed = frame_size;

    out->sensor_id = read_be16(buf + 2);

    if (type == EVP_TYPE_MOTION)
    {
        out->type      = SENSOR_MOTION;
        out->value     = 1.0;
        out->timestamp = (time_t)read_be32(buf + 4);
    }
    else
    {
        uint8_t state = buf[4];

        if (state > 1U)
        {
            return PARSE_ERROR;
        }

        out->type      = SENSOR_DOOR;
        out->value     = (double)state;
        out->timestamp = (time_t)read_be32(buf + 5);
    }

    return PARSE_OK;
}

static size_t no_reply(parse_result_t result, uint8_t *reply, size_t cap)
{
    (void)result;
    (void)reply;
    (void)cap;
    return 0;
}

const protocol_adapter_t g_evp_adapter =
{
    .name        = "EVP",
    .port_offset = 2,
    .parse       = evp_parse,
    .build_reply = no_reply
};
