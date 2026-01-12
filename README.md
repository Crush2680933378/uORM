# uJSON

uJSON 是一个极简、轻量级且现代化的 C++17 JSON 库。它设计初衷是为了提供简单直观的 API，同时保持高性能和零外部依赖。

## ✨ 核心特性

-   **现代 C++**: 基于 C++17 标准，利用 `std::variant`、`std::shared_ptr` 等现代特性。
-   **直观 API**: 支持直观的下标访问、链式调用和类型转换。
-   **零依赖**: 仅依赖 C++ 标准库，易于集成到任何项目中。
-   **异常安全**: 完善的异常处理机制 (`uJSON::ParseError`, `uJSON::TypeError` 等)，确保代码健壮性。
-   **灵活构建**: 支持构建为静态库或动态库，并在 Windows 上自动处理 DLL 导出。
-   **内存安全**: 内部自动管理内存，防止内存泄漏。

## 📦 集成方法

### 使用 CMake

1.  将 `uJSON` 目录放入您的 `thirdparty` 文件夹。
2.  在您的 `CMakeLists.txt` 中添加：

    ```cmake
    add_subdirectory(thirdparty/uJSON)
    target_link_libraries(your_target PUBLIC uJSON) # 链接动态库
    # 或者
    target_link_libraries(your_target PUBLIC uJSON_static) # 链接静态库
    ```

## 🚀 快速上手

### 1. 包含头文件

```cpp
#include <uJSON/ujson.h>
using json = uJSON::Value;
```

### 2. 解析 JSON

```cpp
std::string json_str = R"({
    "name": "uJSON",
    "version": 1.0,
    "features": ["fast", "simple"]
})";

try {
    json j = json::parse(json_str);
    std::string name = j["name"].get<std::string>();
    double version = j["version"].get<double>();
    
    std::cout << "Library: " << name << " v" << version << std::endl;
} catch (const uJSON::Exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```

### 3. 生成 JSON

```cpp
json j = json::object();
j["id"] = 123;
j["active"] = true;
j["data"] = json::array();
j["data"].push_back(1);
j["data"].push_back(2);

std::cout << j << std::endl; 
// 输出: {"id":123,"active":true,"data":[1,2]}
```

### 4. 异常处理

uJSON 提供了细粒度的异常类：

*   `uJSON::ParseError`: 解析格式错误。
*   `uJSON::TypeError`: 类型访问错误（如把数组当对象用）。
*   `uJSON::RuntimeError`: 其他运行时错误（如数组越界）。

```cpp
try {
    json j = json::object();
    j.get<int>(); // 抛出 TypeError
} catch (const uJSON::TypeError& e) {
    // 处理类型错误
}
```

## 🛠️ 构建选项

| 选项 | 说明 |
| :--- | :--- |
| `BUILD_SHARED_LIBS` | 控制默认构建行为（uJSON 显式提供了 `uJSON` 和 `uJSON_static` 两个目标，此选项影响不大） |

## 📄 许可证

MIT License

