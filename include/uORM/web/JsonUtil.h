#pragma once
// 文件说明：
// JsonUtil：uJSON 辅助函数 —— SqlValue / QueryResult 与 JSON 互转，
// 以及 Web 层通用的小工具。

#include <uJSON/ujson.h>
#include "uORM/orm/SqlValue.h"
#include "uORM/orm/QueryResult.h"
#include <string>
#include <sstream>
#include <atomic>
#include <chrono>

namespace uORM {
namespace web {

// SqlValue -> uJSON（NULL -> null）
inline uJSON::Value jsonValue(const SqlValue& v) {
    if (auto* i = std::get_if<long long>(&v)) {
        if (*i > 9007199254740992LL || *i < -9007199254740992LL) {
            // 超出 double 安全整数范围：用字符串避免精度丢失
            return uJSON::Value(std::to_string(*i));
        }
        if (*i >= -2147483648LL && *i <= 2147483647LL) {
            return uJSON::Value(static_cast<int>(*i));
        }
        return uJSON::Value(static_cast<double>(*i));
    }
    if (auto* d = std::get_if<double>(&v)) return uJSON::Value(*d);
    if (auto* s = std::get_if<std::string>(&v)) return uJSON::Value(*s);
    return uJSON::Value(nullptr);
}

// QueryResult -> uJSON: {columns:[...], rows:[[...],...], rowCount:N}
inline uJSON::Value jsonResult(const QueryResult& result) {
    auto out = uJSON::Value::object();
    auto cols = uJSON::Value::array();
    for (const auto& c : result.columns) cols.push_back(uJSON::Value(c));
    out["columns"] = cols;

    auto rows = uJSON::Value::array();
    for (const auto& row : result.rows) {
        auto jrow = uJSON::Value::array();
        for (const auto& v : row) jrow.push_back(jsonValue(v));
        rows.push_back(jrow);
    }
    out["rows"] = rows;
    out["rowCount"] = uJSON::Value(static_cast<int>(result.rowCount()));
    return out;
}

inline std::string dump(const uJSON::Value& v) {
    std::ostringstream out;
    out << v;
    return out.str();
}

// 生成简易 id（连接配置标识）
inline std::string genId() {
    static std::atomic<unsigned long long> seq{0};
    unsigned long long n = ++seq;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "c" << std::hex << now << "-" << std::hex << n;
    return out.str();
}

} // namespace web
} // namespace uORM
