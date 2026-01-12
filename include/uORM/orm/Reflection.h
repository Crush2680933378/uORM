#pragma once 
#include <string> 
#include <tuple> 
#include <vector> 
#include <type_traits> 
#include <sstream>
#include <array>

namespace uORM { 

// 类型映射特性：将 C++ 类型映射到 SQL 类型
template<typename T> struct TypeMapping; 

// 基本类型映射
template<> struct TypeMapping<int> { static constexpr const char* type = "INT"; }; 
template<> struct TypeMapping<long> { static constexpr const char* type = "BIGINT"; }; 
template<> struct TypeMapping<long long> { static constexpr const char* type = "BIGINT"; }; 
template<> struct TypeMapping<unsigned int> { static constexpr const char* type = "INT UNSIGNED"; }; 
template<> struct TypeMapping<unsigned long> { static constexpr const char* type = "BIGINT UNSIGNED"; }; 
template<> struct TypeMapping<unsigned long long> { static constexpr const char* type = "BIGINT UNSIGNED"; }; 
template<> struct TypeMapping<float> { static constexpr const char* type = "FLOAT"; }; 
template<> struct TypeMapping<double> { static constexpr const char* type = "DOUBLE"; }; 
template<> struct TypeMapping<std::string> { static constexpr const char* type = "VARCHAR(255)"; }; 
template<> struct TypeMapping<bool> { static constexpr const char* type = "TINYINT(1)"; }; 
// 可根据需要添加更多映射 (例如 chrono 时间类型)

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
