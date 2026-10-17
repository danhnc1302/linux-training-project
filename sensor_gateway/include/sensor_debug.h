#ifndef SENSOR_DEBUG_H
#define SENSOR_DEBUG_H

#include "sensor_types.h"

static inline const char *sensor_type_name(sensor_type_t type)
{
    switch (type)
    {
    case SENSOR_TEMPERATURE: return "temperature";
    case SENSOR_HUMIDITY:    return "humidity";
    case SENSOR_MOTION:      return "motion";
    case SENSOR_DOOR:        return "door";
    case SENSOR_SMOKE_GAS:   return "smoke_gas";
    default:                 return "unknown";
    }
}

static inline const char *sensor_type_unit(sensor_type_t type)
{
    switch (type)
    {
    case SENSOR_TEMPERATURE: return "C";
    case SENSOR_HUMIDITY:    return "%RH";
    case SENSOR_SMOKE_GAS:   return "ppm";
    default:                 return "";
    }
}

#endif
