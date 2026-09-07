# uORM

uORM 是一个现代化的、轻量级的 C++17 ORM 与 **Web 数据库管理台**。一份实体定义即可操作 **MySQL / MariaDB、PostgreSQL、SQLite**，并内置基于 standalone asio 的 HTTP 服务 + React 前端——浏览器里直接管理各种数据库，无需安装 Navicat / DBeaver 等本地客户端。

## ✨ 核心特性

- **运行时多驱动**：MySQL(libmariadb)、PostgreSQL(libpq)、SQLite(sqlite3) 在编译期按可用性编入，运行期按名字创建。一个进程可同时连接多种数据库。
- **Web 管理台**：连接管理、库表浏览（分页/排序/表结构）、SQL 控制台（参数化 + 自动 LIMIT），开箱即用。
- **编译期映射**：`UORM_REFLECTION(User, id, name, age)` 一行注册；也支持完整列定义的 `UORM_TABLE_*` 宏。
- **方言感知 DDL**：同一份实体定义在三库上建表（`AUTO_INCREMENT` / `GENERATED ... IDENTITY` / `AUTOINCREMENT`、`DATETIME`→`TIMESTAMP` 等自动转换）。
- **安全查询**：统一 `?` 占位符 + 预编译参数绑定（PG 自动转 `$n`），杜绝 SQL 注入。
- **事务**：`IConnection::begin/commit/rollback` + RAII `Transaction` + `withTransaction(ds, lambda)`（异常自动回滚）。
- **连接池**：`DataSource` 多数据源、有界池、获取超时、借出前 ping 健康检查、失效自动重建。
- **CRUD 全覆盖**：`save`（自增主键自动写回）/ `update` / `remove` / `findOne` / `select` / `count` / `sum` / `avg` / `max` / `min` / `saveOrUpdate`（upsert）/ `selectDynamic`（join/聚合投影）。

## 📦 依赖

