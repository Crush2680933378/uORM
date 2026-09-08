#pragma once
// 文件说明：
// AuditLog —— 用户操作日志（审计日志）。
// - JSONL 追加写：一行一条 JSON，线程安全，带写入轮转（超过 8MB 换 .1 文件）
// - query()：按用户/动作/关键字过滤，返回最新优先的分页结果
//
// 典型条目：登录成功/失败、SQL 执行（含语句摘要）、连接与用户管理操作、
// 每个被拒绝的请求（权限不足）等。

#include <uJSON/ujson.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace uORM {
namespace web {

struct AuditEntry {
    std::string time;      // "YYYY-MM-DD HH:MM:SS"
    std::string user;      // 用户名（未登录为 "-"）
    std::string role;      // 角色
    std::string action;    // 动作类型（login / sql.execute / conn.create / ...）
    std::string target;    // 操作目标（连接 id / 表名 / 用户名 ...）
    std::string status;    // "ok" / "denied" / "error"
    std::string detail;    // 摘要或错误信息
    std::string ip;        // 客户端 IP
};

class AuditLog {
public:
    explicit AuditLog(std::string path, std::size_t rotateBytes = 8 * 1024 * 1024)
        : path_(std::move(path)), rotateBytes_(rotateBytes) {}

    // 追加一条日志（线程安全；文件超过阈值时轮转为 <path>.1）；time 留空自动填充
    void append(AuditEntry e) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (e.time.empty()) {
            std::time_t t = std::time(nullptr);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            e.time = buf;
        }
        if (entryCount_ == 0 || fileSizeExceeds()) rotateIfPossible();

        std::ofstream f(path_, std::ios::app);
        if (!f.is_open()) return;
        f << "{\"time\":\"" << escape(e.time) << "\",\"user\":\"" << escape(e.user)
          << "\",\"role\":\"" << escape(e.role) << "\",\"action\":\"" << escape(e.action)
          << "\",\"target\":\"" << escape(e.target) << "\",\"status\":\"" << escape(e.status)
          << "\",\"detail\":\"" << escape(e.detail) << "\",\"ip\":\"" << escape(e.ip) << "\"}\n";
        ++entryCount_;
    }

    // 查询：按用户/动作前缀/状态过滤，返回最新优先的分页结果
    struct Query {
        std::string user;
        std::string actionPrefix;
        std::string status;
        std::size_t limit = 200;
        std::size_t offset = 0;
    };

    std::vector<AuditEntry> query() const { return query(Query{}); }

    std::vector<AuditEntry> query(const Query& q) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AuditEntry> all;
        std::ifstream f(path_);
        if (!f.is_open()) return all;
        std::string line;
        while (std::getline(f, line)) {
            if (line.size() < 2) continue;
            // 简易 JSONL 解析：借助 uJSON
            auto j = uJSON::Value::parse(line);
            AuditEntry e;
            e.time = j.contains("time") ? j.at("time").get<std::string>() : "";
            e.user = j.contains("user") ? j.at("user").get<std::string>() : "";
            e.role = j.contains("role") ? j.at("role").get<std::string>() : "";
            e.action = j.contains("action") ? j.at("action").get<std::string>() : "";
            e.target = j.contains("target") ? j.at("target").get<std::string>() : "";
            e.status = j.contains("status") ? j.at("status").get<std::string>() : "";
            e.detail = j.contains("detail") ? j.at("detail").get<std::string>() : "";
            e.ip = j.contains("ip") ? j.at("ip").get<std::string>() : "";
            all.push_back(std::move(e));
        }

        // 过滤（最新的在前）
        std::vector<AuditEntry> filtered;
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            if (!q.user.empty() && it->user != q.user) continue;
            if (!q.actionPrefix.empty() && it->action.rfind(q.actionPrefix, 0) != 0) continue;
            if (!q.status.empty() && it->status != q.status) continue;
            filtered.push_back(*it);
        }
        if (q.offset >= filtered.size()) return {};
        std::vector<AuditEntry> out(
            filtered.begin() + static_cast<long>(q.offset),
            filtered.begin() + static_cast<long>(std::min(q.offset + q.limit, filtered.size())));
        return out;
    }

private:
    bool fileSizeExceeds() const {
        std::error_code ec;
        auto size = std::filesystem::file_size(path_, ec);
        return !ec && size > rotateBytes_;
    }

    void rotateIfPossible() {
        std::error_code ec;
        std::string rotated = path_ + ".1";
        std::filesystem::rename(path_, rotated, ec);
        entryCount_ = 0;
    }

    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char ch : s) {
            switch (ch) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += ch; break;
            }
        }
        return out;
    }

    std::string path_;
    std::size_t rotateBytes_;
    std::size_t entryCount_ = 0;
    mutable std::mutex mutex_;
};

} // namespace web
} // namespace uORM
