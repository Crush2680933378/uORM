# uORM

uORM 是一个现代化的、轻量级的 C++17 ORM 与 **Web 数据库管理台**。一份实体定义即可操作 **MySQL / MariaDB、PostgreSQL、SQLite**，并内置基于 standalone asio 的 HTTP 服务 + React 前端——浏览器里直接管理各种数据库，无需安装 Navicat / DBeaver 等本地客户端。

## ✨ 核心特性

- **运行时多驱动**：MySQL(libmariadb)、PostgreSQL(libpq)、SQLite(sqlite3) 在编译期按可用性编入，运行期按名字创建。一个进程可同时连接多种数据库。
- **Web 管理台**：连接管理、库表浏览（分页/排序/表结构）、SQL 控制台（参数化 + 自动 LIMIT），开箱即用。
- **编译期映射**：`UORM_REFLECTION(User, id, name, age)` 一行注册；也支持完整列定义的 `UORM_TABLE_*` 宏。
- **方言感知 DDL**：同一份实体定义在三库上建表（`AUTO_INCREMENT` / `GENERATED ... IDENTITY` / `AUTOINCREMENT`、`DATETIME`→`TIMESTAMP` 等自动转换）。
- **安全查询**：统一 `?` 占位符 + 预编译参数绑定（PG 自动转 `$n`），杜绝 SQL 注入。
- **声明式索引**：`UORM_TABLE_END_WITH_INDEXES` + `UORM_INDEX_DEF`（单列/唯一/复合索引），建表自动创建（存在性检查幂等）；`createIndex`/`dropIndex`/`indexExists` API。
- **复合主键**：`UORM_COMPOSITE_PK(T, "colA", "colB")` 表级约束，update/remove/saveOrUpdate 全链路感知。
- **外键**：字段约束写 `REFERENCES parent(id)` 即可（MySQL 的内联 REFERENCES 会被自动转成表级 FOREIGN KEY——MySQL 解析器接受但忽略内联写法的陷阱已处理）。
- **轻量迁移**：`db.syncTable<T>()` 缺表建表、缺列自动补列（剥离不安全约束）、补建索引；另有 `addColumn`/`dropColumn`/`renameColumn`/`renameTable`/`tableExists`/`existingColumns`。
- **replace 语义**：`db.replace(e)` 冲突时整行替换——MySQL `REPLACE INTO`、SQLite `INSERT OR REPLACE`、PG `ON CONFLICT (pk) DO UPDATE` 全列，方言各自最优路径。
- **updateSome**：`db.updateSome(e, &User::name, &User::age)` 按实体只更新指定字段，其余列不动。
- **异步 API**：`db.queryAsync(sql, params)` / `executeAsync` / `selectAsync<T>` / `countAsync<T>` 返回 `std::future`，共享线程池执行，异常经 future 传播；连接池线程安全。
- **类型安全查询**：`db.query<User>().where(&User::age, uORM::GT, 18).orderByDesc(&User::id).all()`——成员指针当列名，写错字段编译不过；同一组条件可直接 `.set(...).update()` / `.remove()`（无 WHERE 拒绝执行，防全表误操作）。
- **RAII 事务作用域**：`auto tx = db.txBegin(); ...; tx->commit();`——忘提交/抛异常析构自动回滚，连接自动归还；也支持 `db.tx(lambda)`。
- **批量插入**：`db.saveRange(items)` 单条多行 VALUES、按驱动参数上限分块、自增 id 三库各自正确写回。
- **事务**：`IConnection::begin/commit/rollback` + RAII `Transaction`/`TxScope` + `withTransaction(ds, lambda)`（异常自动回滚）。
- **连接池**：`DataSource` 多数据源、有界池、获取超时、空闲感知 ping（空闲 <30s 的连接直接复用，省一次往返）、失效自动重建。
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

### 3. CRUD（推荐：Database 门面）

```cpp
try {
    uORM::DataSourceConfig cfg;
    cfg.params.driver = "mysql";
    cfg.params.host = "127.0.0.1";
    cfg.params.port = 3306;
    cfg.params.username = "root";
    cfg.params.password = "***";
    cfg.params.database = "uorm_db";
    uORM::DataSource ds(cfg);

    uORM::Database db(ds);   // 绑定数据源，之后每个操作一行搞定

    db.createTable<User>();

    User user{0, "Trae", 25};
    db.save(user);                                  // 自增 id 写回 user.id
    auto tom = db.findById<User>(user.id);          // 按主键查
    auto adults = db.find<User>("age >= ?", 18);    // 条件查

    uORM::Query q;
    q.like("name", "%T%").ge("age", 18).orderBy("id", false).limit(10);
    auto users = db.select<User>(q);

    std::vector<User> batch = /* ... */;
    db.saveRange(batch);                            // 批量插入（单条多行 VALUES）

    db.tx([&](uORM::IConnection& conn) {            // 事务（异常自动回滚）
        uORM::Mapper<User>::update(user, conn);
        uORM::Mapper<User>::save(User{0, "Ann", 30}, conn);
    });

    db.saveOrUpdate(user);                          // Upsert
} catch (const uORM::Exception& e) {
    std::cerr << "uORM 错误: " << e.what() << std::endl;
}
```

