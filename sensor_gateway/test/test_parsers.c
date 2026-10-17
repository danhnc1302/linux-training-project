/*
 * Unit tests for the 4 protocol adapters, using the examples of section 3.4.
 * Build & run:  make test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "protocol_adapter.h"
#include "tbp_adapter.h"
#include "hlp_adapter.h"
#include "evp_adapter.h"
#include "sgp_adapter.h"

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond, name)                                   \
    do {                                                    \
        if (cond) { g_passed++; printf("  PASS  %s\n", name); } \
        else      { g_failed++; printf("  FAIL  %s\n", name); } \
    } while (0)

static parse_result_t run(const protocol_adapter_t *a, const uint8_t *buf,
                          size_t len, sensor_data_t *out, size_t *consumed)
{
    memset(out, 0, sizeof(*out));
    *consumed = 12345;
    return a->parse(buf, len, out, consumed);
}

static void test_crc(void)
{
    const uint8_t vec[] = "123456789";

    printf("CRC-16/CCITT-FALSE\n");
    CHECK(crc16_ccitt_false(vec, 9) == 0x29B1, "\"123456789\" -> 0x29B1");
}

static void test_tbp(void)
{
    const uint8_t ex[] = { 0x0A, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x35, 0x40,
                           0x00, 0xB9, 0x55, 0x69, 0x00, 0x00, 0x00, 0x00 };
    uint8_t two[36];
    sensor_data_t d;
    size_t c;
    parse_result_t r;

    printf("TBP\n");

    r = run(&g_tbp_adapter, ex, sizeof(ex), &d, &c);
    CHECK(r == PARSE_OK && c == 18, "example frame -> PARSE_OK, consumed 18");
    CHECK(d.sensor_id == 10 && d.value == 21.5 && d.timestamp == 1767225600 &&
          d.type == SENSOR_TEMPERATURE, "id=10 value=21.5 ts=1767225600 type=1");

    r = run(&g_tbp_adapter, ex, 17, &d, &c);
    CHECK(r == PARSE_NEED_MORE, "17 bytes (fragment) -> PARSE_NEED_MORE");

    memcpy(two, ex, 18);
    memcpy(two + 18, ex, 18);
    r = run(&g_tbp_adapter, two, sizeof(two), &d, &c);
    CHECK(r == PARSE_OK && c == 18, "2 glued frames -> first frame, consumed 18");

    two[9] = 0x7F;   /* exponent huge -> value out of range */
    r = run(&g_tbp_adapter, two, 18, &d, &c);
    CHECK(r == PARSE_ERROR && c == 18, "value out of range -> PARSE_ERROR, drop 18");
}

static void test_hlp(void)
{
    const char *ex = "HUM 21 54.3 1767225600\n";
    char long_line[80];
    sensor_data_t d;
    size_t c;
    parse_result_t r;

    printf("HLP\n");

    r = run(&g_hlp_adapter, (const uint8_t *)ex, strlen(ex), &d, &c);
    CHECK(r == PARSE_OK && c == strlen(ex), "example line -> PARSE_OK");
    CHECK(d.sensor_id == 21 && fabs(d.value - 54.3) < 1e-9 &&
          d.timestamp == 1767225600 && d.type == SENSOR_HUMIDITY,
          "id=21 value=54.3 ts=1767225600 type=2");

    r = run(&g_hlp_adapter, (const uint8_t *)ex, 10, &d, &c);
    CHECK(r == PARSE_NEED_MORE, "no LF yet -> PARSE_NEED_MORE");

    r = run(&g_hlp_adapter, (const uint8_t *)"HUM 21  54.3 1767225600\n", 24, &d, &c);
    CHECK(r == PARSE_ERROR && c == 24, "double space -> PARSE_ERROR, drop line");

    r = run(&g_hlp_adapter, (const uint8_t *)"TMP 21 54.3 1767225600\n", 23, &d, &c);
    CHECK(r == PARSE_ERROR, "wrong keyword -> PARSE_ERROR");

    r = run(&g_hlp_adapter, (const uint8_t *)"HUM 21 154.3 1767225600\n", 24, &d, &c);
    CHECK(r == PARSE_ERROR, "humidity > 100 -> PARSE_ERROR");

    memset(long_line, 'X', sizeof(long_line));
    r = run(&g_hlp_adapter, (const uint8_t *)long_line, sizeof(long_line), &d, &c);
    CHECK(r == PARSE_ERROR && c == 64, "64 bytes without LF -> PARSE_ERROR, drop 64");
}

