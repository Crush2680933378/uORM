#pragma once
// 文件说明：
// Transaction：RAII 事务守卫 + withTransaction 便捷函数。
// 析构时若尚未提交则自动回滚；异常安全。

#include "uORM/driver/DBInterfaces.h"
#include "uORM/driver/DataSource.h"
#include "uORM/orm/Error.h"
#include <type_traits>
#include <utility>

namespace uORM {

class Transaction {
public:
    explicit Transaction(IConnection& conn) : conn_(conn) {
        conn_.begin();
    }

    ~Transaction() {
        if (!finished_) {
            try {
                conn_.rollback();
            } catch (...) {
                // 析构中吞掉回滚异常，避免 terminate
            }
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        conn_.commit();
        finished_ = true;
    }

    void rollback() {
        conn_.rollback();
        finished_ = true;
    }

    IConnection& connection() { return conn_; }

private:
    IConnection& conn_;
    bool finished_ = false;
};

// 便捷：在数据源上以事务执行 lambda；异常时自动回滚并重抛
template<typename F>
auto withTransaction(DataSource& ds, F&& f) -> decltype(std::declval<F>()(std::declval<IConnection&>())) {
    auto conn = ds.getConnection();
    Transaction tx(*conn);
    using R = decltype(f(*conn));
    if constexpr (std::is_void_v<R>) {
        try {
            f(*conn);
            tx.commit();
        } catch (...) {
            tx.rollback();
            throw;
        }
    } else {
        try {
            R result = f(*conn);
            tx.commit();
            return result;
        } catch (...) {
            tx.rollback();
            throw;
        }
    }
}

} // namespace uORM
