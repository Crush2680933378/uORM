#pragma once 
#include <string> 
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace uORM { 

// 驱动类型枚举
enum class DriverType {
    MySQL,
    PostgreSQL
};

// 配置基类，定义了读取各种配置文件的接口
class ConfigBase { 
public: 
    virtual ~ConfigBase() = default; 
    // 读取数据库配置
    virtual bool readDataBaseconfig(const std::string& path) = 0; 
    // 读取JWT配置
    virtual bool readJWTconfig(const std::string& path) = 0; 
    // 读取邮件服务配置
    virtual bool readEmailconfig(const std::string& path) = 0; 
    // 读取Redis配置
    virtual bool readRedisconfig(const std::string& path) = 0; 
}; 

// 数据库配置数据结构
struct DataBaseConfigData { 
    DriverType driver_type = DriverType::MySQL; // 默认为 MySQL
    std::string hostname; // 主机地址
    int port;             // 端口号
    std::string username; // 用户名
    std::string password; // 密码
    std::string dataname; // 数据库名
    int poolsize;         // 连接池大小
    
    // 检查配置是否有效
    bool isValid() const { 
        return !hostname.empty() && (port > 0 && port < 65535) && !username.empty() && !password.empty() && !dataname.empty() && poolsize > 0; 
    } 
}; 

// Redis配置数据结构
struct RedisConfigData { 
    std::string hostname; // 主机地址
    int port;             // 端口号
    std::string password; // 密码
    int poolsize;         // 连接池大小
    int timeout_seconds;  // 超时时间(秒)
    int database_index;   // 数据库索引
    
    // 检查配置是否有效
    bool isValid() const { 
        return !hostname.empty() && (port > 0 && port < 65535) && poolsize > 0 && timeout_seconds >= 0 && database_index >= 0; 
    } 
}; 

// JWT配置数据结构 (待实现)
struct JWTConfigData {}; 
// 邮件配置数据结构 (待实现)
struct EmailConfigData {}; 

// 配置管理器，单例模式，负责加载和保存系统配置
class ConfigManager : public ConfigBase { 
public: 
    // 获取单例实例
    static ConfigManager& getInstance() {
        static ConfigManager instance;
        return instance;
    }

    virtual ~ConfigManager() override = default; 
    
    // 实现基类接口，从文件读取配置
    virtual bool readDataBaseconfig(const std::string& path) override {
        std::ifstream f(path); 
        if (!f.is_open()) return false; 
        try { 
            json j; 
            f >> j; 
            f.close(); 
            
            // 检查JSON结构是否包含必要的字段
            if (!j.contains("DataBaseConfig")) return false; 
            if (!j.at("DataBaseConfig").is_object()) return false; 
            const json db = j.at("DataBaseConfig"); 
            
            // 验证每个配置项的存在性和类型
            if (!db.contains("hostname") || !db.at("hostname").is_string()) return false; 
            if (!db.contains("username") || !db.at("username").is_string()) return false; 
            if (!db.contains("password") || !db.at("password").is_string()) return false; 
            if (!db.contains("dataname") || !db.at("dataname").is_string()) return false; 
            if (!db.contains("port") || !db.at("port").is_number_integer()) return false; 
            if (!db.contains("poolsize") || !db.at("poolsize").is_number_integer()) return false; 

            // 读取驱动类型，默认为 mysql
            if (db.contains("driver") && db.at("driver").is_string()) {
                std::string drv = db.at("driver").get<std::string>();
                if (drv == "postgres" || drv == "postgresql") {
                    databaseconfigdata_.driver_type = DriverType::PostgreSQL;
                } else {
                    databaseconfigdata_.driver_type = DriverType::MySQL;
                }
            } else {
                databaseconfigdata_.driver_type = DriverType::MySQL;
            }

            // 填充配置数据
            databaseconfigdata_.hostname = db.at("hostname").get<std::string>(); 
            databaseconfigdata_.port = db.at("port").get<int>(); 
            databaseconfigdata_.username = db.at("username").get<std::string>(); 
            databaseconfigdata_.password = db.at("password").get<std::string>(); 
            databaseconfigdata_.dataname = db.at("dataname").get<std::string>(); 
            databaseconfigdata_.poolsize = db.at("poolsize").get<int>(); 
            
            return databaseconfigdata_.isValid(); 
        } catch (...) { 
            return false; 
        } 
    }

    virtual bool readRedisconfig(const std::string& path) override {
        std::ifstream f(path); 
        if (!f.is_open()) return false; 
        try { 
            json j; 
            f >> j; 
            f.close(); 
            
            // 检查JSON结构是否包含必要的字段
            if (!j.contains("RedisConfig")) return false; 
            if (!j.at("RedisConfig").is_object()) return false; 
            const json r = j.at("RedisConfig"); 
            
            // 验证每个配置项的存在性和类型
            if (!r.contains("hostname") || !r.at("hostname").is_string()) return false; 
            if (!r.contains("password") || !r.at("password").is_string()) return false; 
            if (!r.contains("port") || !r.at("port").is_number_integer()) return false; 
            if (!r.contains("poolsize") || !r.at("poolsize").is_number_integer()) return false; 
    
            // 填充配置数据
            redisconfigdata_.hostname = r.at("hostname").get<std::string>(); 
            redisconfigdata_.port = r.at("port").get<int>(); 
            redisconfigdata_.password = r.at("password").get<std::string>(); 
            redisconfigdata_.poolsize = r.at("poolsize").get<int>(); 
            
            return redisconfigdata_.isValid(); 
        } catch (...) { 
            return false; 
        } 
    }

    virtual bool readJWTconfig(const std::string& path) override {
        return true; 
    }

    virtual bool readEmailconfig(const std::string& path) override {
        return true; 
    }
    
    // 禁止拷贝和赋值
    ConfigManager(const ConfigManager&) = delete; 
    ConfigManager& operator=(const ConfigManager&) = delete; 

public: 
    // 公开的配置数据成员
    DataBaseConfigData databaseconfigdata_; 
    RedisConfigData redisconfigdata_; 
    JWTConfigData jwtconfigdata_; 
    EmailConfigData emailconfigdata_; 

private: 
    // 私有构造函数
    ConfigManager() = default; 
}; 
} // namespace uORM 