static void test_evp(void)
{
    const uint8_t motion[] = { 0x01, 0x06, 0x00, 0x1E, 0x69, 0x55, 0xB9, 0x00 };
    const uint8_t door[]   = { 0x02, 0x07, 0x00, 0x28, 0x01, 0x69, 0x55, 0xB9, 0x00 };
    const uint8_t bad_type[] = { 0x09, 0x06 };
    const uint8_t bad_len[]  = { 0x01, 0x07 };
    uint8_t bad_state[9];
    sensor_data_t d;
    size_t c;
    parse_result_t r;

    printf("EVP\n");

    r = run(&g_evp_adapter, motion, sizeof(motion), &d, &c);
    CHECK(r == PARSE_OK && c == 8, "motion example -> PARSE_OK, consumed 8");
    CHECK(d.sensor_id == 30 && d.value == 1.0 && d.timestamp == 1767225600 &&
          d.type == SENSOR_MOTION, "id=30 value=1 ts=1767225600 type=3");

    r = run(&g_evp_adapter, door, sizeof(door), &d, &c);
    CHECK(r == PARSE_OK && c == 9, "door example -> PARSE_OK, consumed 9");
    CHECK(d.sensor_id == 40 && d.value == 1.0 && d.timestamp == 1767225600 &&
          d.type == SENSOR_DOOR, "id=40 open ts=1767225600 type=4");

    r = run(&g_evp_adapter, door, 5, &d, &c);
    CHECK(r == PARSE_NEED_MORE, "half door frame -> PARSE_NEED_MORE");

    r = run(&g_evp_adapter, bad_type, sizeof(bad_type), &d, &c);
    CHECK(r == PARSE_ERROR && c == 1, "unknown type -> PARSE_ERROR, drop 1");

    r = run(&g_evp_adapter, bad_len, sizeof(bad_len), &d, &c);
    CHECK(r == PARSE_ERROR && c == 1, "wrong length -> PARSE_ERROR, drop 1");

    memcpy(bad_state, door, sizeof(door));
    bad_state[4] = 7;
    r = run(&g_evp_adapter, bad_state, sizeof(bad_state), &d, &c);
    CHECK(r == PARSE_ERROR && c == 9, "door state 7 -> PARSE_ERROR, drop 9");
}

static void test_sgp(void)
{
    const uint8_t ex[] = { 0x7E, 0x00, 0x32, 0x00, 0x78,
                           0x69, 0x55, 0xB9, 0x00, 0xAC, 0xF5 };
    uint8_t buf[32];
    uint8_t reply[4];
    sensor_data_t d;
    size_t c;
    parse_result_t r;

    printf("SGP\n");

    CHECK(crc16_ccitt_false(ex + 1, 8) == 0xACF5, "CRC of example = 0xACF5");

    r = run(&g_sgp_adapter, ex, sizeof(ex), &d, &c);
    CHECK(r == PARSE_OK && c == 11, "example frame -> PARSE_OK, consumed 11");
    CHECK(d.sensor_id == 50 && d.value == 120.0 && d.timestamp == 1767225600 &&
          d.type == SENSOR_SMOKE_GAS, "id=50 value=120 ts=1767225600 type=5");

    r = run(&g_sgp_adapter, ex, 10, &d, &c);
    CHECK(r == PARSE_NEED_MORE, "10 bytes -> PARSE_NEED_MORE");

    memcpy(buf, ex, 11);
    buf[10] ^= 0xFF;
    r = run(&g_sgp_adapter, buf, 11, &d, &c);
    CHECK(r == PARSE_ERROR && c == 11, "bad CRC -> PARSE_ERROR, drop 11");

    buf[0] = 0x00; buf[1] = 0x11;
    memcpy(buf + 2, ex, 11);            /* garbage, then a valid frame */
    r = run(&g_sgp_adapter, buf, 13, &d, &c);
    CHECK(r == PARSE_ERROR && c == 2, "bad magic -> PARSE_ERROR, skip to next 0x7E");
    r = run(&g_sgp_adapter, buf + c, 13 - c, &d, &c);
    CHECK(r == PARSE_OK && d.sensor_id == 50, "resync -> next frame PARSE_OK");

    memset(buf, 0x00, 11);
    r = run(&g_sgp_adapter, buf, 5, &d, &c);
    CHECK(r == PARSE_NEED_MORE, "5 garbage bytes, no 0x7E -> wait (1 NAK per frame)");
    r = run(&g_sgp_adapter, buf, 11, &d, &c);
    CHECK(r == PARSE_ERROR && c == 11, "11 garbage bytes -> PARSE_ERROR, drop 11");

    CHECK(g_sgp_adapter.build_reply(PARSE_OK, reply, sizeof(reply)) == 1 &&
          reply[0] == 0x06, "build_reply(OK) -> ACK 0x06");
    CHECK(g_sgp_adapter.build_reply(PARSE_ERROR, reply, sizeof(reply)) == 1 &&
          reply[0] == 0x15, "build_reply(ERROR) -> NAK 0x15");
    CHECK(g_tbp_adapter.build_reply(PARSE_OK, reply, sizeof(reply)) == 0,
          "TBP build_reply -> 0 (no reply)");
}

int main(void)
{
    test_crc();
    test_tbp();
    test_hlp();
    test_evp();
    test_sgp();

    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
