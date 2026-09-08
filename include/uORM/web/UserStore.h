#pragma once
// 文件说明：
// 用户存储与会话管理（Web 控制台鉴权）。
// - 三级角色：superadmin（超级管理员）/ admin（管理员）/ user（普通用户）
// - 口令：16 字节随机盐 + SHA-256(salt + password)，仅存哈希
// - 会话：随机 token -> {用户, 角色, 过期时间}，惰性过期清理
// - users.json 持久化用户列表

#include "uORM/web/Sha256.h"
#include <uJSON/ujson.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace uORM {
namespace web {

inline bool isValidRole(const std::string& role) {
    return role == "superadmin" || role == "admin" || role == "user";
}

inline std::string randomHex(std::size_t bytes) {
    static thread_local std::mt19937_64 rng{
        static_cast<std::uint64_t>(std::random_device{}()) *
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes * 2; ++i)
        out += digits[rng() % 16];
    return out;
}

inline std::string hashPassword(const std::string& salt, const std::string& password) {
    return sha256Hex(salt + password);
}

// ---------------------------------------------------------------------------
// 用户存储
// ---------------------------------------------------------------------------
struct UserRecord {
    std::string username;
    std::string salt;
    std::string hash;       // sha256(salt + password)
    std::string role;       // superadmin / admin / user
    std::string createdAt;
};

class UserStore {
public:
    explicit UserStore(std::string storePath) : path_(std::move(storePath)) {
        load();
    }

    void load() {
        std::lock_guard<std::mutex> lock(mutex_);
        users_.clear();
        std::ifstream f(path_);
        if (!f.is_open()) return;
        try {
            uJSON::Value j;
            f >> j;
            if (!j.is_object() || !j.contains("users")) return;
            for (const auto& u : j.at("users").get_array()) {
                UserRecord r;
                r.username = u.at("username").get<std::string>();
                r.salt = u.contains("salt") ? u.at("salt").get<std::string>() : "";
                r.hash = u.contains("hash") ? u.at("hash").get<std::string>() : "";
                r.role = u.contains("role") ? u.at("role").get<std::string>() : "user";
                r.createdAt = u.contains("createdAt") ? u.at("createdAt").get<std::string>() : "";
                users_[r.username] = r;
            }
        } catch (const std::exception& e) {
            std::cerr << "uORM: failed to load users: " << e.what() << "\n";
        }
    }

    void save() {
        std::lock_guard<std::mutex> lock(mutex_);
        uJSON::Value j = uJSON::Value::object();
        auto arr = uJSON::Value::array();
        for (const auto& kv : users_) {
            auto o = uJSON::Value::object();
            o["username"] = uJSON::Value(kv.second.username);
            o["salt"] = uJSON::Value(kv.second.salt);
            o["hash"] = uJSON::Value(kv.second.hash);
            o["role"] = uJSON::Value(kv.second.role);
            o["createdAt"] = uJSON::Value(kv.second.createdAt);
            arr.push_back(o);
        }
        j["users"] = arr;
        std::ofstream f(path_);
        if (f.is_open()) f << j;
    }

    bool add(const std::string& username, const std::string& password,
             const std::string& role, std::string* err = nullptr) {
        if (!isValidRole(role)) { if (err) *err = "invalid role"; return false; }
        std::lock_guard<std::mutex> lock(mutex_);
        if (username.empty() || users_.count(username)) {
            if (err) *err = "username empty or exists";
            return false;
        }
        UserRecord r;
        r.username = username;
        r.salt = randomHex(16);
        r.hash = hashPassword(r.salt, password);
        r.role = role;
        r.createdAt = nowString();
        users_[username] = r;
        return true;
    }

    bool verify(const std::string& username, const std::string& password) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return false;
        return it->second.hash == hashPassword(it->second.salt, password);
    }

    bool setPassword(const std::string& username, const std::string& newPassword) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return false;
        it->second.salt = randomHex(16);
        it->second.hash = hashPassword(it->second.salt, newPassword);
        return true;
    }

    bool setRole(const std::string& username, const std::string& role) {
        if (!isValidRole(role)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return false;
        it->second.role = role;
        return true;
    }

    bool remove(const std::string& username) {
        std::lock_guard<std::mutex> lock(mutex_);
        return users_.erase(username) > 0;
    }

    bool exists(const std::string& username) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return users_.count(username) != 0;
    }

    std::string roleOf(const std::string& username) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(username);
        return it == users_.end() ? "" : it->second.role;
    }

    std::vector<UserRecord> list() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<UserRecord> out;
        for (const auto& kv : users_) out.push_back(kv.second);
        return out;
    }

    bool hasSuperAdmin() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : users_)
            if (kv.second.role == "superadmin") return true;
        return false;
    }

private:
    static std::string nowString() {
        std::time_t t = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        return buf;
    }

    std::string path_;
    mutable std::mutex mutex_;
    std::map<std::string, UserRecord> users_;
};

// ---------------------------------------------------------------------------
// 会话管理（内存态；重启后需重新登录）
// ---------------------------------------------------------------------------
class SessionManager {
public:
    struct Session {
        std::string username;
        std::string role;
        std::chrono::steady_clock::time_point expiry;
    };

    // 默认会话有效期 24 小时（滑动续期）
    std::string create(const std::string& username, const std::string& role,
                       std::chrono::hours ttl = std::chrono::hours(24)) {
        std::string token = randomHex(32);
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[token] = Session{username, role, std::chrono::steady_clock::now() + ttl};
        return token;
    }

    // 校验并滑动续期；无效/过期返回空 Session
    Session validate(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) return {};
        if (std::chrono::steady_clock::now() >= it->second.expiry) {
            sessions_.erase(it);
            return {};
        }
        it->second.expiry = std::chrono::steady_clock::now() + std::chrono::hours(24);
        return it->second;
    }

    void revoke(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(token);
    }

private:
    std::mutex mutex_;
    std::map<std::string, Session> sessions_;
};

} // namespace web
} // namespace uORM
