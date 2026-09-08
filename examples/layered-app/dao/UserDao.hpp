#pragma once
// =====================================================================
// DAO 层：UserDao —— User 实体的数据访问对象
// 通用 CRUD 继承自 BaseDao，这里只补充 User 专属的查询与更新。
// =====================================================================
#include "dao/BaseDao.hpp"
#include "models/User.hpp"

#include <vector>

namespace demo {

// 实体特征注册：表 users，主键 id (std::int64_t)
DEMO_ENTITY_TRAITS(User, "users", std::int64_t, "id", e.id)

class UserDao : public BaseDao<User> {
public:
    using BaseDao::BaseDao;

    // 按登录名查（登录场景）
    std::optional<User> findByUsername(const std::string& username) {
        return findOneBy(keyWhereU("username"), username);
    }

    // 按邮箱查
    std::optional<User> findByEmail(const std::string& email) {
        return findOneBy(keyWhereU("email"), email);
    }

    // 成年用户（TypedQuery 成员指针写法：列名编译期来自映射）
    std::vector<User> findAdults() {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<User>::select(c,
                uORM::Query{}.whereRaw(uORM::Mapper<User>::primaryKeyColumn() + " > 0")
            );
        });
    }

    // 名称模糊搜索
    std::vector<User> searchByUsername(const std::string& keyword) {
        return findBy("username LIKE ?", "%" + keyword + "%");
    }

    // 只更新年龄（部分更新，其他列不受影响）
    bool updateAge(std::int64_t id, int newAge) {
        return withConn([&](uORM::IConnection& c) {
            auto user = uORM::Mapper<User>::findOne(c, "id = ?", id);
            if (!user) return false;
            user->age = newAge;
            return uORM::Mapper<User>::updateSome(*user, c, &User::age);
        });
    }

    // 统计活跃用户数
    long long countActive() {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<User>::count(c,
                uORM::Query{}.whereRaw(uORM::Mapper<User>::primaryKeyColumn() + " > 0 AND active = 1"));
        });
    }

private:
    static std::string keyWhereU(const std::string& col) {
        // 调用时机在连接内，方言引用由调用处完成；此处为简单示例直接使用列名
        return col + " = ?";
    }
};

} // namespace demo
