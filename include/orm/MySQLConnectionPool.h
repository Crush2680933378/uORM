#pragma once
// 文件说明：
// MySQLConnectionPool 提供基于 MySQL Connector/C++ 的连接池实现。
// 通过单例模式统一管理连接复用，支持无效连接自动重建、RAII方式归还连接。

#include <mysql_driver.h>               // MySQL 驱动入口
#include <mysql_connection.h>           // 连接对象
#include <cppconn/driver.h>             // 通用驱动接口
#include <cppconn/exception.h>          // 异常类型
#include <cppconn/statement.h>          // 普通语句
#include <cppconn/prepared_statement.h> // 预编译语句
#include <cppconn/resultset.h>          // 结果集
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>

namespace uORM {
class MySQLConnectionPool {
public:
    // 获取连接池单例实例（使用配置管理器中的数据库配置进行初始化）
    static MySQLConnectionPool& instance();
    // 获取连接（使用std::unique_ptr与自定义删除器实现RAII归还）
    std::unique_ptr<sql::Connection, std::function<void(sql::Connection*)>> getConnection();
    // 禁止拷贝与赋值
    MySQLConnectionPool(const MySQLConnectionPool&) = delete;
    MySQLConnectionPool& operator=(const MySQLConnectionPool&) = delete;
private:
    // 构造函数：注入主机、端口、用户、密码、数据库名及池大小
    MySQLConnectionPool(const std::string& host, int port, const std::string& user, const std::string& pass, const std::string& db, int poolSize);
    // 析构函数：安全释放池中所有连接
    ~MySQLConnectionPool();
    // 初始化连接池：按poolSize创建连接并放入队列
    void initializePool();
    // 归还连接：若无效则重建后再放回池中
    void releaseConnection(sql::Connection* conn);
private:
    // 连接队列：存放可用连接
    std::queue<sql::Connection*> connections_;
    // 互斥锁与条件变量：保障并发安全与等待机制
    std::mutex mutex_;
    std::condition_variable cond_;
    // 配置参数缓存
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::string database_;
    int poolSize_;
};
} // namespace uORM
