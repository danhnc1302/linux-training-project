/*
 * Data manager - processing logic (Step 5)
 *
 * All state here is local to the Data manager thread, so no lock is needed.
 * The sbuffer is only touched in datamgr_thread.c when reading an element.
 */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datamgr.h"
#include "logger.h"

#define LINE_MAX_LEN 128

static const struct
{
    const char *name;
    sensor_type_t type;
} TYPE_NAMES[] =
{
    { "temperature", SENSOR_TEMPERATURE },
    { "humidity",    SENSOR_HUMIDITY    },
    { "motion",      SENSOR_MOTION      },
    { "door",        SENSOR_DOOR        },
    { "smoke_gas",   SENSOR_SMOKE_GAS   },
};

static void log_data(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

static void log_data(const char *format, ...)
{
    char msg[200];          /* section 3.8: message max 200 bytes */
    va_list args;

    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    logger_log("DATA", msg);
}

/* ---------------- room-sensor file (section 3.6) ---------------- */

static int parse_type(const char *name, sensor_type_t *type)
{
    for (size_t i = 0; i < sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0]); i++)
    {
        if (strcmp(name, TYPE_NAMES[i].name) == 0)
        {
            *type = TYPE_NAMES[i].type;
            return 0;
        }
    }
    return -1;
}

static datamgr_sensor_t *find_sensor(datamgr_t *dm, sensor_id_t id)
{
    for (size_t i = 0; i < dm->n; i++)
    {
        if (dm->sensors[i].info.id == id)
        {
            return &dm->sensors[i];
        }
    }
    return NULL;
}

int datamgr_init(datamgr_t *dm, const char *path)
{
    char line[LINE_MAX_LEN];
    int line_no = 0;
    FILE *fp;

    memset(dm, 0, sizeof(*dm));

    fp = fopen(path, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "[DATAMGR] cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        unsigned room;
        unsigned id;
        char type_name[32];
        char extra[2];
        sensor_type_t type;
        char *p = line;

        line_no++;

        while (isspace((unsigned char)*p))
        {
            p++;
        }
        if (*p == '\0' || *p == '#')
        {
            continue;                       /* blank line or comment */
        }

        if (sscanf(p, "%u %u %31s %1s", &room, &id, type_name, extra) != 3 ||
            room > UINT16_MAX || id > UINT16_MAX ||
            parse_type(type_name, &type) != 0)
        {
            fprintf(stderr, "[DATAMGR] %s:%d: invalid line\n", path, line_no);
            fclose(fp);
            return -1;
        }

        if (find_sensor(dm, (sensor_id_t)id) != NULL)
        {
            fprintf(stderr, "[DATAMGR] %s:%d: duplicate sensor ID %u\n",
                    path, line_no, id);
            fclose(fp);
            return -1;
        }

        if (dm->n == MAX_SENSORS)
        {
            fprintf(stderr, "[DATAMGR] %s: more than %d sensors\n",
                    path, MAX_SENSORS);
            fclose(fp);
            return -1;
        }

        dm->sensors[dm->n].info.room = (uint16_t)room;
        dm->sensors[dm->n].info.id = (sensor_id_t)id;
        dm->sensors[dm->n].info.type = type;
        dm->sensors[dm->n].state = STATE_COLLECTING;
        dm->n++;
    }

    fclose(fp);
    return 0;
}

/* ---------------- helpers ---------------- */

/* Add a sample. Return true when the window is full (average valid). */
static bool update_running_avg(datamgr_sensor_t *s, double value)
{
    double sum = 0.0;

    s->window[s->next] = value;
    s->next = (s->next + 1) % RUN_AVG_LENGTH;

    if (s->count < RUN_AVG_LENGTH)
    {
        s->count++;
    }
    if (s->count < RUN_AVG_LENGTH)
    {
        return false;                       /* still collecting */
    }

    for (size_t i = 0; i < RUN_AVG_LENGTH; i++)
    {
        sum += s->window[i];
    }
    s->running_avg = sum / RUN_AVG_LENGTH;

    return true;
}

/* Compare the running average with [low, high].
 * Log only when entering an alert (or switching low <-> high),
 * not on every sample. */
