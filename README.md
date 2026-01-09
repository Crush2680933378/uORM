# uORM: 现代 C++17/20/23 ORM（MySQL）

uORM 是一个轻量现代 C++ ORM，提供统一的 Model/DAO 抽象，支持：
- 基于模型元数据自动建表
- 统一 CRUD 接口（适配器层实现）
- MySQL 适配器（可扩展 PostgreSQL/SQLite）
- 跨平台（Windows/Linux）

## 目录结构
- include/orm：公开头文件（命名空间 uORM）
- src/orm：库实现源码
- examples：使用示例
- cmake：CMake 包配置文件

## 依赖
- C++17 或以上
- CMake 3.16+
- MySQL Connector/C++
- nlohmann_json（可选，JSON 配置解析）

Windows 需配置 MySQL Connector/C++ 的包含与库路径；Linux 使用包管理器安装（如 `apt install libmysqlcppconn-dev`）。

## 构建（生成动态库）

Windows（MSVC 或 MinGW）：
```powershell
cmake -S . -B build -DUORM_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build --prefix C:\uorm-install
```

Linux（GCC/Clang）：
```bash
cmake -S . -B build -DUORM_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /usr/local
```

安装后会提供 CMake 包：
- 包名称：UORM
- 目标：uORM::uorm
- 头文件路径：include/orm/...

## 以包形式使用

CMakeLists.txt：
```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(UORM CONFIG REQUIRED)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE uORM::uorm)
```

main.cpp 示例：
```cpp
#include "orm/ConfigManager.h"
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

## 手动链接说明
若未安装为包，可直接将本仓库作为子目录：
```cmake
add_subdirectory(uorm)
target_link_libraries(myapp PRIVATE uORM::uorm)
target_include_directories(myapp PRIVATE uorm/include)
```
同时确保 MySQL Connector/C++ 可被链接（Windows 需在 CMake 或 IDE 配置库路径）。

## GitHub 仓库使用指南
1. 创建 GitHub 仓库（例如 `uorm`）
2. 推送本项目文件（包含 CMakeLists.txt、include、src、examples、cmake、README.md）
3. 设置 Actions（可选）以在 Windows/Linux 上 CI 构建
4. 在 README 中标注依赖与安装说明（已提供）

## 扩展计划
- PostgreSQL/SQLite 适配器
- 事务与批量操作
- 迁移与版本管理
- QueryBuilder 与复杂条件

## 许可
MIT License
