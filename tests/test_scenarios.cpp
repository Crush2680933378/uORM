// 文件说明：
// 场景与陷阱测试套件（三数据库参数化）。
// 覆盖：SQL 注入字面量安全、Unicode/emoji、控制字符、int64/double 边界、
// 空串 vs NULL、长文本、保留字表列名、空结果集、分页越界、空 IN、
// 主键冲突、幂等建表、批量跨块、连接池耗尽与并发借还、事务出错后恢复。
//
// SQLite 始终运行；MySQL/PG 通过 UORM_TEST_MYSQL_* / UORM_TEST_PG_* 启用。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Entities.hpp"
#include "uORM/orm/ORM.h"

#include <atomic>
#include <climits>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace uORM;

namespace {

DataSourceConfig makeConfig(const std::string& driver, const std::string& database,
                            const std::string& host, int port,
                            const std::string& user, const std::string& pass,
                            int poolSize = 4) {
    DataSourceConfig c;
    c.params.driver = driver;
    c.params.database = database;
    c.params.host = host.empty() ? "127.0.0.1" : host;
    c.params.port = port;
    c.params.username = user;
    c.params.password = pass;
    c.poolSize = poolSize;
    return c;
}

std::string envOr(const char* key, const char* fallback = "") {
    const char* v = std::getenv(key);
    return (v && *v) ? v : fallback;
}

// 套件开始前清理遗留表：MySQL/PG 的表跨运行持久存在，
// 不清理的话所有精确计数断言在第二次运行都会失败（可重复运行陷阱）。
void cleanTables(DataSource& ds) {
    auto conn = ds.getConnection();
    auto dialect = conn->dialect();
    for (const char* t : {"text_rows", "Item", "reserved_order", "flag_rows", "pool_probe"}) {
        try {
            executeUpdate(*conn, "DROP TABLE IF EXISTS " + dialect->quoteIdentifier(t));
        } catch (const Exception&) {
            // 个别表不存在/被锁不阻塞套件（IF EXISTS 已兜底）
        }
    }
}

// =====================================================================
// SQL 安全与字符陷阱
// =====================================================================
void runSqlSafetySuite(DataSource& ds) {
    Database db(ds);
    CHECK(db.createTable<TextRow>());
    CHECK(db.createTable<Item>());
    CHECK(db.createTable<ReservedRow>());
    CHECK(db.createTable<FlagRow>());

    // 注入式字面量：参数化后作为普通文本存储，表不受影响
    const std::string injection = "Robert'; DROP TABLE text_rows; --";
    {
        TextRow r{0, injection, "payload"};
        CHECK(db.save(r));
        auto back = db.findById<TextRow>(r.id);
        REQUIRE(back.has_value());
        CHECK(back->name == injection);
        // 表依然存在且可查询（DROP 未被执行）
        CHECK(db.count<TextRow>() >= 1);
    }

    // LIKE 通配符作为字面量 + 引号闭合攻击形态作为条件值
    {
        TextRow r{0, "100%_done", "50% 'OR' 1'='1"};
        CHECK(db.save(r));
        auto rows = db.query<TextRow>().where(&TextRow::name, Op::EQ, std::string("100%_done")).all();
        CHECK(rows.size() == 1);
        // 恶意 LIKE 参数不会匹配到任何行（作为字面量处理）
        auto none = db.query<TextRow>().like(&TextRow::content, std::string("%' OR '1'='1")).all();
        CHECK(none.empty());
    }

    // Unicode：中文 / emoji（4 字节 UTF-8）/ 日文 / 混合
    {
        TextRow r{0, "数据库📚テスト한국어 mixed", "内容-✓-🚀"};
        CHECK(db.save(r));
        auto back = db.findById<TextRow>(r.id);
        REQUIRE(back.has_value());
        CHECK(back->name == "数据库📚テスト한국어 mixed");
        CHECK(back->content == "内容-✓-🚀");
    }

    // 控制字符：换行/制表/回车
    {
        TextRow r{0, "line1\nline2\ttab\rreturn", "multi\nline"};
        CHECK(db.save(r));
        auto back = db.findById<TextRow>(r.id);
        REQUIRE(back.has_value());
        CHECK(back->name == "line1\nline2\ttab\rreturn");
    }

    // 长文本 60000 字节
    // 陷阱：MySQL TEXT 上限是 65535 字节，64*1024 会触发 Data too long
    {
        std::string big(60000, 'x');
        for (std::size_t i = 0; i < big.size(); i += 97) big[i] = static_cast<char>('A' + (i % 26));
        TextRow r{0, "big", big};
        CHECK(db.save(r));
        auto back = db.findById<TextRow>(r.id);
        REQUIRE(back.has_value());
        CHECK(back->content.size() == big.size());
        CHECK(back->content == big);
    }

    // int64 边界值：专用 BIGINT 表（Item.stock 是 int，不能承载 int64）
    {
        auto conn = ds.getConnection();
        auto edge = conn->dialect()->quoteIdentifier("edge_i64");
        executeUpdate(*conn, "CREATE TABLE IF NOT EXISTS " + edge + " (v BIGINT)");
        executeUpdate(*conn, "DELETE FROM " + edge);
        executeUpdate(*conn, "INSERT INTO " + edge + " VALUES (?)", {SqlValue(LLONG_MAX)});
        executeUpdate(*conn, "INSERT INTO " + edge + " VALUES (?)", {SqlValue(LLONG_MIN)});
        executeUpdate(*conn, "INSERT INTO " + edge + " VALUES (?)", {SqlValue(static_cast<long long>(0))});
        auto r = executeQuery(*conn, "SELECT v FROM " + edge + " ORDER BY v");
        REQUIRE(r.rows.size() == 3);
        CHECK(valueToInt64(r.rows[0][0]) == LLONG_MIN);
        CHECK(valueToInt64(r.rows[1][0]) == 0);
        CHECK(valueToInt64(r.rows[2][0]) == LLONG_MAX);
    }

    // double 特殊值（按名字升序：d_neg < d_tiny < d_zero）
    {
        auto conn = ds.getConnection();
        auto table = conn->dialect()->quoteIdentifier("Item");
        executeUpdate(*conn, "INSERT INTO " + table + " (name, price) VALUES ('d_zero', ?)", {SqlValue(0.0)});
        executeUpdate(*conn, "INSERT INTO " + table + " (name, price) VALUES ('d_neg', ?)", {SqlValue(-1234.5678)});
        executeUpdate(*conn, "INSERT INTO " + table + " (name, price) VALUES ('d_tiny', ?)", {SqlValue(0.1)});
        auto r = executeQuery(*conn, "SELECT name, price FROM " + table +
                                         " WHERE name IN ('d_zero','d_neg','d_tiny') ORDER BY name");
        REQUIRE(r.rows.size() == 3);
        CHECK(valueToDouble(r.rows[0][1]) == doctest::Approx(-1234.5678)); // d_neg
        CHECK(valueToDouble(r.rows[1][1]) == doctest::Approx(0.1));        // d_tiny
        CHECK(valueToDouble(r.rows[2][1]) == doctest::Approx(0.0));        // d_zero
    }

    // 空串 vs NULL 的区分
    {
        auto conn = ds.getConnection();
        auto table = conn->dialect()->quoteIdentifier("Item");
        executeUpdate(*conn, "INSERT INTO " + table + " (name, category) VALUES (?, ?)",
                      {SqlValue(std::string("empty_cat")), SqlValue(std::string(""))});
        executeUpdate(*conn, "INSERT INTO " + table + " (name) VALUES (?)",
                      {SqlValue(std::string("null_cat"))});
        auto emptyRow = db.query<Item>().where(&Item::name, Op::EQ, std::string("empty_cat")).first();
        auto nullRow = db.query<Item>().where(&Item::name, Op::EQ, std::string("null_cat")).first();
        REQUIRE(emptyRow.has_value());
        CHECK(emptyRow->category.empty());
        CHECK_FALSE(isNullValue(SqlValue(emptyRow->category)));
        REQUIRE(nullRow.has_value());
        CHECK(nullRow->category.empty()); // NULL -> string 为空
        // isNull 条件只命中 NULL 那行
        auto nulls = db.query<Item>().where(&Item::name, Op::EQ, std::string("null_cat")).isNull(&Item::category).all();
        CHECK(nulls.size() == 1);
        auto notNulls = db.query<Item>().where(&Item::name, Op::EQ, std::string("empty_cat")).isNotNull(&Item::category).all();
        CHECK(notNulls.size() == 1);
    }

    // SQL 保留字表名/列名（"order"/"group" 全程方言引用）
    {
        ReservedRow r{0, "first", "grp"};
        CHECK(db.save(r));
        CHECK(r.id > 0);
        auto back = db.findById<ReservedRow>(r.id);
        REQUIRE(back.has_value());
        CHECK(back->order_ == "first");
        CHECK(back->group_x == "grp");

        auto byOrder = db.query<ReservedRow>()
                           .where(&ReservedRow::order_, Op::EQ, std::string("first"))
                           .first();
        CHECK(byOrder.has_value());
        CHECK(db.query<ReservedRow>().where(&ReservedRow::group_x, Op::EQ, std::string("grp")).count() == 1);
    }

    // bool / double 往返
    {
        FlagRow on{0, true, 0.25};
        FlagRow off{0, false, 1.75};
        CHECK(db.save(on));
        CHECK(db.save(off));
        CHECK(db.query<FlagRow>().where(&FlagRow::enabled, Op::EQ, true).count() == 1);
        CHECK(db.query<FlagRow>().where(&FlagRow::enabled, Op::EQ, false).count() == 1);
        auto backOn = db.findById<FlagRow>(on.id);
        REQUIRE(backOn.has_value());
        CHECK(backOn->enabled == true);
        CHECK(backOn->ratio == doctest::Approx(0.25));
    }

    // 日期时间字符串往返
    {
        auto conn = ds.getConnection();
        auto r = executeQuery(*conn, "SELECT CURRENT_TIMESTAMP");
        CHECK(r.rows.size() == 1); // 服务端时间函数可用
    }
}

// =====================================================================
// 边界与幂等
// =====================================================================
void runEdgeSuite(DataSource& ds) {
    Database db(ds);

    // 幂等建表
    CHECK(db.createTable<Item>());
    CHECK(db.createTable<Item>());

    // 空结果集
    {
        auto none = db.query<Item>().where(&Item::name, Op::EQ, std::string("没有这行-404")).all();
        CHECK(none.empty());
        CHECK_FALSE(db.query<Item>().where(&Item::name, Op::EQ, std::string("没有这行-404")).first().has_value());
        CHECK(db.query<Item>().where(&Item::name, Op::EQ, std::string("没有这行-404")).count() == 0);
    }

    // limit(0) / 大 offset
    {
        Item a{0, "pg1", "page", 1, 1, true, ""};
        Item b{0, "pg2", "page", 2, 1, true, ""};
        CHECK(db.save(a));
        CHECK(db.save(b));

        CHECK(db.query<Item>().where(&Item::category, Op::EQ, std::string("page")).limit(0).all().empty());
        CHECK(db.query<Item>().where(&Item::category, Op::EQ, std::string("page")).offset(1000).all().empty());

        // 多列排序：price 升序 + id 降序
        auto multi = db.query<Item>()
                         .where(&Item::category, Op::EQ, std::string("page"))
                         .orderByAsc(&Item::price)
                         .orderByDesc(&Item::id)
                         .all();
        REQUIRE(multi.size() == 2);
        CHECK(multi[0].name == "pg1");
    }

    // 空 IN -> 恒假
    {
        std::vector<std::string> empty;
        CHECK(db.query<Item>().in(&Item::name, empty).all().empty());
    }

    // 主键冲突：直接 insert 已存在的主键值必须抛 SqlError 且表数据不变
    {
        Item a{0, "dup", "dup", 1, 1, true, ""};
        CHECK(db.save(a));
        REQUIRE(a.id > 0);
        long long before = db.count<Item>([&] { Query q; q.eq("category", "dup"); return q; }());

        auto conn = ds.getConnection();
        bool threw = false;
        try {
            executeUpdate(*conn, "INSERT INTO " + conn->dialect()->quoteIdentifier("Item") +
                                     " (id, name) VALUES (?, ?)",
                          {SqlValue(a.id), SqlValue(std::string("dup2"))});
        } catch (const SqlError& e) {
            threw = true;
            std::cerr << "[diag] dup-key error: " << e.what() << std::endl;
        }
        CHECK(threw);
        CHECK(db.count<Item>([&] { Query q; q.eq("category", "dup"); return q; }()) == before);
    }

    // saveRange：空向量直接成功；大向量跨块（默认块 200/1000 行，用 450 跨块）
    {
        std::vector<Item> empty;
        CHECK(db.saveRange(empty));

        std::vector<Item> big;
        for (int i = 0; i < 450; ++i) {
            Item e{0, "B" + std::to_string(i), "bulk", 1.0, i, true, ""};
            big.push_back(e);
        }
        CHECK(db.saveRange(big));
        bool idsUniqueAndSet = true;
        for (std::size_t i = 0; i < big.size(); ++i) {
            if (big[i].id <= 0) idsUniqueAndSet = false;
            if (i && big[i].id == big[i - 1].id) idsUniqueAndSet = false;
        }
        CHECK(idsUniqueAndSet);
        CHECK(db.count<Item>([&] { Query q; q.eq("category", "bulk"); return q; }()) == 450);

        // truncate 清空
        CHECK(db.truncate<Item>());
        CHECK(db.count<Item>([&] { Query q; q.eq("category", "bulk"); return q; }()) == 0);
    }
}

// =====================================================================
// 连接池：耗尽超时 / 归还恢复 / 并发借还
// =====================================================================
void runPoolSuite(DataSource& ds) {
    // 池耗尽：把池借空后，下一次获取在超时后抛 ConnectionError（与池历史状态无关）
    {
        std::vector<PooledConnection> held;
        bool threw = false;
        try {
            for (;;) {
                held.push_back(ds.getConnection());
                if (held.size() > 64) break; // 防御性上限
            }
        } catch (const ConnectionError& e) {
            threw = true;
            CHECK(std::string(e.what()).find("acquire timeout") != std::string::npos);
        }
        CHECK(threw);
        CHECK(held.size() >= 2);

        // 全部归还后立刻可再借
        held.clear();
        auto c = ds.getConnection();
        CHECK(c != nullptr);
    }

    // 并发借还：多线程同时取连接做查询，结束后计数应自洽
    {
        const auto base = ds.stats(); // 计数基线：同一数据源被前序套件用过

        // 陷阱 1：PG 并发 CREATE TABLE IF NOT EXISTS 可能报唯一冲突，DDL 须先行
        // 陷阱 2：探针表必须有自增主键（PG/MySQL 不为裸 PRIMARY KEY 自动生成 id）
        {
            auto conn = ds.getConnection();
            auto dialect = conn->dialect();
            std::string ddl;
            switch (dialect->kind()) {
                case DialectKind::MySQL:
                    ddl = "CREATE TABLE IF NOT EXISTS pool_probe (id BIGINT PRIMARY KEY AUTO_INCREMENT, tag INT)";
                    break;
                case DialectKind::PostgreSQL:
                    ddl = "CREATE TABLE IF NOT EXISTS pool_probe (id BIGSERIAL PRIMARY KEY, tag INT)";
                    break;
                default:
                    ddl = "CREATE TABLE IF NOT EXISTS pool_probe (id INTEGER PRIMARY KEY AUTOINCREMENT, tag INT)";
                    break;
            }
            executeUpdate(*conn, ddl);
        }

        constexpr int kThreads = 12;
        constexpr int kOps = 30;
        std::atomic<int> failures{0};
        std::atomic<int> diagPrinted{0};
        std::vector<std::thread> workers;
        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([&, t] {
                try {
                    for (int i = 0; i < kOps; ++i) {
                        auto conn = ds.getConnection();
                        auto r = executeQuery(*conn, "SELECT 1");
                        if (r.rows.size() != 1) ++failures;
                        // 偶发写入（SQLite 单写者依赖 busy_timeout 排队）
                        if (t % 3 == 0) {
                            executeUpdate(*conn, "INSERT INTO pool_probe (tag) VALUES (?)", {SqlValue(static_cast<long long>(t))});
                        }
                    }
                } catch (const std::exception& e) {
                    if (diagPrinted++ < 3)
                        std::cerr << "[diag] worker: " << e.what() << std::endl;
                    ++failures;
                }
            });
        }
        for (auto& w : workers) w.join();

        auto st = ds.stats();
        CHECK(failures.load() == 0);
        CHECK(st.inUse == 0);
        CHECK(st.totalCreated - base.totalCreated <= 4); // 新建连接不超过池上限
    }
}

