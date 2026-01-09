#include "common/utils/ConfigManager.h"
#include "orm/MySQLAdapter.h"
#include "orm/DAO.h"
#include "orm/Model.h"
#include "orm/Field.h"
#include <optional>
#include <string>
#include <vector>

// 示例模型：User
// 通过实现table与schema，接入统一DAO即可自动建表与CRUD
using namespace uORM;
struct User : Model<User> {
    int64_t id;                        // 主键（自增）
    std::string name;                  // 用户名
    std::string email;                 // 邮箱
    std::optional<std::string> bio;    // 个人简介（可空）
    static std::string table() { return "users"; }
    static std::vector<AnyField<User>> schema() {
        return {
            // id为主键自增；VarChar字段设置长度；bio可空
            makeField<User, int64_t>("id", &User::id, SqlType::BigInt, 0, false, true, true),
            makeField<User, std::string>("name", &User::name, SqlType::VarChar, 255, false, false, false),
            makeField<User, std::string>("email", &User::email, SqlType::VarChar, 255, false, false, false),
            makeField<User, std::optional<std::string>>("bio", &User::bio, SqlType::Text, 0, true, false, false)
        };
    }
};

int main() {
    // 读取数据库配置
    ConfigManager::getInstance().readDataBaseconfig("config.json");
    // 选择数据库适配器（当前为MySQL）
    auto adapter = std::make_shared<MySQLAdapter>();
    // 基于适配器创建User的DAO
    DAO<User> userDao(adapter);
    // 自动建表
    userDao.createTable();
    // 插入示例数据（不设置id，自增）
    User u{};
    u.name = "alice";
    u.email = "alice@example.com";
    auto id = userDao.insert(u);
    if (id) u.id = *id;
    // 更新可空字段
    u.bio = std::string("hello");
    userDao.update(u);
    // 按主键查询
    auto got = userDao.findById<int64_t>(u.id);
    // 查询全部
    auto all = userDao.findAll();
    // 删除数据
    userDao.deleteById<int64_t>(u.id);
    return 0;
}
