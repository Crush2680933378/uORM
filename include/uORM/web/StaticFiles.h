#pragma once
// 文件说明：
// StaticFiles：静态文件托管（Web 控制台前端 dist 目录）。
// - 按扩展名推断 Content-Type
// - SPA fallback：非 API 路径回落到 index.html（前端路由刷新可用）

#include "uORM/web/Http.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <map>
#include <functional>

namespace uORM {
namespace web {

class StaticFiles {
public:
    explicit StaticFiles(std::string root) : root_(std::move(root)) {}

    // 尝试服务静态文件；找不到返回 404（调用方决定是否 SPA fallback）
    HttpResponse serve(const std::string& urlPath) const {
        std::string rel = urlPath;
        if (rel == "/" || rel.empty()) rel = "/index.html";
        // 防目录穿越
        if (rel.find("..") != std::string::npos) {
            return HttpResponse::error(403, "forbidden");
        }
        std::string full = root_ + rel;

        std::ifstream f(full, std::ios::binary);
        if (!f.is_open()) {
            return HttpResponse::error(404, "not found");
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        HttpResponse resp;
        resp.status = 200;
        resp.body = ss.str();
        resp.headers["Content-Type"] = mimeOf(full);
        resp.headers["Cache-Control"] = "no-cache";
        return resp;
    }

    // SPA fallback：文件不存在时返回 index.html（用于前端路由）
    HttpResponse serveWithFallback(const std::string& urlPath) const {
        HttpResponse r = serve(urlPath);
        if (r.status == 404) {
            r = serve("/index.html");
        }
        return r;
    }

private:
    static const char* mimeOf(const std::string& path) {
        auto endsWith = [&path](const char* suffix) {
            std::size_t n = std::strlen(suffix);
            return path.size() >= n && path.compare(path.size() - n, n, suffix) == 0;
        };
        if (endsWith(".html") || endsWith(".htm")) return "text/html; charset=utf-8";
        if (endsWith(".js")) return "application/javascript; charset=utf-8";
        if (endsWith(".css")) return "text/css; charset=utf-8";
        if (endsWith(".json")) return "application/json; charset=utf-8";
        if (endsWith(".svg")) return "image/svg+xml";
        if (endsWith(".png")) return "image/png";
        if (endsWith(".jpg") || endsWith(".jpeg")) return "image/jpeg";
        if (endsWith(".gif")) return "image/gif";
        if (endsWith(".ico")) return "image/x-icon";
        if (endsWith(".woff")) return "font/woff";
        if (endsWith(".woff2")) return "font/woff2";
        return "application/octet-stream";
    }

    std::string root_;
};

} // namespace web
} // namespace uORM
