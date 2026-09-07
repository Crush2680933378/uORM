#pragma once
// 文件说明：
// SQLiteWrapper 基于 sqlite3 C API 实现 IConnection 抽象。
// - 零部署：database 即文件路径（:memory: 为内存库）
// - 统一 ? 占位符；完整 NULL 语义（SqlValue，按单元格动态类型）
// - 事务 / lastInsertRowId

#include "uORM/driver/DBInterfaces.h"
#include <sqlite3.h>
#include <memory>
#include <vector>
#include <string>

namespace uORM {

// SQLite 方言
class SQLiteDialect : public ISqlDialect {
public:
    DialectKind kind() const override { return DialectKind::SQLite; }
    std::string quoteIdentifier(const std::string& id) const override { return "\"" + id + "\""; }
    std::string getAutoIncrementModifier() const override { return "AUTOINCREMENT"; }
    bool supportsReturningId() const override { return false; }
    std::string getTableOptions(const std::string&) const override { return ""; }
    std::string getLastInsertIdSql() const override { return "SELECT last_insert_rowid()"; }
};

namespace detail {

// 按单元格动态类型转换
inline SqlValue sqliteCellValue(sqlite3_stmt* stmt, int col) {
    switch (sqlite3_column_type(stmt, col)) {
        case SQLITE_INTEGER: return static_cast<long long>(sqlite3_column_int64(stmt, col));
        case SQLITE_FLOAT:   return sqlite3_column_double(stmt, col);
        case SQLITE_NULL:    return nullptr;
        case SQLITE_BLOB:
        case SQLITE_TEXT:
        default: {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            int len = sqlite3_column_bytes(stmt, col);
            if (!text) return std::string();
            return std::string(text, static_cast<std::size_t>(len));
        }
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// 结果集（预编译语句逐步读取）
// ---------------------------------------------------------------------------
class SQLiteResultSet : public IResultSet {
public:
    SQLiteResultSet(sqlite3* db, sqlite3_stmt* stmt, std::string sql)
        : db_(db), stmt_(stmt), sql_(std::move(sql)), done_(false) {
        int n = sqlite3_column_count(stmt_);
        for (int i = 0; i < n; ++i) {
            const char* name = sqlite3_column_name(stmt_, i);
            names_.push_back(name ? name : "");
        }
    }

    ~SQLiteResultSet() override {
        if (stmt_) sqlite3_finalize(stmt_);
    }

    bool next() override {
        if (done_) return false;
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) {
            done_ = true;
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
            return false;
        }
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
        throw SqlError("SQLite step error: " + err + " [SQL: " + sql_ + "]");
    }

    std::size_t columnCount() const override { return names_.size(); }
    std::string columnName(std::size_t index) const override { return names_.at(index); }

    SqlValue getSqlValue(std::size_t index) override {
        if (index >= names_.size()) throw SqlError("SQLite column index out of range");
        if (!stmt_) throw SqlError("SQLite result set exhausted");
        return detail::sqliteCellValue(stmt_, static_cast<int>(index));
    }

protected:
    long long columnIndex(const std::string& colName) const override {
        for (std::size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == colName) return static_cast<long long>(i);
        return -1;
    }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_;
    std::string sql_;
    std::vector<std::string> names_;
    bool done_;
};

// ---------------------------------------------------------------------------
// 普通语句（sqlite3_exec，支持多语句 DDL）
// ---------------------------------------------------------------------------
class SQLiteStatement : public IStatement {
public:
    explicit SQLiteStatement(sqlite3* db) : db_(db) {}

    void execute(const std::string& sql) override {
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::string err = errMsg ? errMsg : "unknown";
            sqlite3_free(errMsg);
            throw SqlError("SQLite error: " + err + " [SQL: " + sql + "]");
        }
    }

    unsigned long long executeUpdate(const std::string& sql) override {
        execute(sql);
        return static_cast<unsigned long long>(sqlite3_changes64(db_));
    }

    std::unique_ptr<IResultSet> executeQuery(const std::string& sql) override {
        return queryOne(sql);
    }

private:
    // 执行单条查询语句（内部复用）
    std::unique_ptr<IResultSet> queryOne(const std::string& sql) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            std::string err = sqlite3_errmsg(db_);
            if (stmt) sqlite3_finalize(stmt);
            throw SqlError("SQLite prepare error: " + err + " [SQL: " + sql + "]");
        }
        return std::make_unique<SQLiteResultSet>(db_, stmt, sql);
    }

