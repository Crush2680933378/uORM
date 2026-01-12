#include "uORM/orm/ORM.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>

// ==========================================
// 1. 定义数据模型 (Models)
// ==========================================

// 商品表模型
struct Product {
    int id;
    std::string name;
    std::string category;
    double price;
    int stock;
    bool is_active;
    std::string created_at;
};

// 订单表模型
struct Order {
    long long id;
    int user_id;
    int product_id;
    int quantity;
    double total_amount;
    std::string status; // "PENDING", "PAID", "SHIPPED", "CANCELLED"
    std::string order_time;
};

// ==========================================
// 2. 注册 ORM 映射
// 映射宏 (类名, 表名)
UORM_TABLE_BEGIN(Product, "products")
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(name, "name", NOT NULL),
    UORM_FIELD(category, "category", NOT NULL),
    UORM_FIELD(price, "price", NOT NULL),
    UORM_FIELD(stock, "stock", DEFAULT 0),
    UORM_FIELD(is_active, "is_active", DEFAULT 1),
    UORM_FIELD_TYPE(created_at, "created_at", "DATETIME", DEFAULT CURRENT_TIMESTAMP)
UORM_TABLE_END()

UORM_TABLE_BEGIN(Order, "orders")
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(user_id, "user_id", NOT NULL),
    UORM_FIELD(product_id, "product_id", NOT NULL),
    UORM_FIELD(quantity, "quantity", NOT NULL),
    UORM_FIELD(total_amount, "total_amount", NOT NULL),
    UORM_FIELD(status, "status", DEFAULT 'PENDING'),
    UORM_FIELD_TYPE(order_time, "order_time", "DATETIME", DEFAULT CURRENT_TIMESTAMP)
UORM_TABLE_END()

// ==========================================
// 3. 辅助函数
// ==========================================
std::string getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void initData() {
    // 初始化产品数据
    uORM::Schema::createTable<Product>();
    uORM::Schema::createTable<Order>();

    // 清空旧数据 (仅作演示)
    // 实际生产环境请勿随意 truncate
    uORM::Mapper<Product>::truncate();
    uORM::Mapper<Order>::truncate();
    
    std::cout << "正在初始化示例数据..." << std::endl;

    uORM::Mapper<Product>::save({0, "iPhone 15", "Electronics", 999.99, 50, true, getCurrentTime()});
    uORM::Mapper<Product>::save({0, "MacBook Pro", "Electronics", 1999.99, 20, true, getCurrentTime()});
    uORM::Mapper<Product>::save({0, "Coffee Mug", "Home", 19.99, 100, true, getCurrentTime()});
    uORM::Mapper<Product>::save({0, "T-Shirt", "Clothing", 29.99, 200, true, getCurrentTime()});
    uORM::Mapper<Product>::save({0, "Old Phone", "Electronics", 50.00, 0, false, getCurrentTime()});
}

// ==========================================
// 4. 复杂查询演示
// ==========================================

void demonstrateQueryBuilder() {
    std::cout << "\n=== 演示复杂查询构造器 ===" << std::endl;

    // 场景 1: 查找所有价格在 100 到 2000 之间的电子产品，按价格降序排列
    {
        std::cout << "\n[Query 1] 查找价格 100-2000 的电子产品 (降序):" << std::endl;
        uORM::Query query;
        query.eq("category", "Electronics")
             .between("price", 100.0, 2000.0)
             .orderBy("price", false); // false = DESC

        auto products = uORM::Mapper<Product>::select(query);
        for (const auto& p : products) {
            std::cout << "  - " << p.name << " ($" << p.price << ")" << std::endl;
        }
    }

    // 场景 2: 查找库存紧张 (stock < 30) 且处于激活状态的产品
    {
        std::cout << "\n[Query 2] 查找库存 < 30 的在售产品:" << std::endl;
        uORM::Query query;
        query.lt("stock", 30)
             .eq("is_active", true);

        auto products = uORM::Mapper<Product>::select(query);
        for (const auto& p : products) {
            std::cout << "  - " << p.name << " (Stock: " << p.stock << ")" << std::endl;
        }
    }

    // 场景 3: 模糊查询 (查找名字包含 "Phone" 的产品)
    {
        std::cout << "\n[Query 3] 查找名字包含 'Phone' 的产品:" << std::endl;
        uORM::Query query;
        query.like("name", "%Phone%");

        auto products = uORM::Mapper<Product>::select(query);
        for (const auto& p : products) {
            std::cout << "  - " << p.name << std::endl;
        }
    }

    // 场景 4: 复杂逻辑 (A AND (B OR C))
    // 查找 (Category = 'Home') OR (Price > 1000)
    // 目前 Query Builder 默认是链式 AND，支持 or_() 切换
    {
        std::cout << "\n[Query 4] 查找 家居用品 OR 价格大于 1000 的商品:" << std::endl;
        uORM::Query query;
        query.eq("category", "Home")
             .or_()
             .gt("price", 1000.0);

        auto products = uORM::Mapper<Product>::select(query);
        for (const auto& p : products) {
            std::cout << "  - " << p.name << " [" << p.category << "] ($" << p.price << ")" << std::endl;
        }
    }
    
    // 场景 5: IN 查询
    {
        std::cout << "\n[Query 5] 查找特定 ID 集合 (1, 3, 5) 的产品:" << std::endl;
        uORM::Query query;
        query.in("id", std::vector<int>{1, 3, 5});
        
        auto products = uORM::Mapper<Product>::select(query);
        for (const auto& p : products) {
             std::cout << "  - ID:" << p.id << " " << p.name << std::endl;
        }
    }
}

void demonstrateCRUD() {
    std::cout << "\n=== 演示基本 CRUD 操作 ===" << std::endl;

    // Create
    Product newP{0, "Gaming Mouse", "Electronics", 59.99, 10, true, getCurrentTime()};
    if (uORM::Mapper<Product>::save(newP)) {
        std::cout << "创建成功: " << newP.name << std::endl;
    }

    // Read (Find One)
    uORM::Query q;
    q.eq("name", "Gaming Mouse");
    auto pOpt = uORM::Mapper<Product>::selectOne(q);
    
    if (pOpt) {
        auto p = *pOpt;
        std::cout << "读取成功: " << p.name << ", ID: " << p.id << std::endl;

        // Update
        p.price = 49.99; // 降价
        p.stock -= 1;    // 卖出一个
        if (uORM::Mapper<Product>::update(p)) {
            std::cout << "更新成功: 新价格 " << p.price << ", 库存 " << p.stock << std::endl;
        }

        // Delete
        // uORM::Mapper<Product>::remove(p);
        // std::cout << "删除成功" << std::endl;
    }
}

int main() {
    // 1. 读取配置
    if (!uORM::ConfigManager::getInstance().readDataBaseconfig("config.json")) {
        std::cerr << "配置文件读取失败，请确保 config.json 存在且格式正确。" << std::endl;
        return 1;
    }

    // 2. 初始化连接池
    try {
        uORM::ConnectionPool::instance();
        std::cout << "数据库连接成功!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "数据库连接失败: " << e.what() << std::endl;
        return 1;
    }

    // 3. 初始化表和数据
    initData();

    // 4. 运行演示
    demonstrateCRUD();
    demonstrateQueryBuilder();

    return 0;
}
