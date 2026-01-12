# uORM

uORM 是一个现代化的、轻量级的 C++17 ORM (Object-Relational Mapping) 库。它旨在提供简单、直观且类型安全的数据库操作接口，支持 **MySQL** 和 **PostgreSQL**。

## ✨ 核心特性

-   **多数据库支持**: 无缝切换 MySQL 和 PostgreSQL，底层差异对用户透明。
-   **编译期反射**: 基于宏和模板元编程，实现零开销的结构体到数据库表的映射。
-   **安全查询构造器**: 流式 API (`Query` Builder) 构建 SQL，自动参数绑定，**杜绝 SQL 注入**。
-   **自动 Schema 管理**: 支持 `createTable` 自动建表，`truncate` 清空数据。
-   **CRUD 全覆盖**: 提供 `save`, `select`, `update`, `remove` 等标准操作。
-   **RAII 连接池**: 内置高性能线程安全连接池，支持自动重连和资源回收。
-   **健壮的异常处理**: 统一的异常体系 (`uORM::Exception`)，精准报告配置、连接及 SQL 执行错误。
-   **JSON 配置**: 集成轻量级 `uJSON` 库，配置文件简单易读。

## 📦 依赖环境

-   **C++ 标准**: C++17 或更高
-   **构建工具**: CMake 3.16+
-   **依赖库**:
    -   **uJSON**: 内置高性能 JSON 库 (位于 `thirdparty/uJSON`)。
    -   **MySQL**: [MySQL Connector/C++](https://dev.mysql.com/downloads/connector/cpp/)
    -   **PostgreSQL**: [libpqxx](https://github.com/jtv/libpqxx) (可选)

## 🔌 在其他项目中使用

推荐将 uORM 作为子模块（Submodule）集成。

### 1. 推荐的项目结构

```text
MyProject/
├── CMakeLists.txt          # 项目构建文件
├── main.cpp                # 您的源代码
├── config.json             # 数据库配置文件
└── thirdparty/
    └── uORM/               # 将 uORM 仓库克隆到这里
```

### 2. CMakeLists.txt 配置

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyProject CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 引入 uORM
add_subdirectory(thirdparty/uORM)

# 2. 定义可执行文件
add_executable(MyApp main.cpp)

# 3. 链接 uORM
target_link_libraries(MyApp PRIVATE uORM::uorm)

# (可选) 复制配置文件到构建目录
configure_file(config.json ${CMAKE_CURRENT_BINARY_DIR}/config.json COPYONLY)
```

## 🚀 快速开始

### 1. 定义模型 (Model)

使用 `UORM_TABLE_BEGIN` 系列宏定义数据模型。

```cpp
#include <uORM/orm/ORM.h>

struct User {
    int id;
    std::string name;
    int age;
    std::string email;
    std::string created_at;
};

// 注册表结构: 类名, 表名
UORM_TABLE_BEGIN(User, "users")
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(name, "name", NOT NULL),
    UORM_FIELD(age, "age", DEFAULT 18),
    UORM_FIELD(email, "email", UNIQUE),
    UORM_FIELD_TYPE(created_at, "created_at", "DATETIME", DEFAULT CURRENT_TIMESTAMP)
UORM_TABLE_END()
```

### 2. 配置文件 (config.json)

在可执行文件同级目录创建 `config.json`：

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
*   `driver`: 支持 `mysql` 或 `postgresql`。

### 3. 编写代码 (main.cpp)

```cpp
#include <iostream>
#include <uORM/orm/ORM.h>

int main() {
    try {
        // 1. 加载配置
        // 如果配置错误或文件不存在，将抛出 uORM::ConfigurationError
        uORM::ConfigManager::getInstance().readDataBaseconfig("config.json");
        
        // 2. 初始化连接池
        // 如果连接失败，将抛出 uORM::ConnectionError
        uORM::ConnectionPool::instance();
        std::cout << "数据库连接成功!" << std::endl;

        // 3. 自动建表
        uORM::Schema::createTable<User>();

        // 4. 插入数据
        User user{0, "Trae", 25, "trae@example.com", ""};
        if (uORM::Mapper<User>::save(user)) {
            std::cout << "用户保存成功" << std::endl;
        }

        // 5. 查询数据
        uORM::Query q;
        q.eq("name", "Trae");
        auto result = uORM::Mapper<User>::selectOne(q);
        if (result) {
            std::cout << "查询结果: " << result->name << ", ID: " << result->id << std::endl;
        }

    } catch (const uORM::Exception& e) {
        std::cerr << "uORM 错误: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "系统错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## 🛠 功能详解

### 查询构造器 (Query Builder)

```cpp
uORM::Query query;

// 链式调用
query.eq("status", "active")
     .gt("age", 18)
     .like("name", "%Trae%")
     .orderBy("created_at", false) // 降序
     .limit(10);

auto users = uORM::Mapper<User>::select(query);
```

### 异常处理

uORM 提供了完善的异常层级：

*   `uORM::Exception` (基类)
    *   `uORM::ConfigurationError`: 配置加载或解析错误
    *   `uORM::DatabaseError`: 数据库相关错误
        *   `uORM::ConnectionError`: 连接失败
        *   `uORM::SqlError`: SQL 执行错误

## 🔨 构建指南

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

## 📄 许可证

MIT License

