#!/usr/bin/env python3
"""uORM C ABI 的 Python 绑定演示（ctypes，无需任何 C 编译）。

前置：uorm_c 动态库已构建（build/uorm_c.dll / libuorm_c.so），且
驱动依赖（libmariadb/libpq/sqlite3）位于动态库搜索路径。
"""
import ctypes
import os
import sys

# ---- 加载动态库 ----
_here = os.path.dirname(os.path.abspath(__file__))
_build = os.path.join(_here, "..", "build")
_candidates = [
    os.path.join(_build, "uorm_c.dll"),
    os.path.join(_build, "libuorm_c.dll"),    # MinGW 命名
    os.path.join(_build, "libuorm_c.so"),
    os.path.join(_build, "libuorm_c.dylib"),
    "uorm_c",
]
lib = None
for path in _candidates:
    try:
        lib = ctypes.CDLL(path)
        break
    except OSError:
        continue
if lib is None:
    sys.exit("找不到 uorm_c 动态库，请先构建（cmake --build build）")

# ---- 错误码 / 类型常量（与 uorm_c.h 一致）----
UORM_OK = 0
UORM_TYPE_NULL, UORM_TYPE_INT64, UORM_TYPE_DOUBLE, UORM_TYPE_STRING = 0, 1, 2, 3


class UormParam(ctypes.Structure):
    """uorm_param: {int type; long long i64; double f64; const char* s;}"""
    _fields_ = [
        ("type", ctypes.c_int),
        ("i64", ctypes.c_longlong),
        ("f64", ctypes.c_double),
        ("s", ctypes.c_char_p),
    ]


class UormDsOptions(ctypes.Structure):
    """uorm_ds_options"""
    _fields_ = [
        ("driver", ctypes.c_char_p),
        ("host", ctypes.c_char_p),
        ("port", ctypes.c_int),
        ("username", ctypes.c_char_p),
        ("password", ctypes.c_char_p),
        ("database", ctypes.c_char_p),
        ("pool_size", ctypes.c_int),
        ("acquire_timeout_ms", ctypes.c_int),
        ("use_tls", ctypes.c_int),
    ]


# ---- 函数签名 ----
lib.uorm_version.restype = ctypes.c_char_p
lib.uorm_driver_count.restype = ctypes.c_int
lib.uorm_last_error.restype = ctypes.c_char_p
lib.uorm_ds_create.restype = ctypes.c_void_p
lib.uorm_ds_create.argtypes = [ctypes.POINTER(UormDsOptions)]
lib.uorm_ds_destroy.argtypes = [ctypes.c_void_p]
lib.uorm_ds_execute.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                ctypes.POINTER(UormParam), ctypes.c_int,
                                ctypes.POINTER(ctypes.c_longlong)]
lib.uorm_ds_query.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                              ctypes.POINTER(UormParam), ctypes.c_int,
                              ctypes.POINTER(ctypes.c_void_p)]
lib.uorm_result_row_count.argtypes = [ctypes.c_void_p]
lib.uorm_result_row_count.restype = ctypes.c_int
lib.uorm_result_column_count.argtypes = [ctypes.c_void_p]
lib.uorm_result_column_count.restype = ctypes.c_int
lib.uorm_result_column_name.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.uorm_result_column_name.restype = ctypes.c_char_p
lib.uorm_value_string.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
lib.uorm_value_string.restype = ctypes.c_char_p
lib.uorm_value_double.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
lib.uorm_value_double.restype = ctypes.c_double
lib.uorm_value_int64.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
lib.uorm_value_int64.restype = ctypes.c_longlong
lib.uorm_result_destroy.argtypes = [ctypes.c_void_p]


def check(status, what):
    if status != UORM_OK:
        raise RuntimeError(f"{what} 失败({status}): {lib.uorm_last_error().decode()}")


print("uORM 版本:", lib.uorm_version().decode())

# ---- 打开 SQLite 数据源 ----
opt = UormDsOptions()
opt.driver = b"sqlite"
opt.database = b"uorm_python_demo.db"
opt.pool_size = 2
ds = lib.uorm_ds_create(ctypes.byref(opt))
assert ds, lib.uorm_last_error().decode()

# ---- 建表 + 参数化插入 ----
check(lib.uorm_ds_execute(
    ds, b"CREATE TABLE IF NOT EXISTS products "
        b"(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, price REAL)",
    None, 0, None), "建表")

for name, price in [("iPhone", 999.5), ("Mug", 19.9)]:
    params = (UormParam * 1)()
    params[0].type = UORM_TYPE_STRING
    params[0].s = name.encode()
    check(lib.uorm_ds_execute(
        ds, b"INSERT INTO products (name) VALUES (?)", params, 1, None), "插入")

# 更新价格（浮点参数）
params = (UormParam * 1)()
params[0].type = UORM_TYPE_DOUBLE
params[0].f64 = 999.5
check(lib.uorm_ds_execute(
    ds, b"UPDATE products SET price = ? WHERE name = 'iPhone'", params, 1, None), "更新")

# ---- 查询 ----
rs = ctypes.c_void_p()
check(lib.uorm_ds_query(ds, b"SELECT id, name, price FROM products", None, 0,
                        ctypes.byref(rs)), "查询")
rows = lib.uorm_result_row_count(rs)
cols = lib.uorm_result_column_count(rs)
names = [lib.uorm_result_column_name(rs, c).decode() for c in range(cols)]
print(f"查询结果 {rows} 行 x {cols} 列: {names}")
for r in range(rows):
    print(" ", [lib.uorm_value_string(rs, r, c).decode() for c in range(cols)])
lib.uorm_result_destroy(rs)
lib.uorm_ds_destroy(ds)
print("Python 绑定演示完成")
