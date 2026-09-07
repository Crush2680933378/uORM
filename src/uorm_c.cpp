// 文件说明：
// uorm_c.cpp —— C ABI 的唯一实现翻译单元。
// 把 header-only 的 C++ 内核编译进动态库，对外只导出 uorm_c.h 中的稳定 C 符号。
// 所有 C++ 异常在此边界转换为错误码 + 线程局部错误消息。

#define UORM_C_BUILDING

#include "uORM/abi/uorm_c.h"

#include "uORM/orm/ORM.h"
#include "uORM/driver/DataSource.h"
#include "uORM/driver/DriverRegistry.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace uORM;

namespace {

thread_local std::string t_lastError;

void setLastError(std::string msg) {
    t_lastError = std::move(msg);
}

const char* lastErrorC() {
    return t_lastError.empty() ? "" : t_lastError.c_str();
}

// 池超时错误细分（DataSource 以文本报告超时）
int mapConnectionError(const ConnectionError& e) {
    std::string what = e.what();
    if (what.find("acquire timeout") != std::string::npos) return UORM_ERR_POOL_TIMEOUT;
    return UORM_ERR_CONNECTION;
}

// 统一的异常 -> 状态码转换
template<typename Fn>
int guardStatus(Fn&& fn) {
    try {
        t_lastError.clear();
        fn();
        return UORM_OK;
    } catch (const ConnectionError& e) {
        setLastError(e.what());
        return mapConnectionError(e);
    } catch (const SqlError& e) {
        setLastError(e.what());
        return UORM_ERR_SQL;
    } catch (const ConfigurationError& e) {
        setLastError(e.what());
        return UORM_ERR_CONFIG;
    } catch (const OrmError& e) {
        setLastError(e.what());
        return UORM_ERR_UNKNOWN_DRIVER;
    } catch (const Exception& e) {
        setLastError(e.what());
        return UORM_ERR_SQL;
    } catch (const std::bad_alloc&) {
        setLastError("out of memory");
        return UORM_ERR_OUT_OF_MEMORY;
    } catch (const std::exception& e) {
        setLastError(e.what());
        return UORM_ERR_SQL;
    } catch (...) {
        setLastError("unknown error");
        return UORM_ERR_SQL;
    }
}

SqlValue toSqlValue(const uorm_param& p) {
    switch (p.type) {
        case UORM_TYPE_INT64:  return SqlValue(p.i64);
        case UORM_TYPE_DOUBLE: return SqlValue(p.f64);
        case UORM_TYPE_STRING: return p.s ? SqlValue(std::string(p.s)) : SqlValue(nullptr);
        case UORM_TYPE_NULL:
        default:               return SqlValue(nullptr);
    }
}

std::vector<SqlValue> toSqlValues(const uorm_param* params, int count) {
    std::vector<SqlValue> out;
    if (params && count > 0) {
        out.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) out.push_back(toSqlValue(params[i]));
    }
    return out;
}

bool inRange(const QueryResult& r, int row, int column) {
    return row >= 0 && row < static_cast<int>(r.rows.size()) &&
           column >= 0 && column < static_cast<int>(r.columns.size());
}

} // namespace

// ---------------------------------------------------------------------------
// 句柄布局（对 C 完全不透明）
// ---------------------------------------------------------------------------
struct uorm_data_source {
    std::unique_ptr<DataSource> ds;
};

struct uorm_connection {
    PooledConnection conn;   // 析构自动归还池
};

struct uorm_statement {
    std::unique_ptr<IPreparedStatement> stmt;
};

struct uorm_result {
    QueryResult result;
};

