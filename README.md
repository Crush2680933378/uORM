# uORM

uORM 是一个现代化的、轻量级的 C++ ORM (Object-Relational Mapping) 库，基于 C++17 标准。它支持 **MySQL** 和 **PostgreSQL**，提供类型安全的编译期反射、自动 Schema 管理、强大的链式查询构造器以及高性能的连接池管理。

## ✨ 核心特性

-   **多数据库支持**: 支持 MySQL 和 PostgreSQL，可通过配置无缝切换。
-   **编译期反射**: 利用宏和模板元编程，在 C++ 结构体与数据库表之间建立零开销映射。
-   **安全查询构造器**: 提供流式 API (`Query` Builder) 构建复杂 SQL，自动处理参数绑定，**杜绝 SQL 注入**。
-   **自动 Schema 管理**: 支持 `createTable` 自动建表，`truncate` 清空数据，支持自定义 SQL 类型（如 `TIMESTAMP`, `ENUM`）。
-   **CRUD 全覆盖**: 简单的 `save` (Create), `findAll`/`select` (Read), `update` (Update), `remove`/`truncate` (Delete) 接口。
-   **RAII 连接池**: 内置线程安全的连接池，支持自动重连和资源自动回收。
-   **JSON 配置**: 简单易用的 JSON 配置文件。

## 📦 依赖环境

-   **C++ 标准**: C++17 或更高
-   **构建工具**: CMake 3.16+
-   **依赖库**:
    -   [nlohmann/json](https://github.com/nlohmann/json) (用于配置解析)
    -   MySQL: [MySQL Connector/C++](https://dev.mysql.com/downloads/connector/cpp/)
    -   PostgreSQL: [libpqxx](https://github.com/jtv/libpqxx) (可选)

## 🚀 快速开始

### 1. 定义模型 (Model)

使用 `UORM_TABLE_BEGIN` 系列宏定义数据模型。

```cpp
#include "uORM/orm/ORM.h"

struct Product {
    int id;
    std::string name;
    double price;
    int stock;
    std::string created_at;
};

// 注册表结构: 类名, 表名
UORM_TABLE_BEGIN(Product, "products")
    // 字段映射: 成员变量, 数据库列名, 约束条件
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(name, "name", NOT NULL),
    UORM_FIELD(price, "price", NOT NULL),
    UORM_FIELD(stock, "stock", DEFAULT 0),
    // 显式指定 SQL 类型 (解决 MySQL 5.7+ VARCHAR 默认值问题)
    UORM_FIELD_TYPE(created_at, "created_at", "DATETIME", DEFAULT CURRENT_TIMESTAMP)
UORM_TABLE_END()
```

### 2. 配置文件 (config.json)

在可执行文件同级目录创建 `config.json`：

```json
{
    "driver_type": 1, 
    "hostname": "127.0.0.1",
    "port": 3306,
    "username": "root",
    "password": "your_password",
    "dataname": "uorm_db",
    "poolsize": 5
}
```
*   `driver_type`: `1` (MySQL), `2` (PostgreSQL)

### 3. 初始化与使用

```cpp
#include "uORM/orm/ORM.h"
#include <iostream>

int main() {
    // 1. 加载配置
    if (!uORM::ConfigManager::getInstance().readDataBaseconfig("config.json")) {
        return -1;
    }
    
    // 2. 初始化连接池
    uORM::ConnectionPool::instance();
    
    // 3. 自动建表
    uORM::Schema::createTable<Product>();
    
    // 4. 清空旧数据 (可选)
    uORM::Mapper<Product>::truncate();

    // 5. 插入数据
    Product p{0, "iPhone 15", 999.99, 100, ""};
    uORM::Mapper<Product>::save(p);
    
    // 6. 使用查询构造器 (推荐)
    uORM::Query q;
    q.eq("name", "iPhone 15")
     .gt("price", 500.0);
     
    auto productOpt = uORM::Mapper<Product>::selectOne(q);
    if (productOpt) {
        std::cout << "Found: " << productOpt->name << std::endl;
    }

    return 0;
}
```

## 🛠 功能详解

### 查询构造器 (Query Builder)

uORM 提供强大的链式调用 API，支持复杂的 SQL 逻辑。

```cpp
uORM::Query query;

// 基础条件
query.eq("category", "Electronics")
     .gt("price", 100.0);

// 逻辑组合 (OR)
query.eq("status", "pending")
     .or_()
     .lt("stock", 10);

// 范围与集合
query.between("created_at", "2023-01-01", "2023-12-31")
     .in("id", std::vector<int>{1, 2, 3});

// 模糊查询
query.like("name", "%Pro%");

// 排序与分页
query.orderBy("price", false) // false = DESC
     .limit(20)
     .offset(0);

// 执行查询
auto results = uORM::Mapper<Product>::select(query);
```

### 事务支持

目前 uORM 的每个 `Mapper` 操作（如 `save`, `update`）都是原子性的（基于单次连接获取）。复杂的事务支持正在开发中，可以通过获取原生连接手动控制。

## 🔨 构建指南

### 作为子项目集成

将 uORM 放入项目的 `thirdparty` 目录，并在 `CMakeLists.txt` 中添加：

```cmake
add_subdirectory(thirdparty/uORM)
target_link_libraries(your_app PRIVATE uORM::uorm)
```

### 独立构建与安装

```bash
mkdir build && cd build
# 默认构建静态库，开启示例
cmake .. -DUORM_BUILD_SHARED=OFF -DBUILD_EXAMPLES=ON
cmake --build .

# 运行示例
./uORM_example
```

### 编译选项

| 选项 | 默认值 | 说明 |
| :--- | :--- | :--- |
| `UORM_BUILD_SHARED` | `ON` | 构建动态库 (ON) 或静态库 (OFF) |
| `USE_POSTGRESQL` | `OFF` | 启用 PostgreSQL 支持 (默认 MySQL) |
| `BUILD_EXAMPLES` | `ON` | 构建示例程序 |

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

## 📄 许可证

MIT License
