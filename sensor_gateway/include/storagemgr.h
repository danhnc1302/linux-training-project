#ifndef STORAGEMGR_H
#define STORAGEMGR_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <sqlite3.h>

#include "sensor_types.h"

#ifndef DB_PATH
#define DB_PATH "gateway.db"
#endif

#define SQL_MAX_ATTEMPTS     3       /* connection attempts before giving up */
#define SQL_RETRY_DELAY_MS   2000    /* wait between two attempts            */
#define SQL_BUSY_TIMEOUT_MS  1000    /* wait for a lock held by another app  */

typedef enum { SQL_CONNECTED, SQL_LOST, SQL_DOWN } sql_state_t;

typedef struct
{
    sqlite3      *db;
    sqlite3_stmt *insert;
    sql_state_t   state;
    int           attempts;
    const char   *db_path;

    /* fields added to the provided skeleton (section 3.9 allows it) */
    int           retry_delay_ms;
    dev_t         dev;          /* identity of the opened file, to detect */
    ino_t         ino;          /* that it was renamed / deleted          */
    uint64_t      stored;       /* number of rows inserted                */
} storagemgr_t;

typedef enum
{
    SQL_RESULT_OK = 0,
    SQL_RESULT_FAILED = -1,     /* 3 attempts failed                    */
    SQL_RESULT_STOPPED = -2     /* gateway is shutting down             */
} sql_result_t;

void storagemgr_init(storagemgr_t *s, const char *db_path, int retry_delay_ms);

/* One connection attempt. allow_create: create the file if missing. */
int storagemgr_open(storagemgr_t *s, bool allow_create);

void storagemgr_close(storagemgr_t *s);

/* Up to SQL_MAX_ATTEMPTS attempts, waiting retry_delay_ms in between. */
sql_result_t storagemgr_connect(storagemgr_t *s, bool allow_create);

/* INSERT one measurement. Return 0, or -1 if the connection is lost. */
int storagemgr_insert(storagemgr_t *s, const sensor_data_t *data);

/* Thread entry, arg = sbuffer_t * */
void *storagemgr_thread(void *arg);

#endif
