#pragma once
// 文件说明：
// ConnectionPool 是"默认单数据源"的兼容入口（单例，配置来自 ConfigManager）。
// v2：实现委托给 DataSource（有界池 + 获取超时 + ping 健康检查）。
//     多数据源请直接使用 DataSource（见 DataSource.h）。

#include "uORM/driver/DBInterfaces.h"
#include "uORM/driver/DataSource.h"
#include "uORM/driver/ConfigManager.h"
#include "uORM/orm/Error.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace uORM {

class ConnectionPool {
public:
    // 获取连接池单例实例
    static ConnectionPool& instance() {
        static ConnectionPool inst;
        return inst;
    }

    // 获取连接（RAII 归还）
    PooledConnection getConnection() {
        return impl().getConnection();
    }

    // 获取当前数据源的 SQL 方言
    std::shared_ptr<ISqlDialect> getDialect() {
        return impl().dialect();
    }

    // 访问底层数据源（事务/原生查询/统计）
    DataSource& source() { return impl(); }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

private:
    ConnectionPool() {
        const auto& cfg = ConfigManager::getInstance().databaseconfigdata_;

        DataSourceConfig dsc;
        dsc.params.driver = cfg.driver;
        dsc.params.host = cfg.hostname;
        dsc.params.port = cfg.port;
        dsc.params.username = cfg.username;
        dsc.params.password = cfg.password;
        dsc.params.database = cfg.dataname;
        dsc.poolSize = cfg.poolsize > 0 ? cfg.poolsize : 5;
        dsc.name = "default";

        source_ = std::make_unique<DataSource>(std::move(dsc));
    }

    DataSource& impl() {
        if (!source_) throw ConnectionError("ConnectionPool not initialized");
        return *source_;
    }

    std::unique_ptr<DataSource> source_;
};

} // namespace uORM
