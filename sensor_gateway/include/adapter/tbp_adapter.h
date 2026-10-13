#include <stdint.h>

typedef struct
{
    uint16_t sensor_id;
    double value;
    uint64_t timestamp;
} __attribute__((packed)) tbp_frame_t;