仍兼容旧写法：`uORM::ConfigManager` 读 config.json + `uORM::ConnectionPool::instance()` 全局池 + 静态 `uORM::Mapper<T>::xxx`（含 config.json 加载）；所有 Mapper 操作也有 `IConnection&` 重载供事务内使用。

### 4. 类型安全查询与 RAII 事务（推荐）

```cpp
uORM::Database db(ds);

// 条件/排序/分页全部用成员指针指定列——写错字段编译期就报错
auto adults = db.query<User>()
                  .where(&User::age, uORM::GT, 18)
                  .like(&User::name, "%T%")
                  .orderByDesc(&User::id)
                  .limit(10)
                  .all();
auto tom = db.query<User>().where(&User::name, uORM::Op::EQ, std::string("Trae")).first();

// 一组条件直接驱动 UPDATE / DELETE（无 WHERE 拒绝执行）
db.query<User>().where(&User::id, uORM::Op::EQ, user.id)
    .set(&User::age, 26).update();

// RAII 事务作用域：commit() 提交；忘提交/异常 -> 析构自动回滚，连接自动归还
{
    auto tx = db.txBegin();
    uORM::Mapper<User>::save(user, *tx);
    uORM::Mapper<User>::update(user, *tx);
    tx->commit();
}
```

### 5. 多数据源（运行时选择驱动）

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

## 🌐 C ABI 接口（跨语言绑定）

uORM 的整个 C++ 内核编译进一个动态库 `uorm_c`（`uorm_c.dll` / `libuorm_c.so`），对外只暴露 `include/uORM/abi/uorm_c.h` 中的 **纯 C 稳定符号**——任何能调 C 的语言（C/C++/Python/Rust/Go/C#/Java/JNI…）都可以直接使用，不受 C++ 编译器与 STL 版本差异影响。

分层模型：

| 句柄 | 含义 | 生命周期 |
| :--- | :--- | :--- |
| `uorm_data_source` | 一个数据库 + 有界连接池（线程安全） | 手动 `uorm_ds_destroy` |
| `uorm_connection` | 从池借出的连接（事务/语句复用） | `uorm_conn_release` 归还池 |
| `uorm_statement` | 预编译语句，可重复绑定执行 | `uorm_stmt_destroy`（先于连接归还） |
| `uorm_result` | 查询结果（列名 + 行） | `uorm_result_destroy`；字符串由结果集持有（零拷贝） |

所有调用失败返回负错误码（`uorm_status`），详情通过 `uorm_last_error()` 获取（线程局部）。

C 侧最小示例：

```c
#include <uORM/abi/uorm_c.h>

uorm_ds_options opt = {0};
opt.driver = "sqlite";              /* 运行时选择驱动 */
opt.database = "app.db";
uorm_data_source* ds = uorm_ds_create(&opt);

uorm_param p = { UORM_TYPE_STRING, .s = "Alice" };
uorm_result* rs = NULL;
uorm_ds_query(ds, "SELECT id, name FROM users WHERE name = ?", &p, 1, &rs);

for (int r = 0; r < uorm_result_row_count(rs); ++r)
    printf("%lld %s\n", uorm_value_int64(rs, r, 0), uorm_value_string(rs, r, 1));

uorm_result_destroy(rs);
uorm_ds_destroy(ds);
```

完整示例见 [`examples/c_demo.c`](examples/c_demo.c)（事务/预编译语句/错误处理）与 [`examples/python_ctypes_demo.py`](examples/python_ctypes_demo.py)（Python ctypes，零编译直接运行）：

```bash
python examples/python_ctypes_demo.py
# uORM 版本: 0.4.0
# 查询结果 2 行 x 3 列: ['id', 'name', 'price']
```

构建开关：`UORM_BUILD_C_ABI`（默认 `ON`）。集成到其他项目：链接 `uORM::c` 目标或直接分发 `uorm_c` 动态库 + 头文件。

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
| `UORM_BUILD_C_ABI` | `ON` | C ABI 动态库 `uorm_c` |
| `UORM_BUILD_TESTS` | `OFF` | 构建测试（含纯 C 的 `uorm_test_c_abi`） |
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
│   ├── web/          # HttpServer/Router/Http/ConnectionManager/StaticFiles/JsonUtil
│   └── abi/          # uorm_c.h —— 纯 C ABI 稳定接口
├── src/              # uorm_c.cpp（C ABI 实现，内核唯一的编译单元）
├── webapp/           # React (Vite + AntD) 前端
├── examples/         # full_usage_example / http_demo / webconsole / c_demo / python_ctypes_demo
├── tests/            # doctest 单元测试 + 三库集成测试 + 纯 C ABI 测试
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
