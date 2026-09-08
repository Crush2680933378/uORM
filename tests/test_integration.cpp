// 文件说明：
// uORM 集成测试：三数据库（SQLite/MySQL/PostgreSQL）完整 ORM 流程。
//
// - SQLite：始终运行（本地文件库，零依赖）
// - MySQL：设置环境变量 UORM_TEST_MYSQL_HOST/PORT/USER/PASS/DB 后运行，未设置则跳过
// - PostgreSQL：设置环境变量 UORM_TEST_PG_HOST/PORT/USER/PASS/DB 后运行，未设置则跳过

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "uORM/orm/ORM.h"
#include <cstdlib>
#include <iostream>

using namespace uORM;

// 注意：实体类型必须在全局命名空间（UORM_* 宏会在 uORM 命名空间内特化 TableMeta）
struct Item {
    long long id;
    std::string name;
    std::string category;
    double price;
    int stock;
    bool active;
    std::string created_at;
};
UORM_REFLECTION(Item, id, name, category, price, stock, active, created_at)

// 未注册的类：验证 TypedQuery 拒绝外来成员指针
struct NotMapped {
    std::string name;
};

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
    c.poolSize = 2;
    c.validateOnAcquire = true;
    return c;
}

std::string envOr(const char* key, const char* fallback = "") {
    const char* v = std::getenv(key);
    return (v && *v) ? v : fallback;
}

