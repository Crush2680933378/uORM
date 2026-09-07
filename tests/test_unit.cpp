// 文件说明：
// uORM 单元测试（doctest）：不依赖数据库的部分。
// - Query 构造器
// - SQL 方言（占位符转换/标识符引用/类型归一化）
// - 反射宏（UORM_TABLE_* 与 UORM_REFLECTION）
// - SqlValue 转换辅助
// - Web Router 模式匹配

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "uORM/orm/ORM.h"
#include "uORM/web/Router.h"

using namespace uORM;

// ---------------- 测试用实体 ----------------
struct Person {
    int id;
    std::string name;
    int age;
};
UORM_REFLECTION(Person, id, name, age)

struct LegacyRow {
    int uid;
    std::string title;
    double score;
};
UORM_TABLE_BEGIN(LegacyRow, "legacy_rows")
    UORM_FIELD(uid, "uid", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(title, "title", NOT NULL),
    UORM_FIELD(score, "score", DEFAULT 0)
UORM_TABLE_END()

// ---------------- Query ----------------
TEST_CASE("Query 基本条件与参数顺序") {
    Query q;
    q.eq("name", "Tom").gt("age", 18).like("name", "%T%");
    CHECK(q.getWhere() == "name = ? AND age > ? AND name LIKE ?");
    REQUIRE(q.getParams().size() == 3);
    CHECK(valueToString(q.getParams()[0]) == "Tom");
    CHECK(valueToInt64(q.getParams()[1]) == 18);
}

TEST_CASE("Query or_ / 括号分组") {
    Query q;
    q.eq("a", 1).or_().beginGroup().eq("b", 2).or_().eq("c", 3).endGroup().eq("d", 4);
    CHECK(q.getWhere() == "a = ? OR (b = ? OR c = ?) AND d = ?");
    CHECK(q.getParams().size() == 4);
}

TEST_CASE("Query in/between/isNull") {
    Query q;
    std::vector<int> ids{1, 2, 3};
    q.in("id", ids).between("age", 10, 20).isNotNull("name");
    CHECK(q.getWhere() == "id IN (?, ?, ?) AND age BETWEEN ? AND ? AND name IS NOT NULL");
    CHECK(q.getParams().size() == 5);

    Query e;
    std::vector<int> empty;
    e.in("id", empty);
    CHECK(e.getWhere() == "1=0");
}

TEST_CASE("Query 排序分页") {
    Query q;
    q.orderBy("a").orderBy("b", false).limit(10).offset(20);
    CHECK(q.getOrderBy() == " ORDER BY a ASC, b DESC");
    CHECK(q.getLimit() == " LIMIT 10");
    CHECK(q.getOffset() == " OFFSET 20");
}

TEST_CASE("Query 投影与分组") {
    Query q;
    q.selectRaw("category, COUNT(*) AS cnt").groupBy("category").having("COUNT(*) > ?");
    q.havingParam(SqlValue{2});
    CHECK(q.getSelectRaw() == "category, COUNT(*) AS cnt");
    CHECK(q.getGroupBy() == " GROUP BY category");
    CHECK(q.getHaving() == " HAVING COUNT(*) > ?");
    CHECK(q.getParams().size() == 1);
}

// ---------------- 方言 ----------------
TEST_CASE("PG 占位符转换（跳过字符串与标识符）") {
    std::string sql = "SELECT * FROM t WHERE a = ? AND b = 'x?y' AND c = \"we?rd\" AND d = ?";
    std::string converted = placeholdersToDollar(sql);
    CHECK(converted == "SELECT * FROM t WHERE a = $1 AND b = 'x?y' AND c = \"we?rd\" AND d = $2");

    CHECK(placeholdersToDollar("INSERT INTO t VALUES (?, 'a''b?', ?)") ==
          "INSERT INTO t VALUES ($1, 'a''b?', $2)");
}

TEST_CASE("标识符引用") {
    MySQLDialect my;
    PostgreSQLDialect pg;
    CHECK(my.quoteIdentifier("col") == "`col`");
    CHECK(pg.quoteIdentifier("col") == "\"col\"");
}

TEST_CASE("PG 类型名归一化") {
    PostgreSQLDialect pg;
    CHECK(pg.normalizeTypeName("DATETIME") == "TIMESTAMP");
    CHECK(pg.normalizeTypeName("DOUBLE") == "DOUBLE PRECISION");
    CHECK(pg.normalizeTypeName("TINYINT(1)") == "BOOLEAN");
    CHECK(pg.normalizeTypeName("VARCHAR(100)") == "VARCHAR(100)");
}

TEST_CASE("类型映射按方言区分") {
    MySQLDialect my;
    PostgreSQLDialect pg;
    CHECK(std::string(sqlTypeNameFor<bool>(my)) == "TINYINT(1)");
    CHECK(std::string(sqlTypeNameFor<bool>(pg)) == "BOOLEAN");
    CHECK(std::string(sqlTypeNameFor<long long>(pg)) == "BIGINT");
    CHECK(std::string(sqlTypeNameFor<std::string>(pg)) == "VARCHAR(255)");
}

// ---------------- 反射 ----------------
TEST_CASE("UORM_REFLECTION 宏注册") {
    CHECK(is_registered_v<Person>);
    CHECK(std::string(TableMeta<Person>::name) == "Person");
    auto fields = TableMeta<Person>::get_fields();
    CHECK(std::tuple_size<decltype(fields)>::value == 3);

    // 第一个字段是 id -> PRIMARY KEY AUTO_INCREMENT
    bool first = std::apply([](auto&& f, auto&&...) {
        return std::string(f.constraint_sql).find("PRIMARY KEY") != std::string::npos &&
               std::string(f.constraint_sql).find("AUTO_INCREMENT") != std::string::npos &&
               std::string(f.column_name) == "id";
    }, fields);
    CHECK(first);
}

TEST_CASE("UORM_TABLE 宏注册（列名可与成员名不同）") {
    CHECK(is_registered_v<LegacyRow>);
    CHECK(std::string(TableMeta<LegacyRow>::name) == "legacy_rows");
    auto fields = TableMeta<LegacyRow>::get_fields();
    bool titleMapped = std::apply([](auto&&... f) {
        bool ok = false;
        ((  std::is_same_v<typename std::decay_t<decltype(f)>::Type, std::string> &&
            std::string(f.column_name) == "title" ? (ok = true, true) : false ), ...);
        return ok;
    }, fields);
    CHECK(titleMapped);
}

TEST_CASE("未注册类型静态检测") {
    struct NotRegistered {};
    CHECK_FALSE(is_registered_v<NotRegistered>);
}

// ---------------- SqlValue ----------------
TEST_CASE("SqlValue 转换辅助") {
    SqlValue n = nullptr;
    CHECK(isNullValue(n));
    CHECK(valueToInt64(n) == 0);
    CHECK(valueToString(n).empty());

    SqlValue i = 42LL;
    CHECK(valueToInt64(i) == 42);
    CHECK(valueToDouble(i) == 42.0);
    CHECK(valueToBool(i));
    CHECK(valueToString(i) == "42");

    SqlValue s = std::string("t");
    CHECK(valueToBool(s));
    SqlValue f = std::string("f");
    CHECK_FALSE(valueToBool(f));
    SqlValue zero = std::string("0");
    CHECK_FALSE(valueToBool(zero));

    SqlValue d = 3.5;
    CHECK(valueToDouble(d) == doctest::Approx(3.5));
}

// ---------------- Router ----------------
using namespace uORM::web;

TEST_CASE("Router 模式匹配与参数提取") {
    std::map<std::string, std::string> params;
    CHECK(Router::match("/api/connections/{id}/tables", "/api/connections/c123/tables", params));
    CHECK(params.at("id") == "c123");

    params.clear();
    CHECK_FALSE(Router::match("/api/{a}/{b}", "/api/onlyone", params));
    CHECK_FALSE(Router::match("/api/x", "/api/y", params));

    params.clear();
    CHECK(Router::match("/", "/", params));
}

TEST_CASE("Router 分发与方法匹配") {
    Router r;
    r.get("/hello", [](const HttpRequest&) { return HttpResponse::text("get-hello"); });
    r.post("/hello", [](const HttpRequest& req) { return HttpResponse::text("post:" + req.body); });

    HttpRequest getReq;
    getReq.method = "GET";
    getReq.path = "/hello";
    CHECK(r.dispatch(getReq).body == "get-hello");

    HttpRequest postReq;
    postReq.method = "POST";
    postReq.path = "/hello";
    postReq.body = "x";
    CHECK(r.dispatch(postReq).body == "post:x");

    HttpRequest delReq;
    delReq.method = "DELETE";
    delReq.path = "/hello";
    CHECK(r.dispatch(delReq).status == 404);
}
