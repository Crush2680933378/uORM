#pragma once
// =====================================================================
// Service 层：业务编排（跨 DAO 的事务都在这里）
//
// 规则：
//   - Controller/UI 只调用 Service，不直接碰 DAO/SQL
//   - Service 决定事务边界：一个业务动作 = 一个事务
//   - 异常向上传播，由最外层转换为用户可读的响应
// =====================================================================
#include <stdexcept>
#include <string>

#include "dao/OrderDao.hpp"   // 含 OrderDao 与 ProductDao
#include "dao/UserDao.hpp"

namespace demo {

// 业务异常（Service 层语义错误）
class BusinessError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---- 用户服务 ----
class UserService {
public:
    explicit UserService(uORM::DataSource& ds) : dao_(ds) {}

    // 注册：用户名/邮箱唯一性检查 + 插入
    std::int64_t registerUser(const std::string& username,
                              const std::string& email, int age) {
        if (auto exist = dao_.findByUsername(username))
            throw BusinessError("用户名已存在: " + username);
        if (auto exist = dao_.findByEmail(email))
            throw BusinessError("邮箱已被使用: " + email);

        User u{0, username, email, age, true, ""};
        dao_.insert(u);             // 自增 id 写回 u.id
        return u.id;
    }

    // 停用账号（部分更新 + 语义命名）
    bool deactivate(std::int64_t userId) {
        auto u = dao_.findById(userId);
        if (!u) throw BusinessError("用户不存在");
        u->active = false;
        return dao_.updateFields(*u, &User::active);
    }

    long long activeCount() { return dao_.countActive(); }

    std::vector<User> list(int page, int pageSize) {
        return dao_.findPage(page, pageSize, "id");
    }

    std::optional<User> get(std::int64_t id) { return dao_.findById(id); }

private:
    UserDao dao_;
};

// ---- 订单服务：跨 DAO 事务（扣库存 + 建订单 原子完成）----
class OrderService {
public:
    explicit OrderService(uORM::DataSource& ds)
        : ds_(ds), productDao_(ds), orderDao_(ds) {}

    // 下单：原子扣库存 + 创建订单，任一步失败整体回滚（防超卖）
    std::int64_t placeOrder(std::int64_t userId, std::int64_t productId, int qty) {
        Product product;
        if (auto p = productDao_.findById(productId)) product = *p;
        else throw BusinessError("商品不存在");
        if (!product.on_sale) throw BusinessError("商品已下架");
        double amount = product.price * qty;

        return uORM::withTransaction(ds_, [&](uORM::IConnection& c) -> std::int64_t {
            // 1. 原子扣库存（stock >= qty 条件防止超卖与负库存）
            auto upd = "UPDATE products SET stock = stock - ? WHERE id = ? AND stock >= ?";
            auto stmt = c.prepareStatement(upd);
            uORM::bindSqlValue(stmt.get(), 1, uORM::SqlValue(static_cast<long long>(qty)));
            uORM::bindSqlValue(stmt.get(), 2, uORM::SqlValue(productId));
            uORM::bindSqlValue(stmt.get(), 3, uORM::SqlValue(static_cast<long long>(qty)));
            std::uint64_t affected = stmt->executeUpdate();
            if (affected == 0) throw BusinessError("库存不足");

            // 2. 创建订单（自增 id 写回 order.id）
            Order order{0, userId, product.name, qty, amount, "PAID", ""};
            uORM::Mapper<Order>::save(order, c);
            return order.id;
        });
    }

    // 取消订单：回补库存 + 状态置 CANCELLED（同一事务）
    void cancelOrder(std::int64_t orderId) {
        auto order = [&] {
            auto conn = ds_.getConnection();
            auto r = uORM::Mapper<Order>::findOne(*conn, "id = ?", orderId);
            return r;
        }();
        if (!order) throw BusinessError("订单不存在");
        if (order->status == "CANCELLED") return; // 幂等：已取消直接返回

        uORM::withTransaction(ds_, [&](uORM::IConnection& c) {
            // 回补库存（按商品名定位；示例简化，实际应存 product_id）
            auto upd = "UPDATE products SET stock = stock + ? WHERE name = ?";
            auto stmt = c.prepareStatement(upd);
            uORM::bindSqlValue(stmt.get(), 1, uORM::SqlValue(static_cast<long long>(order->quantity)));
            uORM::bindSqlValue(stmt.get(), 2, order->product);
            stmt->executeUpdate();

            auto set = c.prepareStatement("UPDATE orders SET status = 'CANCELLED' WHERE id = ?");
            uORM::bindSqlValue(set.get(), 1, uORM::SqlValue(orderId));
            set->executeUpdate();
        });
    }

    std::vector<Order> ordersOf(std::int64_t userId) {
        return orderDao_.findByUser(userId);
    }

    double userSpent(std::int64_t userId) { return orderDao_.totalAmountOf(userId); }

private:
    uORM::DataSource& ds_;
    ProductDao productDao_;
    OrderDao orderDao_;
};

} // namespace demo
