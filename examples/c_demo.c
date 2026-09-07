/*
 * c_demo.c —— 纯 C 程序使用 uORM C ABI 的最小示例。
 * 演示：数据源创建、建表、参数化插入、查询取值、事务、错误处理。
 * 编译后与 uorm_c 动态库及其驱动依赖一起运行。
 */
#include <uORM/abi/uorm_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(status, what)                                              \
    do {                                                                 \
        if ((status) != UORM_OK) {                                       \
            fprintf(stderr, "[%s] %s 失败 (%d): %s\n", __func__, what,   \
                    (status), uorm_last_error());                         \
            exit(1);                                                     \
        }                                                                \
    } while (0)

static void run_query_demo(uorm_data_source* ds) {
    uorm_result* rs = NULL;
    uorm_param min_price;
    min_price.type = UORM_TYPE_DOUBLE;
    min_price.f64 = 1.0;
    int st = uorm_ds_query(ds, "SELECT name, price FROM products WHERE price >= ? ORDER BY price DESC",
                           &min_price, 1, &rs);
    CHECK(st, "查询");
    int rows = uorm_result_row_count(rs);
    int cols = uorm_result_column_count(rs);
    printf("查询结果 %d 行 x %d 列\n", rows, cols);
    for (int r = 0; r < rows; ++r) {
        printf("  %s = %.2f\n",
               uorm_value_string(rs, r, 0),
               uorm_value_double(rs, r, 1));
    }
    uorm_result_destroy(rs);
}

int main(void) {
    printf("uORM C ABI 版本: %s\n", uorm_version());

    char name[32];
    for (int i = 0; i < uorm_driver_count(); ++i) {
        if (uorm_driver_name(i, name, sizeof(name)) == UORM_OK)
            printf("  可用驱动: %s\n", name);
    }

    /* 1. 创建 SQLite 数据源（池大小 1，避免 :memory: 多库问题用文件库） */
    uorm_ds_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.driver = "sqlite";
    opt.database = "uorm_c_demo.db";
    opt.pool_size = 2;
    uorm_data_source* ds = uorm_ds_create(&opt);
    if (!ds) {
        fprintf(stderr, "创建数据源失败: %s\n", uorm_last_error());
        return 1;
    }

    /* 2. 建表 + 参数化插入 */
    long long affected = 0;
    CHECK(uorm_ds_execute(ds,
        "CREATE TABLE IF NOT EXISTS products ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, price REAL)",
        NULL, 0, &affected), "建表");

    uorm_param ins[2];
    ins[0].type = UORM_TYPE_STRING; ins[0].s = "iPhone";
    ins[1].type = UORM_TYPE_DOUBLE; ins[1].f64 = 999.5;
    CHECK(uorm_ds_execute(ds, "INSERT INTO products (name, price) VALUES (?, ?)",
                          ins, 2, &affected), "插入");
    printf("插入影响 %lld 行\n", affected);

    ins[0].s = "Mug"; ins[1].f64 = 19.9;
    CHECK(uorm_ds_execute(ds, "INSERT INTO products (name, price) VALUES (?, ?)",
                          ins, 2, &affected), "插入2");

    /* 3. 查询 */
    run_query_demo(ds);

    /* 4. 事务：提交一笔 */
    uorm_connection* conn = NULL;
    CHECK(uorm_ds_acquire(ds, &conn), "借出连接");
    CHECK(uorm_conn_begin(conn), "begin");
    ins[0].s = "Cherry"; ins[1].f64 = 12.0;
    CHECK(uorm_conn_execute(conn, "INSERT INTO products (name, price) VALUES (?, ?)",
                            ins, 2, NULL), "事务内插入");
    CHECK(uorm_conn_commit(conn), "commit");
    printf("事务提交成功, last_insert_id=%lld\n", uorm_conn_last_insert_id(conn));

    /* 5. 事务：回滚一笔 */
    CHECK(uorm_conn_begin(conn), "begin2");
    ins[0].s = "Ghost"; ins[1].f64 = 0.0;
    CHECK(uorm_conn_execute(conn, "INSERT INTO products (name, price) VALUES (?, ?)",
                            ins, 2, NULL), "事务内插入2");
    CHECK(uorm_conn_rollback(conn), "rollback");
    printf("事务回滚成功\n");

    /* 6. 预编译语句复用 */
    uorm_statement* stmt = NULL;
    CHECK(uorm_conn_prepare(conn, "SELECT price FROM products WHERE name = ?", &stmt), "prepare");
    CHECK(uorm_stmt_bind_string(stmt, 1, "iPhone"), "bind");
    uorm_result* rs = NULL;
    CHECK(uorm_stmt_query(stmt, &rs), "stmt query");
    if (uorm_result_row_count(rs) > 0)
        printf("预编译查询 iPhone price = %.2f\n", uorm_value_double(rs, 0, 0));
    uorm_result_destroy(rs);

    CHECK(uorm_stmt_bind_string(stmt, 1, "Mug"), "rebind");
    CHECK(uorm_stmt_query(stmt, &rs), "stmt query2");
    if (uorm_result_row_count(rs) > 0)
        printf("预编译查询 Mug price = %.2f\n", uorm_value_double(rs, 0, 0));
    uorm_result_destroy(rs);
    uorm_stmt_destroy(stmt);
    uorm_conn_release(conn);

    /* 7. 错误处理演示 */
    rs = NULL;
    int bad = uorm_ds_query(ds, "SELECT * FROM no_such_table", NULL, 0, &rs);
    printf("错误 SQL 返回码=%d, 错误信息=%s\n", bad, uorm_last_error());

    uorm_ds_destroy(ds);
    printf("C ABI 演示完成\n");
    return 0;
}
