#include "adapter/tbp_adapter.h"
#include "adapter/protocol_adapter.h"

parse_result_t tbp_parse(
    const uint8_t *buf,
    size_t len,
    size_t *consumed,
    sensor_data_t *data)
{
    if (len < 18)
    {
        return PARSE_NEED_MORE;
    }

    tbp_frame_t frame;

    memcpy(&frame,
           buf,
           sizeof(frame));

    data->sensor_id = frame.sensor_id;
    data->value = frame.value;
    data->timestamp = frame.timestamp;

    *consumed = sizeof(frame);

    return PARSE_OK;
}