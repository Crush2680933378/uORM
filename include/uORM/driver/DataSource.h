#pragma once
// 文件说明：
// DataSource 表示一个数据库（驱动 + 连接参数）及其专属的有界连接池。
// - 多数据源：一个进程可创建任意多个 DataSource，各自独立池化（Web 管理台的基础）
// - 有界池：超过 poolSize 后等待，acquireTimeoutMs 超时抛出（修复旧池无限等待问题）
// - 健康检查：借出前 ping 校验，失效连接自动重建
// - RAII：连接析构自动归还池

#include "uORM/driver/DBInterfaces.h"
#include "uORM/driver/DriverRegistry.h"
#include "uORM/orm/Error.h"
#include "uORM/orm/QueryResult.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <atomic>

namespace uORM {

// 数据源配置
struct DataSourceConfig {
    ConnectionParams params;      // 连接参数（driver/host/port/user/password/database）
    std::string name;             // 数据源名称（多数据源管理用，可空）
    int poolSize = 5;             // 池上限
    int acquireTimeoutMs = 3000;  // 获取连接最长等待（毫秒）
    bool validateOnAcquire = true;// 借出时做健康检查
    // 空闲感知 ping：连接空闲超过该毫秒才执行 ping 校验（远程库可省一次 RTT）。
    // 0 = 每次借出都 ping（最保守）。
    int idlePingMs = 30000;
};

// 池化连接句柄：析构自动归还
using PooledConnection = std::unique_ptr<IConnection, std::function<void(IConnection*)>>;

class DataSource {
public:
    explicit DataSource(DataSourceConfig cfg) : config_(std::move(cfg)) {}

    ~DataSource() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!idle_.empty()) {
            delete idle_.front().conn;
            idle_.pop_front();
        }
    }

    DataSource(const DataSource&) = delete;
    DataSource& operator=(const DataSource&) = delete;

    // 获取连接；池满等待，超时抛 ConnectionError
    PooledConnection getConnection() {
        const auto now = std::chrono::steady_clock::now();
        const auto deadline = now + std::chrono::milliseconds(config_.acquireTimeoutMs);

        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            if (!idle_.empty()) {
                IdleEntry entry = idle_.back();
                idle_.pop_back();
                ++inUse_;
                lock.unlock();

                // 空闲感知校验：刚归还的连接直接复用，省一次 ping 往返
                const auto idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.returnedAt).count();
                const bool needPing = config_.validateOnAcquire && idleMs > config_.idlePingMs;
                if (needPing && !entry.conn->ping()) {
                    lock.lock();
                    --inUse_;
                    --totalCreated_; // 失效连接移出计数，由重建补位
                    delete entry.conn;
                    lock.unlock();
                    try {
                        IConnection* fresh = createConnection();
                        lock.lock();
                        ++inUse_;
                        return wrap(fresh);
                    } catch (const Exception&) {
                        throw ConnectionError("DataSource '" + name() + "': connection lost and reconnect failed");
                    }
                }
                return wrap(entry.conn);
            }

            if (totalCreated_ < config_.poolSize) {
                lock.unlock();
                IConnection* conn = nullptr;
                try {
                    conn = createConnection(); // 成功时内部已计数
                } catch (const Exception&) {
                    lock.lock();
                    waitUntil(lock, deadline);
                    continue;
                }
                lock.lock();
                ++inUse_;
                return wrap(conn);
            }

            // 池满：等待归还
            if (!waitUntil(lock, deadline)) {
                throw ConnectionError("DataSource '" + name() +
                                      "': acquire timeout after " +
                                      std::to_string(config_.acquireTimeoutMs) + " ms (pool full)");
            }
        }
    }

    // 绕过池新建连接（管理类用途：schema 迁移、探活等）
    std::unique_ptr<IConnection> openNewConnection() const {
        return createConnectionChecked();
    }

    // 便捷：自动借还连接执行 lambda
    template<typename F>
    auto withConnection(F&& f) -> decltype(std::declval<F>()(std::declval<IConnection&>())) {
        auto conn = getConnection();
        return f(*conn);
    }

    // 便捷：原生查询（借连接执行后归还）
    QueryResult query(const std::string& sql, const std::vector<SqlValue>& params = {}) {
        auto conn = getConnection();
        return executeQuery(*conn, sql, params);
    }

    // 便捷：原生 DML/DDL 执行
    unsigned long long execute(const std::string& sql, const std::vector<SqlValue>& params = {}) {
        auto conn = getConnection();
        return executeUpdate(*conn, sql, params);
    }

    std::shared_ptr<ISqlDialect> dialect() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dialect_) {
            auto conn = createConnectionChecked();
            dialect_ = conn->dialect();
        }
        return dialect_;
    }

    const std::string& name() const { return config_.name.empty() ? config_.params.driver : config_.name; }
    const DataSourceConfig& config() const { return config_; }

    struct Stats {
        int idle;
        int inUse;
        int totalCreated;
    };

    Stats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return Stats{static_cast<int>(idle_.size()), inUse_, totalCreated_};
    }

private:
    // 在截止时间前等待归还通知；返回 false 表示超时
    bool waitUntil(std::unique_lock<std::mutex>& lock, const std::chrono::steady_clock::time_point& deadline) {
        return cond_.wait_until(lock, deadline, [&] {
            return !idle_.empty() || totalCreated_ < config_.poolSize;
        });
    }

    IConnection* createConnection() {
        // 只在成功时计数，避免失败路径虚增池上限
        auto conn = DriverRegistry::instance().createOrThrow(config_.params);
        ++totalCreated_;
        return conn.release();
    }

    std::unique_ptr<IConnection> createConnectionChecked() const {
        return DriverRegistry::instance().createOrThrow(config_.params);
    }

    PooledConnection wrap(IConnection* conn) {
        return PooledConnection(conn, [this](IConnection* c) { release(c); });
    }

    void release(IConnection* conn) {
        if (!conn) return;
        std::lock_guard<std::mutex> lock(mutex_);
        --inUse_;
        idle_.push_back(IdleEntry{conn, std::chrono::steady_clock::now()});
        cond_.notify_one();
    }

    DataSourceConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    struct IdleEntry {
        IConnection* conn;
        std::chrono::steady_clock::time_point returnedAt;
    };
    std::deque<IdleEntry> idle_;
    int inUse_ = 0;
    int totalCreated_ = 0;
    std::shared_ptr<ISqlDialect> dialect_;
};

} // namespace uORM
