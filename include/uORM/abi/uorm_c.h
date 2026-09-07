/*
 * uorm_c.h —— uORM 的纯 C ABI 接口
 * =====================================================================
 * 设计目标：
 *   1. ABI 稳定：只暴露 extern "C" 函数与不透明句柄，跨编译器/STL 版本可用
 *   2. 语言无关：Python(ctypes)/Rust/Go/C# 等任何能调 C 的语言可直接绑定
 *   3. 零拷贝取值：结果集中的字符串由结果集持有，读取无需释放
 *
 * 分层模型：
 *   uorm_data_source  一个数据库 + 一个有界连接池（线程安全）
 *   uorm_connection   从池借出的连接（RAII 归还，用于事务/复用语句）
 *   uorm_stmt         预编译语句（绑定参数后可重复执行）
 *   uorm_result       查询结果（列名 + 行数据，取值函数访问）
 *
 * 约定：
 *   - 除 uorm_ds_destroy 等析构函数外，所有函数线程安全
 *     （uorm_data_source 本身线程安全；connection/stmt/result 不共享）
 *   - 所有字符串入参为 UTF-8，仅在调用期间被借用
 *   - 所有字符串出参（结果集内容）由结果集持有，uorm_result_destroy 后失效
 *   - 调用失败返回负数错误码，详情用 uorm_last_error() 获取（线程局部）
 *
 * 用法示例：
 *   uorm_ds_options opt = {0};
 *   opt.driver = "sqlite"; opt.database = "app.db";
 *   uorm_data_source* ds = uorm_ds_create(&opt);
 *   uorm_result* rs = NULL;
 *   uorm_param p = { UORM_TYPE_STRING, .s = "Alice" };
 *   uorm_ds_query(ds, "SELECT * FROM users WHERE name = ?", &p, 1, &rs);
 *   for (int r = 0; r < uorm_result_row_count(rs); ++r)
 *       puts(uorm_value_string(rs, r, 0));
 *   uorm_result_destroy(rs);
 *   uorm_ds_destroy(ds);
 */
#ifndef UORM_C_H
#define UORM_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 导出与调用约定 ---- */
#if defined(_WIN32) && defined(UORM_C_SHARED)
#  ifdef UORM_C_BUILDING
#    define UORM_API __declspec(dllexport)
#  else
#    define UORM_API __declspec(dllimport)
#  endif
#else
#  define UORM_API
#endif

#if defined(_WIN32) && defined(_M_IX86)
#  define UORM_CALL __cdecl
#else
#  define UORM_CALL
#endif

/* ---- 版本 ---- */
#define UORM_VERSION_MAJOR 0
#define UORM_VERSION_MINOR 4
#define UORM_VERSION_PATCH 0
#define UORM_VERSION_STRING "0.4.0"

/* ---- 错误码 ---- */
typedef enum uorm_status {
    UORM_OK                    = 0,
    UORM_ERR_INVALID_ARG       = -1,  /* 参数为 NULL / 非法 */
    UORM_ERR_CONFIG            = -2,  /* 配置错误 */
    UORM_ERR_CONNECTION        = -3,  /* 连接失败 */
    UORM_ERR_SQL               = -4,  /* SQL 执行错误 */
    UORM_ERR_UNKNOWN_DRIVER    = -5,  /* 驱动未编入或名字未知 */
    UORM_ERR_OUT_OF_MEMORY     = -6,
    UORM_ERR_NOT_FOUND         = -7,  /* 连接配置 / 列不存在 */
    UORM_ERR_BUFFER_TOO_SMALL  = -8,  /* 输出缓冲区不足 */
    UORM_ERR_POOL_TIMEOUT      = -9,  /* 连接池获取超时 */
    UORM_ERR_INDEX_RANGE       = -10  /* 行/列下标越界 */
} uorm_status;

/* ---- 值类型 ---- */
typedef enum uorm_type {
    UORM_TYPE_NULL   = 0,
    UORM_TYPE_INT64  = 1,
    UORM_TYPE_DOUBLE = 2,
    UORM_TYPE_STRING = 3
} uorm_type;

/* ---- 不透明句柄 ---- */
typedef struct uorm_data_source uorm_data_source;
typedef struct uorm_connection  uorm_connection;
typedef struct uorm_statement   uorm_statement;
typedef struct uorm_result      uorm_result;

/* ---- 参数绑定 ----
 * 与 SQL 中的 ? 占位符按序对应（下标从 1 由数组顺序决定）。
 * type == UORM_TYPE_NULL 时忽略其余字段。
 * s 所指内存在调用期间必须有效。
 */
typedef struct uorm_param {
    int         type;   /* uorm_type */
    long long   i64;
    double      f64;
    const char* s;
} uorm_param;

/* ---- 数据源配置 ---- */
typedef struct uorm_ds_options {
    const char* driver;             /* "mysql" / "postgresql" / "sqlite" */
    const char* host;               /* SQLite 忽略；NULL 视为 "127.0.0.1" */
    int         port;               /* 0 = 驱动默认端口 */
    const char* username;
    const char* password;
    const char* database;           /* 数据库名；SQLite 为文件路径（:memory: 单连接时可用） */
    int         pool_size;          /* <=0 取默认 5 */
    int         acquire_timeout_ms; /* <=0 取默认 3000 */
    int         use_tls;            /* 0/1；MySQL 默认关闭 TLS */
} uorm_ds_options;

/* =====================================================================
 * 全局
 * ===================================================================== */

/* 库版本字符串，如 "0.4.0"。静态生命周期。 */
UORM_API const char* UORM_CALL uorm_version(void);

/* 已编入的驱动数量；driver_name 按下标枚举，返回 UORM_OK，
 * buf 不足时返回 UORM_ERR_BUFFER_TOO_SMALL（buf 仍会写入截断值）。 */
