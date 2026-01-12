#pragma once 
#include "uORM/driver/DBInterfaces.h" 
#include <mysql_connection.h> 
#include <cppconn/prepared_statement.h> 
#include <cppconn/resultset.h> 
#include <cppconn/statement.h> 

namespace uORM { 

// MySQL 结果集包装 
class MySQLResultSet : public IResultSet { 
public: 
    MySQLResultSet(sql::ResultSet* rs) : rs_(rs) {} 
    bool next() override { return rs_->next(); } 
    int getInt(const std::string& colName) override { return rs_->getInt(colName); } 
    long long getInt64(const std::string& colName) override { return rs_->getInt64(colName); } 
    unsigned int getUInt(const std::string& colName) override { return rs_->getUInt(colName); } 
    std::string getString(const std::string& colName) override { return rs_->getString(colName); } 
    bool getBoolean(const std::string& colName) override { return rs_->getBoolean(colName); } 
    double getDouble(const std::string& colName) override { return rs_->getDouble(colName); } 
private: 
    std::unique_ptr<sql::ResultSet> rs_; 
}; 

// MySQL 预编译语句包装 
class MySQLPreparedStatement : public IPreparedStatement { 
public: 
    MySQLPreparedStatement(sql::PreparedStatement* stmt) : stmt_(stmt) {} 
    void executeUpdate() override { stmt_->executeUpdate(); } 
    std::unique_ptr<IResultSet> executeQuery() override { 
        return std::make_unique<MySQLResultSet>(stmt_->executeQuery()); 
    } 
    void setInt(int index, int val) override { stmt_->setInt(index, val); } 
    void setInt64(int index, long long val) override { stmt_->setInt64(index, val); } 
    void setUInt(int index, unsigned int val) override { stmt_->setUInt(index, val); } 
    void setString(int index, const std::string& val) override { stmt_->setString(index, val); } 
    void setBoolean(int index, bool val) override { stmt_->setBoolean(index, val); } 
    void setDouble(int index, double val) override { stmt_->setDouble(index, val); } 
private: 
    std::unique_ptr<sql::PreparedStatement> stmt_; 
}; 

// MySQL 语句包装 
class MySQLStatement : public IStatement { 
public: 
    MySQLStatement(sql::Statement* stmt) : stmt_(stmt) {} 
    void execute(const std::string& sql) override { stmt_->execute(sql); } 
    std::unique_ptr<IResultSet> executeQuery(const std::string& sql) override { 
        return std::make_unique<MySQLResultSet>(stmt_->executeQuery(sql)); 
    } 
private: 
    std::unique_ptr<sql::Statement> stmt_; 
}; 

// MySQL 连接包装 
class MySQLConnection : public IConnection { 
public: 
    MySQLConnection(sql::Connection* conn) : conn_(conn) {} 
    ~MySQLConnection() { 
        // sql::Connection 析构时会自动释放资源，或由 unique_ptr 管理 
        if(conn_) delete conn_; 
    } 
    bool isValid() override { return conn_ && conn_->isValid(); } 
    void setSchema(const std::string& db) override { 
        // conn_->setSchema(db); // MySQL Connector C++ 1.1 的 setSchema 可能会有问题，或者在某些版本中不生效
        // 我们可以显式执行 USE db
        std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
        stmt->execute("USE " + db);
    } 
    
    std::unique_ptr<IStatement> createStatement() override { 
        return std::make_unique<MySQLStatement>(conn_->createStatement()); 
    } 
    
    std::unique_ptr<IPreparedStatement> prepareStatement(const std::string& sql) override { 
        return std::make_unique<MySQLPreparedStatement>(conn_->prepareStatement(sql)); 
    } 
    
private: 
    sql::Connection* conn_; // 拥有所有权 
}; 

} // namespace uORM 
