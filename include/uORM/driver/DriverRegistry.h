#pragma once
// 文件说明：
// DriverRegistry 维护 "驱动名 -> 连接工厂" 的运行时注册表。
// 各驱动在编译期通过 UORM_ENABLE_* 宏决定是否编入，运行期按名字创建，
// 使一个进程可以同时连接多种数据库（Web 管理台的基础能力）。

#include "uORM/driver/DBInterfaces.h"
#include "uORM/orm/Error.h"
#include <map>
#include <mutex>
#include <vector>
#include <functional>

#ifdef UORM_ENABLE_MYSQL
#include "uORM/driver/mysql/MySQLWrapper.h"
#endif

#ifdef UORM_ENABLE_POSTGRESQL
#include "uORM/driver/postgresql/PostgreSQLWrapper.h"
#endif

#ifdef UORM_ENABLE_SQLITE
#include "uORM/driver/sqlite/SQLiteWrapper.h"
#endif

namespace uORM {

class DriverRegistry {
public:
    using Factory = std::function<std::unique_ptr<IConnection>(const ConnectionParams&)>;

    static DriverRegistry& instance() {
        static DriverRegistry inst;
        return inst;
    }

    // 注册驱动（也可注册用户自定义驱动）
    void registerDriver(const std::string& name, Factory factory) {
        std::lock_guard<std::mutex> lock(mutex_);
        factories_[name] = std::move(factory);
    }

    // 按参数创建连接；未知驱动或驱动未编入时抛出 ConnectionError
    std::unique_ptr<IConnection> create(const ConnectionParams& params) const {
        Factory factory;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = factories_.find(params.driver);
            if (it == factories_.end()) return nullptr;
            factory = it->second;
        }
        return factory(params);
    }

    // 便捷方法：创建失败（未知驱动/连接失败）直接抛异常
    std::unique_ptr<IConnection> createOrThrow(const ConnectionParams& params) const {
        auto conn = create(params);
        if (!conn) {
            throw ConnectionError("Unknown or disabled database driver: '" + params.driver +
                                  "' (enabled: " + driverList() + ")");
        }
        return conn;
    }

    bool hasDriver(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return factories_.count(name) != 0;
    }

    std::vector<std::string> driverNames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& kv : factories_) names.push_back(kv.first);
        return names;
    }

    std::string driverList() const {
        auto names = driverNames();
        std::string out;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i) out += ", ";
            out += names[i];
        }
        return out.empty() ? "(none)" : out;
    }

private:
    DriverRegistry() {
        registerBuiltins();
    }

    void registerBuiltins() {
#ifdef UORM_ENABLE_MYSQL
        registerDriver("mysql", [](const ConnectionParams& p) {
            return std::unique_ptr<IConnection>(new MySQLConnection(p));
        });
        registerDriver("mariadb", [](const ConnectionParams& p) {
            return std::unique_ptr<IConnection>(new MySQLConnection(p));
        });
#endif
#ifdef UORM_ENABLE_POSTGRESQL
        registerDriver("postgresql", [](const ConnectionParams& p) {
            return std::unique_ptr<IConnection>(new PostgreSQLConnection(p));
        });
        registerDriver("postgres", [](const ConnectionParams& p) {
            return std::unique_ptr<IConnection>(new PostgreSQLConnection(p));
        });
#endif
#ifdef UORM_ENABLE_SQLITE
        registerDriver("sqlite", [](const ConnectionParams& p) {
            return std::unique_ptr<IConnection>(new SQLiteConnection(p));
        });
#endif
    }

    std::map<std::string, Factory> factories_;
    mutable std::mutex mutex_;
};

} // namespace uORM
