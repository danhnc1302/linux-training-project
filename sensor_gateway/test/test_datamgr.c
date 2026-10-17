/*
 * Unit test for the Data manager (Step 5 verification):
 * known data, every sensor type has at least one alert case and one
 * normal case; unknown ID and mismatched type are tested too.
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>

#include "datamgr.h"

#define MAX_LOGS 64

static char g_logs[MAX_LOGS][256];
static int g_log_count = 0;
static int g_passed = 0;
static int g_failed = 0;

/* Stub of logger_log: record messages instead of writing to the FIFO */
void logger_log(const char *source, const char *message)
{
    if (g_log_count < MAX_LOGS)
    {
        snprintf(g_logs[g_log_count++], sizeof(g_logs[0]), "%s %s",
                 source, message);
    }
}

static void clear_logs(void)
{
    g_log_count = 0;
}

static int has_log(const char *text)
{
    for (int i = 0; i < g_log_count; i++)
    {
        if (strstr(g_logs[i], text) != NULL)
        {
            return 1;
        }
    }
    return 0;
}

#define CHECK(cond, name)                                         \
    do {                                                          \
        if (cond) { g_passed++; printf("  PASS  %s\n", name); }  \
        else      { g_failed++; printf("  FAIL  %s\n", name); }  \
    } while (0)

static void feed(datamgr_t *dm, sensor_id_t id, sensor_type_t type,
                 const double *values, int n)
{
    for (int i = 0; i < n; i++)
    {
        sensor_data_t d;

        memset(&d, 0, sizeof(d));
        d.sensor_id = id;
        d.type = type;
        d.value = values[i];
        d.timestamp = 1767225600 + i;
        datamgr_process(dm, &d);
    }
}

static int write_map(const char *path)
{
    FILE *fp = fopen(path, "w");

    if (fp == NULL)
    {
        return -1;
    }
    fputs("# room  sensor_id  type\n"
          "501     10         temperature\n"
          "\n"
          "501     21         humidity\n"
          "602     30         motion\n"
          "703     40         door\n"
          "804     50         smoke_gas\n", fp);
    fclose(fp);
    return 0;
}

static void test_room_file(void)
{
    datamgr_t dm;
    FILE *fp;

    printf("Room-sensor file\n");

    CHECK(datamgr_init(&dm, "test_map.tmp") == 0 && dm.n == 5,
          "5 sensors loaded, comment and blank line skipped");
    CHECK(dm.sensors[0].info.room == 501 && dm.sensors[0].info.id == 10 &&
          dm.sensors[0].info.type == SENSOR_TEMPERATURE, "first line parsed");
    CHECK(datamgr_init(&dm, "no_such_file.map") == -1, "missing file -> -1");

    fp = fopen("bad_map.tmp", "w");
    fputs("501 10 temperature\n501 11 pressure\n", fp);
    fclose(fp);
    CHECK(datamgr_init(&dm, "bad_map.tmp") == -1, "unknown type -> -1");

    fp = fopen("bad_map.tmp", "w");
    fputs("501 10 temperature\n502 10 humidity\n", fp);
    fclose(fp);
    CHECK(datamgr_init(&dm, "bad_map.tmp") == -1, "duplicate ID -> -1");
    remove("bad_map.tmp");
}

static void test_temperature(void)
{
    datamgr_t dm;
    const double normal[] = { 17, 18, 17, 18, 17 };
    const double hot[] = { 22, 22, 22, 22, 22, 23 };
    const double cold[] = { 10, 10, 10, 10, 10 };

    printf("Temperature (running avg of %d, %.0f..%.0f C)\n",
           RUN_AVG_LENGTH, TEMP_MIN_C, TEMP_MAX_C);
    datamgr_init(&dm, "test_map.tmp");

    clear_logs();
    feed(&dm, 10, SENSOR_TEMPERATURE, normal, 4);
    CHECK(dm.sensors[0].state == STATE_COLLECTING, "4 samples -> collecting");
    feed(&dm, 10, SENSOR_TEMPERATURE, normal + 4, 1);
    CHECK(dm.sensors[0].state == STATE_NORMAL && g_log_count == 0,
          "avg 17.4 -> normal, no log");

    clear_logs();
    feed(&dm, 10, SENSOR_TEMPERATURE, hot, 6);
    CHECK(dm.sensors[0].state == STATE_ALERT, "avg > 20 -> alert");
    CHECK(has_log("The sensor node with 10 reports it's too hot "
                  "(running avg temperature = "), "log 'too hot'");
    CHECK(g_log_count == 1, "alert logged once, not on every sample");

    clear_logs();
    feed(&dm, 10, SENSOR_TEMPERATURE, cold, 5);
    /* the window still holds old values: the alert fires when the
       average first drops below 15 (12.6 here), not at 10.0 */
    CHECK(has_log("The sensor node with 10 reports it's too cold "
                  "(running avg temperature = 12.6)"),
          "hot -> cold logs 'too cold' when avg crosses 15 (12.6)");

    clear_logs();
    feed(&dm, 10, SENSOR_TEMPERATURE, normal, 5);
    feed(&dm, 10, SENSOR_TEMPERATURE, hot, 5);
    CHECK(has_log("too hot"), "back to normal, then hot again -> logged again");
}

