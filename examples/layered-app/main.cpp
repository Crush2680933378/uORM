// 文件说明：
// 分层架构演示 —— 展示 uORM 推荐的项目组织方式：
//
//   main.cpp          组装层：创建 DataSource，装配 Service
//   service/          业务编排：事务边界、业务规则、跨 DAO 操作
//   dao/              数据访问：BaseDao 泛型 CRUD + 具体 DAO 专属查询
//   models/           实体定义：纯数据 + UORM 映射宏
//
// 运行前无需任何配置文件（示例使用 SQLite 文件库）。
#include <iostream>

#include "dao/OrderDao.hpp"   // 含 OrderDao 与 ProductDao
#include "dao/UserDao.hpp"
#include "models/User.hpp"
#include "service/Service.hpp"

using namespace demo;

int main() {
    // ---- 组装层：一个 DataSource 贯穿所有层（可注入/可替换实现）----
    uORM::DataSourceConfig cfg;
    cfg.params.driver = "sqlite";
    cfg.params.database = "layered_demo.db";
    cfg.poolSize = 2;
    uORM::DataSource ds(cfg);

    UserDao userDao(ds);
    ProductDao productDao(ds);
    OrderDao orderDao(ds);

    try {
        // ---- 建表（演示用；生产环境建议用迁移工具管理）----
        ds.execute("CREATE TABLE IF NOT EXISTS users ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE, "
                   "email TEXT NOT NULL UNIQUE, age INT, active INT DEFAULT 1, "
                   "created_at TEXT DEFAULT CURRENT_TIMESTAMP)");
        ds.execute("CREATE TABLE IF NOT EXISTS products ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
                   "price REAL NOT NULL, stock INT NOT NULL DEFAULT 0, on_sale INT DEFAULT 1)");
        ds.execute("CREATE TABLE IF NOT EXISTS orders ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INT NOT NULL, "
                   "product TEXT NOT NULL, quantity INT NOT NULL, amount REAL NOT NULL, "
                   "status TEXT NOT NULL DEFAULT 'PENDING', "
                   "created_at TEXT DEFAULT CURRENT_TIMESTAMP)");

        // ================= Model/DAO/Service 分层演示 =================

        // 1. Service：注册用户（含唯一性校验）
        UserService userService(ds);
        auto tomId = userService.registerUser("tom", "tom@demo.io", 28);
        auto jerryId = userService.registerUser("jerry", "jerry@demo.io", 35);
        std::cout << "[Service] 注册用户 tom(id=" << tomId << "), jerry(id=" << jerryId << ")\n";

        // 重复注册 -> 业务异常
        try {
            userService.registerUser("tom", "another@demo.io", 20);
        } catch (const BusinessError& e) {
            std::cout << "[Service] 预期业务异常: " << e.what() << "\n";
        }

        // 2. ProductDao：上架商品 + 批量插入
        std::vector<Product> products{
            Product{0, "iPhone", 9999.0, 50, true},
            Product{0, "MacBook", 19999.0, 20, true},
            Product{0, "Coffee", 35.0, 200, true},
        };
        productDao.insertRange(products);
        std::cout << "[DAO] 批量插入商品 " << products.size() << " 件\n";

        // 3. OrderService：下单（事务：原子扣库存 + 建订单）
        OrderService orderService(ds);
        auto orderId = orderService.placeOrder(tomId, products[0].id, 1);
        std::cout << "[Service] 下单成功 order=" << orderId << "\n";

        // 4. DAO 专属查询
        auto tom = userDao.findById(tomId);
        std::cout << "[DAO] findById: " << tom->username << "\n";
        for (const auto& o : orderDao.findByUser(tomId))
            std::cout << "[DAO] 订单 " << o.id << " -> " << o.product
                      << " x" << o.quantity << " (" << o.status << ")\n";
        std::cout << "[DAO] tom 累计消费 " << orderDao.totalAmountOf(tomId) << " 元\n";

        // 5. 分页查询
        auto page2 = userDao.findPage(1, 1, "id"); // 第 1 页每页 1 条
        if (!page2.empty())
            std::cout << "[DAO] 分页第 1 条: " << page2[0].username << "\n";

        // 6. 部分更新（只改 age）
        auto ju = userDao.findById(jerryId);
        if (ju) {
            ju->age = 36;
            userDao.updateFields(*ju, &User::age);
            std::cout << "[DAO] updateSome 部分更新 OK, age=" << ju->age << "\n";
        }

        // 7. 下单失败演示：库存不足 -> 整体回滚（无订单产生）
        try {
            orderService.placeOrder(tomId, products[2].id, 999);
        } catch (const BusinessError& e) {
            std::cout << "[Service] 预期业务异常: " << e.what() << "\n";
        }

        // 8. 取消订单：回补库存 + 状态置 CANCELLED
        orderService.cancelOrder(orderId);
        auto orders = orderDao.findByUser(tomId);
        std::cout << "[Service] 取消后订单状态: " << orders[0].status << "\n";

        // 9. upsert 与删除
        auto u = userDao.findById(jerryId);
        u->email = "jerry@new.io";
        userDao.saveOrUpdate(*u);
        std::cout << "[DAO] saveOrUpdate OK, email=" << u->email << "\n";

        std::cout << "\n分层架构演示全部通过\n";
    } catch (const std::exception& e) {
        std::cerr << "演示失败: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
