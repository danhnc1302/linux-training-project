/*
 * Storage manager thread (Step 6)
 *
 * An element is only marked "stored" in sbuffer AFTER the INSERT succeeds
 * (peek -> insert -> commit). While the database is down, the measurements
 * simply stay in sbuffer, so nothing is lost.
 * No sbuffer lock is held during database I/O.
 */
#include <stdio.h>

#include "common.h"
#include "logger.h"
#include "sbuffer.h"
#include "sensor_debug.h"
#include "storagemgr.h"

void *storagemgr_thread(void *arg)
{
    sbuffer_t *buffer = (sbuffer_t *)arg;
    storagemgr_t stor;
    sensor_data_t data;

    logger_log("STOR", "Thread started");

    storagemgr_init(&stor, DB_PATH, SQL_RETRY_DELAY_MS);

    if (storagemgr_connect(&stor, true) == SQL_RESULT_FAILED)
    {
        gateway_request_stop("unable to connect to SQL server");
        goto out;
    }

    while (sbuffer_peek_storagemgr(buffer, &data) == SBUFFER_SUCCESS)
    {
        /* Retry the SAME measurement until it is stored */
        while (storagemgr_insert(&stor, &data) != 0)
        {
            sql_result_t result;

            logger_log("STOR", "Connection to SQL server lost.");
            stor.state = SQL_LOST;
            storagemgr_close(&stor);

            printf("[STORAGEMGR] DB lost, %zu measurement(s) kept in sbuffer\n",
                   sbuffer_size(buffer));

            /* Reconnect without creating a new file: a renamed or deleted
               database is reported, not silently replaced by an empty one */
            result = storagemgr_connect(&stor, false);
            if (result == SQL_RESULT_FAILED)
            {
                gateway_request_stop("unable to connect to SQL server");
                goto out;
            }
            if (result == SQL_RESULT_STOPPED)
            {
                goto out;
            }
        }

        sbuffer_commit_storagemgr(buffer);

#ifdef DEBUG
        printf("[STORAGEMGR] seq=%lu id=%u type=%s value=%.2f%s ts=%ld stored\n",
               (unsigned long)data.seq, data.sensor_id,
               sensor_type_name(data.type), data.value,
               sensor_type_unit(data.type), (long)data.timestamp);
#endif
    }

out:
    storagemgr_close(&stor);

    printf("[STORAGEMGR] Exit (%lu row(s) stored)\n", (unsigned long)stor.stored);
    logger_log("STOR", "Thread stopped");

    return NULL;
}
