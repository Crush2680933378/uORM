#pragma once
// 文件说明：
// 集成测试共享实体。实体类型必须放在全局命名空间（UORM_* 宏会在
// uORM 命名空间内特化 TableMeta），因此抽成公共头供多个测试 TU 复用。

#include "uORM/orm/ORM.h"

// 通用实体：覆盖整型/浮点/布尔/字符串/默认值时间戳
struct Item {
    long long id;
    std::string name;
    std::string category;
    double price;
    int stock;
    bool active;
    std::string created_at;
};
UORM_REFLECTION(Item, id, name, category, price, stock, active, created_at)

// 未注册的类：验证 TypedQuery 拒绝外来成员指针
struct NotMapped {
    std::string name;
};

// 长文本表：content 显式 TEXT（默认 string 映射是 VARCHAR(255)）
struct TextRow {
    long long id;
    std::string name;
    std::string content;
};
UORM_TABLE_BEGIN(TextRow, "text_rows")
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(name, "name", NOT NULL),
    UORM_FIELD_TYPE(content, "content", "TEXT")
UORM_TABLE_END()

// SQL 保留字陷阱：表列名使用保留字 order / group，全程依赖方言引用
struct ReservedRow {
    long long id;
    std::string order_;
    std::string group_x;
};
UORM_TABLE_BEGIN(ReservedRow, "reserved_order")
    UORM_FIELD(id, "id", PRIMARY KEY AUTO_INCREMENT),
    UORM_FIELD(order_, "order", NOT NULL),
    UORM_FIELD(group_x, "group", NOT NULL)
UORM_TABLE_END()

// 布尔/浮点往返表
struct FlagRow {
    long long id;
    bool enabled;
    double ratio;
};
UORM_REFLECTION_NAMED(FlagRow, "flag_rows", id, enabled, ratio)
