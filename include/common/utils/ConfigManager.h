#pragma once
#include <string>
namespace uORM {
class ConfigBase {
public:
    virtual ~ConfigBase() = default;
    virtual bool readDataBaseconfig(const std::string& path) = 0;
    virtual bool readJWTconfig(const std::string& path) = 0;
    virtual bool readEmailconfig(const std::string& path) = 0;
    virtual bool readRedisconfig(const std::string& path) = 0;
};
struct DataBaseConfigData {
    std::string hostname;
    int port;
    std::string username;
    std::string password;
    std::string dataname;
    int poolsize;
    bool isValid() const {
        return !hostname.empty() && (port > 0 && port < 65535) && !username.empty() && !password.empty() && !dataname.empty() && poolsize > 0;
    }
};
struct RedisConfigData {
    std::string hostname;
    int port;
    std::string password;
    int poolsize;
    int timeout_seconds;
    int database_index;
    bool isValid() const {
        return !hostname.empty() && (port > 0 && port < 65535) && poolsize > 0 && timeout_seconds >= 0 && database_index >= 0;
    }
};
struct JWTConfigData {};
struct EmailConfigData {};
class ConfigManager : public ConfigBase {
public:
    static ConfigManager& getInstance();
    virtual ~ConfigManager() override = default;
    virtual bool readDataBaseconfig(const std::string& path) override;
    virtual bool readRedisconfig(const std::string& path) override;
    virtual bool readJWTconfig(const std::string& path) override;
    virtual bool readEmailconfig(const std::string& path) override;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
public:
    DataBaseConfigData databaseconfigdata_;
    RedisConfigData redisconfigdata_;
    JWTConfigData jwtconfigdata_;
    EmailConfigData emailconfigdata_;
private:
    ConfigManager() = default;
};
} // namespace uORM
