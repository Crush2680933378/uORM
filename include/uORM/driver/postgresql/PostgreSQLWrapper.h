#pragma once
// 文件说明：
// PostgreSQLWrapper 基于官方 libpq C API 实现 IConnection 抽象。
// - 修复历史 bug：统一 ? 占位符，在底层自动转换为 PG 的 $1/$2/... 风格
// - 参数以文本格式传输，NULL 语义完整（SqlValue）
// - 事务 / ping / lastInsertId(SELECT lastval())

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "uORM/driver/DBInterfaces.h"
#include <libpq-fe.h>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace uORM {

namespace detail {

// PostgreSQL OID -> 分类
inline bool isPgIntOid(Oid t) { return t == 20 /*INT8*/ || t == 21 /*INT2*/ || t == 23 /*INT4*/; }
inline bool isPgFloatOid(Oid t) {
    return t == 700 /*FLOAT4*/ || t == 701 /*FLOAT8*/ || t == 1700 /*NUMERIC*/;
}
inline bool isPgBoolOid(Oid t) { return t == 16 /*BOOL*/; }

inline SqlValue parsePgText(Oid type, const char* v) {
    if (!v) return nullptr;
    if (isPgBoolOid(type)) return (*v == 't' || *v == '1') ? 1LL : 0LL;
    if (isPgIntOid(type)) return static_cast<long long>(std::strtoll(v, nullptr, 10));
    if (isPgFloatOid(type)) return std::strtod(v, nullptr);
    return std::string(v);
}

} // namespace detail

// ---------------------------------------------------------------------------
// 结果集（libpq 结果一次性返回全部行）
// ---------------------------------------------------------------------------
class PostgreSQLResultSet : public IResultSet {
public:
    explicit PostgreSQLResultSet(PGresult* res) : res_(res) {
        nFields_ = static_cast<std::size_t>(PQnfields(res));
        nTuples_ = static_cast<std::size_t>(PQntuples(res));
        for (std::size_t i = 0; i < nFields_; ++i) {
            names_.emplace_back(PQfname(res, static_cast<int>(i)));
            types_.push_back(PQftype(res, static_cast<int>(i)));
        }
    }

    bool next() override { return ++row_ < static_cast<long long>(nTuples_); }

    std::size_t columnCount() const override { return nFields_; }
    std::string columnName(std::size_t index) const override { return names_.at(index); }

    SqlValue getSqlValue(std::size_t index) override {
        if (index >= nFields_) throw SqlError("PostgreSQL column index out of range");
        if (PQgetisnull(res_.get(), static_cast<int>(row_), static_cast<int>(index))) return nullptr;
        return detail::parsePgText(types_[index], PQgetvalue(res_.get(), static_cast<int>(row_), static_cast<int>(index)));
    }

protected:
    long long columnIndex(const std::string& colName) const override {
        for (std::size_t i = 0; i < nFields_; ++i)
            if (names_[i] == colName) return static_cast<long long>(i);
        return -1;
    }

private:
    struct ResDeleter { void operator()(PGresult* r) const { if (r) PQclear(r); } };
    std::unique_ptr<PGresult, ResDeleter> res_;
    std::size_t nFields_ = 0;
    std::size_t nTuples_ = 0;
    long long row_ = -1;
    std::vector<std::string> names_;
    std::vector<Oid> types_;
};

// ---------------------------------------------------------------------------
// 普通语句
// ---------------------------------------------------------------------------
class PostgreSQLStatement : public IStatement {
public:
    explicit PostgreSQLStatement(PGconn* conn) : conn_(conn) {}

    void execute(const std::string& sql) override {
        PGresult* r = PQexec(conn_, sql.c_str());
        checkResult(r, sql);
        PQclear(r);
    }

    unsigned long long executeUpdate(const std::string& sql) override {
        PGresult* r = PQexec(conn_, sql.c_str());
        checkResult(r, sql);
        std::string affected = PQcmdTuples(r);
        PQclear(r);
        return affected.empty() ? 0 : static_cast<unsigned long long>(std::strtoull(affected.c_str(), nullptr, 10));
    }

    std::unique_ptr<IResultSet> executeQuery(const std::string& sql) override {
        PGresult* r = PQexec(conn_, sql.c_str());
        checkResult(r, sql);
        if (PQresultStatus(r) != PGRES_TUPLES_OK)
            throw SqlError("PostgreSQL executeQuery expected rows: " + sql);
        return std::make_unique<PostgreSQLResultSet>(r);
    }

private:
    void checkResult(PGresult* r, const std::string& sql) {
        if (!r) throw SqlError(std::string("PostgreSQL OOM/error: ") + PQerrorMessage(conn_) + " [SQL: " + sql + "]");
        ExecStatusType st = PQresultStatus(r);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
            std::string err = PQresultErrorField(r, PG_DIAG_MESSAGE_PRIMARY)
                                  ? PQresultErrorField(r, PG_DIAG_MESSAGE_PRIMARY)
                                  : PQerrorMessage(conn_);
            PQclear(r);
            throw SqlError("PostgreSQL error: " + err + " [SQL: " + sql + "]");
        }
    }

    PGconn* conn_;
};

// ---------------------------------------------------------------------------
// 预编译语句（? -> $n，参数以文本格式传递）
// ---------------------------------------------------------------------------
class PostgreSQLPreparedStatement : public IPreparedStatement {
public:
    PostgreSQLPreparedStatement(PGconn* conn, const std::string& sql, std::shared_ptr<ISqlDialect> dialect)
        : conn_(conn), dialect_(std::move(dialect)) {
        convertedSql_ = dialect_ ? dialect_->convertPlaceholders(sql) : sql;
    }

