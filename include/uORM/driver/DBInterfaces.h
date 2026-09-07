#pragma once
// 文件说明：
// DBInterfaces 定义 uORM 的数据库驱动抽象接口（v2）。
// 所有数据库（MySQL/PostgreSQL/SQLite）驱动都实现这套接口，
// 通过 DriverRegistry 在运行时按名字创建连接，一个进程可同时使用多种数据库。

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <optional>
#include <cstdio>
#include <cstdlib>

#include "uORM/orm/SqlValue.h"
#include "uORM/orm/Error.h"
#include "uORM/driver/SqlDialect.h"

namespace uORM {

// 连接参数：与具体驱动无关的统一描述
struct ConnectionParams {
    std::string driver;             // "mysql" / "postgresql" / "sqlite"
    std::string host = "127.0.0.1"; // SQLite 忽略
    int port = 0;                   // 0 = 驱动默认端口
    std::string username;
    std::string password;
    std::string database;           // 数据库名；SQLite 为文件路径
    bool autoCommit = true;
    bool useTLS = false;            // MySQL: 关闭可规避链路对 TLS 握手的干扰；PG: 传 sslmode
};

// 数值/字符串转换辅助（供各驱动包装层与 Mapper 复用）
inline long long valueToInt64(const SqlValue& v) {
    if (auto* i = std::get_if<long long>(&v)) return *i;
    if (auto* d = std::get_if<double>(&v)) return static_cast<long long>(*d);
    if (auto* s = std::get_if<std::string>(&v)) return std::strtoll(s->c_str(), nullptr, 10);
    return 0;
}

inline double valueToDouble(const SqlValue& v) {
    if (auto* d = std::get_if<double>(&v)) return *d;
    if (auto* i = std::get_if<long long>(&v)) return static_cast<double>(*i);
    if (auto* s = std::get_if<std::string>(&v)) return std::strtod(s->c_str(), nullptr);
    return 0.0;
}

inline bool valueToBool(const SqlValue& v) {
    if (auto* i = std::get_if<long long>(&v)) return *i != 0;
    if (auto* d = std::get_if<double>(&v)) return *d != 0.0;
    if (auto* s = std::get_if<std::string>(&v)) {
        return !s->empty() && *s != "0" && *s != "f" && *s != "false" && *s != "F" && *s != "n";
    }
    return false;
}

inline std::string valueToString(const SqlValue& v) {
    if (auto* s = std::get_if<std::string>(&v)) return *s;
    if (auto* i = std::get_if<long long>(&v)) return std::to_string(*i);
    if (auto* d = std::get_if<double>(&v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", *d);
        return buf;
    }
    return "";
}

inline bool isNullValue(const SqlValue& v) {
    return std::holds_alternative<std::nullptr_t>(v);
}

// 结果集接口
class IResultSet {
public:
    virtual ~IResultSet() = default;

    virtual bool next() = 0;

    // 元数据
    virtual std::size_t columnCount() const = 0;
    virtual std::string columnName(std::size_t index) const = 0;

    // 统一取值（推荐）：NULL 语义完整
    virtual SqlValue getSqlValue(std::size_t index) = 0;
    SqlValue getSqlValue(const std::string& colName) {
        auto idx = columnIndex(colName);
        if (idx < 0) throw SqlError("Unknown column: " + colName);
        return getSqlValue(static_cast<std::size_t>(idx));
    }

    // 类型化取值（便捷接口）
    virtual int getInt(const std::string& colName) { return static_cast<int>(valueToInt64(getSqlValue(colName))); }
    virtual long long getInt64(const std::string& colName) { return valueToInt64(getSqlValue(colName)); }
    virtual unsigned int getUInt(const std::string& colName) { return static_cast<unsigned int>(valueToInt64(getSqlValue(colName))); }
    virtual std::string getString(const std::string& colName) { return valueToString(getSqlValue(colName)); }
    virtual bool getBoolean(const std::string& colName) { return valueToBool(getSqlValue(colName)); }
    virtual double getDouble(const std::string& colName) { return valueToDouble(getSqlValue(colName)); }

protected:
    // 返回列下标，不存在返回 -1
    virtual long long columnIndex(const std::string& colName) const = 0;
};

// 普通语句接口（DDL / 无参 SQL）
class IStatement {
public:
    virtual ~IStatement() = default;
    // 执行 DDL 或无结果集语句
    virtual void execute(const std::string& sql) = 0;
    // 执行 DML，返回受影响行数
    virtual unsigned long long executeUpdate(const std::string& sql) = 0;
    virtual std::unique_ptr<IResultSet> executeQuery(const std::string& sql) = 0;
};

// 预编译语句接口（参数绑定，杜绝 SQL 注入）
class IPreparedStatement {
public:
    virtual ~IPreparedStatement() = default;

    // 执行 DML，返回受影响行数
    virtual unsigned long long executeUpdate() = 0;
    virtual std::unique_ptr<IResultSet> executeQuery() = 0;

    // 核心绑定接口（下标从 1 开始）
    virtual void setNull(int index) = 0;
    virtual void setInt64(int index, long long val) = 0;
    virtual void setDouble(int index, double val) = 0;
    virtual void setString(int index, const std::string& val) = 0;

    // 兼容便捷接口
    void setInt(int index, int val) { setInt64(index, val); }
    void setUInt(int index, unsigned int val) { setInt64(index, static_cast<long long>(val)); }
    void setBoolean(int index, bool val) { setInt64(index, val ? 1 : 0); }
};

// 数据库连接接口
class IConnection {
public:
    virtual ~IConnection() = default;

    virtual bool isValid() = 0;
    // 健康检查（连接池回收/借出时使用）
    virtual bool ping() = 0;
    virtual void setSchema(const std::string& db) = 0;

    // 本连接对应的 SQL 方言
    virtual std::shared_ptr<ISqlDialect> dialect() = 0;

    virtual std::unique_ptr<IStatement> createStatement() = 0;
    virtual std::unique_ptr<IPreparedStatement> prepareStatement(const std::string& sql) = 0;

    // 事务（隐式：驱动内部保证 autocommit 语义正确）
    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    // 最近一次自增 ID；不支持时返回 -1（PG 请使用 RETURNING）
    virtual long long lastInsertId() = 0;
};

} // namespace uORM
