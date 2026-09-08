#pragma once
// 文件说明：
// Schema 负责数据库结构的生成和管理（建表/删表）。
// v2：DDL 完全方言感知 —— 同一份实体定义可在 MySQL/PostgreSQL/SQLite 上建表。

#include "uORM/orm/Reflection.h"
#include "uORM/driver/ConnectionPool.h"
#include <string>
#include <vector>
#include <set>
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

        // 复合主键（UORM_COMPOSITE_PK 声明）
        if constexpr (CompositePK<T>::enabled) {
            auto pkCols = CompositePK<T>::columns();
            ss << ", PRIMARY KEY (";
            for (std::size_t i = 0; i < pkCols.size(); ++i) {
                if (i) ss << ", ";
                ss << dialect->quoteIdentifier(pkCols[i]);
            }
            ss << ")";
        }

        // 陷阱：MySQL 解析器接受但忽略字段内联 "REFERENCES t(c)" 语法，
        // 必须生成表级 FOREIGN KEY 子句；PG/SQLite 的内联形式已生效，不重复生成。
        if (dialect->kind() == DialectKind::MySQL) {
            auto fields = TableMeta<T>::get_fields();
            std::apply([&](auto&&... field) {
                ((
                    [&] {
                        std::string cons(field.constraint_sql);
                        auto pos = cons.find("REFERENCES ");
                        if (pos == std::string::npos) return;
                        std::string ref = cons.substr(pos + 11); // REFERENCES 之后
                        ss << ", FOREIGN KEY (" << dialect->quoteIdentifier(field.column_name)
                           << ") REFERENCES " << ref;
                    }(), 0
                ), ...);
            }, fields);
        }

        // 追加表选项 (如 ENGINE, CHARSET 等)，方言会自动忽略不支持项
        ss << ") " << dialect->getTableOptions(TableMeta<T>::options) << ";";

        if (!execute(conn, ss.str())) return false;

        // 声明式索引在表创建后补建（存在性检查，幂等）
        return createIndexes<T>(conn);
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

    // =====================================================================
    // Schema 能力：索引 / 存在性 / 轻量迁移 / 改表
    // =====================================================================

    // 表是否存在
    static bool tableExists(IConnection& conn, const std::string& table) {
        try {
            auto dialect = conn.dialect();
            QueryResult r;
            switch (dialect->kind()) {
                case DialectKind::MySQL:
                    r = executeQuery(conn,
                        "SELECT COUNT(*) FROM information_schema.tables "
                        "WHERE table_schema = DATABASE() AND table_name = ?", {SqlValue(table)});
                    break;
                case DialectKind::PostgreSQL:
                    r = executeQuery(conn,
                        "SELECT COUNT(*) FROM information_schema.tables "
                        "WHERE table_schema = current_schema() AND table_name = ?", {SqlValue(table)});
                    break;
                case DialectKind::SQLite:
                default:
                    r = executeQuery(conn,
                        "SELECT COUNT(*) FROM sqlite_master WHERE type IN ('table','view') "
                        "AND name = ?", {SqlValue(table)});
                    break;
            }
            return !r.rows.empty() && valueToInt64(r.rows[0][0]) > 0;
        } catch (const Exception& e) {
            std::cerr << "Schema error (tableExists): " << e.what() << std::endl;
            return false;
        }
    }

    // 索引是否存在
    static bool indexExists(IConnection& conn, const std::string& table, const std::string& indexName) {
        try {
            auto dialect = conn.dialect();
            QueryResult r;
            switch (dialect->kind()) {
                case DialectKind::MySQL:
                    r = executeQuery(conn,
                        "SELECT COUNT(*) FROM information_schema.statistics "
                        "WHERE table_schema = DATABASE() AND table_name = ? AND index_name = ?",
                        {SqlValue(table), SqlValue(indexName)});
                    break;
                case DialectKind::PostgreSQL:
                    r = executeQuery(conn,
                        "SELECT COUNT(*) FROM pg_indexes "
                        "WHERE schemaname = current_schema() AND tablename = ? AND indexname = ?",
                        {SqlValue(table), SqlValue(indexName)});
                    break;
                case DialectKind::SQLite:
                default:
                    r = executeQuery(conn,
                        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = ?",
                        {SqlValue(indexName)});
                    break;
            }
            return !r.rows.empty() && valueToInt64(r.rows[0][0]) > 0;
        } catch (const Exception& e) {
            std::cerr << "Schema error (indexExists): " << e.what() << std::endl;
            return false;
        }
    }

    // 创建索引（已存在则跳过，幂等）
    static bool createIndex(IConnection& conn, const std::string& table, const std::string& indexName,
                            const std::vector<std::string>& columns, bool unique = false) {
        if (columns.empty() || indexExists(conn, table, indexName)) return true;
        auto dialect = conn.dialect();
        std::string cols;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i) cols += ", ";
            cols += dialect->quoteIdentifier(columns[i]);
        }
        std::string sql = std::string("CREATE ") + (unique ? "UNIQUE " : "") + "INDEX " +
                          dialect->quoteIdentifier(indexName) + " ON " +
                          dialect->quoteIdentifier(table) + " (" + cols + ")";
        try {
            auto stmt = conn.createStatement();
            stmt->execute(sql);
            return true;
        } catch (const Exception& e) {
            std::cerr << "Schema error (createIndex " << indexName << "): " << e.what() << std::endl;
            return false;
        }
    }

    // 删除索引（MySQL 语法不同：DROP INDEX name ON table）
    static bool dropIndex(IConnection& conn, const std::string& table, const std::string& indexName) {
        auto dialect = conn.dialect();
        std::string sql;
        if (dialect->kind() == DialectKind::MySQL) {
            if (!indexExists(conn, table, indexName)) return true;
            sql = "DROP INDEX " + dialect->quoteIdentifier(indexName) + " ON " + dialect->quoteIdentifier(table);
        } else {
            sql = "DROP INDEX IF EXISTS " + dialect->quoteIdentifier(indexName);
        }
        try {
            auto stmt = conn.createStatement();
            stmt->execute(sql);
            return true;
        } catch (const Exception& e) {
            std::cerr << "Schema error (dropIndex): " << e.what() << std::endl;
            return false;
        }
    }

    // 表的现有列名集合
    static std::vector<std::string> existingColumns(IConnection& conn, const std::string& table) {
        std::vector<std::string> out;
        auto dialect = conn.dialect();
        QueryResult r;
        switch (dialect->kind()) {
            case DialectKind::MySQL:
                r = executeQuery(conn,
                    "SELECT COLUMN_NAME FROM information_schema.columns "
                    "WHERE table_schema = DATABASE() AND table_name = ?", {SqlValue(table)});
                break;
            case DialectKind::PostgreSQL:
                r = executeQuery(conn,
                    "SELECT column_name FROM information_schema.columns "
                    "WHERE table_schema = current_schema() AND table_name = ?", {SqlValue(table)});
                break;
            case DialectKind::SQLite:
            default:
                r = executeQuery(conn,
                    "SELECT name FROM pragma_table_info(" + dialect->quoteIdentifier(table) + ")");
                break;
        }
        for (const auto& row : r.rows) out.push_back(valueToString(row[0]));
        return out;
    }

    // 轻量迁移：表不存在则建表；存在则补齐缺失列并补建声明式索引。
    // 补列约束剥离 PRIMARY KEY / AUTO_INCREMENT / NOT NULL / UNIQUE
    // （已有数据的表无法安全添加），DEFAULT 保留。
    template<typename T>
    static bool syncTable(IConnection& conn) {
        if constexpr (!is_registered_v<T>) {
            static_assert(is_registered_v<T>, "类型必须使用 UORM_TABLE 宏进行注册");
            return false;
        }
        auto dialect = conn.dialect();
        const std::string tableName = TableMeta<T>::name;

        if (!tableExists(conn, tableName)) return createTable<T>(conn);

        auto have = existingColumns(conn, tableName);
        std::set<std::string> haveSet(have.begin(), have.end());

        bool changed = false;
        auto fields = TableMeta<T>::get_fields();
        std::apply([&](auto&&... field) {
            ((
                [&] {
                    using FieldType = typename std::decay_t<decltype(field)>::Type;
                    const std::string col = field.column_name;
                    if (haveSet.count(col)) return;
                    std::string baseType = field.sql_type_override
                        ? dialect->normalizeTypeName(field.sql_type_override)
                        : sqlTypeNameFor<FieldType>(*dialect);
                    std::string constraints = cleanConstraints(field.constraint_sql, *dialect);
                    constraints = stripForAddColumn(constraints, *dialect);
                    std::string sql = "ALTER TABLE " + dialect->quoteIdentifier(tableName) +
                                      " ADD COLUMN " + dialect->quoteIdentifier(col) + " " + baseType;
                    if (!constraints.empty()) sql += " " + constraints;
                    if (execute(conn, sql)) {
                        haveSet.insert(col);
                        changed = true;
                    }
                }(), 0
            ), ...);
        }, fields);

        createIndexes<T>(conn);
        return changed;
    }

    template<typename T>
    static bool syncTable() {
        auto connPtr = ConnectionPool::instance().getConnection();
        return syncTable<T>(*connPtr);
    }

    // 新增列（通用 SQL 形式）
    static bool addColumn(IConnection& conn, const std::string& table, const std::string& column,
                          const std::string& type, const std::string& constraints = "") {
        auto dialect = conn.dialect();
        std::string sql = "ALTER TABLE " + dialect->quoteIdentifier(table) +
                          " ADD COLUMN " + dialect->quoteIdentifier(column) + " " + type;
        if (!constraints.empty()) sql += " " + constraints;
        return execute(conn, sql);
    }

    // 删除列（SQLite 3.35+ / MySQL / PG 均支持）
    static bool dropColumn(IConnection& conn, const std::string& table, const std::string& column) {
        auto dialect = conn.dialect();
        std::string sql = "ALTER TABLE " + dialect->quoteIdentifier(table) +
                          " DROP COLUMN " + dialect->quoteIdentifier(column);
        return execute(conn, sql);
    }

    // 重命名列（MySQL 8.0+ / PG / SQLite 3.25+）
    static bool renameColumn(IConnection& conn, const std::string& table,
                             const std::string& oldName, const std::string& newName) {
        auto dialect = conn.dialect();
        std::string sql = "ALTER TABLE " + dialect->quoteIdentifier(table) +
                          " RENAME COLUMN " + dialect->quoteIdentifier(oldName) +
                          " TO " + dialect->quoteIdentifier(newName);
        return execute(conn, sql);
    }

    // 重命名表
    static bool renameTable(IConnection& conn, const std::string& oldName, const std::string& newName) {
        auto dialect = conn.dialect();
        std::string sql = "ALTER TABLE " + dialect->quoteIdentifier(oldName) +
                          " RENAME TO " + dialect->quoteIdentifier(newName);
        return execute(conn, sql);
    }

