#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <stdint.h>
#include <time.h>

typedef struct
{
    uint64_t seq;
    uint16_t sensor_id;
    double value;
    time_t timestamp;
} sensor_data_t;

#endif