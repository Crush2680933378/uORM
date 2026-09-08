#pragma once
// =====================================================================
// Model 层：User 实体
// 原则：Model 只描述数据与表映射，不包含任何数据库访问代码。
// =====================================================================
#include <string>

#include <uORM/orm/ORM.h>

namespace demo {

struct User {
    std::int64_t id = 0;        // 自增主键
    std::string  username;      // 登录名（唯一）
    std::string  email;
    int          age = 0;
    bool         active = true;
    std::string  created_at;    // 留空则由数据库 DEFAULT CURRENT_TIMESTAMP 填充
};

} // namespace demo

// 注册表映射：表名 users，列名与成员名一致
UORM_REFLECTION_NAMED(demo::User, "users", id, username, email, age, active, created_at)