// 对一个数据源执行完整的 ORM 流程断言
void runFullSuite(DataSource& ds) {
    // 建表（先删后建）
    {
        auto conn = ds.getConnection();
        CHECK(Schema::dropTable<Item>(*conn));
        CHECK(Schema::createTable<Item>(*conn));
    }

    // 插入 + 自增主键写回
    long long id1 = 0, id2 = 0;
    {
        auto conn = ds.getConnection();
        Item a{0, "Apple", "fruit", 3.5, 10, true, ""};
        REQUIRE(Mapper<Item>::save(a, *conn));
        CHECK(a.id > 0);
        id1 = a.id;

        Item b{0, "Banana", "fruit", 2.5, 20, true, ""};
        REQUIRE(Mapper<Item>::save(b, *conn));
        id2 = b.id;
        CHECK(id2 != id1);
    }

    // 主键查询
    {
        auto conn = ds.getConnection();
        auto row = Mapper<Item>::findOne(*conn, "id = ?", id1);
        REQUIRE(row.has_value());
        CHECK(row->name == "Apple");
        CHECK(row->price == doctest::Approx(3.5));
        CHECK(row->active == true);
    }

    // 更新
    {
        auto conn = ds.getConnection();
        auto row = Mapper<Item>::findOne(*conn, "id = ?", id1);
        REQUIRE(row.has_value());
        row->price = 4.0;
        row->stock = 9;
        REQUIRE(Mapper<Item>::update(*row, *conn));
        auto after = Mapper<Item>::findOne(*conn, "id = ?", id1);
        REQUIRE(after.has_value());
        CHECK(after->price == doctest::Approx(4.0));
        CHECK(after->stock == 9);
    }

    // Query 构造器 + count
    {
        auto conn = ds.getConnection();
        Query q;
        q.eq("category", "fruit").gt("price", 3.0);
        CHECK(Mapper<Item>::count(*conn, q) == 1);

        auto rows = Mapper<Item>::select(*conn, q);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].name == "Apple");
    }

    // 分页
    {
        auto conn = ds.getConnection();
        Query q;
        q.orderBy("id").limit(1).offset(1);
        auto rows = Mapper<Item>::select(*conn, q);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].id == id2);
    }

    // Upsert：按主键存在则更新
    {
        auto conn = ds.getConnection();
        auto row = Mapper<Item>::findOne(*conn, "id = ?", id2);
        REQUIRE(row.has_value());
        row->price = 9.9;
        REQUIRE(Mapper<Item>::saveOrUpdate(*row, *conn));
        auto after = Mapper<Item>::findOne(*conn, "id = ?", id2);
        REQUIRE(after.has_value());
        CHECK(after->price == doctest::Approx(9.9));
        CHECK(Mapper<Item>::count(*conn) == 2);
    }

    // 事务：提交
    {
        auto before = [&] { auto c = ds.getConnection(); return Mapper<Item>::count(*c); }();
        bool committed = false;
        try {
            withTransaction(ds, [&](IConnection& conn) {
                Item c{0, "Cherry", "fruit", 12.0, 5, true, ""};
                Mapper<Item>::save(c, conn);
                committed = true;
            });
        } catch (const Exception& e) {
            FAIL("transaction commit failed: ", e.what());
        }
        CHECK(committed);
        auto after = [&] { auto c = ds.getConnection(); return Mapper<Item>::count(*c); }();
        CHECK(after == before + 1);
    }

    // 事务：回滚
    {
        auto before = [&] { auto c = ds.getConnection(); return Mapper<Item>::count(*c); }();
        try {
            withTransaction(ds, [&](IConnection& conn) {
                Item d{0, "Durian", "fruit", 30.0, 1, true, ""};
                Mapper<Item>::save(d, conn);
                throw std::runtime_error("rollback on purpose");
            });
        } catch (const std::runtime_error&) {
            // 预期
        }
        auto after = [&] { auto c = ds.getConnection(); return Mapper<Item>::count(*c); }();
        CHECK(after == before);
    }

    // 原生查询（QueryResult）
    {
        auto conn = ds.getConnection();
        std::string table = conn->dialect()->quoteIdentifier(TableMeta<Item>::name);
        auto r = executeQuery(*conn, "SELECT name, price FROM " + table +
                                     " WHERE price >= ? ORDER BY price DESC",
                              {SqlValue{2.0}});
        REQUIRE(r.columnCount() == 2);
        CHECK(r.rows.size() >= 2);
        // 降序：第一行价格 >= 第二行
        CHECK(valueToDouble(r.rows[0][1]) >= valueToDouble(r.rows[1][1]));
    }

    // NULL 语义
    {
        auto conn = ds.getConnection();
        std::string table = conn->dialect()->quoteIdentifier(TableMeta<Item>::name);
        executeUpdate(*conn, "INSERT INTO " + table +
                                 " (name, category, price, stock, active) VALUES (?, ?, ?, ?, ?)",
                      {SqlValue("NullCat"), SqlValue(nullptr), SqlValue(1.5), SqlValue(1LL), SqlValue(1LL)});
        // category 为 NULL —— 类型化映射读回 string 为空
        auto row = Mapper<Item>::findOne(*conn, "name = ?", std::string("NullCat"));
        REQUIRE(row.has_value());
        CHECK(row->category.empty());
    }

    // 删除
    {
        auto conn = ds.getConnection();
        auto row = Mapper<Item>::findOne(*conn, "name = ?", std::string("NullCat"));
        REQUIRE(row.has_value());
        REQUIRE(Mapper<Item>::remove(*row, *conn));
        CHECK_FALSE(Mapper<Item>::findOne(*conn, "name = ?", std::string("NullCat")).has_value());
    }

    // 批量插入 + Database 门面（saveRange/findById/count/tx 全走一遍）
    {
        Database db(ds);

        // 事务内批量插入
        CHECK(db.tx([&](IConnection& conn) {
            std::vector<Item> batch;
            for (int i = 0; i < 7; ++i) {
                Item e{0, "Batch" + std::to_string(i), "batch", 1.0 * i, i, true,
                       "2026-01-01 00:00:00"};
                batch.push_back(e);
            }
            return Mapper<Item>::saveRange(batch, conn); // 事务内批量
        }));

        long long batchCount = 0;
        {
            auto conn = ds.getConnection();
            batchCount = Mapper<Item>::count(*conn, [&] {
                Query q; q.eq("category", "batch"); return q;
            }());
        }
        CHECK(batchCount == 7);

        // 事务外批量插入（默认门面路径），自增主键写回且互不相同
        std::vector<Item> batch2;
        for (int i = 0; i < 3; ++i) {
            Item e{0, "Solo" + std::to_string(i), "solo", 5.0, 1, true,
                   "2026-01-01 00:00:00"};
            batch2.push_back(e);
        }
        CHECK(db.saveRange(batch2));
        bool idsOk = true;
        for (const auto& e : batch2) if (e.id <= 0) idsOk = false;
        CHECK(idsOk);

        // findById 走主键
        auto one = db.findById<Item>(batch2[1].id);
        REQUIRE(one.has_value());
        CHECK(one->name == "Solo1");
        CHECK(one->price == doctest::Approx(5.0));

        // 分块路径（rowsPerChunk 边界不崩、行数正确）
        std::vector<Item> big;
        for (int i = 0; i < 205; ++i) {
            Item e{0, "Big" + std::to_string(i), "big", 1.0, 1, true,
                   "2026-01-01 00:00:00"};
            big.push_back(e);
        }
        CHECK(db.saveRange(big));
        bool bigIdsOk = true;
        for (const auto& e : big) if (e.id <= 0) bigIdsOk = false;
        CHECK(bigIdsOk);
        CHECK(db.count<Item>([&] { Query q; q.eq("category", "big"); return q; }()) == 205);
    }

    // 类型安全查询（成员指针列）+ RAII 事务作用域
    {
        Database db(ds);

        // where + orderBy + limit
        auto rows = db.query<Item>()
                        .where(&Item::category, Op::EQ, std::string("batch"))
                        .orderByAsc(&Item::id)
                        .limit(3)
                        .all();
        REQUIRE(rows.size() == 3);
        CHECK(rows[0].name == "Batch0");
        CHECK(rows[1].name == "Batch1");

        // 嵌套括号 + OR
        auto mixed = db.query<Item>()
                         .where(&Item::category, Op::EQ, std::string("solo"))
                         .beginGroup()
                            .where(&Item::name, Op::EQ, std::string("Solo0"))
                            .orWhere(&Item::name, Op::EQ, std::string("Solo1"))
                         .endGroup()
                         .all();
        CHECK(mixed.size() == 2);

        // in（按名称列，上一节的 Solo2 已被删除）
        auto inRows = db.query<Item>()
                          .in(&Item::name, std::vector<std::string>{"Solo0", "Solo1"})
                          .all();
        CHECK(inRows.size() == 2);

        // set + update
        CHECK(db.query<Item>()
                   .where(&Item::name, Op::EQ, std::string("Solo0"))
                   .set(&Item::price, 7.7)
                   .update());
        auto s0 = db.query<Item>().where(&Item::name, Op::EQ, std::string("Solo0")).first();
        REQUIRE(s0.has_value());
        CHECK(s0->price == doctest::Approx(7.7));

        // remove
        CHECK(db.query<Item>().where(&Item::name, Op::EQ, std::string("Solo2")).remove());
        CHECK_FALSE(db.query<Item>().where(&Item::name, Op::EQ, std::string("Solo2")).first().has_value());

        // RAII 事务作用域：提交
        {
            auto tx = db.txBegin();
            Item e{0, "TxScope", "tx", 1.0, 1, true, "2026-01-01 00:00:00"};
            CHECK(Mapper<Item>::save(e, *tx));
            tx->commit();
        }
        CHECK(db.query<Item>().where(&Item::name, Op::EQ, std::string("TxScope")).first().has_value());

        // RAII 事务作用域：未提交析构自动回滚
        {
            auto tx = db.txBegin();
            Item e{0, "TxGhost", "tx", 1.0, 1, true, "2026-01-01 00:00:00"};
            CHECK(Mapper<Item>::save(e, *tx));
            // 无 commit
        }
        CHECK_FALSE(db.query<Item>().where(&Item::name, Op::EQ, std::string("TxGhost")).first().has_value());

        // 防全表误操作；外来成员指针被拒绝
        CHECK_THROWS_AS(db.query<Item>().update(), OrmError);
        CHECK_THROWS_AS(db.query<Item>().remove(), OrmError);
        CHECK_THROWS_AS(db.query<Item>().where(&NotMapped::name, Op::EQ, std::string("x")).all(),
                        OrmError);
    }

    // 清理
    {
        auto conn = ds.getConnection();
        Schema::dropTable<Item>(*conn);
    }
}

} // namespace

