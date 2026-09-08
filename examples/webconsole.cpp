// 文件说明：
// uORM Web 控制台：浏览器直接管理 MySQL / PostgreSQL / SQLite。
//
// 用法:
//   uORM_webconsole.exe [port] [--static DIR] [--data FILE] [--admin-password PW]
//   默认端口 8080；首次启动自动创建超级管理员（用户名 admin，密码由
//   --admin-password 指定，缺省 admin123），之后可在界面/接口中管理用户。

// 角色：superadmin（超级管理员）/ admin（管理员）/ user（普通用户，只读）
//   - superadmin：用户管理 + 连接管理 + 全部 SQL
//   - admin：连接管理 + 全部 SQL（无用户管理）
//   - user：数据浏览与 SELECT 查询（execute 与连接管理被拒绝）
//
// REST API（请求头 X-Auth-Token 鉴权）:
//   POST   /api/login                              {token}
//   GET    /api/drivers                            可用驱动列表
//   GET    /api/connections                        连接配置（密码屏蔽）
//   POST   /api/connections                        新建连接
//   PUT    /api/connections/{id}                   更新（password 留空=保留）
//   DELETE /api/connections/{id}                   删除
//   POST   /api/connections/{id}/test              测试连接
//   GET    /api/connections/{id}/status            连接池状态
//   GET    /api/connections/{id}/tables            表列表
//   GET    /api/connections/{id}/tables/{t}/columns  列结构
//   GET    /api/connections/{id}/tables/{t}/rows   行浏览 (?limit&offset&orderBy&desc)
//   POST   /api/connections/{id}/query             {sql, maxRows} 任意查询
//   POST   /api/connections/{id}/execute           {sql} DML/DDL

#include <uORM/orm/ORM.h>
#include <uORM/web/HttpServer.h>
#include <uORM/web/ConnectionManager.h>
#include <uORM/web/StaticFiles.h>
#include <uORM/web/JsonUtil.h>
#include <uORM/web/UserStore.h>

#include <cstdlib>
#include <iostream>
#include <random>

using namespace uORM;
using namespace uORM::web;

