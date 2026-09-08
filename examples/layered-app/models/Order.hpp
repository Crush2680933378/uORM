#pragma once
// =====================================================================
// Model 层：Order 实体（演示外键与关联）
// =====================================================================
#include <string>

#include <uORM/orm/ORM.h>

namespace demo {

struct Order {
    std::int64_t id = 0;
    std::int64_t user_id = 0;       // 外键 -> users.id
    std::string  product;           // 商品名（简化示例，实际应再关联商品表）
    int          quantity = 1;
    double       amount = 0.0;
    std::string  status = "PENDING"; // PENDING / PAID / CANCELLED
    std::string  created_at;
};

} // namespace demo

UORM_TABLE_BEGIN(demo::Order, "orders")
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(user_id, "user_id", NOT NULL),
    UORM_FIELD(product, "product", NOT NULL),
    UORM_FIELD(quantity, "quantity", NOT NULL DEFAULT 1),
    UORM_FIELD(amount, "amount", NOT NULL DEFAULT 0),
    UORM_FIELD(status, "status", NOT NULL DEFAULT 'PENDING'),
    UORM_FIELD_TYPE(created_at, "created_at", "DATETIME", DEFAULT CURRENT_TIMESTAMP)
UORM_TABLE_END()