TEST_CASE("SQLite 全流程") {
    auto cfg = makeConfig("sqlite", "uorm_itest.db", "", 0, "", "");
    DataSource ds(cfg);
    runFullSuite(ds);
}

TEST_CASE("MySQL 全流程（需环境变量）") {
    std::string host = envOr("UORM_TEST_MYSQL_HOST");
    if (host.empty()) {
        MESSAGE("跳过 MySQL 集成测试（未设置 UORM_TEST_MYSQL_*）");
        return;
    }
    auto cfg = makeConfig("mysql", envOr("UORM_TEST_MYSQL_DB", "uorm_db"),
                          host, std::atoi(envOr("UORM_TEST_MYSQL_PORT", "3306").c_str()),
                          envOr("UORM_TEST_MYSQL_USER"), envOr("UORM_TEST_MYSQL_PASS"));
    DataSource ds(cfg);
    runFullSuite(ds);
}

TEST_CASE("PostgreSQL 全流程（需环境变量）") {
    std::string host = envOr("UORM_TEST_PG_HOST");
    if (host.empty()) {
        MESSAGE("跳过 PostgreSQL 集成测试（未设置 UORM_TEST_PG_*）");
        return;
    }
    auto cfg = makeConfig("postgresql", envOr("UORM_TEST_PG_DB", "uorm_db"),
                          host, std::atoi(envOr("UORM_TEST_PG_PORT", "5432").c_str()),
                          envOr("UORM_TEST_PG_USER"), envOr("UORM_TEST_PG_PASS"));
    DataSource ds(cfg);
    runFullSuite(ds);
}