static void check_range(datamgr_sensor_t *s, double low, double high,
                        const char *low_word, const char *high_word,
                        const char *quantity)
{
    alert_kind_t alert = ALERT_NONE;

    if (s->running_avg < low)
    {
        alert = ALERT_LOW;
    }
    else if (s->running_avg > high)
    {
        alert = ALERT_HIGH;
    }

    if (alert != ALERT_NONE && alert != s->alert)
    {
        log_data("The sensor node with %u reports it's too %s "
                 "(running avg %s = %.1f)",
                 (unsigned)s->info.id,
                 alert == ALERT_LOW ? low_word : high_word,
                 quantity, s->running_avg);
    }

    s->alert = alert;
    s->state = (alert == ALERT_NONE) ? STATE_NORMAL : STATE_ALERT;
}

/* ---------------- handlers, one per sensor type (section 3.3) ------------ */

static void handle_temperature(datamgr_t *dm, datamgr_sensor_t *s,
                               const sensor_data_t *d)
{
    (void)dm;

    if (update_running_avg(s, d->value))
    {
        check_range(s, TEMP_MIN_C, TEMP_MAX_C, "cold", "hot", "temperature");
    }
}

static void handle_humidity(datamgr_t *dm, datamgr_sensor_t *s,
                            const sensor_data_t *d)
{
    (void)dm;

    if (update_running_avg(s, d->value))
    {
        check_range(s, HUMIDITY_MIN, HUMIDITY_MAX, "dry", "humid", "humidity");
    }
}

/* Motion is an event: every event is logged */
static void handle_motion(datamgr_t *dm, datamgr_sensor_t *s,
                          const sensor_data_t *d)
{
    (void)dm;
    (void)d;

    log_data("The sensor node with %u detected motion", (unsigned)s->info.id);
    s->state = STATE_ALERT;
}

/* Door: log each change of state (the first event counts as a change) */
static void handle_door(datamgr_t *dm, datamgr_sensor_t *s,
                        const sensor_data_t *d)
{
    (void)dm;

    if (!s->has_value || s->last_value != d->value)
    {
        log_data("The sensor node with %u reports the door is %s",
                 (unsigned)s->info.id, d->value != 0.0 ? "open" : "closed");
    }

    s->state = (d->value != 0.0) ? STATE_ALERT : STATE_NORMAL;
}

/* Smoke/gas: instant threshold, no average (must react immediately) */
static void handle_smoke_gas(datamgr_t *dm, datamgr_sensor_t *s,
                             const sensor_data_t *d)
{
    bool alarm = (d->value >= GAS_ALARM_PPM);

    (void)dm;

    if (alarm && s->state != STATE_ALERT)
    {
        log_data("The sensor node with %u reports a smoke/gas alarm "
                 "(concentration = %.0f ppm)",
                 (unsigned)s->info.id, d->value);
    }

    s->state = alarm ? STATE_ALERT : STATE_NORMAL;
}

/* Handler table: a new sensor type = one line here + one function */
static const sensor_handler_t HANDLERS[] =
{
    [SENSOR_TEMPERATURE] = handle_temperature,
    [SENSOR_HUMIDITY]    = handle_humidity,
    [SENSOR_MOTION]      = handle_motion,
    [SENSOR_DOOR]        = handle_door,
    [SENSOR_SMOKE_GAS]   = handle_smoke_gas,
};

#define HANDLER_COUNT (sizeof(HANDLERS) / sizeof(HANDLERS[0]))

/* ---------------- public ---------------- */

/* Policy: log an invalid ID once, not on every sample */
static void report_invalid(datamgr_t *dm, sensor_id_t id)
{
    for (size_t i = 0; i < dm->n_invalid; i++)
    {
        if (dm->invalid_ids[i] == id)
        {
            return;
        }
    }

    if (dm->n_invalid < MAX_SENSORS)
    {
        dm->invalid_ids[dm->n_invalid++] = id;
    }

    log_data("Received sensor data with invalid sensor node ID %u",
             (unsigned)id);
}

datamgr_sensor_t *datamgr_process(datamgr_t *dm, const sensor_data_t *data)
{
    datamgr_sensor_t *s = find_sensor(dm, data->sensor_id);

    /* unknown ID, or ID known but type does not match the file */
    if (s == NULL ||
        s->info.type != data->type ||
        (size_t)data->type >= HANDLER_COUNT ||
        HANDLERS[data->type] == NULL)
    {
        report_invalid(dm, data->sensor_id);
        return NULL;
    }

    HANDLERS[data->type](dm, s, data);

    s->has_value = true;
    s->last_value = data->value;
    s->last_update = data->timestamp;

    return s;
}

const char *datamgr_state_name(sensor_state_t state)
{
    switch (state)
    {
    case STATE_COLLECTING: return "collecting";
    case STATE_NORMAL:     return "normal";
    case STATE_ALERT:      return "alert";
    default:               return "unknown";
    }
}
