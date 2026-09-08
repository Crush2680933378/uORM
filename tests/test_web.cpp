// 文件说明：
// Web 层单元测试：HTTP 请求解析辅助、Router、静态文件（含路径穿越/SPA fallback）、
// JsonUtil（大整数精度）、ConnectionManager 持久化与密码保留语义。全部无数据库依赖。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "uORM/web/Http.h"
#include "uORM/web/Router.h"
#include "uORM/web/StaticFiles.h"
#include "uORM/web/JsonUtil.h"
#include "uORM/web/ConnectionManager.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace uORM;
using namespace uORM::web;

// =====================================================================
// Http
// =====================================================================
TEST_CASE("queryParam：常规/无值/缺 key/URL 编码") {
    HttpRequest req;
    req.query = "limit=10&offset=20&flag&name=Tom%20Hanks+Jr&empty=";

    CHECK(req.queryParam("limit") == "10");
    CHECK(req.queryParam("offset") == "20");
    CHECK(req.queryParam("flag").empty());          // 只有 key 没有值
    CHECK(req.queryParam("name") == "Tom Hanks Jr"); // %20 与 + 都解码为空格
    CHECK(req.queryParam("empty").empty());
    CHECK(req.queryParam("missing").empty());
}

TEST_CASE("urlDecode / urlEncode 往返") {
    CHECK(HttpRequest::urlDecode("%E6%95%B0%E6%8D%AE%E5%BA%93") == "数据库");
    CHECK(HttpRequest::urlDecode("a+b") == "a b");
    CHECK(HttpRequest::urlDecode("%zz") == "%zz");   // 非法序列化原样保留

    const std::string raw = "a b/c?d=e&f+g%h-中文";
    auto encoded = HttpRequest::urlEncode(raw);
    CHECK(HttpRequest::urlDecode(encoded) == raw);
    // 编码后不应包含裸的特殊字符
    CHECK(encoded.find(' ') == std::string::npos);
    CHECK(encoded.find('&') == std::string::npos);
}

