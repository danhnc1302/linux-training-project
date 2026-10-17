/*
 * Data manager thread
 * Copies one element out of sbuffer, then processes it OUTSIDE the lock.
 */
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "datamgr.h"
#include "logger.h"
#include "sbuffer.h"
#include "sensor_debug.h"

static void print_debug(const sensor_data_t *d, const datamgr_sensor_t *s)
{
#ifdef DEBUG
    if (s == NULL)
    {
        printf("[DATAMGR] seq=%lu id=%u type=%s INVALID\n",
               (unsigned long)d->seq, d->sensor_id, sensor_type_name(d->type));
        return;
    }

    printf("[DATAMGR] seq=%lu room=%u id=%u type=%s value=%.2f%s state=%s\n",
           (unsigned long)d->seq, s->info.room, d->sensor_id,
           sensor_type_name(d->type), d->value, sensor_type_unit(d->type),
           datamgr_state_name(s->state));
#else
    (void)d;
    (void)s;
#endif
}

void *datamgr_thread(void *arg)
{
    sbuffer_t *buffer = (sbuffer_t *)arg;
    datamgr_t *dm = calloc(1, sizeof(*dm));
    sensor_data_t data;
    bool ready;

    logger_log("DATA", "Thread started");

    ready = (dm != NULL && datamgr_init(dm, ROOM_SENSOR_FILE) == 0);
    if (!ready)
    {
        /* Without the sensor list nothing can be validated: stop gateway.
           Keep reading sbuffer (sets our flag) so elements are still freed. */
        logger_log("DATA", "Unable to load room-sensor file " ROOM_SENSOR_FILE);
        gateway_request_stop("room-sensor file error");
    }

    /* returns != SUCCESS only after shutdown, when nothing is left */
    while (sbuffer_read_datamgr(buffer, &data) == SBUFFER_SUCCESS)
    {
        if (ready)
        {
            print_debug(&data, datamgr_process(dm, &data));
        }
    }

    free(dm);
    logger_log("DATA", "Thread stopped");

    return NULL;
}