    sqlite3* db_;
};

// ---------------------------------------------------------------------------
// 预编译语句
// ---------------------------------------------------------------------------
class SQLitePreparedStatement : public IPreparedStatement {
public:
    SQLitePreparedStatement(sqlite3* db, const std::string& sql) : db_(db), sql_(sql) {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            std::string err = sqlite3_errmsg(db_);
            stmt_ = nullptr;
            throw SqlError("SQLite prepare error: " + err + " [SQL: " + sql + "]");
        }
    }

    ~SQLitePreparedStatement() override {
        if (stmt_) sqlite3_finalize(stmt_);
    }

    unsigned long long executeUpdate() override {
        stepToCompletion();
        return static_cast<unsigned long long>(sqlite3_changes64(db_));
    }

    std::unique_ptr<IResultSet> executeQuery() override {
        // 返回结果集后由其接管 stmt_ 的 finalize
        int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
            throw SqlError("SQLite step error: " + err + " [SQL: " + sql_ + "]");
        }
        auto* rs = new SQLiteResultSet(db_, stmt_, sql_);
        stmt_ = nullptr; // 所有权已转移
        if (rc == SQLITE_DONE) {
            // 空结果集：包装器在 next() 时返回 false
        }
        return std::unique_ptr<IResultSet>(rs);
    }

    void setNull(int index) override {
        sqlite3_bind_null(stmt_, index);
    }
    void setInt64(int index, long long val) override {
        sqlite3_bind_int64(stmt_, index, val);
    }
    void setDouble(int index, double val) override {
        sqlite3_bind_double(stmt_, index, val);
    }
    void setString(int index, const std::string& val) override {
        strings_.push_back(std::make_unique<std::string>(val));
        sqlite3_bind_text(stmt_, index, strings_.back()->c_str(),
                          static_cast<int>(strings_.back()->size()), SQLITE_STATIC);
    }

private:
    void stepToCompletion() {
        int rc = sqlite3_step(stmt_);
        while (rc == SQLITE_ROW) rc = sqlite3_step(stmt_); // INSERT..RETURNING 等场景兜底
        if (rc != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db_);
            throw SqlError("SQLite step error: " + err + " [SQL: " + sql_ + "]");
        }
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
    std::string sql_;
    std::vector<std::unique_ptr<std::string>> strings_; // 保证绑定缓冲区存活
};

// ---------------------------------------------------------------------------
// 连接
// ---------------------------------------------------------------------------
class SQLiteConnection : public IConnection {
public:
    explicit SQLiteConnection(const ConnectionParams& p) : dialect_(std::make_shared<SQLiteDialect>()) {
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        if (sqlite3_open_v2(p.database.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
            std::string err = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw ConnectionError("SQLite open failed (" + p.database + "): " + err);
        }
        sqlite3_busy_timeout(db_, 5000);
        // 实用默认：外键约束开启
        SQLiteStatement stmt(db_);
        stmt.execute("PRAGMA foreign_keys = ON");
    }

    ~SQLiteConnection() override {
        if (db_) sqlite3_close_v2(db_);
    }

    bool isValid() override { return db_ != nullptr; }
    bool ping() override {
        if (!db_) return false;
        try {
            SQLiteStatement stmt(db_);
            stmt.execute("SELECT 1");
            return true;
        } catch (const Exception&) {
            return false;
        }
    }

    void setSchema(const std::string&) override {
        // SQLite 无 schema 概念（ATTACH 可作为后续扩展）
    }

    std::shared_ptr<ISqlDialect> dialect() override { return dialect_; }

    std::unique_ptr<IStatement> createStatement() override {
        return std::make_unique<SQLiteStatement>(db_);
    }

    std::unique_ptr<IPreparedStatement> prepareStatement(const std::string& sql) override {
        return std::make_unique<SQLitePreparedStatement>(db_, sql);
    }

    void begin() override { SQLiteStatement(db_).execute("BEGIN"); }
    void commit() override { SQLiteStatement(db_).execute("COMMIT"); }
    void rollback() override { SQLiteStatement(db_).execute("ROLLBACK"); }

    long long lastInsertId() override {
        return sqlite3_last_insert_rowid(db_);
    }

private:
    sqlite3* db_ = nullptr;
    std::shared_ptr<ISqlDialect> dialect_;
};

} // namespace uORM
