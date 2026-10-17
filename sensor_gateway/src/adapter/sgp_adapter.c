#include <string.h>

#include "sgp_adapter.h"

#define SGP_PPM_MAX 10000U

uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);

        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

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

static parse_result_t sgp_parse(const uint8_t *buf, size_t len,
                                sensor_data_t *out, size_t *consumed)
{
    uint16_t ppm;

    *consumed = 0;

    if (len == 0)
    {
        return PARSE_NEED_MORE;
    }

    if (buf[0] != SGP_MAGIC)
    {
        /* Bad magic: drop bytes up to the next 0x7E to resynchronize.
         * If no 0x7E yet, wait for at least one frame length of bytes so a
         * fragmented broken frame gives only one error (one NAK). */
        const uint8_t *next = memchr(buf + 1, SGP_MAGIC, len - 1U);

        if (next != NULL)
        {
            *consumed = (size_t)(next - buf);
            return PARSE_ERROR;
        }

        if (len < SGP_FRAME_SIZE)
        {
            return PARSE_NEED_MORE;
        }

        *consumed = len;
        return PARSE_ERROR;
    }

    if (len < SGP_FRAME_SIZE)
    {
        return PARSE_NEED_MORE;
    }

    /* Fixed length: a broken frame is dropped as a whole */
    *consumed = SGP_FRAME_SIZE;

    /* CRC over offset 1..8 */
    if (crc16_ccitt_false(buf + 1, 8) != read_be16(buf + 9))
    {
        return PARSE_ERROR;
    }

    ppm = read_be16(buf + 3);
    if (ppm > SGP_PPM_MAX)
    {
        return PARSE_ERROR;
    }

    out->sensor_id = read_be16(buf + 1);
    out->type      = SENSOR_SMOKE_GAS;
    out->value     = (double)ppm;
    out->timestamp = (time_t)read_be32(buf + 5);

    return PARSE_OK;
}

static size_t sgp_build_reply(parse_result_t result, uint8_t *reply, size_t cap)
{
    if (reply == NULL || cap < 1U || result == PARSE_NEED_MORE)
    {
        return 0;
    }

    reply[0] = (result == PARSE_OK) ? SGP_ACK : SGP_NAK;
    return 1;
}

const protocol_adapter_t g_sgp_adapter =
{
    .name        = "SGP",
    .port_offset = 3,
    .parse       = sgp_parse,
    .build_reply = sgp_build_reply
};
