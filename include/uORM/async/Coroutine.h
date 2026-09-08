#pragma once
// 文件说明：
// Coroutine.h —— C++20 协程版异步 API（opt-in，需要 asio 与 C++20）。
//
// 阻塞式驱动调用被卸载到协程线程池线程上执行；协程的组合/恢复/生命周期
// 由 asio::co_spawn 管理（久经验证，避免手写 Task 的生命周期陷阱）。
//
// 用法：
//   asio::awaitable<void> work(uORM::DataSource& ds) {
//       auto rs  = co_await uORM::queryCoro(ds, "SELECT * FROM users WHERE age >= ?",
//                                            {uORM::SqlValue(std::int64_t(18))});
//       co_await uORM::executeCoro(ds, "DELETE FROM logs WHERE old = 1");
//   }
//   auto fut = uORM::spawn(work(ds));   // std::future<void>
//   fut.get();                          // 异常在此重抛
//
// 事务：
//   co_await uORM::transactionCoro(ds, [&](uORM::IConnection& c) {
//       uORM::executeUpdate(c, "INSERT ...");
//   });

#include <asio.hpp>

#include "uORM/driver/DataSource.h"
#include "uORM/orm/QueryResult.h"
#include "uORM/orm/Transaction.h"

#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

#if defined(__cpp_coroutines) || (defined(_MSC_VER) && _MSVC_LANG >= 202002L)
#  define UORM_HAS_COROUTINES 1
#endif

#ifndef UORM_HAS_COROUTINES
#  warning "uORM Coroutine.h 需要 C++20 协程支持（-std=c++20）"
#endif

namespace uORM {

// 协程专用线程池（阻塞式驱动调用在此执行）
inline asio::thread_pool& coroPool() {
    static asio::thread_pool pool(std::max(2u, std::thread::hardware_concurrency()));
    return pool;
}

namespace detail {

// 把阻塞调用卸载到协程线程池线程（协作式让出，完成后在同一线程恢复）
template<typename F>
asio::awaitable<std::invoke_result_t<F>> offload(F fn) {
    co_await asio::post(coroPool(), asio::use_awaitable);
    co_return fn();
}

} // namespace detail

// ---------------- 协程版查询/执行 ----------------
inline asio::awaitable<QueryResult> queryCoro(DataSource& ds, std::string sql,
                                              std::vector<SqlValue> params = {}) {
    co_await asio::post(coroPool(), asio::use_awaitable);
    co_return ds.query(sql, std::move(params));
}

inline asio::awaitable<std::uint64_t> executeCoro(DataSource& ds, std::string sql,
                                                  std::vector<SqlValue> params = {}) {
    co_await asio::post(coroPool(), asio::use_awaitable);
    co_return ds.execute(sql, std::move(params));
}

// 协程版事务：body 在事务连接上执行；异常自动回滚并传播
inline asio::awaitable<void> transactionCoro(DataSource& ds,
                                             std::function<void(IConnection&)> body) {
    co_await asio::post(coroPool(), asio::use_awaitable);
    auto conn = ds.getConnection();
    Transaction tx(*conn);
    try {
        body(*conn);
        tx.commit();
    } catch (...) {
        try { conn->rollback(); } catch (...) {}
        throw;
    }
}

// ---------------- spawn：协程 -> std::future ----------------
// 注意：桥接必须用协程函数（非 lambda 捕获协程）——
// MinGW GCC 上 lambda 捕获 + move-await 会产生未定义行为。
namespace detail {

inline asio::awaitable<void> runAndFulfilVoid(asio::awaitable<void> aw,
                                              std::shared_ptr<std::promise<void>> pr) {
    try {
        co_await std::move(aw);
        pr->set_value();
    } catch (...) {
        pr->set_exception(std::current_exception());
    }
}

template<typename T>
asio::awaitable<void> runAndFulfil(asio::awaitable<T> aw,
                                   std::shared_ptr<std::promise<T>> pr) {
    try {
        pr->set_value(co_await std::move(aw));
    } catch (...) {
        pr->set_exception(std::current_exception());
    }
}

} // namespace detail

// ---------------- spawn：协程 -> std::future ----------------
inline std::future<void> spawn(asio::awaitable<void> aw) {
    auto pr = std::make_shared<std::promise<void>>();
    auto fut = pr->get_future();
    asio::co_spawn(coroPool(), detail::runAndFulfilVoid(std::move(aw), pr), asio::detached);
    return fut;
}

template<typename T>
std::future<T> spawn(asio::awaitable<T> aw) {
    auto pr = std::make_shared<std::promise<T>>();
    auto fut = pr->get_future();
    asio::co_spawn(coroPool(), detail::runAndFulfil(std::move(aw), pr), asio::detached);
    return fut;
}

} // namespace uORM
