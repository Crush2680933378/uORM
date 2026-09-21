// 文件说明：
// C++20 协程版异步 API 测试（需要 C++20 与 asio）。
// 验证：协程查询/执行、顺序组合、事务协程、异常传播、spawn->future 桥接。
//
// 注意：协程一律使用命名协程函数而非捕获型 lambda 协程——
// 老版本 GCC（<=13，含 CI 的 Ubuntu GCC）对后者存在内部编译器错误（ICE）。

#include "uORM/async/Coroutine.h"
#include "uORM/orm/ORM.h"

#include <cassert>
#include <cstdio>
#include <string>

static int g_failed = 0;
static int g_total = 0;

#define OK(cond)                                                        \
    do {                                                                \
        ++g_total;                                                      \
        if (!(cond)) {                                                  \
            ++g_failed;                                                 \
            std::fprintf(stderr, "FAIL(%d): %s\n", __LINE__, #cond);    \
        }                                                               \
    } while (0)

// 顺序协程组合：建表 -> 插入 5 行 -> 返回 SUM(v)
// 使用协程函数而非捕获型 lambda（规避老版本 GCC 的 ICE）
static asio::awaitable<int> seedWork(uORM::DataSource& d) {
    co_await uORM::executeCoro(d,
        "CREATE TABLE IF NOT EXISTS coro_t (id INTEGER PRIMARY KEY AUTOINCREMENT, v INT)");
    for (int i = 1; i <= 5; ++i) {
        co_await uORM::executeCoro(d, "INSERT INTO coro_t (v) VALUES (?)",
                                   {uORM::SqlValue(static_cast<long long>(i * 10))});
    }
    auto r = co_await uORM::queryCoro(d, "SELECT COUNT(*), SUM(v) FROM coro_t");
    OK(r.rows.size() == 1);
    co_return static_cast<int>(uORM::valueToInt64(r.rows[0][1]));
}

// 事务体内普通函数（非协程，由 transactionCoro 在事务中调用）
static void insert111(uORM::IConnection& c) {
    uORM::executeUpdate(c, "INSERT INTO coro_t (v) VALUES (111)");
}

int main() {
    uORM::DataSourceConfig cfg;
    cfg.params.driver = "sqlite";
    cfg.params.database = "uorm_coro_test.db";
    cfg.poolSize = 4;
    uORM::DataSource ds(cfg);
    std::remove("uorm_coro_test.db");

    // [1] 顺序协程组合：建表 -> 插入 -> 查询
    {
        auto fut = uORM::spawn(seedWork(ds));
        int total = fut.get();
        OK(total == 150);
        std::printf("[1] 顺序协程 OK, sum=%d\n", total);
    }

    // [2] 事务协程：提交
    {
        auto f = uORM::spawn(uORM::transactionCoro(ds, insert111));
        f.get();
        auto r = uORM::spawn(uORM::queryCoro(ds, "SELECT COUNT(*) FROM coro_t")).get();
        OK(uORM::valueToInt64(r.rows[0][0]) == 6);
        std::printf("[2] 协程事务提交 OK\n");
    }

    // [3] 事务协程：异常回滚
    {
        auto f = uORM::spawn(uORM::transactionCoro(ds, [](uORM::IConnection& c) {
            insert111(c);
            throw std::runtime_error("rollback on purpose");
        }));
        bool threw = false;
        try { f.get(); } catch (const std::exception&) { threw = true; }
        OK(threw);
        auto r = uORM::spawn(uORM::queryCoro(ds, "SELECT COUNT(*) FROM coro_t")).get();
        OK(uORM::valueToInt64(r.rows[0][0]) == 6);
        std::printf("[3] 协程事务回滚 OK\n");
    }

    // [4] 并发协程：8 个同时查询
    {
        std::vector<std::future<uORM::QueryResult>> futs;
        for (int i = 0; i < 8; ++i) {
            futs.push_back(uORM::spawn(uORM::queryCoro(ds, "SELECT ? + ?",
                {uORM::SqlValue(static_cast<long long>(i)), uORM::SqlValue(static_cast<long long>(100))})));
        }
        bool allOk = true;
        for (int i = 0; i < 8; ++i) {
            auto r = futs[i].get();
            if (uORM::valueToInt64(r.rows[0][0]) != 100 + i) allOk = false;
        }
        OK(allOk);
        std::printf("[4] 并发协程 OK\n");
    }

    // [5] 异常经协程传播到 future
    {
        auto f = uORM::spawn(uORM::queryCoro(ds, "SELECT * FROM no_such_coro_table"));
        bool threw = false;
        try { f.get(); } catch (const uORM::SqlError&) { threw = true; }
        OK(threw);
        std::printf("[5] 协程异常传播 OK\n");
    }

    std::remove("uorm_coro_test.db");
    std::printf("协程测试: %d/%d 通过\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