// ---------------- 工具 ----------------
namespace {

std::string genToken() {
    std::random_device rd;
    std::ostringstream out;
    out << std::hex;
    for (int i = 0; i < 8; ++i) out << ((rd() & 0xFFFF) | 0x10000);
    return out.str();
}

bool validIdentifier(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    if (!(std::isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_')) return false;
    for (char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

std::shared_ptr<DataSource> requireSource(ConnectionManager& mgr, const HttpRequest& req) {
    return mgr.source(req.params.at("id"));
}

// 从请求体提取 sql 与 params（参数化查询，NULL 用 null 表示）
bool parseSqlBody(const HttpRequest& req, std::string& sql, std::vector<SqlValue>& params,
                  std::string& err) {
    try {
        auto body = uJSON::Value::parse(req.body);
        if (!body.contains("sql")) { err = "sql is required"; return false; }
        sql = body.at("sql").get<std::string>();
        if (body.contains("params") && body.at("params").is_array()) {
            for (const auto& p : body.at("params").get_array()) {
                switch (p.type()) {
                    case uJSON::Type::Null: params.push_back(nullptr); break;
                    case uJSON::Type::Boolean: params.push_back(p.get<bool>() ? 1LL : 0LL); break;
                    case uJSON::Type::Number: {
                        double d = p.get<double>();
                        if (d == static_cast<double>(static_cast<long long>(d))) {
                            params.push_back(static_cast<long long>(d));
                        } else {
                            params.push_back(d);
                        }
                        break;
                    }
                    case uJSON::Type::String: params.push_back(p.get<std::string>()); break;
                    default: params.push_back(nullptr); break;
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("bad request: ") + e.what();
        return false;
    }
}

// 角色级别：user=1 < admin=2 < superadmin=3
int roleLevel(const std::string& role) {
    if (role == "superadmin") return 3;
    if (role == "admin") return 2;
    if (role == "user") return 1;
    return 0;
}

// 角色级别：user=1 < admin=2 < superadmin=3

} // namespace

int main(int argc, char** argv) {
    unsigned short port = 8080;
    std::string staticDir = "webapp/dist";
    std::string storePath = "connections.json";
    std::string usersPath = "users.json";
    std::string adminPassword;   // 为空 = 自动生成并打印

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--admin-password" && i + 1 < argc) adminPassword = argv[++i];
        else if (a == "--token" && i + 1 < argc) adminPassword = argv[++i]; // 兼容旧参数
        else if (a == "--users" && i + 1 < argc) usersPath = argv[++i];
        else if (a == "--data" && i + 1 < argc) storePath = argv[++i];
        else if (a == "--static" && i + 1 < argc) staticDir = argv[++i];
        else if (a.rfind("--", 0) == 0) { /* 忽略未知选项 */ }
        else port = static_cast<unsigned short>(std::atoi(a.c_str()));
    }

    // 引导超级管理员：首次启动（无任何用户）时创建 admin
    {
        UserStore boot(usersPath);
        if (!boot.hasSuperAdmin()) {
            std::string pw = adminPassword.empty() ? web::randomHex(8) : adminPassword;
            boot.add("admin", pw, "superadmin");
            boot.save();
            std::cout << "[bootstrap] created superadmin user 'admin' with password '"
                      << pw << "' (please change it)" << std::endl;
        }
    }

    UserStore userStore(usersPath);
    ConnectionManager mgr(storePath);
    web::SessionManager sessions;

    Router api;

    // ---------------- 角色检查 ----------------
    // 返回会话；角色级别不足或未认证返回 nullopt
    auto requireLevel = [&](const HttpRequest& req, int minLevel)
        -> std::optional<web::SessionManager::Session> {
        auto sess = sessions.validate(req.header("X-Auth-Token"));
        if (sess.username.empty()) return std::nullopt;
        if (roleLevel(sess.role) < minLevel) return std::nullopt;
        return sess;
    };


    // ---------------- 认证 ----------------
    api.post("/api/login", [&](const HttpRequest& req) {
        try {
            auto body = uJSON::Value::parse(req.body);
            std::string username = body.contains("username") ? body.at("username").get<std::string>() : "";
            std::string password = body.contains("password") ? body.at("password").get<std::string>() : "";
            if (!userStore.verify(username, password))
                return HttpResponse::error(401, "用户名或密码错误");
            std::string token = sessions.create(username, userStore.roleOf(username));
            uJSON::Value out = uJSON::Value::object();
            out["token"] = uJSON::Value(token);
            out["username"] = uJSON::Value(username);
            out["role"] = uJSON::Value(userStore.roleOf(username));
            return HttpResponse::json(dump(out));
        } catch (const std::exception& e) {
            return HttpResponse::error(400, e.what());
        }
    });

    // ---------------- 会话 ----------------

    // ---------------- 会话信息 ----------------
    api.get("/api/me", [&](const HttpRequest& req) {
        auto sess = sessions.validate(req.header("X-Auth-Token"));
        if (sess.username.empty()) return HttpResponse::error(401, "unauthorized");
        uJSON::Value out = uJSON::Value::object();
        out["username"] = uJSON::Value(sess.username);
        out["role"] = uJSON::Value(userStore.roleOf(sess.username));
        return HttpResponse::json(dump(out));
    });

    // ---------------- 修改自己的密码 ----------------
    api.post("/api/auth/password", [&](const HttpRequest& req) {
        auto sess = sessions.validate(req.header("X-Auth-Token"));
        if (sess.username.empty()) return HttpResponse::error(401, "unauthorized");
        std::string oldPw, newPw;
        try {
            auto body = uJSON::Value::parse(req.body);
            oldPw = body.at("old").get<std::string>();
            newPw = body.at("new").get<std::string>();
        } catch (const std::exception& e) {
            return HttpResponse::error(400, e.what());
        }
        if (!userStore.verify(sess.username, oldPw))
            return HttpResponse::error(400, "旧密码错误");
        if (newPw.size() < 6)
            return HttpResponse::error(400, "新密码至少 6 位");
        userStore.setPassword(sess.username, newPw);
        userStore.save();
        return HttpResponse::json("{\"ok\":true}");
    });

    // ---------------- 用户管理（仅超级管理员） ----------------
    api.get("/api/users", [&](const HttpRequest& req) {
        if (roleLevel(userStore.roleOf(sessions.validate(req.header("X-Auth-Token")).username)) < 3)
            return HttpResponse::error(403, "需要超级管理员权限");
        uJSON::Value arr = uJSON::Value::array();
        for (const auto& u : userStore.list()) {
            auto o = uJSON::Value::object();
            o["username"] = uJSON::Value(u.username);
            o["role"] = uJSON::Value(u.role);
            o["createdAt"] = uJSON::Value(u.createdAt);
            arr.push_back(o);
        }
        uJSON::Value out = uJSON::Value::object();
        out["users"] = arr;
        return HttpResponse::json(dump(out));
    });

    api.post("/api/users", [&](const HttpRequest& req) {
        // 仅超级管理员可创建用户
        auto sess = sessions.validate(req.header("X-Auth-Token"));
        if (sess.username.empty() || userStore.roleOf(sess.username) != "superadmin")
            return HttpResponse::error(403, "需要超级管理员权限");
        try {
            auto body = uJSON::Value::parse(req.body);
            std::string username = body.at("username").get<std::string>();
            std::string password = body.at("password").get<std::string>();
            std::string role = body.contains("role") ? body.at("role").get<std::string>() : "user";
            if (!web::isValidRole(role)) return HttpResponse::error(400, "invalid role");
            if (password.size() < 6) return HttpResponse::error(400, "密码至少 6 位");
            if (userStore.exists(username)) return HttpResponse::error(409, "用户已存在");
            UserRecord r;
            r.username = username;
            r.salt = web::randomHex(16);
            r.hash = web::hashPassword(r.salt, password);
            r.role = role;
            userStore.add(r.username, password, role);
            userStore.save();
            return HttpResponse::json("{\"username\":\"" + username + "\"}", 201);
        } catch (const std::exception& e) {
            return HttpResponse::error(400, e.what());
        }
    });

    api.del("/api/users/{name}", [&](const HttpRequest& req) {
        // 仅超级管理员可删除用户
        auto sess = sessions.validate(req.header("X-Auth-Token"));
        if (sess.username.empty() || userStore.roleOf(sess.username) != "superadmin")
            return HttpResponse::error(403, "需要超级管理员权限");
        std::string name = req.params.at("name");
        if (name == sess.username) return HttpResponse::error(400, "不能删除自己");
        if (!userStore.remove(name)) return HttpResponse::error(404, "用户不存在");
        userStore.save();
        return HttpResponse::json("{\"ok\":true}");
    });

    // ---- 修改用户角色（仅超级管理员） ----
    api.put("/api/users/{name}/role", [&](const HttpRequest& req) {
        auto sess = sessions.validate(req.header("X-Auth-Token"));
        if (sess.username.empty() || userStore.roleOf(sess.username) != "superadmin")
            return HttpResponse::error(403, "需要超级管理员权限");
        std::string name = req.params.at("name");
        try {
            auto body = uJSON::Value::parse(req.body);
            std::string role = body.at("role").get<std::string>();
            if (!web::isValidRole(role)) return HttpResponse::error(400, "invalid role");
            if (!userStore.setRole(name, role)) return HttpResponse::error(404, "用户不存在");
            userStore.save();
            return HttpResponse::json("{\"ok\":true}");
        } catch (const std::exception& e) {
            return HttpResponse::error(400, e.what());
        }
    });

    // ---- 重置用户密码（仅超级管理员） ----
    api.put("/api/users/{name}/password", [&](const HttpRequest& req) {
        auto sess = sessions.validate(req.header("X-Auth-Token"));
        if (sess.username.empty() || userStore.roleOf(sess.username) != "superadmin")
            return HttpResponse::error(403, "需要超级管理员权限");
        std::string name = req.params.at("name");
        try {
            auto body = uJSON::Value::parse(req.body);
            std::string pw = body.at("password").get<std::string>();
            if (pw.size() < 6) return HttpResponse::error(400, "密码至少 6 位");
            if (!userStore.setPassword(name, pw)) return HttpResponse::error(404, "用户不存在");
            userStore.save();
            return HttpResponse::json("{\"ok\":true}");
        } catch (const std::exception& e) {
            return HttpResponse::error(400, e.what());
        }
    });

    // ---------------- 驱动 ----------------
    api.get("/api/drivers", [&](const HttpRequest&) {
        auto names = DriverRegistry::instance().driverNames();
        uJSON::Value arr = uJSON::Value::array();
        for (const auto& n : names) arr.push_back(uJSON::Value(n));
        uJSON::Value out = uJSON::Value::object();
        out["drivers"] = arr;
        return HttpResponse::json(dump(out));
    });

    // ---------------- 连接配置 ----------------
    auto profileFromBody = [](const HttpRequest& req) -> ConnectionProfile {
        auto body = uJSON::Value::parse(req.body);
        ConnectionProfile p;
        if (body.contains("name")) p.name = body.at("name").get<std::string>();
        if (body.contains("driver")) p.driver = body.at("driver").get<std::string>();
        if (body.contains("host")) p.host = body.at("host").get<std::string>();
        if (body.contains("port")) p.port = body.at("port").get<int>();
        if (body.contains("username")) p.username = body.at("username").get<std::string>();
        if (body.contains("password")) p.password = body.at("password").get<std::string>();
        if (body.contains("database")) p.database = body.at("database").get<std::string>();
        if (body.contains("poolSize")) p.poolSize = body.at("poolSize").get<int>();
        if (body.contains("useTLS")) p.useTLS = body.at("useTLS").get<bool>();
        return p;
    };

    api.get("/api/connections", [&](const HttpRequest&) {
        uJSON::Value arr = uJSON::Value::array();
        for (const auto& p : mgr.list()) {
            auto o = uJSON::Value::object();
            o["id"] = uJSON::Value(p.id);
            o["name"] = uJSON::Value(p.name);
            o["driver"] = uJSON::Value(p.driver);
            o["host"] = uJSON::Value(p.host);
            o["port"] = uJSON::Value(p.port);
            o["username"] = uJSON::Value(p.username);
            o["database"] = uJSON::Value(p.database);
            o["hasPassword"] = uJSON::Value(!p.password.empty()); // 不回显密码
            o["poolSize"] = uJSON::Value(p.poolSize);
            o["useTLS"] = uJSON::Value(p.useTLS);
            arr.push_back(o);
        }
        uJSON::Value out = uJSON::Value::object();
        out["connections"] = arr;
        return HttpResponse::json(dump(out));
    });

    api.post("/api/connections", [&](const HttpRequest& req) {
        if (!requireLevel(req, 2)) return HttpResponse::error(403, "需要管理员权限");
        try {
            ConnectionProfile p = profileFromBody(req);
            if (p.driver.empty() || p.database.empty()) {
                return HttpResponse::error(400, "driver and database are required");
            }
            p = mgr.add(p);
            mgr.save();
            return HttpResponse::json("{\"id\":\"" + p.id + "\"}", 201);
        } catch (const std::exception& e) {
            return HttpResponse::error(400, e.what());
        }
    });

    api.del("/api/connections/{id}", [&](const HttpRequest& req) {
        if (!requireLevel(req, 2)) return HttpResponse::error(403, "需要管理员权限");
        if (!mgr.remove(req.params.at("id"))) return HttpResponse::error(404, "no such connection");
        mgr.save();
        return HttpResponse::json("{\"ok\":true}");
    });

    api.post("/api/connections/{id}/test", [&](const HttpRequest& req) {
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        try {
            auto conn = src->openNewConnection();
            bool ok = conn->ping();
            uJSON::Value out = uJSON::Value::object();
            out["ok"] = uJSON::Value(ok);
            return HttpResponse::json(dump(out));
        } catch (const Exception& e) {
            return HttpResponse::error(502, e.what());
        }
    });

    api.get("/api/connections/{id}/status", [&](const HttpRequest& req) {
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        auto st = src->stats();
        uJSON::Value out = uJSON::Value::object();
        out["idle"] = uJSON::Value(st.idle);
        out["inUse"] = uJSON::Value(st.inUse);
        out["totalCreated"] = uJSON::Value(st.totalCreated);
        return HttpResponse::json(dump(out));
    });

    // ---------------- 元数据：表与列 ----------------
    api.get("/api/connections/{id}/tables", [&](const HttpRequest& req) {
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        try {
            QueryResult tables, views;
            src->withConnection([&](IConnection& conn) {
                auto dialect = conn.dialect();
                switch (dialect->kind()) {
                    case DialectKind::MySQL:
                        tables = executeQuery(conn,
                            "SELECT TABLE_NAME FROM information_schema.TABLES "
                            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_TYPE='BASE TABLE' ORDER BY TABLE_NAME");
                        views = executeQuery(conn,
                            "SELECT TABLE_NAME FROM information_schema.VIEWS "
                            "WHERE TABLE_SCHEMA = DATABASE() ORDER BY TABLE_NAME");
                        break;
                    case DialectKind::PostgreSQL:
                        tables = executeQuery(conn,
                            "SELECT c.relname FROM pg_class c "
                            "JOIN pg_namespace n ON n.oid = c.relnamespace "
                            "WHERE n.nspname = current_schema() AND c.relkind = 'r' ORDER BY c.relname");
                        views = executeQuery(conn,
                            "SELECT viewname FROM pg_views "
                            "WHERE schemaname = current_schema() ORDER BY viewname");
                        break;
                    case DialectKind::SQLite:
                    default:
                        tables = executeQuery(conn,
                            "SELECT name FROM sqlite_master WHERE type='table' "
                            "AND name NOT LIKE 'sqlite_%' ORDER BY name");
                        views = executeQuery(conn,
                            "SELECT name FROM sqlite_master WHERE type='view' ORDER BY name");
                        break;
                }
                return 0;
            });
            auto toArray = [](QueryResult& r) {
                uJSON::Value arr = uJSON::Value::array();
                for (const auto& row : r.rows) arr.push_back(jsonValue(row[0]));
                return arr;
            };
            uJSON::Value out = uJSON::Value::object();
            out["tables"] = toArray(tables);
            out["views"] = toArray(views);
            return HttpResponse::json(dump(out));
        } catch (const Exception& e) {
            return HttpResponse::error(502, e.what());
        }
    });

    api.get("/api/connections/{id}/tables/{table}/columns", [&](const HttpRequest& req) {
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        std::string table = req.params.at("table");
        if (!validIdentifier(table)) return HttpResponse::error(400, "invalid table name");
        try {
            // 归一化输出：[name, type, nullable, default, isPK] —— 供前端行内编辑定位主键
            QueryResult r = src->withConnection([&table](IConnection& conn) {
                auto dialect = conn.dialect();
                switch (dialect->kind()) {
                    case DialectKind::MySQL:
                        return executeQuery(conn,
                            "SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, IFNULL(COLUMN_DEFAULT,''), "
                            "(COLUMN_KEY = 'PRI') "
                            "FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() "
                            "AND TABLE_NAME = ? ORDER BY ORDINAL_POSITION", {SqlValue(table)});
                    case DialectKind::PostgreSQL:
                        return executeQuery(conn,
                            "SELECT c.column_name, c.data_type, c.is_nullable, "
                            "COALESCE(c.column_default,''), "
                            "COALESCE((SELECT 1 FROM information_schema.table_constraints tc "
                            "  JOIN information_schema.key_column_usage k "
                            "    ON tc.constraint_name = k.constraint_name "
                            "   AND tc.table_schema  = k.table_schema "
                            "   AND tc.table_name    = k.table_name "
                            "  WHERE tc.constraint_type = 'PRIMARY KEY' "
                            "    AND tc.table_name = c.table_name "
                            "    AND tc.table_schema = c.table_schema "
                            "    AND k.column_name = c.column_name), 0) "
                            "FROM information_schema.columns c "
                            "WHERE c.table_schema = current_schema() AND c.table_name = ? "
                            "ORDER BY c.ordinal_position", {SqlValue(table)});
                    case DialectKind::SQLite:
                    default:
                        return executeQuery(conn,
                            "SELECT name, type, CASE WHEN \"notnull\" = 1 THEN 'NO' ELSE 'YES' END, "
                            "COALESCE(\"dflt_value\",''), pk "
                            "FROM pragma_table_info(" + dialect->quoteIdentifier(table) + ")");
                }
            });
            uJSON::Value out = uJSON::Value::object();
            uJSON::Value jr = jsonResult(r);
            out["columns"] = jr["rows"];
            return HttpResponse::json(dump(out));
        } catch (const Exception& e) {
            return HttpResponse::error(502, e.what());
        }
    });

    api.get("/api/connections/{id}/tables/{table}/rows", [&](const HttpRequest& req) {
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        std::string table = req.params.at("table");
        if (!validIdentifier(table)) return HttpResponse::error(400, "invalid table name");

        int limit = 50, offset = 0;
        try {
            if (!req.queryParam("limit").empty()) limit = std::atoi(req.queryParam("limit").c_str());
            if (!req.queryParam("offset").empty()) offset = std::atoi(req.queryParam("offset").c_str());
        } catch (...) {}
        if (limit < 1) limit = 1;
        if (limit > 1000) limit = 1000;
        if (offset < 0) offset = 0;

        std::string order = req.queryParam("orderBy");
        std::string desc = req.queryParam("desc");
        if (!order.empty() && !validIdentifier(order)) return HttpResponse::error(400, "invalid orderBy");

        try {
            QueryResult rows = src->withConnection([&](IConnection& conn) {
                auto dialect = conn.dialect();
                std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(table);
                if (!order.empty()) {
                    sql += " ORDER BY " + dialect->quoteIdentifier(order) + (desc == "1" ? " DESC" : " ASC");
                }
                sql += " LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
                return executeQuery(conn, sql);
            });
            long long total = 0;
            try {
                QueryResult c = src->withConnection([&](IConnection& conn) {
                    auto dialect = conn.dialect();
                    return executeQuery(conn, "SELECT COUNT(*) FROM " + dialect->quoteIdentifier(table));
                });
                if (!c.rows.empty()) total = valueToInt64(c.rows[0][0]);
            } catch (const Exception&) {}

            uJSON::Value out = uJSON::Value::object();
            out["data"] = jsonResult(rows);
            out["total"] = jsonValue(total);
            out["limit"] = uJSON::Value(limit);
            out["offset"] = uJSON::Value(offset);
            return HttpResponse::json(dump(out));
        } catch (const Exception& e) {
            return HttpResponse::error(502, e.what());
        }
    });

    // ---------------- SQL 执行 ----------------
    api.post("/api/connections/{id}/query", [&](const HttpRequest& req) {
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        std::string sql, err;
        std::vector<SqlValue> params;
        int maxRows = 500;
        if (!parseSqlBody(req, sql, params, err)) return HttpResponse::error(400, err);
        try {
            auto body = uJSON::Value::parse(req.body);
            if (body.contains("maxRows")) maxRows = body.at("maxRows").get<int>();
        } catch (const std::exception&) {}
        if (maxRows < 1 || maxRows > 10000) maxRows = 500;

        // SELECT 语句自动加 LIMIT 防止大结果拖垮浏览器
        {
            std::string trimmed = sql;
            auto notSpace = trimmed.find_first_not_of(" \t\r\n(");
            if (notSpace != std::string::npos) trimmed = trimmed.substr(notSpace, 6);
            for (auto& c : trimmed) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (trimmed.rfind("select", 0) == 0 &&
                sql.find("LIMIT") == std::string::npos && sql.find("limit") == std::string::npos) {
                sql += " LIMIT " + std::to_string(maxRows);
            }
        }

        try {
            QueryResult r = src->withConnection([&](IConnection& conn) {
                return executeQuery(conn, sql, params);
            });
            uJSON::Value out = uJSON::Value::object();
            out["result"] = jsonResult(r);
            out["truncated"] = uJSON::Value(r.rowCount() >= static_cast<std::size_t>(maxRows));
            return HttpResponse::json(dump(out));
        } catch (const Exception& e) {
            return HttpResponse::error(502, e.what());
        }
    });

    api.post("/api/connections/{id}/execute", [&](const HttpRequest& req) {
        auto sess = requireLevel(req, 2);
        if (!sess) return HttpResponse::error(403, "需要管理员权限（普通用户只读）");
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        std::string sql, err;
        std::vector<SqlValue> params;
        if (!parseSqlBody(req, sql, params, err)) return HttpResponse::error(400, err);
        try {
            unsigned long long affected = src->withConnection([&](IConnection& conn) {
                return executeUpdate(conn, sql, params);
            });
            uJSON::Value out = uJSON::Value::object();
            out["affected"] = jsonValue(static_cast<long long>(affected));
            return HttpResponse::json(dump(out));
        } catch (const Exception& e) {
            return HttpResponse::error(502, e.what());
        }
    });

    // ---- 建表 DDL ----
    api.get("/api/connections/{id}/tables/{table}/ddl", [&](const HttpRequest& req) {
        auto sess = requireLevel(req, 2);
        if (!sess) return HttpResponse::error(403, "需要管理员权限");
        auto src = requireSource(mgr, req);
        if (!src) return HttpResponse::error(404, "no such connection");
        std::string table = req.params.at("table");
        if (!validIdentifier(table)) return HttpResponse::error(400, "invalid table name");
        try {
            std::string ddl = src->withConnection([&](IConnection& conn) -> std::string {
                auto dialect = conn.dialect();
                switch (dialect->kind()) {
                    case DialectKind::MySQL: {
                        auto r = executeQuery(conn, "SHOW CREATE TABLE " + dialect->quoteIdentifier(table));
                        if (!r.rows.empty()) return valueToString(r.rows[0][1]);
                        return "";
                    }
                    case DialectKind::PostgreSQL: {
                        auto r = executeQuery(conn,
                            "SELECT '  ' || quote_ident(a.attname) || ' ' || format_type(a.atttypid, a.atttypmod) || "
                            "  CASE WHEN a.attnotnull THEN ' NOT NULL' ELSE '' END || "
                            "  CASE WHEN a.atthasdef THEN ' DEFAULT ' || pg_get_expr(ad.adbin, ad.adrelid) ELSE '' END "
                            "FROM pg_attribute a "
                            "JOIN pg_class cl ON cl.oid = a.attrelid "
                            "JOIN pg_namespace n ON n.oid = cl.relnamespace "
                            "LEFT JOIN pg_attrdef ad ON ad.adrelid = a.attrelid AND ad.adnum = a.attnum "
                            "WHERE n.nspname = current_schema() AND cl.relname = ? AND a.attnum > 0 AND NOT a.attisdropped "
                            "ORDER BY a.attnum", {SqlValue(table)});
                        if (r.rows.empty()) return "";
                        std::string cols;
                        for (const auto& row : r.rows) {
                            if (!cols.empty()) cols += ",\n";
                            cols += valueToString(row[0]);
                        }
                        return "CREATE TABLE " + dialect->quoteIdentifier(table) + " (\n" + cols + "\n);";
                    }
                    case DialectKind::SQLite:
                    default: {
                        auto r = executeQuery(conn,
                            "SELECT sql FROM sqlite_master WHERE type='table' AND name = ?",
                            {SqlValue(table)});
                        if (!r.rows.empty()) return valueToString(r.rows[0][0]);
                        return "";
                    }
                }
            });
            uJSON::Value out = uJSON::Value::object();
            out["ddl"] = uJSON::Value(ddl);
            return HttpResponse::json(dump(out));
        } catch (const Exception& e) {
            return HttpResponse::error(502, e.what());
        }
    });

    // ---------------- 组合分发：API 优先，静态兜底（SPA） ----------------
    StaticFiles staticFiles(staticDir);
    auto handle = [&](const HttpRequest& req) -> HttpResponse {
        if (req.path.rfind("/api/", 0) == 0 || req.path == "/api") {
            // 统一会话鉴权（登录除外）
            if (req.path != "/api/login") {
                auto sess = sessions.validate(req.header("X-Auth-Token"));
                if (sess.username.empty())
                    return HttpResponse::error(401, "unauthorized");
            }
            return api.dispatch(req);
        }
        if (req.method == "GET" || req.method == "HEAD") {
            return staticFiles.serveWithFallback(req.path);
        }
        return HttpResponse::error(404, "not found");
    };

    HttpServer::Config cfg;
    cfg.port = port;
    HttpServer server(handle, cfg);

    std::cout << "====================================" << std::endl;
    std::cout << "  uORM Web Console" << std::endl;
    std::cout << "  http://127.0.0.1:" << port << "/" << std::endl;
    std::cout << "  Login: username + password (users.json)" << std::endl;
    std::cout << "  Static dir : " << staticDir << std::endl;
    std::cout << "====================================" << std::endl;
    server.run();
    return 0;
}
