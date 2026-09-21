/*
 * test_c_abi.c —— C ABI 测试（纯 C 编译，验证动态库符号可被纯 C 消费）。
 * SQLite 全流程：数据源/建表/参数化/查询取值/NULL/事务/预编译语句/错误路径。
 */
#include <uORM/abi/uorm_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;
static int g_total = 0;

#define OK(cond, msg)                                                    \
    do {                                                                 \
        ++g_total;                                                       \
        if (!(cond)) {                                                   \
            ++g_failed;                                                  \
            fprintf(stderr, "FAIL(%d): %s | last_error=%s\n", __LINE__,  \
                    msg, uorm_last_error());                             \
        }                                                                \
    } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    /* 版本与驱动 */
    fprintf(stderr, "[stage] version\n");
    OK(uorm_version() != NULL && uorm_version()[0] == '0', "version string");
    OK(uorm_driver_count() >= 1, "at least one driver");

    char drv[32];
    int found_sqlite = 0;
    for (int i = 0; i < uorm_driver_count(); ++i) {
        if (uorm_driver_name(i, drv, sizeof(drv)) == UORM_OK &&
            strcmp(drv, "sqlite") == 0) found_sqlite = 1;
    }
    OK(found_sqlite, "sqlite driver registered");

    /* 错误的驱动名 */
    uorm_ds_options bad;
    memset(&bad, 0, sizeof(bad));
    bad.driver = "oracle";
    bad.database = "x";
    OK(uorm_ds_create(&bad) == NULL, "unknown driver rejected");
    OK(strstr(uorm_last_error(), "oracle") != NULL, "unknown driver error text");

    /* 非法参数 */
    OK(uorm_ds_create(NULL) == NULL, "null options rejected");

    /* 创建 SQLite 数据源 */
    remove("uorm_c_test.db");
    uorm_ds_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.driver = "sqlite";
    opt.database = "uorm_c_test.db";
    opt.pool_size = 2;
    opt.acquire_timeout_ms = 2000;
    uorm_data_source* ds = uorm_ds_create(&opt);
    fprintf(stderr, "[stage] ds created\n");
    OK(ds != NULL, "sqlite data source");
    if (!ds) return 1;

    long long affected = 0;
    OK(uorm_ds_execute(ds,
        "CREATE TABLE items (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT, price REAL, note TEXT)",
        NULL, 0, &affected) == UORM_OK, "create table");

    /* 参数化插入 */
    uorm_param p[3];
    p[0].type = UORM_TYPE_STRING; p[0].s = "Apple";
    p[1].type = UORM_TYPE_DOUBLE; p[1].f64 = 3.5;
    p[2].type = UORM_TYPE_NULL;
    OK(uorm_ds_execute(ds, "INSERT INTO items (name, price, note) VALUES (?, ?, ?)",
                       p, 3, &affected) == UORM_OK && affected == 1, "insert apple");

    p[0].s = "Banana"; p[1].f64 = 2.5;
    OK(uorm_ds_execute(ds, "INSERT INTO items (name, price, note) VALUES (?, ?, ?)",
                       p, 3, &affected) == UORM_OK, "insert banana");

    /* 查询 + 取值 */
    uorm_result* rs = NULL;
    OK(uorm_ds_query(ds, "SELECT id, name, price, note FROM items WHERE price >= ? ORDER BY price DESC",
                     &(uorm_param){ .type = UORM_TYPE_DOUBLE, .f64 = 1.0 }, 1, &rs) == UORM_OK,
       "query");
    OK(rs != NULL && uorm_result_row_count(rs) == 2, "query rows");
    OK(uorm_result_column_count(rs) == 4, "query columns");
    OK(strcmp(uorm_result_column_name(rs, 1), "name") == 0, "column name");
    OK(uorm_value_type(rs, 0, 1) == UORM_TYPE_STRING, "value type string");
    OK(strcmp(uorm_value_string(rs, 0, 1), "Apple") == 0, "first row is Apple (price desc)");
    OK(uorm_value_double(rs, 0, 2) == 3.5, "apple price");
    OK(uorm_value_int64(rs, 0, 0) > 0, "apple id");
    OK(uorm_value_is_null(rs, 0, 3) == 1, "note is null");
    OK(uorm_value_string(rs, 0, 3)[0] == '\0', "null string is empty");
    uorm_result_destroy(rs);

    /* 越界访问安全 */
    OK(uorm_value_double(NULL, 0, 0) == 0.0, "null result safe");
    OK(uorm_result_column_name(rs == NULL ? NULL : rs, 99) == NULL, "column range safe");

    /* 事务：提交 */
    uorm_connection* conn = NULL;
    fprintf(stderr, "[stage] acquired\n");
    OK(uorm_ds_acquire(ds, &conn) == UORM_OK && conn != NULL, "acquire");
    OK(uorm_conn_ping(conn) != 0, "ping");
    OK(uorm_conn_begin(conn) == UORM_OK, "begin");
    p[0].s = "Cherry"; p[1].f64 = 12.0; p[2].type = UORM_TYPE_STRING; p[2].s = "fresh";
    OK(uorm_conn_execute(conn, "INSERT INTO items (name, price, note) VALUES (?, ?, ?)",
                         p, 3, NULL) == UORM_OK, "txn insert");
    OK(uorm_conn_commit(conn) == UORM_OK, "commit");

    /* 事务：回滚 */
    OK(uorm_conn_begin(conn) == UORM_OK, "begin2");
    p[0].s = "Ghost";
    OK(uorm_conn_execute(conn, "INSERT INTO items (name, price, note) VALUES (?, ?, ?)",
                         p, 3, NULL) == UORM_OK, "txn insert2");
    OK(uorm_conn_rollback(conn) == UORM_OK, "rollback");

    OK(uorm_ds_query(ds, "SELECT COUNT(*) FROM items", NULL, 0, &rs) == UORM_OK, "count query");
    OK(uorm_value_int64(rs, 0, 0) == 3, "3 rows after commit+rollback");
    uorm_result_destroy(rs);

    /* 预编译语句复用 */
    uorm_statement* stmt = NULL;
    OK(uorm_conn_prepare(conn, "SELECT price FROM items WHERE name = ?", &stmt) == UORM_OK, "prepare");
    OK(uorm_stmt_bind_string(stmt, 1, "Cherry") == UORM_OK, "bind cherry");
    OK(uorm_stmt_query(stmt, &rs) == UORM_OK && uorm_result_row_count(rs) == 1, "cherry row");
    OK(uorm_value_double(rs, 0, 0) == 12.0, "cherry price");
    uorm_result_destroy(rs);

    OK(uorm_stmt_bind_params(stmt,
        &(uorm_param){ .type = UORM_TYPE_STRING, .s = "Apple" }, 1) == UORM_OK, "bind array");
    OK(uorm_stmt_query(stmt, &rs) == UORM_OK && uorm_result_row_count(rs) == 1, "apple row");
    OK(uorm_value_double(rs, 0, 0) == 3.5, "apple price via stmt");
    uorm_result_destroy(rs);
    uorm_stmt_destroy(stmt);

    /* 池统计 */
    int idle = -1, in_use = -1, created = -1;
    OK(uorm_ds_stats(ds, &idle, &in_use, &created) == UORM_OK, "stats");
    OK(idle >= 1 && in_use >= 1, "stats values sane");

    /* 错误路径：SQL 错误码 + 错误信息 */
    int st = uorm_ds_query(ds, "SELECT * FROM no_such_table", NULL, 0, &rs);
    OK(st == UORM_ERR_SQL, "sql error code");
    OK(strlen(uorm_last_error()) > 0, "sql error text");

    /* 归还连接并销毁数据源 */
    uorm_conn_release(conn);
    uorm_ds_destroy(ds);

    /* 驱动名缓冲不足 */
    char tiny[2];
    OK(uorm_driver_name(0, tiny, sizeof(tiny)) == UORM_ERR_BUFFER_TOO_SMALL, "buffer too small");

    remove("uorm_c_test.db");

    printf("C ABI 测试: %d/%d 通过\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
