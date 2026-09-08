# 跨语言绑定示例

uORM 对外提供两种稳定的非 C++ 使用方式：

| 方式 | 目标/文件 | 适用 |
|---|---|---|
| C ABI 动态库 | `uorm_c`（`include/uORM/abi/uorm_c.h`） | C / Go / Rust / Java / C# / Python… |
| C++ 抽象接口（Pimpl） | `uormpp`（`include/uormpp/UormPP.h`） | C++（实现完全隐藏在动态库内） |

所有示例覆盖同一组功能：打开数据库、建表、唯一索引、参数化插入、查询迭代（含 NULL）、
事务提交/回滚、UPDATE、DELETE、错误处理。

## C（`c_full_demo.c`）

```bash
cmake --build build            # 生成 build/uORM_c_demo
cd build && ./uORM_c_demo
```

## C++（`cpp_full_demo.cpp`，抽象基类 + Pimpl）

```bash
cmake --build build            # 生成 build/uORMPP_demo 与 uormpp 动态库
cd build && ./uORMPP_demo
```

在自己的项目中：

```cmake
target_link_libraries(app PRIVATE uORM::uormpp)   # 或链接 uormpp 动态库 + 头文件
```

## Go（cgo）

```bash
cd examples/bindings/go
export CGO_ENABLED=1
export PATH=/mingw64/bin:$PATH              # 需要 gcc
export PATH="$(cd ../../build && pwd)":$PATH # uorm_c 动态库搜索
go mod init uorm-go-demo
go run go_demo.go
```

## Rust

```bash
cd examples/bindings/rust
rustc --edition 2021 rust_demo.rs -o rust_demo -L ../../build -l dylib=uorm_c
./rust_demo
```

## Java（JNA，无需写 JNI）

```bash
cd examples/bindings/java
# 下载 jna.jar: https://github.com/java-native-access/jna/releases (jna-5.x.jar)
export LD_LIBRARY_PATH="$(cd ../../build && pwd)"   # Windows 用 PATH
javac -cp jna.jar JavaUormDemo.java
java  -cp "jna.jar:." -Djna.library.path=../../build JavaUormDemo
```

## C#（P/Invoke，.NET 6+）

```bash
cd examples/bindings/csharp
dotnet new console -n UormSharpDemo --force
# 将 CSharpUormDemo.cs 内容替换生成的 Program.cs，并把
# uorm_c.dll 与驱动 dll 复制到输出目录（bin/Debug/net*/）
dotnet run
```

## Python（ctypes）见 `examples/python_ctypes_demo.py`。

## 注意事项

- **动态库搜索路径**：Windows 加 PATH；Linux 设 `LD_LIBRARY_PATH`；macOS 设 `DYLD_LIBRARY_PATH`。
- **参数生命周期**：`uorm_param.s` 字符串在调用期间被借用；结果集中的字符串由结果集持有，`destroy` 后失效。
- **事务**：通过 `uorm_ds_acquire` + `begin/commit/rollback` 使用；归还连接前请结束事务。
