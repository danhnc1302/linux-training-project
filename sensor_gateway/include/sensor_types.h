#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <stdint.h>
#include <time.h>

typedef uint16_t sensor_id_t;

typedef enum
{
    SENSOR_TEMPERATURE = 1,   /* degC                     */
    SENSOR_HUMIDITY    = 2,   /* %RH                      */
    SENSOR_MOTION      = 3,   /* 1 = motion detected      */
    SENSOR_DOOR        = 4,   /* 0 = closed, 1 = open     */
    SENSOR_SMOKE_GAS   = 5    /* ppm                      */
} sensor_type_t;

typedef struct
{
    uint64_t      seq;        /* debug: assigned by connmgr          */
    const char   *protocol;   /* debug: "TBP", "HLP", "EVP", "SGP"   */
    sensor_id_t   sensor_id;  /* unique in the system                */
    sensor_type_t type;       /* sensor type                         */
    double        value;      /* unit depends on sensor type         */
    time_t        timestamp;  /* measurement time, seconds, UTC      */
} sensor_data_t;

#endif
