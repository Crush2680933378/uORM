#pragma once
// 文件说明：
// Router：方法 + 路径模式匹配的路由表，支持 {param} 占位符。
//   router.get("/api/connections/{id}/tables", handler);
// 处理器签名：HttpResponse(const HttpRequest&)；抛出异常统一转 500。

#include "uORM/web/Http.h"
#include <functional>
#include <vector>
#include <sstream>

namespace uORM {
namespace web {

using Handler = std::function<HttpResponse(const HttpRequest&)>;

class Router {
public:
    void get(const std::string& pattern, Handler h)   { routes_.push_back({"GET", pattern, std::move(h)}); }
    void post(const std::string& pattern, Handler h)  { routes_.push_back({"POST", pattern, std::move(h)}); }
    void put(const std::string& pattern, Handler h)   { routes_.push_back({"PUT", pattern, std::move(h)}); }
    void del(const std::string& pattern, Handler h)   { routes_.push_back({"DELETE", pattern, std::move(h)}); }
    void route(const std::string& method, const std::string& pattern, Handler h) {
        routes_.push_back({method, pattern, std::move(h)});
    }

    // 静态资源兜底（静态文件/SPA fallback 在 webconsole 里用 route() 挂载）
    HttpResponse dispatch(const HttpRequest& req) const {
        for (const auto& r : routes_) {
            if (r.method != req.method) continue;
            std::map<std::string, std::string> params;
            if (match(r.pattern, req.path, params)) {
                HttpRequest copy = req;
                copy.params = std::move(params);
                return r.handler(copy);
            }
        }
        return HttpResponse::error(404, "no route: " + req.method + " " + req.path);
    }

    // 模式匹配：/api/{id}/tables 匹配 /api/3/tables
    static bool match(const std::string& pattern, const std::string& path,
                      std::map<std::string, std::string>& params) {
        auto pat = split(pattern);
        auto pth = split(path);
        if (pat.size() != pth.size()) return false;
        for (std::size_t i = 0; i < pat.size(); ++i) {
            const auto& p = pat[i];
            if (!p.empty() && p.front() == '{' && p.back() == '}') {
                params[p.substr(1, p.size() - 2)] = HttpRequest::urlDecode(pth[i]);
            } else if (p != pth[i]) {
                return false;
            }
        }
        return true;
    }

private:
    struct Route {
        std::string method;
        std::string pattern;
        Handler handler;
    };

    static std::vector<std::string> split(const std::string& path) {
        std::vector<std::string> out;
        std::string item;
        std::istringstream in(path);
        while (std::getline(in, item, '/')) {
            if (!item.empty()) out.push_back(item);
        }
        if (path == "/") out.push_back("");
        return out;
    }

    std::vector<Route> routes_;
};

} // namespace web
} // namespace uORM
