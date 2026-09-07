#pragma once
// 文件说明：
// ConnectionPool 提供数据库连接池。
// v2：驱动选择改为运行时（通过 DriverRegistry 按配置创建），兼容旧接口。
//     多数据源支持见 DataSource（pool-v2），本类保留为"默认单数据源"入口。

#include "uORM/driver/DBInterfaces.h"
#include "uORM/driver/DriverRegistry.h"
#include "uORM/driver/ConfigManager.h"
#include "uORM/orm/Error.h"
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <iostream>

namespace uORM {

class ConnectionPool {
public:
    // 获取连接池单例实例
    static ConnectionPool& instance() {
        static ConnectionPool inst;
        return inst;
    }

    // 获取连接（RAII 归还）
    std::unique_ptr<IConnection, std::function<void(IConnection*)>> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (connections_.empty()) {
            // 池空时尝试新建（应对突发流量）
            lock.unlock();
            IConnection* conn = nullptr;
            try {
                conn = createRawConnection();
            } catch (const Exception&) {
                conn = nullptr;
            }
            lock.lock();
            if (conn && conn->isValid()) {
                return wrapConnection(conn);
            }
            if (conn) delete conn;
            // 创建失败则等待归还
            cond_.wait(lock, [this] { return !connections_.empty(); });
        }

        IConnection* conn = connections_.front();
        connections_.pop();
        lock.unlock();

        if (!conn->isValid()) {
            delete conn;
            conn = nullptr;
            try {
                conn = createRawConnection();
            } catch (const Exception& e) {
                std::cerr << "uORM: reconnect failed: " << e.what() << std::endl;
            }
            if (!conn || !conn->isValid()) {
                if (conn) delete conn;
                throw ConnectionError("Failed to obtain valid DB connection");
            }
        }
        return wrapConnection(conn);
    }

    // 获取当前数据源的 SQL 方言（可能为空：未初始化或驱动未编入）
    std::shared_ptr<ISqlDialect> getDialect() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dialect_;
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

private:
    ConnectionPool() {
        config_ = ConfigManager::getInstance().databaseconfigdata_;
        initializePool();
    }

    ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connections_.empty()) {
            delete connections_.front();
            connections_.pop();
        }
    }

    // 通过驱动注册表创建连接（运行时按 driver 名字选择）
    IConnection* createRawConnection() {
        ConnectionParams params;
        params.driver = config_.driver;
        params.host = config_.hostname;
        params.port = config_.port;
        params.username = config_.username;
        params.password = config_.password;
        params.database = config_.dataname;

        auto conn = DriverRegistry::instance().createOrThrow(params);
        rememberDialect(*conn);
        return conn.release();
    }

    void rememberDialect(IConnection& conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dialect_) dialect_ = conn.dialect();
    }

    void initializePool() {
        for (int i = 0; i < config_.poolsize; ++i) {
            IConnection* conn = nullptr;
            try {
                conn = createRawConnection();
            } catch (const Exception& e) {
                std::cerr << "uORM: failed to create initial connection #" << i << ": " << e.what() << std::endl;
                continue;
            }
            if (conn && conn->isValid()) {
                std::lock_guard<std::mutex> lock(mutex_);
                connections_.push(conn);
            } else {
                delete conn;
            }
        }
    }

    auto wrapConnection(IConnection* conn)
        -> std::unique_ptr<IConnection, std::function<void(IConnection*)>> {
        auto deleter = [this](IConnection* c) { releaseConnection(c); };
        return std::unique_ptr<IConnection, std::function<void(IConnection*)>>(conn, deleter);
    }

    void releaseConnection(IConnection* conn) {
        if (!conn) return;
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.push(conn);
        cond_.notify_one();
    }

    std::queue<IConnection*> connections_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;

    DataBaseConfigData config_;
    std::shared_ptr<ISqlDialect> dialect_;
};

} // namespace uORM
