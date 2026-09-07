#pragma once
// 文件说明：
// Http.h：HTTP 请求/响应数据结构与序列化辅助（配合 HttpServer 使用）

#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cctype>

namespace uORM {
namespace web {

struct HttpRequest {
    std::string method;
    std::string path;                      // 不含 query string
    std::string query;                     // 原始 query string
    std::map<std::string, std::string> headers;
    std::string body;

    // 覆盖同名的路由参数（由 Router 填充 {param} 占位符）
    std::map<std::string, std::string> params;

    std::string header(const std::string& name) const {
        auto it = headers.find(name);
        return it == headers.end() ? "" : it->second;
    }

    // query 参数解析（简单 key=value&...，值做 URL 解码）
    std::string queryParam(const std::string& key) const {
        std::string::size_type pos = 0;
        while (pos <= query.size()) {
            auto eq = query.find('=', pos);
            auto amp = query.find('&', pos);
            if (amp == std::string::npos) amp = query.size();
            std::string k = (eq == std::string::npos || eq > amp) ? query.substr(pos, amp - pos)
                                                                  : query.substr(pos, eq - pos);
            std::string v = (eq == std::string::npos || eq > amp) ? "" : query.substr(eq + 1, amp - eq - 1);
            if (k == key) return urlDecode(v);
            pos = amp + 1;
        }
        return "";
    }

    static std::string urlDecode(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size()) {
                out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else if (s[i] == '+') {
                out += ' ';
            } else {
                out += s[i];
            }
        }
        return out;
    }

    static std::string urlEncode(const std::string& s) {
        std::ostringstream out;
        out << std::hex;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out << c;
            else out << '%' << std::uppercase << static_cast<int>(c) << std::nouppercase;
        }
        return out.str();
    }
};

struct HttpResponse {
    int status = 200;
    std::map<std::string, std::string> headers;
    std::string body;

    static HttpResponse text(const std::string& body, int status = 200) {
        HttpResponse r;
        r.status = status;
        r.body = body;
        r.headers["Content-Type"] = "text/plain; charset=utf-8";
        return r;
    }

    static HttpResponse json(const std::string& body, int status = 200) {
        HttpResponse r;
        r.status = status;
        r.body = body;
        r.headers["Content-Type"] = "application/json; charset=utf-8";
        return r;
    }

    static HttpResponse html(const std::string& body, int status = 200) {
        HttpResponse r;
        r.status = status;
        r.body = body;
        r.headers["Content-Type"] = "text/html; charset=utf-8";
        return r;
    }

    static HttpResponse error(int status, const std::string& message) {
        HttpResponse r = json("{\"error\":\"\"}", status);
        // 简单转义引号/反斜杠
        std::string safe;
        for (char c : message) {
            if (c == '"' || c == '\\') safe += '\\';
            if (c == '\n') { safe += "\\n"; continue; }
            if (c == '\r') continue;
            safe += c;
        }
        r.body = "{\"error\":\"" + safe + "\"}";
        return r;
    }

    static HttpResponse redirect(const std::string& location) {
        HttpResponse r;
        r.status = 302;
        r.headers["Location"] = location;
        return r;
    }

    std::string serialize(bool keepAlive) const {
        std::ostringstream out;
        out << "HTTP/1.1 " << status << " " << statusText(status) << "\r\n";
        bool hasType = false;
        for (const auto& kv : headers) {
            if (kv.first == "Content-Type") hasType = true;
            out << kv.first << ": " << kv.second << "\r\n";
        }
        if (!hasType) out << "Content-Type: text/plain; charset=utf-8\r\n";
        out << "Content-Length: " << body.size() << "\r\n";
        out << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
        out << "\r\n";
        out << body;
        return out.str();
    }

    static const char* statusText(int code) {
        switch (code) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 301: return "Moved Permanently";
            case 302: return "Found";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 409: return "Conflict";
            case 413: return "Payload Too Large";
            case 500: return "Internal Server Error";
            case 502: return "Bad Gateway";
            default: return "Unknown";
        }
    }
};

} // namespace web
} // namespace uORM
