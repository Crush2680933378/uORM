# uORM：现代 C++ ORM（Linux / MySQL）

uORM 是一个轻量的现代 C++ ORM，提供统一的 Model/DAO 抽象，支持：
- 基于模型元数据自动建表
- 通用 CRUD 接口（适配器层实现）
- MySQL 适配器（可扩展 PostgreSQL/SQLite）
- 命名空间统一为 `uORM`
- 当前 CMake 仅适配 Linux（移除 Windows 专用配置）

## 目录结构
- include/orm：ORM 接口与类型（命名空间 `uORM`）
- include/common/utils：通用配置管理（`ConfigManager.h`）
- src/orm：ORM 实现代码（适配器、连接池）
- src/common/utils：配置管理实现
- examples：使用示例
- cmake：CMake 包配置文件

## 依赖
- C++17+
- CMake 3.16+
- MySQL Connector/C++（如 `apt install libmysqlcppconn-dev`）
- nlohmann_json（可选，JSON 解析，`apt install nlohmann-json3-dev`）

## 构建与安装（Linux）
```bash
cmake -S . -B build -DUORM_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build --prefix /usr/local
```

安装后将提供 CMake 包：
- 包名称：UORM
- 目标：`uORM::uorm`
- 头文件路径：`include/orm/...` 与 `include/common/utils/...`

## 配置文件
项目根目录提供示例配置 `config.json`：
```json
{
  "DataBaseConfig": {
    "hostname": "127.0.0.1",
    "port": 3306,
    "username": "root",
    "password": "password",
    "dataname": "testdb",
    "poolsize": 4
  },
  "RedisConfig": {
    "hostname": "127.0.0.1",
    "port": 6379,
    "password": "",
    "poolsize": 4
  }
}
```
注意：JSON 不支持注释，请按字段类型与范围填写。

## 以包形式使用（find_package）
项目 CMakeLists.txt：
```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(UORM CONFIG REQUIRED)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE uORM::uorm)
```

示例 main.cpp：
```cpp
#include "common/utils/ConfigManager.h"
#include "orm/MySQLAdapter.h"
#include "orm/DAO.h"
#include "orm/Model.h"
#include "orm/Field.h"
#include <optional>
using namespace uORM;
struct User : Model<User> {
    int64_t id;
    std::string name;
    std::string email;
    std::optional<std::string> bio;
    static std::string table() { return "users"; }
    static std::vector<AnyField<User>> schema() {
        return {
            makeField<User, int64_t>("id", &User::id, SqlType::BigInt, 0, false, true, true),
            makeField<User, std::string>("name", &User::name, SqlType::VarChar, 255, false, false, false),
            makeField<User, std::string>("email", &User::email, SqlType::VarChar, 255, false, false, false),
            makeField<User, std::optional<std::string>>("bio", &User::bio, SqlType::Text, 0, true, false, false)
        };
    }
};
int main() {
    ConfigManager::getInstance().readDataBaseconfig("config.json");
    auto adapter = std::make_shared<MySQLAdapter>();
    DAO<User> dao(adapter);
    dao.createTable();
    User u{}; u.name="alice"; u.email="alice@example.com";
    auto id = dao.insert(u); if (id) u.id = *id;
    return 0;
}
```

## 作为子目录使用（未安装）
```cmake
add_subdirectory(uorm)
target_link_libraries(myapp PRIVATE uORM::uorm)
target_include_directories(myapp PRIVATE uorm/include)
```
确保系统可链接 MySQL Connector/C++。

## GitHub 仓库建议
- 推送本项目文件（CMakeLists.txt、include、src、examples、cmake、README.md、LICENSE、.gitignore）
- 可添加 CI（Linux）进行编译验证

## 扩展计划
- PostgreSQL/SQLite 适配器
- 事务与批量操作
- 迁移与版本管理
- QueryBuilder 与复杂条件

## 许可
MIT License
