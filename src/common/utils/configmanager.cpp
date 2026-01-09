#include "common/utils/ConfigManager.h"
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace uORM {
ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}
bool ConfigManager::readDataBaseconfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j;
        f >> j;
        f.close();
        if (!j.contains("DataBaseConfig")) return false;
        if (!j.at("DataBaseConfig").is_object()) return false;
        const json db = j.at("DataBaseConfig");
        if (!db.at("hostname").is_string()) return false;
        if (!db.at("username").is_string()) return false;
        if (!db.at("password").is_string()) return false;
        if (!db.at("dataname").is_string()) return false;
        if (!db.at("port").is_number_integer()) return false;
        if (!db.at("poolsize").is_number_integer()) return false;
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
bool ConfigManager::readRedisconfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j;
        f >> j;
        f.close();
        if (!j.contains("RedisConfig")) return false;
        if (!j.at("RedisConfig").is_object()) return false;
        const json r = j.at("RedisConfig");
        if (!r.at("hostname").is_string()) return false;
        if (!r.at("password").is_string()) return false;
        if (!r.at("port").is_number_integer()) return false;
        if (!r.at("poolsize").is_number_integer()) return false;
        redisconfigdata_.hostname = r.at("hostname").get<std::string>();
        redisconfigdata_.port = r.at("port").get<int>();
        redisconfigdata_.password = r.at("password").get<std::string>();
        redisconfigdata_.poolsize = r.at("poolsize").get<int>();
        return redisconfigdata_.isValid();
    } catch (...) {
        return false;
    }
}
bool ConfigManager::readJWTconfig(const std::string& path) {
    return true;
}
bool ConfigManager::readEmailconfig(const std::string& path) {
    return true;
}
} // namespace uORM
