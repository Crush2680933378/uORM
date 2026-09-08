// 文件说明：
// uormpp_impl.cpp —— 抽象接口 uormpp::IDatabase/IResult/ITransaction 的实现。
// 继承抽象基类的实现类只存在于本编译单元（Pimpl 隐藏实现），
// 外部世界只看到 UormPP.h 中的纯虚接口、值类型与工厂函数。

#include "uormpp/UormPP.h"

#include "uORM/orm/ORM.h"
#include "uORM/driver/DataSource.h"
#include "uORM/driver/DriverRegistry.h"

#include <set>
#include <utility>

using namespace uORM;

namespace {

// ---- 内部异常 -> uormpp::Error ----
std::string toErrorText(const std::exception& e) {
    return e.what();
}

SqlValue toSqlValue(const uormpp::Param& p) {
    switch (p.type) {
        case uormpp::ValueType::Int:    return SqlValue(p.i);
        case uormpp::ValueType::Double: return SqlValue(p.d);
        case uormpp::ValueType::String: return SqlValue(p.s);
        case uormpp::ValueType::Null:
        default:                        return SqlValue(nullptr);
    }
}

std::vector<SqlValue> toSqlValues(const std::vector<uormpp::Param>& params) {
    std::vector<SqlValue> out;
    out.reserve(params.size());
    for (const auto& p : params) out.push_back(toSqlValue(p));
    return out;
}

} // namespace (internal helpers)

// ---------------------------------------------------------------------------
// IResult 实现
// ---------------------------------------------------------------------------
namespace uormpp {

class ResultImpl final : public IResult {
public:
    explicit ResultImpl(QueryResult r) : r_(std::move(r)) {}

    int rowCount() const override { return static_cast<int>(r_.rows.size()); }
    int columnCount() const override { return static_cast<int>(r_.columns.size()); }

    std::string columnName(int column) const override {
        if (column < 0 || column >= static_cast<int>(r_.columns.size()))
            throw Error("result column index out of range: " + std::to_string(column));
        return r_.columns[static_cast<std::size_t>(column)];
    }

    bool isNull(int row, int column) const override {
        return std::holds_alternative<std::nullptr_t>(valueAt(row, column));
    }

    std::int64_t getInt64(int row, int column) const override {
        return valueToInt64(valueAt(row, column));
    }

    double getDouble(int row, int column) const override {
        return valueToDouble(valueAt(row, column));
    }

    std::string getString(int row, int column) const override {
        return valueToString(valueAt(row, column));
    }

private:
    const SqlValue& valueAt(int row, int column) const {
        if (row < 0 || row >= static_cast<int>(r_.rows.size()) ||
            column < 0 || column >= static_cast<int>(r_.columns.size()))
            throw Error("result index out of range");
        return r_.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    }

    QueryResult r_;
};

// ---------------------------------------------------------------------------
// ITransaction 实现：借出连接 + BEGIN；析构未提交自动回滚并归还连接
// ---------------------------------------------------------------------------
class TransactionImpl final : public ITransaction {
public:
    explicit TransactionImpl(DataSource& ds)
        : conn_(ds.getConnection()), tx_(std::make_unique<uORM::Transaction>(*conn_)) {}