extern "C" {

// ---------------------------------------------------------------------------
// 全局
// ---------------------------------------------------------------------------

UORM_API const char* UORM_CALL uorm_version(void) {
    return UORM_VERSION_STRING;
}

UORM_API int UORM_CALL uorm_driver_count(void) {
    return static_cast<int>(DriverRegistry::instance().driverNames().size());
}

UORM_API int UORM_CALL uorm_driver_name(int index, char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) return UORM_ERR_INVALID_ARG;
    auto names = DriverRegistry::instance().driverNames();
    if (index < 0 || index >= static_cast<int>(names.size())) return UORM_ERR_INDEX_RANGE;
    const std::string& name = names[static_cast<std::size_t>(index)];
    if (name.size() + 1 > buf_len) {
        std::strncpy(buf, name.c_str(), buf_len - 1);
        buf[buf_len - 1] = '\0';
        return UORM_ERR_BUFFER_TOO_SMALL;
    }
    std::strcpy(buf, name.c_str());
    return UORM_OK;
}

UORM_API const char* UORM_CALL uorm_last_error(void) {
    return lastErrorC();
}

// ---------------------------------------------------------------------------
// 数据源
// ---------------------------------------------------------------------------

UORM_API uorm_data_source* UORM_CALL uorm_ds_create(const uorm_ds_options* options) {
    t_lastError.clear();
    if (!options || !options->driver || !options->database) {
        setLastError("uorm_ds_create: driver and database are required");
        return nullptr;
    }
    if (!DriverRegistry::instance().hasDriver(options->driver)) {
        setLastError("uorm_ds_create: unknown or disabled driver '" +
                     std::string(options->driver) + "' (available: " +
                     DriverRegistry::instance().driverList() + ")");
        return nullptr;
    }

    DataSourceConfig cfg;
    cfg.params.driver = options->driver;
    cfg.params.host = options->host ? options->host : "127.0.0.1";
    cfg.params.port = options->port;
    cfg.params.username = options->username ? options->username : "";
    cfg.params.password = options->password ? options->password : "";
    cfg.params.database = options->database;
    cfg.params.useTLS = options->use_tls != 0;
    cfg.poolSize = options->pool_size > 0 ? options->pool_size : 5;
    cfg.acquireTimeoutMs = options->acquire_timeout_ms > 0 ? options->acquire_timeout_ms : 3000;

    try {
        auto* handle = new uorm_data_source();
        handle->ds = std::make_unique<DataSource>(std::move(cfg));
        return handle;
    } catch (const std::bad_alloc&) {
        setLastError("out of memory");
        return nullptr;
    } catch (const std::exception& e) {
        setLastError(e.what());
        return nullptr;
    }
}

UORM_API void UORM_CALL uorm_ds_destroy(uorm_data_source* ds) {
    delete ds;
}

UORM_API int UORM_CALL uorm_ds_stats(uorm_data_source* ds,
                                     int* idle, int* in_use, int* total_created) {
    if (!ds) return UORM_ERR_INVALID_ARG;
    auto st = ds->ds->stats();
    if (idle) *idle = st.idle;
    if (in_use) *in_use = st.inUse;
    if (total_created) *total_created = st.totalCreated;
    return UORM_OK;
}

UORM_API int UORM_CALL uorm_ds_query(uorm_data_source* ds,
                                     const char* sql,
                                     const uorm_param* params, int param_count,
                                     uorm_result** out_result) {
    if (!ds || !sql || !out_result) return UORM_ERR_INVALID_ARG;
    auto values = toSqlValues(params, param_count);
    return guardStatus([&] {
        auto r = ds->ds->query(sql, values);
        auto* out = new uorm_result();
        out->result = std::move(r);
        *out_result = out;
    });
}

UORM_API int UORM_CALL uorm_ds_execute(uorm_data_source* ds,
                                       const char* sql,
                                       const uorm_param* params, int param_count,
                                       long long* out_affected) {
    if (!ds || !sql) return UORM_ERR_INVALID_ARG;
    auto values = toSqlValues(params, param_count);
    return guardStatus([&] {
        unsigned long long affected = ds->ds->execute(sql, values);
        if (out_affected) *out_affected = static_cast<long long>(affected);
    });
}

// ---------------------------------------------------------------------------
// 连接
// ---------------------------------------------------------------------------

UORM_API int UORM_CALL uorm_ds_acquire(uorm_data_source* ds, uorm_connection** out_conn) {
    if (!ds || !out_conn) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] {
        auto* handle = new uorm_connection();
        handle->conn = ds->ds->getConnection();
        *out_conn = handle;
    });
}

