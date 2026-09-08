// 文件说明：
// C++ 完整示例 —— 通过抽象基类 uormpp::IDatabase 使用 uORM（Pimpl 隐藏实现）。
// 演示：打开数据库、建表、参数化插入、查询取值（含 NULL）、更新、删除、
//       事务（提交/回滚）、索引存在性、错误处理、池状态。
#include <uormpp/UormPP.h>

#include <iostream>

int main() {
    using namespace uormpp;

    std::cout << "uormpp 版本: " << version() << "\n";
    for (const auto& d : availableDrivers()) std::cout << "  驱动: " << d << "\n";

    try {
        // 1. 打开数据库（SQLite 文件库；换成 mysql/postgresql 只改 Options）
        Options opts;
        opts.driver = "sqlite";
        opts.database = "uormpp_demo.db";
        opts.poolSize = 2;
        auto db = openDatabase(opts);

        // 2. 建表 + 唯一索引
        db->execute("CREATE TABLE IF NOT EXISTS users ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INT, city TEXT)");
        db->createIndex("users", "uq_users_name", {"name"}, true);

        // 3. 参数化插入
        std::uint64_t affected = db->execute(
            "INSERT INTO users (name, age, city) VALUES (?, ?, ?)",
            {Param("Tom"), Param(std::int64_t{25}), Param("北京")});
        std::cout << "插入 " << affected << " 行\n";
        db->execute("INSERT INTO users (name, age, city) VALUES (?, ?, ?)",
                    {Param("Jerry"), Param(std::int64_t{30}), Param("上海")});

        // 4. 查询 + 取值（含 NULL 处理）
        auto rs = db->query("SELECT id, name, age, city FROM users WHERE age >= ? ORDER BY id",
                            {Param(std::int64_t{18})});
        for (int r = 0; r < rs->rowCount(); ++r) {
            std::cout << rs->getInt64(r, 0) << " | " << rs->getString(r, 1) << " | "
                      << rs->getInt64(r, 2) << " | "
                      << (rs->isNull(r, 3) ? std::string("(NULL)") : rs->getString(r, 3)) << "\n";
        }

        // 5. 事务：提交路径
        {
            auto tx = db->beginTransaction();
            tx->execute("INSERT INTO users (name, age, city) VALUES (?, ?, ?)",
                        {Param("TxUser"), Param(std::int64_t{40}), Param("广州")});
            tx->commit();
        }

        // 6. 事务：回滚路径（未 commit 析构自动回滚）
        {
            auto tx = db->beginTransaction();
            tx->execute("INSERT INTO users (name, age) VALUES (?, ?)",
                        {Param("Ghost"), Param(std::int64_t{1})});
            // 无 commit -> 自动回滚
        }

        auto remain = db->query("SELECT COUNT(*) FROM users");
        std::cout << "回滚后剩余行数: " << remain->getInt64(0, 0) << "\n";

        // 7. 更新与删除
        db->execute("UPDATE users SET age = ? WHERE name = ?", {Param(std::int64_t{26}), Param("Tom")});
        db->execute("DELETE FROM users WHERE name = ?", {Param("Jerry")});

        // 8. Schema：索引与表存在性
        std::cout << "users 表存在: " << db->tableExists("users")
                  << ", 索引存在: " << db->indexExists("users", "uq_users_name") << "\n";

        // 9. 池状态
        int idle = 0, inUse = 0, created = 0;
        db->stats(&idle, &inUse, &created);
        std::cout << "池状态: idle=" << idle << " inUse=" << inUse << " created=" << created << "\n";

        // 10. 错误处理：错误 SQL 抛 uormpp::Error
        try {
            db->execute("INSERT INTO no_such_table VALUES (1)");
        } catch (const Error& e) {
            std::cout << "预期错误: " << e.what() << "\n";
        }
    } catch (const Error& e) {
        std::cerr << "uormpp 错误: " << e.what() << "\n";
        return 1;
    }
    std::cout << "C++ 抽象接口演示完成\n";
    return 0;
}
