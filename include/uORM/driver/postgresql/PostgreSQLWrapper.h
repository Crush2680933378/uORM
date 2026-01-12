#pragma once 
#include "uORM/driver/DBInterfaces.h" 
#include <pqxx/pqxx> 
#include <memory> 
#include <iostream> 

namespace uORM { 

// PostgreSQL 结果集包装 
class PostgreSQLResultSet : public IResultSet { 
public: 
    PostgreSQLResultSet(pqxx::result res) : res_(res), currentRow_(-1) {} 
    
    bool next() override { 
        currentRow_++; 
        return currentRow_ < res_.size(); 
    } 
    
    int getInt(const std::string& colName) override { 
        return res_[currentRow_][colName].as<int>(); 
    } 
    
    long long getInt64(const std::string& colName) override { 
        return res_[currentRow_][colName].as<long long>(); 
    } 
    
    unsigned int getUInt(const std::string& colName) override { 
        return res_[currentRow_][colName].as<unsigned int>(); 
    } 
    
    std::string getString(const std::string& colName) override { 
        return res_[currentRow_][colName].as<std::string>(); 
    } 
    
    bool getBoolean(const std::string& colName) override { 
        return res_[currentRow_][colName].as<bool>(); 
    } 
    
    double getDouble(const std::string& colName) override { 
        return res_[currentRow_][colName].as<double>(); 
    } 

private: 
    pqxx::result res_; 
    int currentRow_; 
}; 

// PostgreSQL 预编译语句包装 (简单模拟，libpqxx 的 prepared statement 需要事务上下文) 
// 为了适配接口，我们在这里持有 connection 指针，并在 execute 时创建临时事务或使用传入的事务。 
// 简化起见，我们暂存 SQL 和参数，在 execute 时执行 params。 
class PostgreSQLPreparedStatement : public IPreparedStatement { 
public: 
    PostgreSQLPreparedStatement(pqxx::connection* conn, const std::string& sql) 
        : conn_(conn), sql_(sql), params_() {} 

    void executeUpdate() override { 
        pqxx::work w(*conn_); 
        w.exec_params(sql_, params_); 
        w.commit(); 
    } 

    std::unique_ptr<IResultSet> executeQuery() override { 
        pqxx::work w(*conn_); 
        pqxx::result res = w.exec_params(sql_, params_); 
        w.commit(); 
        return std::make_unique<PostgreSQLResultSet>(res); 
    } 

    void setInt(int index, int val) override { addParam(std::to_string(val)); } 
    void setInt64(int index, long long val) override { addParam(std::to_string(val)); } 
    void setUInt(int index, unsigned int val) override { addParam(std::to_string(val)); } 
    void setString(int index, const std::string& val) override { addParam(val); } 
    void setBoolean(int index, bool val) override { addParam(val ? "true" : "false"); } 
    void setDouble(int index, double val) override { addParam(std::to_string(val)); } 

private: 
    void addParam(const std::string& val) { 
        params_.push_back(val); 
    } 

    pqxx::connection* conn_; 
    std::string sql_; 
    std::vector<std::string> params_; // 简化处理，全转字符串，libpqxx exec_params 支持 
}; 

// PostgreSQL 语句包装 
class PostgreSQLStatement : public IStatement { 
public: 
    PostgreSQLStatement(pqxx::connection* conn) : conn_(conn) {} 
    
    void execute(const std::string& sql) override { 
        pqxx::work w(*conn_); 
        w.exec0(sql); 
        w.commit(); 
    } 
    
    std::unique_ptr<IResultSet> executeQuery(const std::string& sql) override { 
        pqxx::work w(*conn_); 
        pqxx::result res = w.exec(sql); 
        w.commit(); 
        return std::make_unique<PostgreSQLResultSet>(res); 
    } 

private: 
    pqxx::connection* conn_; 
}; 

// PostgreSQL 连接包装 
class PostgreSQLConnection : public IConnection { 
public: 
    PostgreSQLConnection(const std::string& connStr) { 
        try { 
            conn_ = std::make_unique<pqxx::connection>(connStr); 
        } catch (const std::exception& e) { 
            std::cerr << "PG Connect Error: " << e.what() << std::endl; 
            conn_ = nullptr; 
        } 
    } 
    
    bool isValid() override { 
        return conn_ && conn_->is_open(); 
    } 
    
    void setSchema(const std::string& db) override { 
        if (!isValid()) return; 
        // PG 中 schema 和 database 是不同概念。通常连接时指定 DB。 
        // 这里假设是切换 search_path 
        try { 
            pqxx::work w(*conn_); 
            w.exec0("SET search_path TO " + db); 
            w.commit(); 
        } catch (...) {} 
    } 
    
    std::unique_ptr<IStatement> createStatement() override { 
        return std::make_unique<PostgreSQLStatement>(conn_.get()); 
    } 
    
    std::unique_ptr<IPreparedStatement> prepareStatement(const std::string& sql) override { 
        // 转换 SQL 占位符：MySQL 使用 ?，PG 使用 $1, $2... 
        // 这是一个复杂的转换，这里简单假设用户如果用 PG 驱动，需要自己写兼容的 SQL 或者我们在 ORM 层统一处理。 
        // 为了演示，我们暂时不处理占位符转换，假设传入的是 $1 格式或者后续完善转换逻辑。 
        return std::make_unique<PostgreSQLPreparedStatement>(conn_.get(), sql); 
    } 

private: 
    std::unique_ptr<pqxx::connection> conn_; 
}; 

} // namespace uORM 
