#pragma once
// 文件说明：
// Schema 负责数据库结构的生成和管理（建表/删表）。
// v2：DDL 完全方言感知 —— 同一份实体定义可在 MySQL/PostgreSQL/SQLite 上建表。

#include "uORM/orm/Reflection.h"
#include "uORM/driver/ConnectionPool.h"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace uORM {

// Schema 类负责数据库结构的生成和管理
class Schema {
public:
    // 根据类型 T 的元数据创建数据库表
    template<typename T>
    static bool createTable(IConnection& conn) {
        if constexpr (!is_registered_v<T>) {
            static_assert(is_registered_v<T>, "类型必须使用 UORM_TABLE 宏进行注册");
            return false;
        }

        auto dialect = conn.dialect();

        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS " << dialect->quoteIdentifier(TableMeta<T>::name) << " (";

        auto fields = TableMeta<T>::get_fields();
        bool first = true;

        // 遍历所有字段，生成 SQL 列定义
        std::apply([&](auto&&... field) {
            ((
                ss << (first ? "" : ", ")
                   << columnSql(field, *dialect),
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

        // 追加表选项 (如 ENGINE, CHARSET 等)，方言会自动忽略不支持项
        ss << ") " << dialect->getTableOptions(TableMeta<T>::options) << ";";

        return execute(conn, ss.str());
    }

    template<typename T>
    static bool createTable() {
        auto connPtr = ConnectionPool::instance().getConnection();
        return createTable<T>(*connPtr);
    }

    // 删除表
    template<typename T>
    static bool dropTable(IConnection& conn) {
        auto dialect = conn.dialect();
        std::string sql = "DROP TABLE IF EXISTS " + dialect->quoteIdentifier(TableMeta<T>::name) + ";";
        return execute(conn, sql);
    }

    template<typename T>
    static bool dropTable() {
        auto connPtr = ConnectionPool::instance().getConnection();
        return dropTable<T>(*connPtr);
    }

private:
    // 生成单列定义：列名 + 类型 + 约束（方言归一化）
    template<typename Field>
    static std::string columnSql(const Field& field, const ISqlDialect& dialect) {
        using FieldType = typename std::decay_t<decltype(field)>::Type;

        std::string baseType = field.sql_type_override
            ? dialect.normalizeTypeName(field.sql_type_override)
            : sqlTypeNameFor<FieldType>(dialect);

        std::string constraints = cleanConstraints(field.constraint_sql, dialect);

        std::stringstream ss;
        ss << dialect.quoteIdentifier(field.column_name) << " " << baseType;
        if (!constraints.empty()) ss << " " << constraints;
        return ss.str();
    }

    // 清理并适配约束字符串
    static std::string cleanConstraints(const char* constraints, const ISqlDialect& dialect) {
        std::string s(constraints);
        std::replace(s.begin(), s.end(), ',', ' ');

        // 处理 AUTO_INCREMENT：替换为方言对应修饰符（PG: GENERATED ... IDENTITY）
        size_t pos = s.find("AUTO_INCREMENT");
        if (pos != std::string::npos) {
            std::string modifier = dialect.getAutoIncrementModifier();
            if (modifier.empty()) {
                s.replace(pos, 14, "");
            } else if (modifier != "AUTO_INCREMENT") {
                s.replace(pos, 14, modifier);
            }
        }

        // 压缩多余空白
        std::string out;
        bool prevSpace = true; // 抑制开头空格
        for (char c : s) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!prevSpace) out += ' ';
                prevSpace = true;
            } else {
                out += c;
                prevSpace = false;
            }
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    // 执行 SQL 语句
    static bool execute(IConnection& conn, const std::string& sql) {
        try {
            auto stmt = conn.createStatement();
            stmt->execute(sql);
            return true;
        } catch (const Exception& e) {
            std::cerr << "Schema error: " << e.what() << std::endl;
            return false;
        }
    }
};

} // namespace uORM
