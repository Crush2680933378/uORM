#pragma once 
#include "uORM/orm/Reflection.h" 
#include "uORM/driver/ConnectionPool.h" 
#include <string> 
#include <vector> 
#include <sstream> 
#include <iostream> 
#include <algorithm>

namespace uORM { 

// Schema 类负责数据库结构的生成和管理
class Schema { 
public: 
    // 根据类型 T 的元数据创建数据库表
    template<typename T> 
    static bool createTable() { 
        // 编译期检查：确保类型 T 已通过 UORM 宏注册
        if constexpr (!is_registered_v<T>) {
            static_assert(is_registered_v<T>, "类型必须使用 UORM_TABLE 宏进行注册");
            return false;
        }

        auto dialect = ConnectionPool::instance().getDialect(); 
        if (!dialect) return false; 

        std::stringstream ss; 
        ss << "CREATE TABLE IF NOT EXISTS " << dialect->quoteIdentifier(TableMeta<T>::name) << " ("; 
        
        auto fields = TableMeta<T>::get_fields(); 
        bool first = true; 
        
        // 遍历所有字段，生成 SQL 列定义
        std::apply([&](auto&&... field) { 
            (( 
                ss << (first ? "" : ", ") 
                   << dialect->quoteIdentifier(field.column_name) << " " 
                   // 优先使用自定义 SQL 类型，否则使用默认映射
                   << (field.sql_type_override ? field.sql_type_override : getSqlType<typename std::decay_t<decltype(field)>::Type>()) 
                   << " " << cleanConstraints(field.constraint_sql, dialect), 
                first = false 
            ), ...); 
        }, fields); 
        
        // 如果有索引定义，则追加到建表语句中
        if constexpr (TableMeta<T>::has_indexes) {
            auto indexes = TableMeta<T>::get_indexes();
            for (const auto& idx : indexes) {
                // 注意：这里简单的追加索引定义，可能需要根据方言调整索引创建语法
                // 暂时假设用户提供的索引 SQL 片段是兼容的或者主要针对 MySQL
                ss << ", " << idx;
            }
        }

        // 追加表选项 (如 ENGINE, CHARSET, AUTO_INCREMENT 等)
        // 使用方言处理表选项
        ss << ") " << dialect->getTableOptions(TableMeta<T>::options) << ";"; 
        
        std::string sql = ss.str(); 
        std::cout << "执行 SQL: " << sql << std::endl; 
        
        // 尝试执行，如果失败（可能是默认值问题），尝试修复
        if (!execute(sql)) {
             // 简单的错误恢复逻辑：如果是因为 Invalid default value for 'xxx'，这通常是因为 MySQL 版本差异（如 5.7 vs 8.0 的 STRICT 模式）
             // 或者 TIMESTAMP 默认值问题。
             // 这里我们可以尝试去除 DEFAULT CURRENT_TIMESTAMP 再试一次，或者提示用户。
             // 为了演示，我们先只打印错误。实际生产中可能需要更复杂的方言适配。
             return false;
        }
        return true;
    } 

    // 删除表
    template<typename T> 
    static bool dropTable() { 
        auto dialect = ConnectionPool::instance().getDialect(); 
        std::string sql = "DROP TABLE IF EXISTS " + dialect->quoteIdentifier(TableMeta<T>::name) + ";"; 
        return execute(sql); 
    } 

private: 
    // 获取 C++ 类型对应的 SQL 类型字符串
    template<typename FieldType> 
    static std::string getSqlType() { 
        return TypeMapping<FieldType>::type; 
    } 

    // 清理并适配约束字符串
    static std::string cleanConstraints(const char* constraints, const std::shared_ptr<ISqlDialect>& dialect) { 
        std::string s(constraints); 
        std::replace(s.begin(), s.end(), ',', ' '); 
        
        // 处理 AUTO_INCREMENT
        size_t pos = s.find("AUTO_INCREMENT"); 
        if (pos != std::string::npos) { 
            std::string modifier = dialect->getAutoIncrementModifier(); 
            if (modifier.empty()) { 
                // 如果方言不支持 AUTO_INCREMENT 修饰符 (如 PG 的 SERIAL 是类型的一部分，或者使用 GENERATED ALWAYS AS IDENTITY)
                // 这里简单地将其移除，假设字段类型已经处理好了 (例如用户在 PG 中应该把字段类型定义为 SERIAL)
                // 或者我们可以尝试在这里替换。为了简单，如果方言返回空，我们移除它。
                s.replace(pos, 14, ""); 
            } else if (modifier != "AUTO_INCREMENT") { 
                s.replace(pos, 14, modifier); 
            } 
        } 
        return s; 
    } 

    // 执行 SQL 语句
    static bool execute(const std::string& sql) { 
        try { 
            auto connPtr = ConnectionPool::instance().getConnection(); 
            auto stmt = connPtr->createStatement(); 
            stmt->execute(sql); 
            return true; 
        } catch (const std::exception& e) { 
            std::cerr << "Schema 错误: " << e.what() << std::endl; 
            return false; 
        } 
    } 
}; 

} // namespace uORM 