TEST_CASE("HttpResponse 序列化：状态行/长度/连接语义") {
    auto resp = HttpResponse::json("{\"ok\":true}");
    auto text = resp.serialize(true);
    CHECK(text.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(text.find("Content-Type: application/json") != std::string::npos);
    CHECK(text.find("Content-Length: 11\r\n") != std::string::npos);
    CHECK(text.find("Connection: keep-alive") != std::string::npos);

    auto closing = HttpResponse::text("x").serialize(false);
    CHECK(closing.find("Connection: close") != std::string::npos);

    auto err = HttpResponse::error(502, "bad \"gateway\"\nline2");
    CHECK(err.status == 502);
    CHECK(err.body.find("\\\"gateway\\\"") != std::string::npos);
    CHECK(err.body.find("\\n") != std::string::npos);
}

// =====================================================================
// Router
// =====================================================================
TEST_CASE("Router：多段参数/方法不匹配/404") {
    Router r;
    r.get("/api/connections/{id}/tables/{table}", [](const HttpRequest& req) {
        return HttpResponse::text(req.params.at("id") + "|" + req.params.at("table"));
    });
    r.post("/api/echo", [](const HttpRequest& req) { return HttpResponse::text(req.body); });

    HttpRequest get;
    get.method = "GET";
    get.path = "/api/connections/c42/tables/products";
    auto resp = r.dispatch(get);
    CHECK(resp.status == 200);
    CHECK(resp.body == "c42|products");

    get.method = "DELETE"; // 方法不匹配
    CHECK(r.dispatch(get).status == 404);

    get.method = "GET";
    get.path = "/api/connections/only-id"; // 段数不符
    CHECK(r.dispatch(get).status == 404);
}

// =====================================================================
// StaticFiles
// =====================================================================
TEST_CASE("StaticFiles：托管/类型/MIME/路径穿越/SPA fallback") {
    namespace fs = std::filesystem;
    const std::string root = "test_web_root";
    fs::create_directories(root + "/assets");
    { std::ofstream(root + "/index.html") << "<html>home</html>"; }
    { std::ofstream(root + "/assets/app.js") << "console.log(1)"; }

    StaticFiles sf(root);

    // 根路径 -> index.html
    auto home = sf.serve("/");
    CHECK(home.status == 200);
    CHECK(home.body == "<html>home</html>");
    CHECK(home.headers["Content-Type"] == "text/html; charset=utf-8");

    // 资源文件 + MIME
    auto js = sf.serve("/assets/app.js");
    CHECK(js.status == 200);
    CHECK(js.headers["Content-Type"] == "application/javascript; charset=utf-8");

    // 不存在的具体文件 -> 404
    CHECK(sf.serve("/assets/missing.js").status == 404);

    // 路径穿越 -> 403（即使目标存在）
    CHECK(sf.serve("/../secret.txt").status == 403);
    CHECK(sf.serve("/a/../../secret.txt").status == 403);

    // SPA fallback：任何未知路径回落到 index.html（前端路由刷新可用）
    auto spa = sf.serveWithFallback("/users/42/detail");
    CHECK(spa.status == 200);
    CHECK(spa.body == "<html>home</html>");

    // 真实存在的资源不受 fallback 影响
    CHECK(sf.serveWithFallback("/assets/app.js").body == "console.log(1)");

    fs::remove_all(root);
}

// =====================================================================
// JsonUtil
// =====================================================================
TEST_CASE("jsonValue：NULL/大整数精度/常规值") {
    auto n = jsonValue(SqlValue(nullptr));
    CHECK(n.is_null());

    auto normal = jsonValue(SqlValue(static_cast<long long>(123456789)));
    CHECK(normal.is_number());
    CHECK(normal.get<int>() == 123456789);

    // 超过 2^53 的整数转字符串，避免 double 精度丢失
    auto big = jsonValue(SqlValue(static_cast<long long>(9007199254740993LL)));
    CHECK(big.is_string());
    CHECK(big.get<std::string>() == "9007199254740993");

    auto d = jsonValue(SqlValue(0.5));
    CHECK(d.get<double>() == doctest::Approx(0.5));

    auto s = jsonValue(SqlValue(std::string("text")));
    CHECK(s.get<std::string>() == "text");
}

TEST_CASE("jsonResult：列名与行结构") {
    QueryResult qr;
    qr.columns = {"id", "name"};
    qr.rows.push_back({SqlValue(static_cast<long long>(1)), SqlValue(std::string("Tom"))});
    qr.rows.push_back({SqlValue(static_cast<long long>(2)), SqlValue(nullptr)});

    auto j = jsonResult(qr);
    CHECK(j.at("rowCount").get<int>() == 2);
    CHECK(j.at("columns").at(0).get<std::string>() == "id");
    CHECK(j.at("rows").at(1).at(1).is_null());
}

// =====================================================================
// ConnectionManager：CRUD / 持久化 / 密码保留语义
// =====================================================================
TEST_CASE("ConnectionManager：持久化往返与密码语义") {
    const std::string store = "test_connections.json";
    std::filesystem::remove(store);

    std::string id;
    {
        ConnectionManager mgr(store);
        ConnectionProfile p;
        p.name = "main";
        p.driver = "mysql";
        p.host = "db.example.com";
        p.port = 3306;
        p.username = "app";
        p.password = "secret";
        p.database = "shop";
        p.poolSize = 3;
        id = mgr.add(p).id;
        CHECK_FALSE(id.empty());
        mgr.save();
    }

    {
        // 第二个实例从磁盘恢复
        ConnectionManager mgr(store);
        ConnectionProfile p;
        REQUIRE(mgr.get(id, p));
        CHECK(p.driver == "mysql");
        CHECK(p.password == "secret"); // 持久化保留密码（回显层屏蔽，见下）

        // 更新时密码留空 = 保留原密码
        ConnectionProfile changed = p;
        changed.password = "";
        changed.username = "app2";
        CHECK(mgr.update(id, changed));
        ConnectionProfile after;
        REQUIRE(mgr.get(id, after));
        CHECK(after.password == "secret");
        CHECK(after.username == "app2");

        // list() 返回完整配置（明文密码的屏蔽由 Web API 层负责——见 webconsole）
        bool found = false;
        for (const auto& item : mgr.list()) {
            if (item.id == id) {
                found = true;
                CHECK(item.password == "secret");
            }
        }
        CHECK(found);

        // 未知连接更新失败
        CHECK_FALSE(mgr.update("no-such-id", changed));
    }

    std::filesystem::remove(store);
}
