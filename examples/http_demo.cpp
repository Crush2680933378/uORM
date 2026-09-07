#include <uORM/web/HttpServer.h>
#include <iostream>

// HTTP 服务最小演示：路由、参数、JSON、静态兜底
int main(int argc, char** argv) {
    unsigned short port = argc > 1 ? static_cast<unsigned short>(std::atoi(argv[1])) : 8080;

    uORM::web::Router router;

    router.get("/", [](const uORM::web::HttpRequest&) {
        return uORM::web::HttpResponse::html("<h1>uORM HTTP server</h1><p>see /api/hello</p>");
    });

    router.get("/api/hello", [](const uORM::web::HttpRequest& req) {
        std::string name = req.queryParam("name");
        if (name.empty()) name = "world";
        return uORM::web::HttpResponse::json("{\"message\":\"hello, " + name + "\"}");
    });

    router.post("/api/echo", [](const uORM::web::HttpRequest& req) {
        return uORM::web::HttpResponse::text("echo: " + req.body);
    });

    router.get("/api/user/{id}", [](const uORM::web::HttpRequest& req) {
        return uORM::web::HttpResponse::json("{\"id\":\"" + req.params.at("id") + "\"}");
    });

    uORM::web::HttpServer::Config cfg;
    cfg.port = port;
    uORM::web::HttpServer server(router, cfg);
    server.run();
    return 0;
}
