/*
 * Storage manager (Step 6) - SQLite access
 *
 * Connection policy:
 *  - At start: open (create the file if needed) and create table SensorData.
 *  - When an INSERT fails or the database file was renamed/deleted:
 *    "Connection to SQL server lost.", then reconnect (without creating a
 *    new file) up to 3 times, waiting SQL_RETRY_DELAY_MS between attempts.
 *  - After 3 failed attempts: "Unable to connect to SQL server." and the
 *    whole gateway stops.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "common.h"
#include "logger.h"
#include "storagemgr.h"

static const char SQL_CREATE_TABLE[] =
    "CREATE TABLE IF NOT EXISTS SensorData ("
    " id    INTEGER NOT NULL,"
    " type  INTEGER NOT NULL,"
    " value REAL    NOT NULL,"
    " ts    INTEGER NOT NULL);";

static const char SQL_TABLE_EXISTS[] =
    "SELECT count(*) FROM sqlite_master "
    "WHERE type = 'table' AND name = 'SensorData';";

static const char SQL_INSERT[] =
    "INSERT INTO SensorData (id, type, value, ts) VALUES (?, ?, ?, ?);";

static bool is_shutdown(void)
{
    return gateway_stopping();
}

/* Sleep ms, wake up early if the gateway stops. Return false if stopped. */
static bool wait_ms(int ms)
{
    struct timespec step = { 0, 100 * 1000000L };

    for (int waited = 0; waited < ms; waited += 100)
    {
        if (is_shutdown())
        {
            return false;
        }
        nanosleep(&step, NULL);
    }
    return !is_shutdown();
}

/* Return 1 if table exists, 0 if not, -1 on error */
static int table_exists(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    int result = -1;

    if (sqlite3_prepare_v2(db, SQL_TABLE_EXISTS, -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
    {
        result = sqlite3_column_int(stmt, 0) > 0;
    }

    sqlite3_finalize(stmt);
    return result;
}

void storagemgr_init(storagemgr_t *s, const char *db_path, int retry_delay_ms)
{
    memset(s, 0, sizeof(*s));
    s->db_path = db_path;
    s->retry_delay_ms = retry_delay_ms;
    s->state = SQL_DOWN;
}

void storagemgr_close(storagemgr_t *s)
{
    sqlite3_finalize(s->insert);       /* NULL is a harmless no-op */
    s->insert = NULL;

    sqlite3_close(s->db);
    s->db = NULL;
}

int storagemgr_open(storagemgr_t *s, bool allow_create)
{
    int flags = SQLITE_OPEN_READWRITE | (allow_create ? SQLITE_OPEN_CREATE : 0);
    struct stat st;
    int exists;

    if (sqlite3_open_v2(s->db_path, &s->db, flags, NULL) != SQLITE_OK)
    {
        fprintf(stderr, "[STORAGEMGR] open %s: %s\n", s->db_path,
                s->db != NULL ? sqlite3_errmsg(s->db) : "out of memory");
        storagemgr_close(s);
        return -1;
    }

    sqlite3_busy_timeout(s->db, SQL_BUSY_TIMEOUT_MS);

    /* Reading sqlite_master also checks the file is a valid, unlocked DB */
    exists = table_exists(s->db);
    if (exists < 0 ||
        sqlite3_exec(s->db, SQL_CREATE_TABLE, NULL, NULL, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(s->db, SQL_INSERT, -1, &s->insert, NULL) != SQLITE_OK ||
        stat(s->db_path, &st) != 0)
    {
        fprintf(stderr, "[STORAGEMGR] setup %s: %s\n", s->db_path,
                sqlite3_errmsg(s->db));
        storagemgr_close(s);
        return -1;
    }

    s->dev = st.st_dev;
    s->ino = st.st_ino;

    logger_log("STOR", "Connection to SQL server established.");
    if (!exists)
    {
        logger_log("STOR", "New table SensorData created.");
    }

    return 0;
}

sql_result_t storagemgr_connect(storagemgr_t *s, bool allow_create)
{
    for (s->attempts = 1; s->attempts <= SQL_MAX_ATTEMPTS; s->attempts++)
    {
        if (is_shutdown())
        {
            return SQL_RESULT_STOPPED;
        }

        if (storagemgr_open(s, allow_create) == 0)
        {
            s->state = SQL_CONNECTED;
            s->attempts = 0;
            return SQL_RESULT_OK;
        }

        printf("[STORAGEMGR] connection attempt %d/%d failed\n",
               s->attempts, SQL_MAX_ATTEMPTS);

        if (s->attempts < SQL_MAX_ATTEMPTS && !wait_ms(s->retry_delay_ms))
        {
            return SQL_RESULT_STOPPED;
        }
    }

    s->state = SQL_DOWN;
    logger_log("STOR", "Unable to connect to SQL server.");
    return SQL_RESULT_FAILED;
}

/* The file must still be the one we opened (detect rename / delete) */
static bool file_still_there(const storagemgr_t *s)
{
    struct stat st;

    return stat(s->db_path, &st) == 0 &&
           st.st_dev == s->dev &&
           st.st_ino == s->ino;
}

int storagemgr_insert(storagemgr_t *s, const sensor_data_t *data)
{
    int rc;

    if (s->state != SQL_CONNECTED || !file_still_there(s))
    {
        return -1;
    }

    sqlite3_bind_int(s->insert, 1, (int)data->sensor_id);
    sqlite3_bind_int(s->insert, 2, (int)data->type);
    sqlite3_bind_double(s->insert, 3, data->value);
    sqlite3_bind_int64(s->insert, 4, (sqlite3_int64)data->timestamp);

    rc = sqlite3_step(s->insert);
    sqlite3_reset(s->insert);

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "[STORAGEMGR] insert: %s\n", sqlite3_errmsg(s->db));
        return -1;
    }

    s->stored++;
    return 0;
}
