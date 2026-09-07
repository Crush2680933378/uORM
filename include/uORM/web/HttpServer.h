#pragma once
// 文件说明：
// HttpServer：基于 standalone asio 的轻量 HTTP/1.1 服务。
// - 每连接一线程 + keep-alive（管理台规模下最稳的模型）
// - 统一异常 -> 500 JSON；路由匹配由 Router 完成
// - 请求体大小上限（默认 8MB，防滥用）

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <asio.hpp>

#include "uORM/web/Http.h"
#include "uORM/web/Router.h"

#include <atomic>
#include <thread>
#include <functional>
#include <iostream>
#include <sstream>

namespace uORM {
namespace web {

class HttpServer {
public:
    struct Config {
        std::string address = "0.0.0.0";
        unsigned short port = 8080;
        std::size_t maxBodySize = 8 * 1024 * 1024;
        int keepAliveTimeoutSec = 65;
    };

    explicit HttpServer(Router& router, Config cfg) : router_(router), config_(std::move(cfg)) {}

    // 阻塞运行（内部启动 IO 线程后 join）
    void run() {
        runAsync();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

    // 后台运行（用于测试或嵌入既有主循环）
    void runAsync() {
        running_ = true;
        ioc_ = std::make_unique<asio::io_context>();
        acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(
            *ioc_, asio::ip::tcp::endpoint(asio::ip::make_address(config_.address), config_.port));
        std::cout << "uORM web server listening on " << config_.address << ":" << config_.port << std::endl;
        scheduleAccept();
        unsigned int threads = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned int i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { ioc_->run(); });
        }
    }

    void stop() {
        running_ = false;
        if (ioc_) ioc_->stop();
        for (auto& t : workers_) if (t.joinable()) t.join();
        workers_.clear();
    }

    unsigned short port() const { return config_.port; }

private:
    // 异步接受循环：让 io_context 始终持有工作
    void scheduleAccept() {
        auto socket = std::make_shared<asio::ip::tcp::socket>(ioc_->get_executor());
        acceptor_->async_accept(*socket, [this, socket](const asio::error_code& ec) {
            if (!running_.load()) return;
            if (!ec) {
                asio::ip::tcp::socket s = std::move(*socket);
                std::thread([this, ss = std::move(s)]() mutable {
                    handleConnection(std::move(ss));
                }).detach();
            } else {
                std::cerr << "accept error: " << ec.message() << std::endl;
            }
            scheduleAccept();
        });
    }

    void handleConnection(asio::ip::tcp::socket socket) {
        try {
            asio::socket_base::keep_alive option(true);
            socket.set_option(option);

            std::string buffer;
            char chunk[8192];
            for (;;) {
                HttpRequest req;
                std::size_t headerEnd;

                // 1. 读到完整头部
                for (;;) {
                    headerEnd = buffer.find("\r\n\r\n");
                    if (headerEnd != std::string::npos) break;
                    asio::error_code ec;
                    std::size_t n = socket.read_some(asio::buffer(chunk), ec);
                    if (ec) return;
                    buffer.append(chunk, n);
                    if (buffer.size() > config_.maxBodySize) return;
                }

                // 2. 解析请求行与头部
                if (!parseHead(buffer.substr(0, headerEnd), req)) {
                    auto resp = HttpResponse::error(400, "malformed request");
                    writeAll(socket, resp.serialize(false));
                    return;
                }

                // 3. 读 body（Content-Length）
                std::size_t contentLength = 0;
                std::string cl = req.header("Content-Length");
                if (!cl.empty()) contentLength = static_cast<std::size_t>(std::strtoull(cl.c_str(), nullptr, 10));
                if (contentLength > config_.maxBodySize) {
                    auto resp = HttpResponse::error(413, "payload too large");
                    writeAll(socket, resp.serialize(false));
                    return;
                }
                while (buffer.size() < headerEnd + 4 + contentLength) {
                    asio::error_code ec;
                    std::size_t n = socket.read_some(asio::buffer(chunk), ec);
                    if (ec) return;
                    buffer.append(chunk, n);
                }
                req.body = buffer.substr(headerEnd + 4, contentLength);
                buffer.erase(0, headerEnd + 4 + contentLength);

                // 4. 路由分发
                HttpResponse resp;
                try {
                    resp = router_.dispatch(req);
                } catch (const std::exception& e) {
                    resp = HttpResponse::error(500, e.what());
                }

                bool keepAlive = (req.header("Connection") != "close");
                writeAll(socket, resp.serialize(keepAlive));
                if (!keepAlive) return;
                // keep-alive：继续下一轮（可能已积累 pipelined 数据）
            }
        } catch (const std::exception&) {
            // 连接异常：直接关闭
        }
        asio::error_code ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }

    static bool parseHead(const std::string& head, HttpRequest& req) {
        std::istringstream in(head);
        std::string line;
        if (!std::getline(in, line)) return false;
        {
            std::istringstream first(line);
            if (!(first >> req.method >> req.path)) return false;
        }
        // 分离 query string
        auto qpos = req.path.find('?');
        if (qpos != std::string::npos) {
            req.query = req.path.substr(qpos + 1);
            req.path = req.path.substr(0, qpos);
        }
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(0, 1);
            req.headers[key] = value;
        }
        return true;
    }

    static void writeAll(asio::ip::tcp::socket& socket, const std::string& data) {
        asio::error_code ec;
        asio::write(socket, asio::buffer(data), ec);
    }

    Router& router_;
    Config config_;
    std::unique_ptr<asio::io_context> ioc_;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
};

} // namespace web
} // namespace uORM