UORM_API void UORM_CALL uorm_conn_release(uorm_connection* conn) {
    delete conn; // PooledConnection 析构自动归还池
}

UORM_API int UORM_CALL uorm_conn_ping(uorm_connection* conn) {
    if (!conn) return 0;
    try {
        return conn->conn->ping() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

UORM_API int UORM_CALL uorm_conn_begin(uorm_connection* conn) {
    if (!conn) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] { conn->conn->begin(); });
}

UORM_API int UORM_CALL uorm_conn_commit(uorm_connection* conn) {
    if (!conn) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] { conn->conn->commit(); });
}

UORM_API int UORM_CALL uorm_conn_rollback(uorm_connection* conn) {
    if (!conn) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] { conn->conn->rollback(); });
}

UORM_API long long UORM_CALL uorm_conn_last_insert_id(uorm_connection* conn) {
    if (!conn) return -1;
    try {
        return conn->conn->lastInsertId();
    } catch (...) {
        return -1;
    }
}

UORM_API int UORM_CALL uorm_conn_query(uorm_connection* conn,
                                       const char* sql,
                                       const uorm_param* params, int param_count,
                                       uorm_result** out_result) {
    if (!conn || !sql || !out_result) return UORM_ERR_INVALID_ARG;
    auto values = toSqlValues(params, param_count);
    return guardStatus([&] {
        auto r = executeQuery(*conn->conn, sql, values);
        auto* out = new uorm_result();
        out->result = std::move(r);
        *out_result = out;
    });
}

UORM_API int UORM_CALL uorm_conn_execute(uorm_connection* conn,
                                         const char* sql,
                                         const uorm_param* params, int param_count,
                                         long long* out_affected) {
    if (!conn || !sql) return UORM_ERR_INVALID_ARG;
    auto values = toSqlValues(params, param_count);
    return guardStatus([&] {
        unsigned long long affected = executeUpdate(*conn->conn, sql, values);
        if (out_affected) *out_affected = static_cast<long long>(affected);
    });
}

// ---------------------------------------------------------------------------
// 预编译语句
// ---------------------------------------------------------------------------

UORM_API int UORM_CALL uorm_conn_prepare(uorm_connection* conn,
                                         const char* sql,
                                         uorm_statement** out_stmt) {
    if (!conn || !sql || !out_stmt) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] {
        auto* handle = new uorm_statement();
        handle->stmt = conn->conn->prepareStatement(sql);
        *out_stmt = handle;
    });
}

UORM_API int UORM_CALL uorm_stmt_bind_int64(uorm_statement* stmt, int index, long long value) {
    if (!stmt) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] { stmt->stmt->setInt64(index, value); });
}

UORM_API int UORM_CALL uorm_stmt_bind_double(uorm_statement* stmt, int index, double value) {
    if (!stmt) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] { stmt->stmt->setDouble(index, value); });
}

UORM_API int UORM_CALL uorm_stmt_bind_string(uorm_statement* stmt, int index, const char* value) {
    if (!stmt) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] {
        stmt->stmt->setString(index, value ? std::string(value) : std::string());
    });
}

UORM_API int UORM_CALL uorm_stmt_bind_null(uorm_statement* stmt, int index) {
    if (!stmt) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] { stmt->stmt->setNull(index); });
}

UORM_API int UORM_CALL uorm_stmt_bind_params(uorm_statement* stmt,
                                             const uorm_param* params, int param_count) {
    if (!stmt) return UORM_ERR_INVALID_ARG;
    if ((!params && param_count > 0)) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] {
        for (int i = 0; i < param_count; ++i) {
            const uorm_param& p = params[i];
            switch (p.type) {
                case UORM_TYPE_INT64:  stmt->stmt->setInt64(i + 1, p.i64); break;
                case UORM_TYPE_DOUBLE: stmt->stmt->setDouble(i + 1, p.f64); break;
                case UORM_TYPE_STRING:
                    stmt->stmt->setString(i + 1, p.s ? std::string(p.s) : std::string());
                    break;
                case UORM_TYPE_NULL:
                default:
                    stmt->stmt->setNull(i + 1);
                    break;
            }
        }
    });
}

