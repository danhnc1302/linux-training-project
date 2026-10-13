#include <stdio.h>

#include "common.h"
#include "storagemgr.h"
#include "logger.h"
#include "sbuffer.h"

void *storagemgr_thread(void *arg)
{
    sbuffer_t *buffer = (sbuffer_t *)arg;
    sensor_data_t data;

    logger_log("STOR", "Thread started");
    printf("[STORAGEMGR] Start...\n");

    while (!g_gateway.shutdown)
    {
        int ret = sbuffer_read_storagemgr(buffer, &data);

        if (ret == SBUFFER_SUCCESS)
        {
            printf("[STORAGEMGR] seq=%lu id=%u temp=%.2f ts=%ld\n",
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

    printf("[STORAGEMGR] Exit\n");

    logger_log("STOR", "Thread stopped");

    return NULL;
}