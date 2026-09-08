#pragma once
// =====================================================================
// DAO 层：OrderDao / ProductDao
// =====================================================================
#include "dao/BaseDao.hpp"
#include "models/Order.hpp"
#include "models/Product.hpp"

#include <optional>
#include <vector>

namespace demo {

// 实体特征注册
DEMO_ENTITY_TRAITS(Order, "orders", std::int64_t, "id", e.id)
DEMO_ENTITY_TRAITS(Product, "products", std::int64_t, "id", e.id)

// 订单 DAO
class OrderDao : public BaseDao<Order> {
public:
    using BaseDao::BaseDao;

    // 某用户的订单（新 -> 旧）
    std::vector<Order> findByUser(std::int64_t userId) {
        return findBy("user_id = ? ORDER BY id DESC", userId);
    }

    // 某状态订单
    std::vector<Order> findByStatus(const std::string& status) {
        return findBy("status = ? ORDER BY id", status);
    }

    // 用户消费总额（聚合）
    double totalAmountOf(std::int64_t userId) {
        return withConn([&](uORM::IConnection& c) {
            auto r = executeQuery(c,
                "SELECT COALESCE(SUM(amount), 0) FROM " +
                c.dialect()->quoteIdentifier(Traits::table) +
                " WHERE user_id = ?", {uORM::SqlValue(userId)});
            return r.rows.empty() ? 0.0 : uORM::valueToDouble(r.rows[0][0]);
        });
    }
};

// 商品 DAO
class ProductDao : public BaseDao<Product> {
public:
    using BaseDao::BaseDao;

    // 在售商品
    std::vector<Product> findOnSale() {
        return findBy("on_sale = 1 ORDER BY id");
    }

    // 条件扣减库存：库存不足时 affected = 0（防止超卖的原子写法）
    bool reduceStock(std::int64_t id, int qty) {
        return withConn([&](uORM::IConnection& c) -> bool {
            auto sql = "UPDATE " + c.dialect()->quoteIdentifier(Traits::table) +
                       " SET stock = stock - ? WHERE id = ? AND stock >= ?";
            auto stmt = c.prepareStatement(sql);
            uORM::bindSqlValue(stmt.get(), 1, uORM::SqlValue(static_cast<long long>(qty)));
            uORM::bindSqlValue(stmt.get(), 2, uORM::SqlValue(id));
            return stmt->executeUpdate() > 0;
        });
    }

    // 条件回补库存（取消订单时）
    bool restoreStock(std::int64_t id, int qty) {
        return withConn([&](uORM::IConnection& c) -> bool {
            auto sql = "UPDATE " + c.dialect()->quoteIdentifier(Traits::table) +
                       " SET stock = stock + ? WHERE id = ?";
            auto stmt = c.prepareStatement(sql);
            uORM::bindSqlValue(stmt.get(), 1, uORM::SqlValue(static_cast<long long>(qty)));
            uORM::bindSqlValue(stmt.get(), 2, uORM::SqlValue(id));
            return stmt->executeUpdate() > 0;
        });
    }
};

} // namespace demo