UORM_API int UORM_CALL uorm_stmt_query(uorm_statement* stmt, uorm_result** out_result) {
    if (!stmt || !out_result) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] {
        auto r = stmt->stmt->executeQuery();
        auto* out = new uorm_result();
        // IResultSet -> QueryResult：统一进 C ABI 的结果模型
        const std::size_t n = r->columnCount();
        for (std::size_t c = 0; c < n; ++c) out->result.columns.push_back(r->columnName(c));
        while (r->next()) {
            std::vector<SqlValue> row;
            row.reserve(n);
            for (std::size_t c = 0; c < n; ++c) row.push_back(r->getSqlValue(c));
            out->result.rows.push_back(std::move(row));
        }
        *out_result = out;
    });
}

UORM_API int UORM_CALL uorm_stmt_execute(uorm_statement* stmt, long long* out_affected) {
    if (!stmt) return UORM_ERR_INVALID_ARG;
    return guardStatus([&] {
        unsigned long long affected = stmt->stmt->executeUpdate();
        if (out_affected) *out_affected = static_cast<long long>(affected);
    });
}

UORM_API void UORM_CALL uorm_stmt_destroy(uorm_statement* stmt) {
    delete stmt;
}

// ---------------------------------------------------------------------------
// 结果集
// ---------------------------------------------------------------------------

UORM_API void UORM_CALL uorm_result_destroy(uorm_result* result) {
    delete result;
}

UORM_API int UORM_CALL uorm_result_row_count(const uorm_result* result) {
    return result ? static_cast<int>(result->result.rows.size()) : 0;
}

UORM_API int UORM_CALL uorm_result_column_count(const uorm_result* result) {
    return result ? static_cast<int>(result->result.columns.size()) : 0;
}

UORM_API const char* UORM_CALL uorm_result_column_name(const uorm_result* result, int column) {
    if (!result || column < 0 || column >= static_cast<int>(result->result.columns.size())) {
        return nullptr;
    }
    return result->result.columns[static_cast<std::size_t>(column)].c_str();
}

UORM_API int UORM_CALL uorm_value_type(const uorm_result* result, int row, int column) {
    if (!result || !inRange(result->result, row, column)) return UORM_TYPE_NULL;
    const SqlValue& v = result->result.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    if (auto* i = std::get_if<long long>(&v)) { (void)i; return UORM_TYPE_INT64; }
    if (auto* d = std::get_if<double>(&v)) { (void)d; return UORM_TYPE_DOUBLE; }
    if (auto* s = std::get_if<std::string>(&v)) { (void)s; return UORM_TYPE_STRING; }
    return UORM_TYPE_NULL;
}

UORM_API int UORM_CALL uorm_value_is_null(const uorm_result* result, int row, int column) {
    if (!result || !inRange(result->result, row, column)) return 1;
    return isNullValue(result->result.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]) ? 1 : 0;
}

UORM_API long long UORM_CALL uorm_value_int64(const uorm_result* result, int row, int column) {
    if (!result || !inRange(result->result, row, column)) return 0;
    return valueToInt64(result->result.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]);
}

UORM_API double UORM_CALL uorm_value_double(const uorm_result* result, int row, int column) {
    if (!result || !inRange(result->result, row, column)) return 0.0;
    return valueToDouble(result->result.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]);
}

UORM_API const char* UORM_CALL uorm_value_string(const uorm_result* result, int row, int column) {
    if (!result || !inRange(result->result, row, column)) return "";
    // 字符串值直接引用结果集内部存储（零拷贝）；非字符串返回线程局部转换缓存
    thread_local std::string t_conversion;
    const SqlValue& v = result->result.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    if (auto* s = std::get_if<std::string>(&v)) return s->c_str();
    t_conversion = valueToString(v);
    return t_conversion.c_str();
}

} // extern "C"
