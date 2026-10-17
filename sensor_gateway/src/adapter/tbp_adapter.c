#include <math.h>
#include <string.h>

#include "tbp_adapter.h"

#define TBP_FRAME_SIZE  18U
#define TBP_VALUE_MIN   (-40.0)
#define TBP_VALUE_MAX   85.0

static uint16_t read_le16(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static uint64_t read_le64(const uint8_t *b)
{
    uint64_t v = 0;

    for (int i = 7; i >= 0; i--)
    {
        v = (v << 8) | (uint64_t)b[i];
    }

    return v;
}

static parse_result_t tbp_parse(const uint8_t *buf, size_t len,
                                sensor_data_t *out, size_t *consumed)
{
    uint64_t value_bits;
    double value;
    int64_t ts;

    *consumed = 0;

    if (len < TBP_FRAME_SIZE)
    {
        return PARSE_NEED_MORE;
    }

    /* Fixed length: a broken frame is dropped as a whole */
    *consumed = TBP_FRAME_SIZE;

    value_bits = read_le64(buf + 2);
    memcpy(&value, &value_bits, sizeof(value));   /* no pointer cast */

    ts = (int64_t)read_le64(buf + 10);

    if (!isfinite(value) ||
        value < TBP_VALUE_MIN ||
        value > TBP_VALUE_MAX ||
        ts < 0)
    {
        return PARSE_ERROR;
    }

    out->sensor_id = read_le16(buf);
    out->type      = SENSOR_TEMPERATURE;
    out->value     = value;
    out->timestamp = (time_t)ts;

    return PARSE_OK;
}

static size_t no_reply(parse_result_t result, uint8_t *reply, size_t cap)
{
    (void)result;
    (void)reply;
    (void)cap;
    return 0;
}

const protocol_adapter_t g_tbp_adapter =
{
    .name        = "TBP",
    .port_offset = 0,
    .parse       = tbp_parse,
    .build_reply = no_reply
};