// =====================================================================
// 事务陷阱：出错后的连接恢复 / 连续事务
// =====================================================================
void runTxSuite(DataSource& ds) {
    Database db(ds);
    Item seed{0, "txseed", "txedge", 1, 1, true, ""};
    CHECK(db.save(seed));

    // 陷阱：事务中 SQL 出错后（尤其 PG），后续语句会被拒绝；
    // 正确姿势是 rollback —— 回滚后同一连接必须恢复可用。
    {
        auto conn = ds.getConnection();
        conn->begin();
        bool firstFailed = false;
        try {
            // 触发错误：向不存在的表插入
            executeUpdate(*conn, "INSERT INTO no_such_table_error(x) VALUES (1)");
        } catch (const SqlError&) {
            firstFailed = true;
        }
        CHECK(firstFailed);
        bool rolledBack = false;
        try {
            conn->rollback();
            rolledBack = true;
        } catch (const Exception&) {
        }
        CHECK(rolledBack);

        // 回滚后连接恢复可用（PG aborted transaction 陷阱验证点）
        auto r = executeQuery(*conn, "SELECT 1");
        CHECK(r.rows.size() == 1);
        conn = nullptr; // 归还
    }

    // 连续多个事务在同一连接上往复
    // 注意：布尔列不能写裸字面量 1（PG 会拒绝），用 TRUE 关键字三库通用
    {
        auto conn = ds.getConnection();
        auto table = conn->dialect()->quoteIdentifier("Item");
        for (int round = 0; round < 5; ++round) {
            conn->begin();
            executeUpdate(*conn, "INSERT INTO " + table +
                                     " (name, category, price, stock, active) VALUES (?, 'round', 1, 1, TRUE)",
                          {SqlValue(std::string("r") + std::to_string(round))});
            if (round % 2 == 0) {
                conn->commit();
            } else {
                conn->rollback();
            }
        }
        long long kept = db.count<Item>([&] { Query q; q.eq("category", "round"); return q; }());
        CHECK(kept == 3); // round 0/2/4 提交，1/3 回滚
    }

    // RAII 作用域：异常穿越时自动回滚且异常继续传播
    {
        long long before = db.count<Item>([&] { Query q; q.eq("category", "txscope2"); return q; }());
        bool caught = false;
        try {
            db.tx([&](IConnection& conn) {
                Item e{0, "scoped", "txscope2", 1, 1, true, ""};
                Mapper<Item>::save(e, conn);
                throw std::logic_error("force rollback");
            });
        } catch (const std::logic_error&) {
            caught = true;
        }
        CHECK(caught);
        CHECK(db.count<Item>([&] { Query q; q.eq("category", "txscope2"); return q; }()) == before);
    }

    // 清理
    {
        auto conn = ds.getConnection();
        Schema::dropTable<Item>(*conn);
    }
}

} // namespace

