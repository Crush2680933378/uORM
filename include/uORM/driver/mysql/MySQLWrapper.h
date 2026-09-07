#pragma once
// 文件说明：
// MySQLWrapper 基于 libmariadb / libmysqlclient C API 实现 IConnection 抽象。
// - 统一使用 ? 占位符（MySQL prepared statement 原生就是 ?）
// - 完整 NULL 语义（SqlValue）
// - 事务 / ping / lastInsertId

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "uORM/driver/DBInterfaces.h"
#include <mysql.h>
#include <memory>
#include <vector>
#include <mutex>
#include <cstring>
#include <cstdlib>

namespace uORM {

namespace detail {

// 判断 MySQL 字段类型是否为整型/浮点
inline bool isIntFieldType(enum_field_types t) {
    switch (t) {
        case MYSQL_TYPE_TINY: case MYSQL_TYPE_SHORT: case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG: case MYSQL_TYPE_INT24: case MYSQL_TYPE_YEAR:
            return true;
        default:
            return false;
    }
}

inline bool isFloatFieldType(enum_field_types t) {
    switch (t) {
        case MYSQL_TYPE_FLOAT: case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_DECIMAL: case MYSQL_TYPE_NEWDECIMAL:
            return true;
        default:
            return false;
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// 结果集：基于 mysql_stmt 预取结果的实现
// ---------------------------------------------------------------------------
class MySQLStmtResultSet : public IResultSet {
public:
    explicit MySQLStmtResultSet(MYSQL_STMT* stmt) : stmt_(stmt) {
        MYSQL_RES* meta = mysql_stmt_result_metadata(stmt_);
        if (!meta) throw SqlError(std::string("MySQL metadata error: ") + mysql_stmt_error(stmt_));
        unsigned int n = mysql_num_fields(meta);
        MYSQL_FIELD* fields = mysql_fetch_fields(meta);
        names_.reserve(n);
        types_.reserve(n);
        for (unsigned int i = 0; i < n; ++i) {
            names_.emplace_back(fields[i].name);
            types_.push_back(fields[i].type);
            if (detail::isIntFieldType(fields[i].type)) bufferSizes_.push_back(sizeof(long long));
            else if (detail::isFloatFieldType(fields[i].type)) bufferSizes_.push_back(sizeof(double));
            else bufferSizes_.push_back(static_cast<std::size_t>(fields[i].max_length) + 1);
        }
        mysql_free_result(meta);

        buffers_.resize(n);
        binds_.resize(n);
        nulls_.resize(n);
        lengths_.resize(n);
        std::memset(binds_.data(), 0, sizeof(MYSQL_BIND) * n);
        for (unsigned int i = 0; i < n; ++i) {
            buffers_[i].resize(bufferSizes_[i], 0);
            binds_[i].buffer_type = detail::isIntFieldType(types_[i]) ? MYSQL_TYPE_LONGLONG
                                    : detail::isFloatFieldType(types_[i]) ? MYSQL_TYPE_DOUBLE
                                    : MYSQL_TYPE_STRING;
            binds_[i].buffer = buffers_[i].data();
            binds_[i].buffer_length = static_cast<unsigned long>(buffers_[i].size());
            binds_[i].is_null = &nulls_[i];
            binds_[i].length = &lengths_[i];
        }
        if (mysql_stmt_bind_result(stmt_, binds_.data())) {
            throw SqlError(std::string("MySQL bind result error: ") + mysql_stmt_error(stmt_));
        }
    }

    bool next() override {
        int r = mysql_stmt_fetch(stmt_);
        if (r == 0 || r == MYSQL_DATA_TRUNCATED) return true;
        if (r == MYSQL_NO_DATA) return false;
        throw SqlError(std::string("MySQL fetch error: ") + mysql_stmt_error(stmt_));
    }

    std::size_t columnCount() const override { return names_.size(); }
    std::string columnName(std::size_t index) const override { return names_.at(index); }

    SqlValue getSqlValue(std::size_t index) override {
        if (index >= names_.size()) throw SqlError("MySQL column index out of range");
        if (nulls_[index]) return nullptr;
        switch (binds_[index].buffer_type) {
            case MYSQL_TYPE_LONGLONG:
                return *reinterpret_cast<long long*>(buffers_[index].data());
            case MYSQL_TYPE_DOUBLE:
                return *reinterpret_cast<double*>(buffers_[index].data());
            default:
                return std::string(reinterpret_cast<const char*>(buffers_[index].data()), lengths_[index]);
        }
    }

protected:
    long long columnIndex(const std::string& colName) const override {
        for (std::size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == colName) return static_cast<long long>(i);
        return -1;
    }

private:
    MYSQL_STMT* stmt_;
    std::vector<std::string> names_;
    std::vector<enum_field_types> types_;
    std::vector<std::size_t> bufferSizes_;
    std::vector<std::string> buffers_;
    std::vector<MYSQL_BIND> binds_;
    std::vector<my_bool> nulls_;
    std::vector<unsigned long> lengths_;
};

// ---------------------------------------------------------------------------
// 结果集：基于 mysql_store_result 的普通查询实现
// ---------------------------------------------------------------------------
class MySQLFetchResultSet : public IResultSet {
public:
    explicit MySQLFetchResultSet(MYSQL_RES* res) : res_(res) {
        unsigned int n = mysql_num_fields(res_.get());
        MYSQL_FIELD* fields = mysql_fetch_fields(res_.get());
        names_.reserve(n);
        types_.reserve(n);
        for (unsigned int i = 0; i < n; ++i) {
            names_.emplace_back(fields[i].name);
            types_.push_back(fields[i].type);
        }
    }

    bool next() override { return (row_ = mysql_fetch_row(res_.get())) != nullptr; }
    std::size_t columnCount() const override { return names_.size(); }
    std::string columnName(std::size_t index) const override { return names_.at(index); }

    SqlValue getSqlValue(std::size_t index) override {
        if (index >= names_.size()) throw SqlError("MySQL column index out of range");
        const char* v = row_[index];
        if (!v) return nullptr;
        if (detail::isIntFieldType(types_[index])) return static_cast<long long>(std::strtoll(v, nullptr, 10));
        if (detail::isFloatFieldType(types_[index])) return std::strtod(v, nullptr);
        return std::string(v);
    }

protected:
    long long columnIndex(const std::string& colName) const override {
        for (std::size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == colName) return static_cast<long long>(i);
        return -1;
    }

private:
    struct ResDeleter { void operator()(MYSQL_RES* r) const { if (r) mysql_free_result(r); } };
    std::unique_ptr<MYSQL_RES, ResDeleter> res_;
    MYSQL_ROW row_ = nullptr;
    std::vector<std::string> names_;
    std::vector<enum_field_types> types_;
};

// ---------------------------------------------------------------------------
// 普通语句
// ---------------------------------------------------------------------------
class MySQLStatement : public IStatement {
public:
    explicit MySQLStatement(MYSQL* conn) : conn_(conn) {}

    void execute(const std::string& sql) override {
        execSql(sql);
        drainResult();
    }

    unsigned long long executeUpdate(const std::string& sql) override {
        execSql(sql);
        unsigned long long affected = mysql_affected_rows(conn_);
        drainResult();
        return affected;
    }

    std::unique_ptr<IResultSet> executeQuery(const std::string& sql) override {
        execSql(sql);
        MYSQL_RES* res = mysql_store_result(conn_);
        if (!res) {
            if (mysql_field_count(conn_) != 0)
                throw SqlError(std::string("MySQL store result error: ") + mysql_error(conn_));
            throw SqlError("executeQuery: statement produced no result set: " + sql);
        }
        return std::make_unique<MySQLFetchResultSet>(res);
    }

private:
    void execSql(const std::string& sql) {
        if (mysql_real_query(conn_, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
            throw SqlError(std::string("MySQL error: ") + mysql_error(conn_) + " [SQL: " + sql + "]");
    }
    void drainResult() {
        // 丢弃残余结果集，保证连接状态干净
        MYSQL_RES* r;
        while ((r = mysql_store_result(conn_)) != nullptr) mysql_free_result(r);
    }

    MYSQL* conn_;
};

// ---------------------------------------------------------------------------
// 预编译语句
// ---------------------------------------------------------------------------
class MySQLPreparedStatement : public IPreparedStatement {
public:
    MySQLPreparedStatement(MYSQL* conn, const std::string& sql) : conn_(conn), sql_(sql) {
        stmt_ = mysql_stmt_init(conn);
        if (!stmt_) throw SqlError("MySQL stmt_init failed");
        if (mysql_stmt_prepare(stmt_, sql.c_str(), static_cast<unsigned long>(sql.size()))) {
            std::string err = stmtErr();
            mysql_stmt_close(stmt_);
            stmt_ = nullptr;
            throw SqlError(std::string("MySQL prepare error: ") + err + " [SQL: " + sql + "]");
        }
    }

    ~MySQLPreparedStatement() override {
        if (stmt_) mysql_stmt_close(stmt_);
    }

    unsigned long long executeUpdate() override {
        execStmt();
        unsigned long long affected = mysql_stmt_affected_rows(stmt_);
        if (mysql_stmt_more_results(stmt_)) mysql_stmt_next_result(stmt_);
        return affected;
    }

    std::unique_ptr<IResultSet> executeQuery() override {
        execStmt();
        if (mysql_stmt_field_count(stmt_) == 0)
            throw SqlError("executeQuery: statement produced no result set: " + sql_);
        if (mysql_stmt_store_result(stmt_))
            throw SqlError(stmtErr());
        return std::make_unique<MySQLStmtResultSet>(stmt_);
    }

    void setNull(int index) override { ensureSize(index).isNull = true; }
    void setInt64(int index, long long val) override {
        auto& p = ensureSize(index);
        p.isNull = false; p.kind = Param::Kind::Int; p.i = val;
    }
    void setDouble(int index, double val) override {
        auto& p = ensureSize(index);
        p.isNull = false; p.kind = Param::Kind::Double; p.d = val;
    }
    void setString(int index, const std::string& val) override {
        auto& p = ensureSize(index);
        p.isNull = false; p.kind = Param::Kind::Str; p.s = val;
    }

private:
    struct Param {
        enum class Kind { Int, Double, Str } kind = Kind::Int;
        bool isNull = true;
        long long i = 0;
        double d = 0.0;
        std::string s;
    };

    Param& ensureSize(int index) {
        if (index < 1 || static_cast<std::size_t>(index) > 65535)
            throw SqlError("MySQL bind index out of range");
        if (static_cast<std::size_t>(index) > params_.size()) params_.resize(static_cast<std::size_t>(index));
        return params_[static_cast<std::size_t>(index) - 1];
    }

    void execStmt() {
        // MYSQL_BIND 缓冲区必须指向 params_ 中的稳定存储，直到语句执行完成
        std::size_t n = params_.size();
        std::vector<MYSQL_BIND> binds(n);
        std::vector<my_bool> nulls(n);
        std::vector<unsigned long> lengths(n);
        std::memset(binds.data(), 0, sizeof(MYSQL_BIND) * n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& p = params_[i];
            if (p.isNull) {
                binds[i].buffer_type = MYSQL_TYPE_NULL;
                nulls[i] = 1;
                binds[i].is_null = &nulls[i];
                continue;
            }
            switch (p.kind) {
                case Param::Kind::Int:
                    binds[i].buffer_type = MYSQL_TYPE_LONGLONG;
                    binds[i].buffer = const_cast<long long*>(&p.i);
                    break;
                case Param::Kind::Double:
                    binds[i].buffer_type = MYSQL_TYPE_DOUBLE;
                    binds[i].buffer = const_cast<double*>(&p.d);
                    break;
                case Param::Kind::Str:
                    binds[i].buffer_type = MYSQL_TYPE_STRING;
                    binds[i].buffer = const_cast<char*>(p.s.data());
                    binds[i].buffer_length = static_cast<unsigned long>(p.s.size());
                    lengths[i] = static_cast<unsigned long>(p.s.size());
                    binds[i].length = &lengths[i];
                    break;
            }
        }
        if (n > 0 && mysql_stmt_bind_param(stmt_, binds.data()))
            throw SqlError(stmtErr());

        my_bool updateMax = 1;
        mysql_stmt_attr_set(stmt_, STMT_ATTR_UPDATE_MAX_LENGTH, &updateMax);

        if (mysql_stmt_execute(stmt_)) throw SqlError(stmtErr());
    }

    const char* stmtErr() const { return mysql_stmt_error(stmt_); }

    MYSQL* conn_;
    MYSQL_STMT* stmt_ = nullptr;
    std::string sql_;
    std::vector<Param> params_;
};

// ---------------------------------------------------------------------------
// 连接
// ---------------------------------------------------------------------------
class MySQLConnection : public IConnection {
public:
    explicit MySQLConnection(const ConnectionParams& p) {
        std::call_once(libInitFlag_, [] { mysql_library_init(0, nullptr, nullptr); });

        mysql_ = mysql_init(nullptr);
        if (!mysql_) throw ConnectionError("MySQL: mysql_init failed");

        mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
        bool reconnect = 1;
        mysql_optionsv(mysql_, MYSQL_OPT_RECONNECT, &reconnect);
        unsigned int timeout = 10;
        mysql_optionsv(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        // Connector/C 3.4+ 默认尝试 TLS；useTLS=false 时关闭（可规避链路对 TLS 握手的干扰）
        if (!p.useTLS) {
            my_bool off = 0;
            mysql_optionsv(mysql_, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &off);
            mysql_optionsv(mysql_, MYSQL_OPT_SSL_ENFORCE, &off);
        }

        if (!mysql_real_connect(mysql_,
                                p.host.c_str(),
                                p.username.c_str(),
                                p.password.c_str(),
                                p.database.c_str(),
                                p.port > 0 ? p.port : 3306,
                                nullptr, 0)) {
            std::string err = mysql_error(mysql_);
            mysql_close(mysql_);
            mysql_ = nullptr;
            throw ConnectionError("MySQL connect failed: " + err);
        }
        mysql_autocommit(mysql_, p.autoCommit ? 1 : 0);
    }

    ~MySQLConnection() override {
        if (mysql_) mysql_close(mysql_);
    }

    bool isValid() override { return mysql_ && mysql_ping(mysql_) == 0; }
    bool ping() override { return isValid(); }

    void setSchema(const std::string& db) override {
        if (mysql_select_db(mysql_, db.c_str()) != 0)
            throw SqlError(std::string("MySQL select db error: ") + mysql_error(mysql_));
    }

    std::shared_ptr<ISqlDialect> dialect() override { return std::make_shared<MySQLDialect>(); }

    std::unique_ptr<IStatement> createStatement() override {
        return std::make_unique<MySQLStatement>(mysql_);
    }

    std::unique_ptr<IPreparedStatement> prepareStatement(const std::string& sql) override {
        return std::make_unique<MySQLPreparedStatement>(mysql_, sql);
    }

    void begin() override { runSimple("START TRANSACTION"); }
    void commit() override { runSimple("COMMIT"); }
    void rollback() override { runSimple("ROLLBACK"); }

    long long lastInsertId() override { return static_cast<long long>(mysql_insert_id(mysql_)); }

private:
    void runSimple(const char* sql) {
        if (mysql_real_query(mysql_, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
            throw SqlError(std::string("MySQL error: ") + mysql_error(mysql_) + " [" + sql + "]");
    }

    MYSQL* mysql_ = nullptr;
    static std::once_flag libInitFlag_;
};

inline std::once_flag MySQLConnection::libInitFlag_;

} // namespace uORM
