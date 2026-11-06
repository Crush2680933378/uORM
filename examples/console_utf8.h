#pragma once
// =====================================================================
// 示例程序通用：Windows 控制台 UTF-8 输出设置
//
// 背景：源码按 UTF-8 编码，字符串字面量以 UTF-8 字节原样输出；
//       中文 Windows 的 cmd 默认用 GBK(cp936) 代码页解释字节流，
//       UTF-8 中文会显示为乱码（如“版本”→“鐗堟湰”）。
//       stdout 是纯字节流，程序与终端之间没有编码协商机制，
//       因此约定：启动时把控制台代码页切到 UTF-8(65001)。
// Linux/其他平台为空操作。
// =====================================================================
#if defined(_WIN32)
#include <windows.h>
static void demo_console_utf8(void) {
    SetConsoleOutputCP(CP_UTF8);   // 输出代码页 -> UTF-8
    SetConsoleCP(CP_UTF8);         // 输入代码页 -> UTF-8（交互式 demo 读取中文输入时同样正确）
}
#else
static void demo_console_utf8(void) {}
#endif
