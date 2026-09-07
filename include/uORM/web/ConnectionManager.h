#pragma once
// 文件说明：
// ConnectionManager：Web 管理台的连接配置管理。
// - 命名连接配置（driver/host/port/user/password/database/pool）
// - 持久化到 JSON 文件（密码字段可选择性加密的接口预留）
// - 每个配置懒创建一个 DataSource（有界连接池）
// - 列表输出时屏蔽密码

#include "uORM/driver/DataSource.h"
#include "uORM/driver/DriverRegistry.h"
#include "uORM/orm/Error.h"
#include "uORM/web/JsonUtil.h"
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

namespace uORM {
namespace web {

struct ConnectionProfile {
    std::string id;
    std::string name;
    std::string driver;
    std::string host = "127.0.0.1";
    int port = 0;
    std::string username;
    std::string password;
    std::string database;
    int poolSize = 3;
    bool useTLS = false;
};

class ConnectionManager {
public:
    explicit ConnectionManager(std::string storePath) : storePath_(std::move(storePath)) {
        load();
    }

    ~ConnectionManager() {
        save();
    }

    // ---------------- 持久化 ----------------
    void load() {
        std::lock_guard<std::mutex> lock(mutex_);
        profiles_.clear();
        std::ifstream f(storePath_);
        if (!f.is_open()) return;
        try {
            uJSON::Value j;
            f >> j;
            if (!j.is_object() || !j.contains("connections")) return;
            for (const auto& item : j.at("connections").get_array()) {
                ConnectionProfile p;
                p.id = item.at("id").get<std::string>();
                p.name = item.contains("name") ? item.at("name").get<std::string>() : "";
                p.driver = item.at("driver").get<std::string>();
                p.host = item.contains("host") ? item.at("host").get<std::string>() : "127.0.0.1";
                p.port = item.contains("port") ? item.at("port").get<int>() : 0;
                p.username = item.contains("username") ? item.at("username").get<std::string>() : "";
                p.password = item.contains("password") ? item.at("password").get<std::string>() : "";
                p.database = item.contains("database") ? item.at("database").get<std::string>() : "";
                p.poolSize = item.contains("poolSize") ? item.at("poolSize").get<int>() : 3;
                p.useTLS = item.contains("useTLS") ? item.at("useTLS").get<bool>() : false;
                profiles_[p.id] = p;
            }
        } catch (const std::exception& e) {
            std::cerr << "uORM: failed to load connections from " << storePath_ << ": " << e.what() << std::endl;
        }
    }

    void save() {
        std::lock_guard<std::mutex> lock(mutex_);
        uJSON::Value j = uJSON::Value::object();
        auto arr = uJSON::Value::array();
        for (const auto& kv : profiles_) {
            const auto& p = kv.second;
            auto o = uJSON::Value::object();
            o["id"] = uJSON::Value(p.id);
            o["name"] = uJSON::Value(p.name);
            o["driver"] = uJSON::Value(p.driver);
            o["host"] = uJSON::Value(p.host);
            o["port"] = uJSON::Value(p.port);
            o["username"] = uJSON::Value(p.username);
            o["password"] = uJSON::Value(p.password);
            o["database"] = uJSON::Value(p.database);
            o["poolSize"] = uJSON::Value(p.poolSize);
            o["useTLS"] = uJSON::Value(p.useTLS);
            arr.push_back(o);
        }
        j["connections"] = arr;
        std::ofstream f(storePath_);
        if (f.is_open()) f << j;
    }

    // ---------------- CRUD ----------------
    ConnectionProfile add(ConnectionProfile p) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (p.id.empty()) p.id = web::genId();
        profiles_[p.id] = p;
        return p;
    }

    bool update(const std::string& id, const ConnectionProfile& p) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(id);
        if (it == profiles_.end()) return false;
        ConnectionProfile merged = p;
        merged.id = id;
        if (merged.password.empty()) merged.password = it->second.password; // 空密码 = 保留原值
        profiles_[id] = merged;
        dropSourceLocked(id);
        return true;
    }

    bool remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        dropSourceLocked(id);
        return profiles_.erase(id) > 0;
    }

    bool get(const std::string& id, ConnectionProfile& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(id);
        if (it == profiles_.end()) return false;
        out = it->second;
        return true;
    }

    std::vector<ConnectionProfile> list() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ConnectionProfile> out;
        for (const auto& kv : profiles_) out.push_back(kv.second);
        return out;
    }

    // ---------------- 数据源 ----------------
    // 返回 nullptr 表示配置不存在
    std::shared_ptr<DataSource> source(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto pit = profiles_.find(id);
        if (pit == profiles_.end()) return nullptr;
        auto sit = sources_.find(id);
        if (sit != sources_.end()) return sit->second;

        DataSourceConfig cfg;
        const auto& p = pit->second;
        cfg.params.driver = p.driver;
        cfg.params.host = p.host;
        cfg.params.port = p.port;
        cfg.params.username = p.username;
        cfg.params.password = p.password;
        cfg.params.database = p.database;
        cfg.params.useTLS = p.useTLS;
        cfg.poolSize = p.poolSize > 0 ? p.poolSize : 3;
        cfg.name = p.id;

        auto src = std::make_shared<DataSource>(std::move(cfg));
        sources_[id] = src;
        return src;
    }

    void dropSource(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        dropSourceLocked(id);
    }

private:
    void dropSourceLocked(const std::string& id) {
        sources_.erase(id); // DataSource 析构时等待借出的连接归还
    }

    std::string storePath_;
    mutable std::mutex mutex_;
    std::map<std::string, ConnectionProfile> profiles_;
    std::map<std::string, std::shared_ptr<DataSource>> sources_;
};

} // namespace web
} // namespace uORM
