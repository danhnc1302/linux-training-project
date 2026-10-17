/*
 * Test for the Storage manager (Step 6 verification):
 * create table, insert, rename / lock the database file, retries,
 * and measurements kept in sbuffer while the database is down.
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "sbuffer.h"
#include "storagemgr.h"

#define TEST_DB     "test_storage.db"
#define TEST_DB_MV  "test_storage_moved.db"
#define MAX_LOGS    32



static char g_logs[MAX_LOGS][200];
static int g_log_count = 0;
static int g_passed = 0;
static int g_failed = 0;

void logger_log(const char *source, const char *message)
{
    if (g_log_count < MAX_LOGS)
    {
        snprintf(g_logs[g_log_count++], sizeof(g_logs[0]), "%s %s",
                 source, message);
    }
}

static int count_log(const char *text)
{
    int n = 0;

    for (int i = 0; i < g_log_count; i++)
    {
        n += (strstr(g_logs[i], text) != NULL);
    }
    return n;
}

#define CHECK(cond, name)                                         \
    do {                                                          \
        if (cond) { g_passed++; printf("  PASS  %s\n", name); }  \
        else      { g_failed++; printf("  FAIL  %s\n", name); }  \
    } while (0)

static int count_rows(const char *path)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int n = -1;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK &&
        sqlite3_prepare_v2(db, "SELECT count(*) FROM SensorData;", -1,
                           &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
    {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return n;
}

static sensor_data_t sample(sensor_id_t id, double value)
{
    sensor_data_t d;

    memset(&d, 0, sizeof(d));
    d.sensor_id = id;
    d.type = SENSOR_TEMPERATURE;
    d.value = value;
    d.timestamp = 1767225600;
    return d;
}

static void test_create_and_insert(void)
{
    storagemgr_t s;
    sensor_data_t d = sample(10, 21.5);

    printf("Create table and insert\n");
    g_log_count = 0;

    storagemgr_init(&s, TEST_DB, 50);
    CHECK(storagemgr_connect(&s, true) == SQL_RESULT_OK, "connect (create file)");
    CHECK(count_log("Connection to SQL server established.") == 1,
          "log 'Connection to SQL server established.'");
    CHECK(count_log("New table SensorData created.") == 1,
          "log 'New table SensorData created.'");

    CHECK(storagemgr_insert(&s, &d) == 0 && storagemgr_insert(&s, &d) == 0 &&
          storagemgr_insert(&s, &d) == 0, "3 inserts OK");
    CHECK(count_rows(TEST_DB) == 3, "3 rows in SensorData");
    storagemgr_close(&s);

    g_log_count = 0;
    CHECK(storagemgr_connect(&s, true) == SQL_RESULT_OK &&
          count_log("New table") == 0, "reconnect: table exists, not created again");
    storagemgr_close(&s);
}

static void test_rename(void)
{
    storagemgr_t s;
    sensor_data_t d = sample(10, 22.0);

    printf("Database file renamed\n");
    storagemgr_init(&s, TEST_DB, 50);
    storagemgr_connect(&s, true);

    rename(TEST_DB, TEST_DB_MV);
    CHECK(storagemgr_insert(&s, &d) == -1, "insert after rename -> connection lost");
    storagemgr_close(&s);

    g_log_count = 0;
    CHECK(storagemgr_connect(&s, false) == SQL_RESULT_FAILED,
          "reconnect without file -> 3 attempts fail");
    CHECK(s.state == SQL_DOWN && count_log("Unable to connect to SQL server.") == 1,
          "log 'Unable to connect to SQL server.'");
    CHECK(access(TEST_DB, F_OK) != 0, "no empty database silently created");

    rename(TEST_DB_MV, TEST_DB);
    CHECK(storagemgr_connect(&s, false) == SQL_RESULT_OK,
          "file renamed back -> reconnect OK");
    CHECK(storagemgr_insert(&s, &d) == 0 && count_rows(TEST_DB) == 4,
          "insert OK again, old rows kept (4)");
    storagemgr_close(&s);
}

static void test_lock(void)
{
    storagemgr_t s;
    sqlite3 *other = NULL;
    sensor_data_t d = sample(10, 23.0);

    printf("Database locked by another process\n");
    storagemgr_init(&s, TEST_DB, 50);
    storagemgr_connect(&s, true);

    sqlite3_open_v2(TEST_DB, &other, SQLITE_OPEN_READWRITE, NULL);
    sqlite3_exec(other, "BEGIN EXCLUSIVE;", NULL, NULL, NULL);

    CHECK(storagemgr_insert(&s, &d) == -1, "insert while locked -> fails after busy timeout");

    sqlite3_exec(other, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(other);

    CHECK(storagemgr_insert(&s, &d) == 0, "lock released -> insert OK");
    storagemgr_close(&s);
}

static void test_sbuffer_keeps_data(void)
{
    sbuffer_t *b;
    sensor_data_t d1 = sample(10, 1.0);
    sensor_data_t d2 = sample(11, 2.0);
    sensor_data_t out;

    printf("sbuffer keeps data until stored\n");
    sbuffer_init(&b);
    sbuffer_insert(b, &d1);
    sbuffer_insert(b, &d2);

    sbuffer_read_datamgr(b, &out);
    sbuffer_read_datamgr(b, &out);

    CHECK(sbuffer_peek_storagemgr(b, &out) == SBUFFER_SUCCESS &&
          out.sensor_id == 10, "peek returns first element");
    CHECK(sbuffer_peek_storagemgr(b, &out) == SBUFFER_SUCCESS &&
          out.sensor_id == 10, "peek again (DB down) -> same element");
    CHECK(sbuffer_size(b) == 2, "Data manager done, but both elements kept (size 2)");

    sbuffer_commit_storagemgr(b);
    CHECK(sbuffer_size(b) == 1, "commit after INSERT -> element removed (size 1)");
    CHECK(sbuffer_peek_storagemgr(b, &out) == SBUFFER_SUCCESS &&
          out.sensor_id == 11, "next peek -> second element");
    sbuffer_commit_storagemgr(b);

    sbuffer_shutdown(b);
    CHECK(sbuffer_peek_storagemgr(b, &out) == SBUFFER_NO_DATA,
          "shutdown + empty -> SBUFFER_NO_DATA");
    sbuffer_free(&b);
}

int main(void)
{
    remove(TEST_DB);
    remove(TEST_DB_MV);

    test_create_and_insert();
    test_rename();
    test_lock();
    test_sbuffer_keeps_data();

    remove(TEST_DB);
    remove(TEST_DB_MV);

    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
