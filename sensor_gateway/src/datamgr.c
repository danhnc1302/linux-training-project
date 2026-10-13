#include <stdio.h>

#include "common.h"
#include "datamgr.h"
#include "logger.h"
#include "sbuffer.h"

void *datamgr_thread(void *arg)
{
    sbuffer_t *buffer = (sbuffer_t *)arg;
    sensor_data_t data;

    logger_log("DATA", "Thread started");
    printf("[DATAMGR] START...\n");

    while (!g_gateway.shutdown)
    {
        int ret = sbuffer_read_datamgr(buffer, &data);
        if (ret == SBUFFER_SUCCESS)
        {
            printf("[DATAMGR] seq=%lu id=%u temp=%.2f ts=%ld\n",
                data.seq,
                data.sensor_id,
                data.value,
                data.timestamp);
        }
        else
        {
            break;
        }
    }

    printf("[DATAMGR] Exit\n");

    logger_log("DATA", "Thread stopped");

    return NULL;
}