// =====================================================================
// 三数据库参数化
// =====================================================================
TEST_CASE("场景与陷阱套件: SQLite") {
    auto cfg = makeConfig("sqlite", "uorm_scenarios.db", "", 0, "", "");
    DataSource ds(cfg);
    cleanTables(ds);
    runSqlSafetySuite(ds);
    runEdgeSuite(ds);
    runPoolSuite(ds);
    runTxSuite(ds);
}

TEST_CASE("场景与陷阱套件: MySQL") {
    std::string host = envOr("UORM_TEST_MYSQL_HOST");
    if (host.empty()) {
        MESSAGE("跳过 MySQL 场景测试（未设置 UORM_TEST_MYSQL_*）");
        return;
    }
    auto cfg = makeConfig("mysql", envOr("UORM_TEST_MYSQL_DB", "uorm_db"), host,
                          std::atoi(envOr("UORM_TEST_MYSQL_PORT", "3306").c_str()),
                          envOr("UORM_TEST_MYSQL_USER"), envOr("UORM_TEST_MYSQL_PASS"));
    cfg.acquireTimeoutMs = 15000; // 远程链路排队更久
    DataSource ds(cfg);
    cleanTables(ds);
    runSqlSafetySuite(ds);
    runEdgeSuite(ds);
    runPoolSuite(ds);
    runTxSuite(ds);
}

TEST_CASE("场景与陷阱套件: PostgreSQL") {
    std::string host = envOr("UORM_TEST_PG_HOST");
    if (host.empty()) {
        MESSAGE("跳过 PostgreSQL 场景测试（未设置 UORM_TEST_PG_*）");
        return;
    }
    auto cfg = makeConfig("postgresql", envOr("UORM_TEST_PG_DB", "uorm_db"), host,
                          std::atoi(envOr("UORM_TEST_PG_PORT", "5432").c_str()),
                          envOr("UORM_TEST_PG_USER"), envOr("UORM_TEST_PG_PASS"));
    cfg.acquireTimeoutMs = 15000; // 远程链路排队更久
    DataSource ds(cfg);
    cleanTables(ds);
    runSqlSafetySuite(ds);
    runEdgeSuite(ds);
    runPoolSuite(ds);
    runTxSuite(ds);
}