    unsigned long long executeUpdate() override {
        PGresult* r = execParams();
        ExecStatusType st = PQresultStatus(r);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
            std::string err = lastError_;
            PQclear(r);
            throw SqlError("PostgreSQL executeUpdate error: " + err);
        }
        std::string affected = PQcmdTuples(r);
        PQclear(r);
        return affected.empty() ? 0 : static_cast<unsigned long long>(std::strtoull(affected.c_str(), nullptr, 10));
    }

    std::unique_ptr<IResultSet> executeQuery() override {
        PGresult* r = execParams();
        if (PQresultStatus(r) != PGRES_TUPLES_OK) {
            std::string err = lastError_;
            PQclear(r);
            throw SqlError("PostgreSQL executeQuery error: " + err);
        }
        return std::make_unique<PostgreSQLResultSet>(r);
    }

    void setNull(int index) override {
        ensureSize(index).isNull = true;
    }
    void setInt64(int index, long long val) override {
        auto& p = ensureSize(index);
        p.isNull = false; p.text = std::to_string(val);
    }
    void setDouble(int index, double val) override {
        auto& p = ensureSize(index);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", val);
        p.isNull = false; p.text = buf;
    }
    void setString(int index, const std::string& val) override {
        auto& p = ensureSize(index);
        p.isNull = false; p.text = val;
    }

private:
    struct Param {
        bool isNull = true;
        std::string text;
    };

    Param& ensureSize(int index) {
        if (index < 1 || static_cast<std::size_t>(index) > 65535)
            throw SqlError("PostgreSQL bind index out of range");
        if (static_cast<std::size_t>(index) > params_.size()) params_.resize(static_cast<std::size_t>(index));
        return params_[static_cast<std::size_t>(index) - 1];
    }

    PGresult* execParams() {
        std::size_t n = params_.size();
        std::vector<const char*> values(n, nullptr);
        for (std::size_t i = 0; i < n; ++i) {
            if (!params_[i].isNull) values[i] = params_[i].text.c_str();
        }
        PGresult* r = PQexecParams(conn_, convertedSql_.c_str(), static_cast<int>(n),
                                   nullptr,          // 让服务端自行推断参数类型
                                   values.data(), nullptr, nullptr, 0 /*文本格式*/);
        if (!r) throw SqlError(std::string("PostgreSQL error: ") + PQerrorMessage(conn_));
        if (PQresultStatus(r) != PGRES_COMMAND_OK && PQresultStatus(r) != PGRES_TUPLES_OK) {
            lastError_ = PQresultErrorField(r, PG_DIAG_MESSAGE_PRIMARY)
                             ? PQresultErrorField(r, PG_DIAG_MESSAGE_PRIMARY)
                             : PQerrorMessage(conn_);
        }
        return r;
    }

    PGconn* conn_;
    std::shared_ptr<ISqlDialect> dialect_;
    std::string convertedSql_;
    std::vector<Param> params_;
    std::string lastError_;
};

// ---------------------------------------------------------------------------
// 连接
// ---------------------------------------------------------------------------
class PostgreSQLConnection : public IConnection {
public:
    explicit PostgreSQLConnection(const ConnectionParams& p) {
        std::string connInfo =
            "host=" + p.host +
            " port=" + std::to_string(p.port > 0 ? p.port : 5432) +
            " dbname=" + p.database +
            " user=" + p.username +
            " password=" + p.password +
            " connect_timeout=10";
        conn_ = PQconnectdb(connInfo.c_str());
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            std::string err = conn_ ? PQerrorMessage(conn_) : "PQconnectdb OOM";
            close();
            throw ConnectionError("PostgreSQL connect failed: " + err);
        }
        dialect_ = std::make_shared<PostgreSQLDialect>();
    }

    ~PostgreSQLConnection() override { close(); }

    bool isValid() override { return conn_ && PQstatus(conn_) == CONNECTION_OK; }
    bool ping() override {
        if (!conn_) return false;
        PGresult* r = PQexec(conn_, "SELECT 1");
        bool ok = r && PQresultStatus(r) == PGRES_TUPLES_OK;
        if (r) PQclear(r);
        return ok;
    }

    void setSchema(const std::string& schema) override {
        PostgreSQLStatement stmt(conn_);
        stmt.execute("SET search_path TO " + dialect_->quoteIdentifier(schema));
    }

    std::shared_ptr<ISqlDialect> dialect() override { return dialect_; }

    std::unique_ptr<IStatement> createStatement() override {
        return std::make_unique<PostgreSQLStatement>(conn_);
    }

    std::unique_ptr<IPreparedStatement> prepareStatement(const std::string& sql) override {
        return std::make_unique<PostgreSQLPreparedStatement>(conn_, sql, dialect_);
    }

    void begin() override { PostgreSQLStatement(conn_).execute("BEGIN"); }
    void commit() override { PostgreSQLStatement(conn_).execute("COMMIT"); }
    void rollback() override { PostgreSQLStatement(conn_).execute("ROLLBACK"); }

    long long lastInsertId() override {
        try {
            auto res = PostgreSQLStatement(conn_).executeQuery("SELECT lastval()");
            if (res->next()) return valueToInt64(res->getSqlValue(0));
        } catch (const Exception&) {
            return -1; // 本会话没有使用过序列
        }
        return -1;
    }

private:
    void close() {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    PGconn* conn_ = nullptr;
    std::shared_ptr<ISqlDialect> dialect_;
};

} // namespace uORM