- C++17（GCC 8+/Clang 7+/MSVC 2019+，MinGW-w64 实测 GCC 16）
- CMake 3.16+ / Ninja
- [uJSON](https://github.com/Crush2680933378/uJSON)（内置子模块）
- 数据库客户端库（按需，全部可选）：
  - MySQL/MariaDB: `libmariadb` 或 `libmysqlclient`
  - PostgreSQL: `libpq`
  - SQLite: `sqlite3`（驱动可独立工作，推荐始终开启）
- Web 控制台：[standalone asio](https://github.com/chriskohlhoff/asio)（非 boost）
- 前端构建：Node.js 18+（仅开发期需要，运行期由 C++ 服务托管静态文件）
- 测试：doctest（可选）

MSYS2/MinGW 一键安装：

```bash
pacman -S mingw-w64-x86_64-{toolchain,cmake,ninja,libmariadbclient,postgresql,sqlite3,asio,doctest,nodejs}
```

## 🚀 快速开始

### 1. 定义模型

```cpp
#include <uORM/orm/ORM.h>

struct User {
    int id;
    std::string name;
    int age;
};
// 列名 = 成员名；名为 id 的成员自动成为自增主键
UORM_REFLECTION(User, id, name, age)
```

或完整定义（自定义列名/类型/约束）：

```cpp
struct Product {
    int id;
    std::string name;
    double price;
    std::string created_at;
};
UORM_TABLE_BEGIN(Product, "products")
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(name, "name", NOT NULL),
    UORM_FIELD(price, "price", NOT NULL),
    UORM_FIELD_TYPE(created_at, "created_at", "DATETIME", DEFAULT CURRENT_TIMESTAMP)
UORM_TABLE_END()
```

### 2. 配置数据源

`config.json`（`driver` 支持 `mysql` / `postgresql` / `sqlite`）：

```json
{
    "DataBaseConfig": {
        "driver": "mysql",
        "hostname": "127.0.0.1",
        "port": 3306,
        "username": "root",
        "password": "your_password",
        "dataname": "uorm_db",
        "poolsize": 5
    }
}
```

### 3. CRUD

```cpp
try {
    uORM::ConfigManager::getInstance().readDataBaseconfig("config.json");
    auto& ds = uORM::ConnectionPool::instance().source();

    // 建表（方言自动适配）
    uORM::Schema::createTable<User>();

    // 插入（自增 id 自动写回 user.id）
    User user{0, "Trae", 25};
    uORM::Mapper<User>::save(user);

    // 查询
    auto row = uORM::Mapper<User>::findOne("name = ?", "Trae");
    auto list = uORM::Mapper<User>::find("age > ?", 18);

    // Query 构造器
    uORM::Query q;
    q.like("name", "%T%").ge("age", 18).orderBy("id", false).limit(10);
    auto users = uORM::Mapper<User>::select(q);

    // 事务（异常自动回滚）
    uORM::withTransaction(ds, [&](uORM::IConnection& conn) {
        uORM::Mapper<User>::save(user, conn);
        uORM::Mapper<User>::update(user, conn);
    });

    // Upsert
    user.age = 26;
    uORM::Mapper<User>::saveOrUpdate(user);
} catch (const uORM::Exception& e) {
    std::cerr << "uORM 错误: " << e.what() << std::endl;
}
```

### 4. 多数据源（运行时选择驱动）

```cpp
uORM::DataSourceConfig cfg;
cfg.params.driver = "postgresql";           // 运行时字符串，不是模板参数
cfg.params.host = "10.0.0.5";
cfg.params.port = 5432;
cfg.params.username = "app";
cfg.params.password = "***";
cfg.params.database = "orders";
cfg.poolSize = 10;

uORM::DataSource ds(cfg);
auto rows = ds.query("SELECT * FROM orders WHERE status = ?", {uORM::SqlValue("PAID")});
```

## 🌐 Web 管理台

```bash
# 构建前端（仅首次）
cd webapp && npm install && npm run build

# 启动（端口/静态目录/令牌可配）
./build/uORM_webconsole.exe 8080 --static webapp/dist
# ====================================
#   uORM Web Console
#   http://127.0.0.1:8080/
#   Admin token: <启动时随机生成并打印>
# ====================================
```

浏览器打开 `http://127.0.0.1:8080/`，输入令牌登录，即可：

- **连接管理**：添加 MySQL / PostgreSQL / SQLite 连接（密码只存本地 connections.json，界面不回显）
- **数据浏览**：表树 → 分页数据表格（列头点击排序）、表结构查看
- **SQL 控制台**：任意 SQL 执行，SELECT 自动加 LIMIT 防护，错误友好提示

### REST API

所有请求需带 `X-Auth-Token` 头（`POST /api/login` 换取会话语义）：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | `/api/login` | `{token}` |
| GET | `/api/drivers` | 可用驱动 |
| GET/POST | `/api/connections` | 连接列表（密码屏蔽）/ 新建 |
| PUT/DELETE | `/api/connections/{id}` | 更新（password 留空=保留）/ 删除 |
| POST | `/api/connections/{id}/test` | 测试连接 |
| GET | `/api/connections/{id}/status` | 连接池状态 |
| GET | `/api/connections/{id}/tables` | 表列表 |
| GET | `/api/connections/{id}/tables/{t}/columns` | 列结构 |
| GET | `/api/connections/{id}/tables/{t}/rows` | 行浏览 `?limit&offset&orderBy&desc` |
| POST | `/api/connections/{id}/query` | `{sql, params?, maxRows?}` 查询 |
| POST | `/api/connections/{id}/execute` | `{sql, params?}` DML/DDL |

## 🔨 构建与测试

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DUORM_BUILD_TESTS=ON
cmake --build build

# 单元测试（无数据库依赖）
./build/uorm_test_unit

# 集成测试：SQLite 始终运行；MySQL/PG 通过环境变量启用
UORM_TEST_MYSQL_HOST=127.0.0.1 UORM_TEST_MYSQL_USER=root UORM_TEST_MYSQL_PASS=xxx UORM_TEST_MYSQL_DB=uorm_db \
UORM_TEST_PG_HOST=127.0.0.1 UORM_TEST_PG_USER=postgres UORM_TEST_PG_PASS=xxx UORM_TEST_PG_DB=uorm_db \
./build/uorm_test_integration

# 示例程序
./build/uORM_example
./build/uORM_http_demo 18080
```

### CMake 选项

| 选项 | 默认 | 说明 |
| :--- | :--- | :--- |
| `UORM_ENABLE_MYSQL` | `ON` | MySQL 驱动（找到 libmariadb 才生效） |
| `UORM_ENABLE_POSTGRESQL` | `ON` | PostgreSQL 驱动（找到 libpq 才生效） |
| `UORM_ENABLE_SQLITE` | `ON` | SQLite 驱动 |
| `UORM_ENABLE_WEB` | `ON` | Web 控制台（找到 asio 才生效） |
| `UORM_BUILD_TESTS` | `OFF` | 构建测试 |
| `BUILD_EXAMPLES` | `ON` | 构建示例 |

缺失的依赖自动降级（仅告警），至少 SQLite 驱动可保证开箱即用。

## 🧭 作为子模块集成

```bash
git submodule add https://github.com/Crush2680933378/uORM.git thirdparty/uORM
```

```cmake
add_subdirectory(thirdparty/uORM)
target_link_libraries(MyApp PRIVATE uORM::uorm)          # ORM 核心
target_link_libraries(MyWebApp PRIVATE uORM::web)        # ORM + Web 服务
```

## 📁 目录结构

```
uORM/
├── include/uORM/
│   ├── orm/          # Reflection/Mapper/Query/Schema/Transaction/QueryResult/Bind
│   ├── driver/       # DBInterfaces/DriverRegistry/DataSource/SqlDialect + 各驱动实现
│   └── web/          # HttpServer/Router/Http/ConnectionManager/StaticFiles/JsonUtil
├── webapp/           # React (Vite + AntD) 前端
├── examples/         # full_usage_example / http_demo / webconsole
├── tests/            # doctest 单元测试 + 三库集成测试
└── thirdparty/uJSON  # JSON 库子模块
```

## 🗺️ 路线图

- [ ] 异步查询 API（asio C++20 协程）
- [ ] HTTPS / TLS 加密连接（MySQL 已支持 useTLS 开关）
- [ ] WebSocket 实时日志与长查询取消
- [ ] Schema 迁移与自动加列（compare entity ↔ table）
- [ ] CSV / JSON 导出、数据行编辑
- [ ] prepared statement 缓存
- [ ] 多用户与权限管理

## 📄 许可证

MIT License
