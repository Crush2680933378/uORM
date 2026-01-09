#include "orm/MySQLConnectionPool.h"
#include "common/utils/ConfigManager.h"
#include <iostream>
namespace uORM {
MySQLConnectionPool& MySQLConnectionPool::instance() {
    auto& cfg = ConfigManager::getInstance().databaseconfigdata_;
    static MySQLConnectionPool inst(cfg.hostname, cfg.port, cfg.username, cfg.password, cfg.dataname, cfg.poolsize);
    return inst;
}
std::unique_ptr<sql::Connection, std::function<void(sql::Connection*)>> MySQLConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]{ return !connections_.empty(); });
    auto conn = connections_.front();
    connections_.pop();
    lock.unlock();
    if (!conn->isValid()) {
        try {
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
            auto* newConn = driver->connect(host_ + ":" + std::to_string(port_), user_, password_);
            newConn->setSchema(database_);
            conn = newConn;
        } catch (const sql::SQLException& e) {
            std::cerr << e.what() << std::endl;
            throw;
        }
    }
    auto deleter = [this](sql::Connection* c){ releaseConnection(c); };
    return std::unique_ptr<sql::Connection, decltype(deleter)>(conn, deleter);
}
MySQLConnectionPool::MySQLConnectionPool(const std::string& host, int port, const std::string& user, const std::string& pass, const std::string& db, int poolSize)
    : host_(host), port_(port), user_(user), password_(pass), database_(db), poolSize_(poolSize) {
    initializePool();
}
MySQLConnectionPool::~MySQLConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!connections_.empty()) {
        auto conn = connections_.front();
        connections_.pop();
        delete conn;
    }
}
void MySQLConnectionPool::initializePool() {
    sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
    for (int i = 0; i < poolSize_; ++i) {
        try {
            sql::Connection* conn = driver->connect(host_ + ":" + std::to_string(port_), user_, password_);
            conn->setSchema(database_);
            connections_.push(conn);
        } catch (const sql::SQLException& e) {
            std::cerr << e.what() << std::endl;
            throw;
        }
    }
}
void MySQLConnectionPool::releaseConnection(sql::Connection* conn) {
    if (conn && conn->isValid()) {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.push(conn);
    } else {
        try {
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
            auto* newConn = driver->connect(host_ + ":" + std::to_string(port_), user_, password_);
            newConn->setSchema(database_);
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.push(newConn);
            if (conn) delete conn;
        } catch (const sql::SQLException& e) {
            if (conn) delete conn;
            std::cerr << e.what() << std::endl;
            throw;
        }
    }
    cond_.notify_one();
}
} // namespace uORM
