#pragma once 
#include <string> 
#include <memory> 
#include <vector> 

namespace uORM { 

// 前置声明 
class IResultSet; 
class IPreparedStatement; 
class IStatement; 

// 数据库连接接口 
class IConnection { 
public: 
    virtual ~IConnection() = default; 
    virtual bool isValid() = 0; 
    virtual void setSchema(const std::string& db) = 0; 
    virtual std::unique_ptr<IStatement> createStatement() = 0; 
    virtual std::unique_ptr<IPreparedStatement> prepareStatement(const std::string& sql) = 0; 
    // 可以添加 commit, rollback 等事务接口 
}; 

// 结果集接口 
class IResultSet { 
public: 
    virtual ~IResultSet() = default; 
    virtual bool next() = 0; 
    
    // 获取值的接口 
    virtual int getInt(const std::string& colName) = 0; 
    virtual long long getInt64(const std::string& colName) = 0; 
    virtual unsigned int getUInt(const std::string& colName) = 0; 
    virtual std::string getString(const std::string& colName) = 0; 
    virtual bool getBoolean(const std::string& colName) = 0; 
    virtual double getDouble(const std::string& colName) = 0; 
}; 

// 普通语句接口 
class IStatement { 
public: 
    virtual ~IStatement() = default; 
    virtual void execute(const std::string& sql) = 0; 
    virtual std::unique_ptr<IResultSet> executeQuery(const std::string& sql) = 0; 
}; 

// 预编译语句接口 
class IPreparedStatement { 
public: 
    virtual ~IPreparedStatement() = default; 
    virtual void executeUpdate() = 0; 
    virtual std::unique_ptr<IResultSet> executeQuery() = 0; 
    
    // 绑定参数接口 
    virtual void setInt(int index, int val) = 0; 
    virtual void setInt64(int index, long long val) = 0; 
    virtual void setUInt(int index, unsigned int val) = 0; 
    virtual void setString(int index, const std::string& val) = 0; 
    virtual void setBoolean(int index, bool val) = 0; 
    virtual void setDouble(int index, double val) = 0; 
}; 

} // namespace uORM 
