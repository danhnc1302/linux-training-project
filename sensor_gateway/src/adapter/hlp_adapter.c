#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hlp_adapter.h"

#define HLP_MAX_LINE    64U     /* including LF */
#define HLP_VALUE_MIN   0.0
#define HLP_VALUE_MAX   100.0

/* Line format (without LF): "HUM <id> <value> <ts>", exactly one space */
static int parse_line(const char *s, sensor_data_t *out)
{
    char *end;
    unsigned long id;
    double value;
    long long ts;

    if (strncmp(s, "HUM ", 4) != 0)
    {
        return -1;
    }
    s += 4;

    /* id */
    if (!isdigit((unsigned char)*s))
    {
        return -1;
    }
    errno = 0;
    id = strtoul(s, &end, 10);
    if (errno != 0 || *end != ' ' || id > UINT16_MAX)
    {
        return -1;
    }
    s = end + 1;

    /* value: must start with a digit (rejects "nan", "inf", double space) */
    if (!isdigit((unsigned char)*s))
    {
        return -1;
    }
    errno = 0;
    value = strtod(s, &end);
    if (errno != 0 || *end != ' ' || !isfinite(value) ||
        value < HLP_VALUE_MIN || value > HLP_VALUE_MAX)
    {
        return -1;
    }
    s = end + 1;

    /* ts */
    if (!isdigit((unsigned char)*s))
    {
        return -1;
    }
    errno = 0;
    ts = strtoll(s, &end, 10);
    if (errno != 0 || *end != '\0' || ts < 0)
    {
        return -1;
    }

    out->sensor_id = (sensor_id_t)id;
    out->type      = SENSOR_HUMIDITY;
    out->value     = value;
    out->timestamp = (time_t)ts;

    return 0;
}

static parse_result_t hlp_parse(const uint8_t *buf, size_t len,
                                sensor_data_t *out, size_t *consumed)
{
    char line[HLP_MAX_LINE];
    size_t scan = (len < HLP_MAX_LINE) ? len : HLP_MAX_LINE;
    const uint8_t *lf;
    size_t line_len;

    *consumed = 0;

    lf = memchr(buf, '\n', scan);

    if (lf == NULL)
    {
        if (len >= HLP_MAX_LINE)
        {
            /* 64 bytes without LF: line too long, drop them */
            *consumed = HLP_MAX_LINE;
            return PARSE_ERROR;
        }

        return PARSE_NEED_MORE;
    }

    line_len = (size_t)(lf - buf);     /* without LF, at most 63 */
    *consumed = line_len + 1U;          /* drop the whole line */

    memcpy(line, buf, line_len);
    line[line_len] = '\0';

    if (parse_line(line, out) != 0)
    {
        return PARSE_ERROR;
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

const protocol_adapter_t g_hlp_adapter =
{
    .name        = "HLP",
    .port_offset = 1,
    .parse       = hlp_parse,
    .build_reply = no_reply
};
