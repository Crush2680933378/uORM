#pragma once 
// 文件说明： 
// ConnectionPool 提供通用的数据库连接池实现。 
// 支持 MySQL 和 PostgreSQL，通过配置自动选择驱动。 

#include "uORM/driver/DBInterfaces.h" 
#include "uORM/driver/SqlDialect.h" 
#include "uORM/driver/ConfigManager.h" 
#include "uORM/orm/Error.h"
#include <functional> 
#include <queue> 
#include <mutex> 
#include <condition_variable> 
#include <memory> 
#include <string> 
#include <iostream>

// 条件包含驱动头文件
#ifdef USE_MYSQL
#include "uORM/driver/mysql/MySQLWrapper.h" 
#include <cppconn/driver.h>
#if __has_include(<mysql_driver.h>)
#include <mysql_driver.h>
#endif
#if __has_include(<jdbc/mysql_driver.h>)
#include <jdbc/mysql_driver.h>
#endif
#endif

#ifdef USE_POSTGRESQL
#include "uORM/driver/postgresql/PostgreSQLWrapper.h" 
#endif

namespace uORM { 
class ConnectionPool { 
public: 
    // 获取连接池单例实例 
    static ConnectionPool& instance() {
        static ConnectionPool inst; 
        return inst; 
    }
    
    // 获取连接（使用std::unique_ptr与自定义删除器实现RAII归还） 
    std::unique_ptr<IConnection, std::function<void(IConnection*)>> getConnection() { 
        std::unique_lock<std::mutex> lock(mutex_); 
        // 简单的等待策略，如果池空了就等 
        // 实际生产中可能需要超时机制或动态扩容 
        if (connections_.empty()) { 
            // 如果队列为空，尝试创建一个新连接（应对突发流量或初始化失败的情况） 
            auto conn = createRawConnection(); 
            if (conn && conn->isValid()) { 
                 auto deleter = [this](IConnection* c){ releaseConnection(c); }; 
                 return std::unique_ptr<IConnection, decltype(deleter)>(conn, deleter); 
            } 
            // 如果创建失败，则等待归还 
            cond_.wait(lock, [this]{ return !connections_.empty(); }); 
        } 
        
        auto conn = connections_.front(); 
        connections_.pop(); 
        lock.unlock(); 
        
        // 检查有效性 
        if (!conn->isValid()) { 
            delete conn; 
            conn = createRawConnection(); 
            if (!conn || !conn->isValid()) { 
                if(conn) delete conn; 
                throw ConnectionError("Failed to obtain valid DB connection"); 
            } 
        } 
        
        auto deleter = [this](IConnection* c){ releaseConnection(c); }; 
        return std::unique_ptr<IConnection, decltype(deleter)>(conn, deleter); 
    }
    
    // 获取当前的 SQL 方言 
    std::shared_ptr<ISqlDialect> getDialect() const {
        return dialect_; 
    }

    // 禁止拷贝与赋值 
    ConnectionPool(const ConnectionPool&) = delete; 
    ConnectionPool& operator=(const ConnectionPool&) = delete; 
private: 
    // 构造函数 
    ConnectionPool() { 
        // 加载配置 
        config_ = ConfigManager::getInstance().databaseconfigdata_; 
        
        // 初始化方言 
        // 根据宏定义决定默认方言，或运行时检查
        if (config_.driver_type == DriverType::PostgreSQL) { 
    #ifdef USE_POSTGRESQL
            dialect_ = std::make_shared<PostgreSQLDialect>(); 
    #else
            std::cerr << "Error: PostgreSQL driver not compiled in!" << std::endl;
            // Fallback or throw? For now just log.
    #endif
        } else { 
    #ifdef USE_MYSQL
            dialect_ = std::make_shared<MySQLDialect>(); 
    #else
            std::cerr << "Error: MySQL driver not compiled in!" << std::endl;
    #endif
        } 
        
        initializePool(); 
    }

    // 析构函数：安全释放池中所有连接 
    ~ConnectionPool() { 
        std::lock_guard<std::mutex> lock(mutex_); 
        while (!connections_.empty()) { 
            auto conn = connections_.front(); 
            connections_.pop(); 
            delete conn; 
        } 
    }
    
    // 创建新连接的辅助函数 
    IConnection* createRawConnection() { 
        if (config_.driver_type == DriverType::PostgreSQL) { 
    #ifdef USE_POSTGRESQL
            // 构建 PG 连接字符串: "host=... port=... dbname=... user=... password=..." 
            std::string connStr = "host=" + config_.hostname + 
                                  " port=" + std::to_string(config_.port) + 
                                  " dbname=" + config_.dataname + 
                                  " user=" + config_.username + 
                                  " password=" + config_.password; 
            return new PostgreSQLConnection(connStr); 
    #else
            return nullptr;
    #endif
        } else { 
            // MySQL 
    #ifdef USE_MYSQL
            try { 
                sql::Driver* driver = get_driver_instance(); 
                auto* conn = driver->connect(config_.hostname + ":" + std::to_string(config_.port), 
                                             config_.username, config_.password); 
                // 创建连接后，尝试设置 schema，或者在连接字符串中指定 (connect 只有 host, user, pass)
                // setSchema(config_.dataname) 应该在 connect 后调用
                // 但是 MySQLWrapper 中封装了 setSchema 逻辑。
                // 这里我们直接创建 Wrapper，然后调用 setSchema
                auto* wrapper = new MySQLConnection(conn);
                try {
                    wrapper->setSchema(config_.dataname);
                } catch (const std::exception& e) {
                     // 如果数据库不存在，可能需要先创建？
                     // 或者直接抛出
                     std::cerr << "Warning: Failed to select database '" << config_.dataname << "': " << e.what() << std::endl;
                     // 如果是为了 create database，这里可能允许失败
                }
                return wrapper;
            } catch (const std::exception& e) { 
                std::cerr << "MySQL Connect Error: " << e.what() << std::endl; 
                return nullptr; 
            } 
    #else
            return nullptr;
    #endif
        } 
    }
    
    // 初始化连接池 
    void initializePool() { 
        for (int i = 0; i < config_.poolsize; ++i) { 
            auto conn = createRawConnection(); 
            if (conn && conn->isValid()) { 
                connections_.push(conn); 
            } else { 
                std::cerr << "Failed to create initial connection #" << i << std::endl; 
                if (conn) delete conn; 
            } 
        } 
    }

    // 归还连接 
    void releaseConnection(IConnection* conn) { 
        if (conn) { 
            // 可以在这里做一些重置操作，如回滚未提交事务 
            std::lock_guard<std::mutex> lock(mutex_); 
            connections_.push(conn); 
            cond_.notify_one(); 
        } 
    }

private: 
    // 连接队列 
    std::queue<IConnection*> connections_; 
    // 互斥锁与条件变量 
    std::mutex mutex_; 
    std::condition_variable cond_; 
    
    // 配置参数 
    DataBaseConfigData config_; 
    
    // SQL 方言实例 
    std::shared_ptr<ISqlDialect> dialect_; 
}; 
} // namespace uORM 