private:
    // 补建 T 的全部声明式索引（幂等）
    template<typename T>
    static bool createIndexes(IConnection& conn) {
        auto defs = TableMeta<T>::get_index_defs();
        for (const auto& def : defs) {
            std::vector<std::string> cols;
            for (unsigned char i = 0; i < def.columnCount && i < 8; ++i) {
                if (def.columns[i]) cols.emplace_back(def.columns[i]);
            }
            if (!createIndex(conn, TableMeta<T>::name, def.name, cols, def.unique)) return false;
        }
        return true;
    }

    // ADD COLUMN 场景的约束清洗：已有数据的表无法安全添加这些约束
    static std::string stripForAddColumn(std::string constraints, const ISqlDialect& dialect) {
        auto removeToken = [&constraints](const std::string& token) {
            std::size_t pos;
            while ((pos = constraints.find(token)) != std::string::npos) {
                constraints.replace(pos, token.size(), "");
            }
        };
        removeToken("PRIMARY KEY");
        removeToken("AUTO_INCREMENT");
        removeToken(dialect.getAutoIncrementModifier()); // PG IDENTITY / SQLite AUTOINCREMENT
        removeToken("NOT NULL");
        removeToken("UNIQUE");
        // 压缩空白
        std::string out;
        bool prevSpace = true;
        for (char c : constraints) {
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