UORM_API int UORM_CALL uorm_driver_count(void);
UORM_API int UORM_CALL uorm_driver_name(int index, char* buf, size_t buf_len);

/* 最近一次错误的描述（线程局部，静态生命周期，无需释放；无错误时返回 ""）。 */
UORM_API const char* UORM_CALL uorm_last_error(void);

/* =====================================================================
 * 数据源（连接池）
 * ===================================================================== */

/* 创建数据源。失败返回 NULL（见 uorm_last_error）。 */
UORM_API uorm_data_source* UORM_CALL uorm_ds_create(const uorm_ds_options* options);

/* 销毁数据源并关闭池中所有空闲连接（借出的连接在归还时清理）。 */
UORM_API void UORM_CALL uorm_ds_destroy(uorm_data_source* ds);

/* 池统计：idle/in_use/total_created，任一出参可为 NULL。 */
UORM_API int UORM_CALL uorm_ds_stats(uorm_data_source* ds,
                                     int* idle, int* in_use, int* total_created);

/* 便捷查询：借连接执行 -> 取回全部结果 -> 归还连接。 */
UORM_API int UORM_CALL uorm_ds_query(uorm_data_source* ds,
                                     const char* sql,
                                     const uorm_param* params, int param_count,
                                     uorm_result** out_result);

/* 便捷执行：DML/DDL，affected 可为 NULL。 */
UORM_API int UORM_CALL uorm_ds_execute(uorm_data_source* ds,
                                       const char* sql,
                                       const uorm_param* params, int param_count,
                                       long long* out_affected);

/* =====================================================================
 * 连接（事务 / 语句复用）
 * ===================================================================== */

/* 从池借出一个连接（池满时按配置超时等待）。 */
UORM_API int UORM_CALL uorm_ds_acquire(uorm_data_source* ds, uorm_connection** out_conn);

/* 归还连接到池（归还不回滚未提交事务，请自行 begin/commit/rollback 配对）。 */
UORM_API void UORM_CALL uorm_conn_release(uorm_connection* conn);

/* 健康检查：非 0 表示连接可用。 */
UORM_API int UORM_CALL uorm_conn_ping(uorm_connection* conn);

/* 事务。 */
UORM_API int UORM_CALL uorm_conn_begin(uorm_connection* conn);
UORM_API int UORM_CALL uorm_conn_commit(uorm_connection* conn);
UORM_API int UORM_CALL uorm_conn_rollback(uorm_connection* conn);

/* 最近一次自增 ID；不支持时返回 -1（PG 建议 INSERT ... RETURNING）。 */
UORM_API long long UORM_CALL uorm_conn_last_insert_id(uorm_connection* conn);

/* 连接级查询/执行（事务内使用）。 */
UORM_API int UORM_CALL uorm_conn_query(uorm_connection* conn,
                                       const char* sql,
                                       const uorm_param* params, int param_count,
                                       uorm_result** out_result);
UORM_API int UORM_CALL uorm_conn_execute(uorm_connection* conn,
                                         const char* sql,
                                         const uorm_param* params, int param_count,
                                         long long* out_affected);

/* =====================================================================
 * 预编译语句（可重复绑定执行；必须在所属连接 release 之前销毁）
 * ===================================================================== */

UORM_API int UORM_CALL uorm_conn_prepare(uorm_connection* conn,
                                         const char* sql,
                                         uorm_statement** out_stmt);

/* 设置参数（index 从 1 开始）。重复设置会覆盖。 */
UORM_API int UORM_CALL uorm_stmt_bind_int64(uorm_statement* stmt, int index, long long value);
UORM_API int UORM_CALL uorm_stmt_bind_double(uorm_statement* stmt, int index, double value);
UORM_API int UORM_CALL uorm_stmt_bind_string(uorm_statement* stmt, int index, const char* value);
UORM_API int UORM_CALL uorm_stmt_bind_null(uorm_statement* stmt, int index);

/* 一次绑定多个参数（数组形式，顺序即 1..n）。 */
UORM_API int UORM_CALL uorm_stmt_bind_params(uorm_statement* stmt,
                                             const uorm_param* params, int param_count);

/* 执行。注意：语句参数缓冲在 stmt 内部，结果取出前不要再改参数。 */
UORM_API int UORM_CALL uorm_stmt_query(uorm_statement* stmt, uorm_result** out_result);
UORM_API int UORM_CALL uorm_stmt_execute(uorm_statement* stmt, long long* out_affected);

UORM_API void UORM_CALL uorm_stmt_destroy(uorm_statement* stmt);

/* =====================================================================
 * 结果集
 * ===================================================================== */

UORM_API void UORM_CALL uorm_result_destroy(uorm_result* result);

UORM_API int UORM_CALL uorm_result_row_count(const uorm_result* result);
UORM_API int UORM_CALL uorm_result_column_count(const uorm_result* result);

/* 列名（结果集持有，随 result 销毁失效）；越界返回 NULL。 */
UORM_API const char* UORM_CALL uorm_result_column_name(const uorm_result* result, int column);

/* 取值：越界返回负错误码。字符串由结果集持有，随销毁失效。 */
UORM_API int UORM_CALL uorm_value_type(const uorm_result* result, int row, int column);
UORM_API int UORM_CALL uorm_value_is_null(const uorm_result* result, int row, int column);
UORM_API long long UORM_CALL uorm_value_int64(const uorm_result* result, int row, int column);
UORM_API double UORM_CALL uorm_value_double(const uorm_result* result, int row, int column);
UORM_API const char* UORM_CALL uorm_value_string(const uorm_result* result, int row, int column);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UORM_C_H */