static void test_humidity(void)
{
    datamgr_t dm;
    const double normal[] = { 50, 50, 50, 50, 50 };
    const double dry[] = { 20, 20, 20, 20, 20 };
    const double humid[] = { 80, 80, 80, 80, 80 };

    printf("Humidity (running avg, %.0f..%.0f %%)\n", HUMIDITY_MIN, HUMIDITY_MAX);
    datamgr_init(&dm, "test_map.tmp");

    clear_logs();
    feed(&dm, 21, SENSOR_HUMIDITY, normal, 5);
    CHECK(dm.sensors[1].state == STATE_NORMAL && g_log_count == 0, "avg 50 -> normal");

    feed(&dm, 21, SENSOR_HUMIDITY, dry, 5);
    CHECK(has_log("The sensor node with 21 reports it's too dry "
                  "(running avg humidity = 26.0)"),
          "log 'too dry' when avg crosses 30 (26.0)");

    clear_logs();
    feed(&dm, 21, SENSOR_HUMIDITY, humid, 5);
    CHECK(has_log("The sensor node with 21 reports it's too humid "
                  "(running avg humidity = "), "log 'too humid'");
}

static void test_motion(void)
{
    datamgr_t dm;
    const double events[] = { 1, 1 };

    printf("Motion (every event logged)\n");
    datamgr_init(&dm, "test_map.tmp");

    clear_logs();
    feed(&dm, 30, SENSOR_MOTION, events, 2);
    CHECK(has_log("The sensor node with 30 detected motion") && g_log_count == 2,
          "2 events -> 2 logs");
}

static void test_door(void)
{
    datamgr_t dm;
    const double states[] = { 1, 1, 0, 0, 1 };

    printf("Door (log on each state change)\n");
    datamgr_init(&dm, "test_map.tmp");

    clear_logs();
    feed(&dm, 40, SENSOR_DOOR, states, 5);
    CHECK(g_log_count == 3, "open, open, closed, closed, open -> 3 logs");
    CHECK(has_log("The sensor node with 40 reports the door is open"), "log 'door is open'");
    CHECK(has_log("The sensor node with 40 reports the door is closed"), "log 'door is closed'");
    CHECK(dm.sensors[3].state == STATE_ALERT, "door open -> alert state");
}

static void test_smoke_gas(void)
{
    datamgr_t dm;
    const double normal[] = { 120 };
    const double alarm[] = { 300, 450 };
    const double back[] = { 100 };

    printf("Smoke/gas (instant threshold >= %.0f ppm)\n", GAS_ALARM_PPM);
    datamgr_init(&dm, "test_map.tmp");

    clear_logs();
    feed(&dm, 50, SENSOR_SMOKE_GAS, normal, 1);
    CHECK(dm.sensors[4].state == STATE_NORMAL && g_log_count == 0,
          "120 ppm -> normal right away (no averaging)");

    feed(&dm, 50, SENSOR_SMOKE_GAS, alarm, 2);
    CHECK(has_log("The sensor node with 50 reports a smoke/gas alarm "
                  "(concentration = 300 ppm)"), "300 ppm -> alarm on first sample");
    CHECK(g_log_count == 1, "450 ppm while in alarm -> not logged again");

    feed(&dm, 50, SENSOR_SMOKE_GAS, back, 1);
    CHECK(dm.sensors[4].state == STATE_NORMAL, "100 ppm -> normal");
}

static void test_invalid(void)
{
    datamgr_t dm;
    const double v[] = { 18, 18 };
    sensor_data_t d;

    printf("Invalid sensor data\n");
    datamgr_init(&dm, "test_map.tmp");

    clear_logs();
    memset(&d, 0, sizeof(d));
    d.sensor_id = 99;
    d.type = SENSOR_TEMPERATURE;
    CHECK(datamgr_process(&dm, &d) == NULL, "unknown ID 99 -> rejected");
    CHECK(has_log("Received sensor data with invalid sensor node ID 99"),
          "log 'invalid sensor node ID 99'");

    feed(&dm, 99, SENSOR_TEMPERATURE, v, 2);
    CHECK(g_log_count == 1, "same invalid ID again -> logged once only");

    clear_logs();
    feed(&dm, 21, SENSOR_TEMPERATURE, v, 1);   /* 21 is humidity in the file */
    CHECK(has_log("Received sensor data with invalid sensor node ID 21"),
          "type mismatch (21 is humidity) -> invalid");
    CHECK(dm.sensors[1].count == 0, "mismatched data not added to the average");
}

int main(void)
{
    if (write_map("test_map.tmp") != 0)
    {
        printf("cannot create test file\n");
        return 1;
    }

    test_room_file();
    test_temperature();
    test_humidity();
    test_motion();
    test_door();
    test_smoke_gas();
    test_invalid();

    remove("test_map.tmp");

    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
