#pragma once
#include <string>
#include <tuple>
#include <vector>
#include <type_traits>
#include <sstream>
#include <array>

#include "uORM/driver/SqlDialect.h"

namespace uORM {

// 类型映射：将 C++ 类型映射到各数据库的 SQL 类型
// 说明：主模板 fallback 为 TEXT，未注册类型建议显式使用 UORM_FIELD_TYPE。
template<typename T> struct SqlTypeNames {
    static constexpr const char* mysql = "TEXT";
    static constexpr const char* postgresql = "TEXT";
    static constexpr const char* sqlite = "TEXT";
};

template<> struct SqlTypeNames<int> {
    static constexpr const char* mysql = "INT";
    static constexpr const char* postgresql = "INTEGER";
    static constexpr const char* sqlite = "INTEGER";
};
template<> struct SqlTypeNames<long> {
    static constexpr const char* mysql = "BIGINT";
    static constexpr const char* postgresql = "BIGINT";
    static constexpr const char* sqlite = "INTEGER";
};
template<> struct SqlTypeNames<long long> {
    static constexpr const char* mysql = "BIGINT";
    static constexpr const char* postgresql = "BIGINT";
    static constexpr const char* sqlite = "INTEGER";
};
template<> struct SqlTypeNames<unsigned int> {
    static constexpr const char* mysql = "INT UNSIGNED";
    static constexpr const char* postgresql = "BIGINT";
    static constexpr const char* sqlite = "INTEGER";
};
template<> struct SqlTypeNames<unsigned long> {
    static constexpr const char* mysql = "BIGINT UNSIGNED";
    static constexpr const char* postgresql = "NUMERIC(20)";
    static constexpr const char* sqlite = "INTEGER";
};
template<> struct SqlTypeNames<unsigned long long> {
    static constexpr const char* mysql = "BIGINT UNSIGNED";
    static constexpr const char* postgresql = "NUMERIC(20)";
    static constexpr const char* sqlite = "INTEGER";
};
template<> struct SqlTypeNames<float> {
    static constexpr const char* mysql = "FLOAT";
    static constexpr const char* postgresql = "REAL";
    static constexpr const char* sqlite = "REAL";
};
template<> struct SqlTypeNames<double> {
    static constexpr const char* mysql = "DOUBLE";
    static constexpr const char* postgresql = "DOUBLE PRECISION";
    static constexpr const char* sqlite = "REAL";
};
template<> struct SqlTypeNames<bool> {
    static constexpr const char* mysql = "TINYINT(1)";
    static constexpr const char* postgresql = "BOOLEAN";
    static constexpr const char* sqlite = "INTEGER";
};
template<> struct SqlTypeNames<std::string> {
    static constexpr const char* mysql = "VARCHAR(255)";
    static constexpr const char* postgresql = "VARCHAR(255)";
    static constexpr const char* sqlite = "TEXT";
};

// 按方言取类型名
template<typename T>
const char* sqlTypeNameFor(const ISqlDialect& dialect) {
    switch (dialect.kind()) {
        case DialectKind::PostgreSQL: return SqlTypeNames<T>::postgresql;
        case DialectKind::SQLite:     return SqlTypeNames<T>::sqlite;
        case DialectKind::MySQL:      return SqlTypeNames<T>::mysql;
    }
    return SqlTypeNames<T>::mysql;
}

// 旧接口兼容：默认给 MySQL 风格类型名
template<typename T> struct TypeMapping {
    static constexpr const char* type = SqlTypeNames<T>::mysql;
};

// SQL 约束常量定义
struct Constraints {
    static constexpr const char* PrimaryKey = "PRIMARY KEY"; // 主键
    static constexpr const char* AutoIncrement = "AUTO_INCREMENT"; // 自增
    static constexpr const char* NotNull = "NOT_NULL"; // 内部标记，映射到 "NOT NULL"
    static constexpr const char* Unique = "UNIQUE"; // 唯一约束
};

// 字段元数据结构体：保存字段的详细信息
template<typename Class, typename T>
struct FieldMeta {
    using Type = T;
    using ClassType = Class;

    T Class::* member_ptr; // 成员变量指针
    const char* column_name; // 数据库列名
    const char* constraint_sql; // 原始 SQL 约束字符串，如 "NOT NULL AUTO_INCREMENT"
    const char* sql_type_override; // 自定义 SQL 类型（如 "ENUM(...)"），若提供则覆盖默认映射

    constexpr FieldMeta(T Class::* ptr, const char* name, const char* constraints = "", const char* type_override = nullptr)
        : member_ptr(ptr), column_name(name), constraint_sql(constraints), sql_type_override(type_override) {}
};

// 表元数据模板基类
template<typename T>
struct TableMeta {
    // 必须特化
    static constexpr bool is_registered = false;
    static constexpr const char* options = "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    static constexpr bool has_indexes = false;
};

// 辅助变量：检查类型是否已注册
template<typename T>
constexpr bool is_registered_v = TableMeta<T>::is_registered;

} // namespace uORM