    std::unique_ptr<IResult> query(const std::string& sql,
                                   const std::vector<Param>& params) override {
        try {
            auto values = toSqlValues(params);
            auto pstmt = tx_->connection().prepareStatement(sql);
            for (std::size_t i = 0; i < values.size(); ++i)
                uORM::bindSqlValue(pstmt.get(), static_cast<int>(i + 1), values[i]);
            auto rs = pstmt->executeQuery();

            // 物化为对外结果
            const std::size_t n = rs->columnCount();
            std::vector<std::string> cols;
            cols.reserve(n);
            for (std::size_t i = 0; i < n; ++i) cols.push_back(rs->columnName(i));
            std::vector<std::vector<SqlValue>> rows;
            while (rs->next()) {
                std::vector<SqlValue> row;
                row.reserve(n);
                for (std::size_t i = 0; i < n; ++i) row.push_back(rs->getSqlValue(i));
                rows.push_back(std::move(row));
            }
            return std::unique_ptr<IResult>(new ResultImpl(QueryResult{std::move(cols), std::move(rows)}));
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

    std::uint64_t execute(const std::string& sql, const std::vector<Param>& params) override {
        try {
            auto values = toSqlValues(params);
            auto pstmt = tx_->connection().prepareStatement(sql);
            for (std::size_t i = 0; i < values.size(); ++i)
                uORM::bindSqlValue(pstmt.get(), static_cast<int>(i + 1), values[i]);
            return pstmt->executeUpdate();
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

    void commit() override {
        try {
            tx_->commit();
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

    void rollback() override {
        try {
            tx_->rollback();
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

private:
    PooledConnection conn_;
    std::unique_ptr<uORM::Transaction> tx_;
};

// ---------------------------------------------------------------------------
// IDatabase 实现
// ---------------------------------------------------------------------------
class DatabaseImpl final : public IDatabase {
public:
    explicit DatabaseImpl(uormpp::Options o) {
        if (o.host.empty()) o.host = "127.0.0.1";
        if (o.poolSize <= 0) o.poolSize = 5;
        if (o.acquireTimeoutMs <= 0) o.acquireTimeoutMs = 3000;

        DataSourceConfig cfg;
        cfg.params.driver = o.driver;
        cfg.params.host = o.host;
        cfg.params.port = o.port;
        cfg.params.username = o.username;
        cfg.params.password = o.password;
        cfg.params.database = o.database;
        cfg.params.useTLS = o.useTLS;
        cfg.poolSize = o.poolSize;
        cfg.acquireTimeoutMs = o.acquireTimeoutMs;
        ds_ = std::make_unique<DataSource>(std::move(cfg));
    }

    std::unique_ptr<IResult> query(const std::string& sql,
                                   const std::vector<Param>& params) override {
        try {
            auto r = ds_->query(sql, toSqlValues(params));
            return std::unique_ptr<IResult>(new ResultImpl(std::move(r)));
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

    std::uint64_t execute(const std::string& sql, const std::vector<Param>& params) override {
        try {
            return ds_->execute(sql, toSqlValues(params));
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

    std::unique_ptr<ITransaction> beginTransaction() override {
        try {
            return std::unique_ptr<ITransaction>(new TransactionImpl(*ds_));
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

    bool tableExists(const std::string& table) override {
        return withConn([&](IConnection& c) { return Schema::tableExists(c, table); });
    }

    bool indexExists(const std::string& table, const std::string& indexName) override {
        return withConn([&](IConnection& c) { return Schema::indexExists(c, table, indexName); });
    }

    bool createIndex(const std::string& table, const std::string& indexName,
                     const std::vector<std::string>& columns, bool unique) override {
        return withConn([&](IConnection& c) {
            return Schema::createIndex(c, table, indexName, columns, unique);
        });
    }

    bool dropIndex(const std::string& table, const std::string& indexName) override {
        return withConn([&](IConnection& c) { return Schema::dropIndex(c, table, indexName); });
    }

    bool addColumn(const std::string& table, const std::string& column,
                   const std::string& type, const std::string& constraints) override {
        return withConn([&](IConnection& c) {
            return Schema::addColumn(c, table, column, type, constraints);
        });
    }

    bool dropColumn(const std::string& table, const std::string& column) override {
        return withConn([&](IConnection& c) { return Schema::dropColumn(c, table, column); });
    }

    bool renameColumn(const std::string& table, const std::string& oldName,
                      const std::string& newName) override {
        return withConn([&](IConnection& c) {
            return Schema::renameColumn(c, table, oldName, newName);
        });
    }

    bool renameTable(const std::string& oldName, const std::string& newName) override {
        return withConn([&](IConnection& c) { return Schema::renameTable(c, oldName, newName); });
    }

    void stats(int* idle, int* inUse, int* totalCreated) override {
        auto st = ds_->stats();
        if (idle) *idle = st.idle;
        if (inUse) *inUse = st.inUse;
        if (totalCreated) *totalCreated = st.totalCreated;
    }

private:
    template<typename F>
    bool withConn(F&& fn) {
        try {
            auto conn = ds_->getConnection();
            return fn(*conn);
        } catch (const std::exception& e) {
            throw Error(toErrorText(e));
        }
    }

    std::unique_ptr<DataSource> ds_;
};

} // namespace uormpp

// ---------------------------------------------------------------------------
// 工厂与全局
// ---------------------------------------------------------------------------
namespace {

uormpp::Options normalize(uormpp::Options o) {
    if (o.host.empty()) o.host = "127.0.0.1";
    if (o.poolSize <= 0) o.poolSize = 5;
    if (o.acquireTimeoutMs <= 0) o.acquireTimeoutMs = 3000;
    return o;
}

} // namespace

namespace uormpp {

std::unique_ptr<IDatabase> openDatabase(const Options& options) {
    if (options.driver.empty()) throw Error("openDatabase: driver is required");
    if (!DriverRegistry::instance().hasDriver(options.driver)) {
        throw Error("openDatabase: unknown or disabled driver '" + options.driver +
                    "' (available: " + DriverRegistry::instance().driverList() + ")");
    }
    try {
        DataSourceConfig cfg;
        cfg.params.driver = options.driver;
        cfg.params.host = options.host;
        cfg.params.port = options.port;
        cfg.params.username = options.username;
        cfg.params.password = options.password;
        cfg.params.database = options.database;
        cfg.params.useTLS = options.useTLS;
        cfg.poolSize = options.poolSize;
        cfg.acquireTimeoutMs = options.acquireTimeoutMs;
        return std::unique_ptr<IDatabase>(new DatabaseImpl(normalize(options)));
    } catch (const std::exception& e) {
        throw Error(std::string("openDatabase failed: ") + e.what());
    }
}

std::vector<std::string> availableDrivers() {
    return DriverRegistry::instance().driverNames();
}

std::string version() {
    return UORMPP_VERSION_STRING;
}

} // namespace uormpp
