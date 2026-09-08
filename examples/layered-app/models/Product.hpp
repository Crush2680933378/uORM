#pragma once
// =====================================================================
// Model 层：Product 实体（演示库存与条件更新）
// =====================================================================
#include <string>

#include <uORM/orm/ORM.h>

namespace demo {

struct Product {
    std::int64_t id = 0;
    std::string  name;
    double       price = 0.0;
    int          stock = 0;
    bool         on_sale = true;
};

} // namespace demo

UORM_REFLECTION_NAMED(demo::Product, "products", id, name, price, stock, on_sale)