// 宏定义：开始注册表结构
#define UORM_TABLE_BEGIN(Type, TableName) \
    namespace uORM { \
    template<> struct TableMeta<Type> { \
        using EntityType = Type; \
        static constexpr bool is_registered = true; \
        static constexpr const char* name = TableName; \
        static constexpr auto get_fields() { \
            return std::make_tuple(

// 宏定义：注册字段 (使用默认类型映射)
#define UORM_FIELD(Member, ColumnName, ...) \
            uORM::FieldMeta<EntityType, decltype(EntityType::Member)>{&EntityType::Member, ColumnName, #__VA_ARGS__, nullptr}

// 宏定义：注册字段 (指定 SQL 类型)
#define UORM_FIELD_TYPE(Member, ColumnName, SqlType, ...) \
            uORM::FieldMeta<EntityType, decltype(EntityType::Member)>{&EntityType::Member, ColumnName, #__VA_ARGS__, SqlType}

// 宏定义：结束表注册 (使用默认选项)
#define UORM_TABLE_END() \
            ); \
        } \
        static constexpr const char* options = "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"; \
        static constexpr bool has_indexes = false; \
    }; \
    }

// 宏定义：结束表注册 (指定扩展选项和索引)
// 用法: UORM_TABLE_END_WITH_OPTS("ENGINE=InnoDB...", "INDEX ...", "INDEX ...")
#define UORM_TABLE_END_WITH_OPTS(TableOptions, ...) \
            ); \
        } \
        static constexpr const char* options = TableOptions; \
        static constexpr bool has_indexes = true; \
        static constexpr auto get_indexes() { \
            return std::array<const char*, sizeof((const char*[]){__VA_ARGS__}) / sizeof(const char*)>{__VA_ARGS__}; \
        } \
    }; \
    }

// ---------------------------------------------------------------------------
// UORM_REFLECTION 简写宏（ormpp 风格）
// 列名 = 成员名；名为 id 的成员自动映射为自增主键。
//   struct User { int id; std::string name; int age; };
//   UORM_REFLECTION(User, id, name, age)
// 表名默认为类型名；自定义表名用 UORM_REFLECTION_NAMED。
// ---------------------------------------------------------------------------

namespace uORM {
namespace detail {
// 编译期字符串相等比较（用于把名为 id 的成员标记为主键）
constexpr bool strEq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return *a == *b;
}
} // namespace detail
} // namespace uORM

#define UORM_REFL_FIELD(Type, Member) \
            uORM::FieldMeta<EntityType, decltype(EntityType::Member)>{ \
                &EntityType::Member, #Member, \
                uORM::detail::strEq(#Member, "id") ? "PRIMARY KEY AUTO_INCREMENT" : "", nullptr}

// 变参字段展开（支持 1~16 个成员）
#define UORM_REFL_CONCAT2(a, b) a##b
#define UORM_REFL_CONCAT(a, b) UORM_REFL_CONCAT2(a, b)
#define UORM_REFL_GETN(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,N,...) N
#define UORM_REFL_COUNT(...) UORM_REFL_GETN(__VA_ARGS__,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1)

#define UORM_REFL_FIELDS_1(Type, M1) UORM_REFL_FIELD(Type, M1)
#define UORM_REFL_FIELDS_2(Type, M1, M2) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_1(Type, M2)
#define UORM_REFL_FIELDS_3(Type, M1, M2, M3) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_2(Type, M2, M3)
#define UORM_REFL_FIELDS_4(Type, M1, M2, M3, M4) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_3(Type, M2, M3, M4)
#define UORM_REFL_FIELDS_5(Type, M1, M2, M3, M4, M5) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_4(Type, M2, M3, M4, M5)
#define UORM_REFL_FIELDS_6(Type, M1, M2, M3, M4, M5, M6) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_5(Type, M2, M3, M4, M5, M6)
#define UORM_REFL_FIELDS_7(Type, M1, M2, M3, M4, M5, M6, M7) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_6(Type, M2, M3, M4, M5, M6, M7)
#define UORM_REFL_FIELDS_8(Type, M1, M2, M3, M4, M5, M6, M7, M8) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_7(Type, M2, M3, M4, M5, M6, M7, M8)
#define UORM_REFL_FIELDS_9(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_8(Type, M2, M3, M4, M5, M6, M7, M8, M9)
#define UORM_REFL_FIELDS_10(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9, M10) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_9(Type, M2, M3, M4, M5, M6, M7, M8, M9, M10)
#define UORM_REFL_FIELDS_11(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_10(Type, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11)
#define UORM_REFL_FIELDS_12(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_11(Type, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12)
#define UORM_REFL_FIELDS_13(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_12(Type, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13)
#define UORM_REFL_FIELDS_14(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13, M14) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_13(Type, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13, M14)
#define UORM_REFL_FIELDS_15(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13, M14, M15) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_14(Type, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13, M14, M15)
#define UORM_REFL_FIELDS_16(Type, M1, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13, M14, M15, M16) UORM_REFL_FIELD(Type, M1), UORM_REFL_FIELDS_15(Type, M2, M3, M4, M5, M6, M7, M8, M9, M10, M11, M12, M13, M14, M15, M16)
#define UORM_REFL_FIELDS(Type, ...) UORM_REFL_CONCAT(UORM_REFL_FIELDS_, UORM_REFL_COUNT(__VA_ARGS__))(Type, __VA_ARGS__)

#define UORM_REFLECTION_NAMED(Type, TableName, ...) \
    namespace uORM { \
    template<> struct TableMeta<Type> { \
        using EntityType = Type; \
        static constexpr bool is_registered = true; \
        static constexpr const char* name = TableName; \
        static constexpr auto get_fields() { \
            return std::make_tuple(UORM_REFL_FIELDS(Type, __VA_ARGS__)); \
        } \
        static constexpr const char* options = "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"; \
        static constexpr bool has_indexes = false; \
    }; \
    }

#define UORM_REFLECTION(Type, ...) UORM_REFLECTION_NAMED(Type, #Type, __VA_ARGS__)
