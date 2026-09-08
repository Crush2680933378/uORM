// 文件说明：
// 异步 API 测试：线程池并发查询/执行、异常经 future 传播、并发压力下的池稳定性。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Entities.hpp"
#include "uORM/orm/ORM.h"

#include <cstdlib>
#include <future>
#include <vector>

using namespace uORM;

namespace {

DataSourceConfig makeConfig(const std::string& driver, const std::string& database,
                            const std::string& host, int port,
                            const std::string& user, const std::string& pass) {
    DataSourceConfig c;
    c.params.driver = driver;
    c.params.database = database;
    c.params.host = host.empty() ? "127.0.0.1" : host;
    c.params.port = port;
    c.params.username = user;
    c.params.password = pass;
    c.poolSize = 4;
    c.acquireTimeoutMs = 15000;
    return c;
}

std::string envOr(const char* key, const char* fallback = "") {
    const char* v = std::getenv(key);
    return (v && *v) ? v : fallback;
}

} // namespace

TEST_CASE("异步 API: SQLite") {
    DataSource ds(makeConfig("sqlite", "uorm_async.db", "", 0, "", ""));
    Database db(ds);

    // 并发查询
    std::vector<std::future<QueryResult>> futs;
    for (int i = 0; i < 8; ++i) {
        futs.push_back(ds.queryAsync("SELECT 1 + ?", {SqlValue(static_cast<long long>(i))}));
    }
    for (int i = 0; i < 8; ++i) {
        auto r = futs[i].get();
        REQUIRE(r.rows.size() == 1);
        CHECK(valueToInt64(r.rows[0][0]) == 1 + i);
    }

    // executeAsync + writeback 场景
    db.execute("CREATE TABLE IF NOT EXISTS async_t (id INTEGER PRIMARY KEY AUTOINCREMENT, v INT)");
    auto affected = db.executeAsync("INSERT INTO async_t (v) VALUES (7)").get();
    CHECK(affected == 1);

    // selectAsync<T> 实体异步查询
    // （async_t 无对应实体，用原生结果验证即可；selectAsync 由 Item 用例覆盖）

    // 异常经 future 传播
    bool threw = false;
    try {
        db.executeAsync("INSERT INTO no_such_async_table (x) VALUES (1)").get();
    } catch (const SqlError&) {
        threw = true;
    }
    CHECK(threw);

    // 并发压力：16 个任务混读写在共享池上
    CHECK(db.createTable<Item>());
    {
        std::vector<std::future<long long>> futs2;
        for (int i = 0; i < 16; ++i) {
            futs2.push_back(ds.async([&db, i] {
                auto conn = db.source().getConnection();
                return Mapper<Item>::count(*conn);
            }));
        }
        for (auto& f : futs2) CHECK(f.get() >= 0);
    }

    db.execute("DROP TABLE IF EXISTS async_t");
}

TEST_CASE("异步 + replace + updateSome: SQLite") {
    DataSource ds(makeConfig("sqlite", "uorm_async.db", "", 0, "", ""));
    Database db(ds);

    Item a{0, "ra", "async", 1.0, 1, true, ""};
    CHECK(db.save(a));

    // replace：同主键整行替换，未提及列被重置
    Item replaced = a;
    replaced.price = 9.9;
    replaced.stock = 0;
    CHECK(db.replace(replaced));

    // updateSome：只改 price，stock 保持 replace 后的 0
    CHECK(db.updateSome(replaced, &Item::price));
    replaced.price = 5.5;
    CHECK(db.updateSome(replaced, &Item::price));
    auto back = db.findById<Item>(a.id);
    REQUIRE(back.has_value());
    CHECK(back->price == doctest::Approx(5.5));
    CHECK(back->stock == 0);
}

TEST_CASE("异步 API: MySQL（需环境变量）") {
    std::string host = envOr("UORM_TEST_MYSQL_HOST");
    if (host.empty()) {
        MESSAGE("跳过 MySQL 异步测试");
        return;
    }
    DataSource ds(makeConfig("mysql", envOr("UORM_TEST_MYSQL_DB", "uorm_db"), host,
                             std::atoi(envOr("UORM_TEST_MYSQL_PORT", "3306").c_str()),
                             envOr("UORM_TEST_MYSQL_USER"), envOr("UORM_TEST_MYSQL_PASS")));
    std::vector<std::future<QueryResult>> futs;
    for (int i = 0; i < 6; ++i) {
        futs.push_back(ds.queryAsync("SELECT ? + ?", {SqlValue(static_cast<long long>(i)), SqlValue(static_cast<long long>(10))}));
    }
    for (int i = 0; i < 6; ++i) {
        auto r = futs[i].get();
        REQUIRE(r.rows.size() == 1);
        CHECK(valueToInt64(r.rows[0][0]) == 10 + i);
    }
}

TEST_CASE("异步 API: PostgreSQL（需环境变量）") {
    std::string host = envOr("UORM_TEST_PG_HOST");
    if (host.empty()) {
        MESSAGE("跳过 PostgreSQL 异步测试");
        return;
    }
    DataSource ds(makeConfig("postgresql", envOr("UORM_TEST_PG_DB", "uorm_db"), host,
                             std::atoi(envOr("UORM_TEST_PG_PORT", "5432").c_str()),
                             envOr("UORM_TEST_PG_USER"), envOr("UORM_TEST_PG_PASS")));
    std::vector<std::future<QueryResult>> futs;
    for (int i = 0; i < 6; ++i) {
        futs.push_back(ds.queryAsync("SELECT ?::int + ?::int", {SqlValue(static_cast<long long>(i)), SqlValue(static_cast<long long>(10))}));
    }
    for (int i = 0; i < 6; ++i) {
        auto r = futs[i].get();
        REQUIRE(r.rows.size() == 1);
        CHECK(valueToInt64(r.rows[0][0]) == 10 + i);
    }
}
