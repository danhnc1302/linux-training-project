#ifndef DATAMGR_H
#define DATAMGR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "sensor_config.h"
#include "sensor_types.h"

#ifndef ROOM_SENSOR_FILE
#define ROOM_SENSOR_FILE "room_sensor.map"
#endif

#define MAX_SENSORS 64

typedef enum { STATE_COLLECTING, STATE_NORMAL, STATE_ALERT } sensor_state_t;

/* Which alert is active, so that too cold -> too hot is logged again */
typedef enum { ALERT_NONE, ALERT_LOW, ALERT_HIGH } alert_kind_t;

typedef struct                  /* one line of the room-sensor file */
{
    uint16_t      room;
    sensor_id_t   id;
    sensor_type_t type;
} sensor_info_t;

typedef struct
{
    sensor_info_t  info;
    double         window[RUN_AVG_LENGTH];  /* ring buffer for running avg */
    size_t         count, next;
    double         running_avg;
    sensor_state_t state;

    /* fields added to the provided skeleton (section 3.9 allows it) */
    alert_kind_t   alert;                   /* active alert kind           */
    bool           has_value;               /* at least one sample seen    */
    double         last_value;              /* latest value (door state)   */
    time_t         last_update;
} datamgr_sensor_t;

typedef struct
{
    datamgr_sensor_t sensors[MAX_SENSORS];
    size_t n;

    /* invalid IDs already logged: log once per ID, not on every sample */
    sensor_id_t invalid_ids[MAX_SENSORS];
    size_t n_invalid;
} datamgr_t;

typedef void (*sensor_handler_t)(datamgr_t *, datamgr_sensor_t *,
                                 const sensor_data_t *);

/* Load the room-sensor file. Return 0 on success, -1 on error. */
int datamgr_init(datamgr_t *dm, const char *path);

/* Validate and process one measurement.
 * Return the sensor entry, or NULL if the ID or type is invalid. */
datamgr_sensor_t *datamgr_process(datamgr_t *dm, const sensor_data_t *data);

const char *datamgr_state_name(sensor_state_t state);

/* Thread entry, arg = sbuffer_t * */
void *datamgr_thread(void *arg);

#endif
