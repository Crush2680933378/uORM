#pragma once
// 文件说明：
// Mapper 提供实体对象的 CRUD 操作（v3）。
// - 所有操作均提供 IConnection& 重载：可在事务内执行（见 Transaction.h）
//   不带连接参数的版本从默认数据源（ConnectionPool）借连接
// - 方言从连接动态获取；NULL 语义完整；自增主键自动写回

#include "uORM/orm/Reflection.h"
#include "uORM/orm/Bind.h"
#include "uORM/driver/ConnectionPool.h"
#include "uORM/orm/Transaction.h"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <optional>
#include <type_traits>
#include "uORM/orm/Query.h"

namespace uORM {

template<typename T>
class Mapper {
public:
    // ---------------- 保存 (INSERT) ----------------
    // 成功后自增主键自动写回 entity
    static bool save(T& entity, IConnection& conn) {
        auto dialect = conn.dialect();

        std::stringstream ss;
        ss << "INSERT INTO " << dialect->quoteIdentifier(TableMeta<T>::name) << " (";

        auto fields = TableMeta<T>::get_fields();
        bool first = true;

        std::apply([&](auto&&... field) {
            ((
                (!shouldSkipInsert(field, entity) ? (
                    ss << (first ? "" : ", ") << dialect->quoteIdentifier(field.column_name),
                    first = false
                ) : 0)
            ), ...);
        }, fields);

        ss << ") VALUES (";

        first = true;
        std::apply([&](auto&&... field) {
            ((
                (!shouldSkipInsert(field, entity) ? (
                    ss << (first ? "" : ", ") << "?",
                    first = false
                ) : 0)
            ), ...);
        }, fields);

        ss << ")";

        std::string returningColumn;
        if (dialect->supportsReturningId()) {
            returningColumn = findAutoIncrementColumn(fields);
            if (!returningColumn.empty()) {
                ss << " RETURNING " << dialect->quoteIdentifier(returningColumn);
            }
        }

        try {
            auto pstmt = conn.prepareStatement(ss.str());

            int index = 1;
            std::apply([&](auto&&... field) {
                ((
                    (!shouldSkipInsert(field, entity) ? (
                        uORM::bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
                    ) : 0)
                ), ...);
            }, fields);

            if (dialect->supportsReturningId()) {
                auto res = pstmt->executeQuery();
                if (res->next() && !returningColumn.empty()) {
                    writeBackAutoIncrement(fields, entity, res->getSqlValue(0));
                }
            } else {
                pstmt->executeUpdate();
                long long newId = conn.lastInsertId();
                if (newId > 0) writeBackAutoIncrement(fields, entity, newId);
            }
            return true;
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("保存失败: ") + e.what());
        }
    }

    static bool save(T& entity) {
        auto conn = ConnectionPool::instance().getConnection();
        return save(entity, *conn);
    }

    // 兼容临时对象传参：save({0, "name", ...})
    static bool save(T&& entity) { return save(entity); }

    // ---------------- 更新 (UPDATE)：按主键 ----------------
    // 与 save 一致：空字符串且有默认值的字段不参与 SET（避免清空数据库管理的列）
    static bool update(const T& entity, IConnection& conn) {
        auto dialect = conn.dialect();

        std::stringstream ss;
        ss << "UPDATE " << dialect->quoteIdentifier(TableMeta<T>::name) << " SET ";

        auto fields = TableMeta<T>::get_fields();
        bool first = true;

        std::apply([&](auto&&... field) {
            ((
                (shouldSkipUpdate(field, entity) ? 0 : (
                    ss << (first ? "" : ", ") << dialect->quoteIdentifier(field.column_name) << " = ?",
                    first = false
                ))
            ), ...);
        }, fields);

        // 全部字段都被跳过时退化为空 UPDATE；直接成功返回
        if (first) return true;

        ss << " WHERE ";
        first = true;
        std::apply([&](auto&&... field) {
            ((
                (isPrimaryKey(field.constraint_sql) ? (
                    ss << (first ? "" : " AND ") << dialect->quoteIdentifier(field.column_name) << " = ?",
                    first = false
                ) : 0)
            ), ...);
        }, fields);

        try {
            auto pstmt = conn.prepareStatement(ss.str());

            int index = 1;
            std::apply([&](auto&&... field) {
                ((
                    (shouldSkipUpdate(field, entity) ? 0 : (
                        uORM::bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
                    ))
                ), ...);
            }, fields);

            std::apply([&](auto&&... field) {
                ((
                    (isPrimaryKey(field.constraint_sql) ? (
                        uORM::bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
                    ) : 0)
                ), ...);
            }, fields);

            pstmt->executeUpdate();
            return true;
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("更新失败: ") + e.what());
        }
    }

    static bool update(const T& entity) {
        auto conn = ConnectionPool::instance().getConnection();
        return update(entity, *conn);
    }

    // ---------------- 删除 (DELETE)：按主键 ----------------
    static bool remove(const T& entity, IConnection& conn) {
        auto dialect = conn.dialect();

        std::stringstream ss;
        ss << "DELETE FROM " << dialect->quoteIdentifier(TableMeta<T>::name) << " WHERE ";

        bool first = true;
        auto fields = TableMeta<T>::get_fields();
        std::apply([&](auto&&... field) {
            ((
                (isPrimaryKey(field.constraint_sql) ? (
                    ss << (first ? "" : " AND ") << dialect->quoteIdentifier(field.column_name) << " = ?",
                    first = false
                ) : 0)
            ), ...);
        }, fields);

        try {
            auto pstmt = conn.prepareStatement(ss.str());

            int index = 1;
            std::apply([&](auto&&... field) {
                ((
                    (isPrimaryKey(field.constraint_sql) ? (
                        uORM::bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
                    ) : 0)
                ), ...);
            }, fields);

            pstmt->executeUpdate();
            return true;
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("删除失败: ") + e.what());
        }
    }

    static bool remove(const T& entity) {
        auto conn = ConnectionPool::instance().getConnection();
        return remove(entity, *conn);
    }

    // ---------------- 清空 (TRUNCATE / DELETE FROM) ----------------
    static bool truncate(IConnection& conn) {
        auto dialect = conn.dialect();
        std::string sql;
        if (dialect->kind() == DialectKind::SQLite) {
            sql = "DELETE FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        } else {
            sql = "TRUNCATE TABLE " + dialect->quoteIdentifier(TableMeta<T>::name);
        }
        try {
            auto stmt = conn.createStatement();
            stmt->execute(sql);
            return true;
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("清空表失败: ") + e.what());
        }
    }

    static bool truncate() {
        auto conn = ConnectionPool::instance().getConnection();
        return truncate(*conn);
    }

    // ---------------- 查询 ----------------
    static std::vector<T> findAll(IConnection& conn) {
        auto dialect = conn.dialect();
        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        return queryRows(conn, sql);
    }

    static std::vector<T> findAll() {
        auto conn = ConnectionPool::instance().getConnection();
        return findAll(*conn);
    }

    template<typename... Args>
    static std::optional<T> findOne(IConnection& conn, const std::string& whereClause, Args&&... args) {
        auto dialect = conn.dialect();

        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        if (!whereClause.empty()) {
            sql += " WHERE " + whereClause;
        }
        sql += " LIMIT 1";

        auto list = queryRows(conn, sql, std::forward<Args>(args)...);
        if (list.empty()) return std::nullopt;
        return list[0];
    }

    template<typename... Args>
    static std::optional<T> findOne(const std::string& whereClause, Args&&... args) {
        auto conn = ConnectionPool::instance().getConnection();
        return findOne(*conn, whereClause, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static std::vector<T> find(IConnection& conn, const std::string& whereClause, Args&&... args) {
        auto dialect = conn.dialect();

        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        if (!whereClause.empty()) {
            sql += " WHERE " + whereClause;
        }
        return queryRows(conn, sql, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static std::vector<T> find(const std::string& whereClause, Args&&... args) {
        auto conn = ConnectionPool::instance().getConnection();
        return find(*conn, whereClause, std::forward<Args>(args)...);
    }

    static std::vector<T> select(IConnection& conn, const Query& query) {
        auto dialect = conn.dialect();

        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);

        std::string where = query.getWhere();
        if (!where.empty()) {
            sql += " WHERE " + where;
        }

        sql += query.getOrderBy();
        sql += query.getLimit();
        sql += query.getOffset();

        return queryRowsWithParams(conn, sql, query.getParams());
    }

    static std::vector<T> select(const Query& query) {
        auto conn = ConnectionPool::instance().getConnection();
        return select(*conn, query);
    }

    static std::optional<T> selectOne(IConnection& conn, const Query& query) {
        auto results = select(conn, query);
        if (results.empty()) return std::nullopt;
        return results[0];
    }

    static std::optional<T> selectOne(const Query& query) {
        auto conn = ConnectionPool::instance().getConnection();
        return selectOne(*conn, query);
    }

    static long long count(IConnection& conn, const Query& query = Query()) {
        auto dialect = conn.dialect();

        std::string sql = "SELECT COUNT(*) AS count_val FROM " + dialect->quoteIdentifier(TableMeta<T>::name);

        std::string where = query.getWhere();
        if (!where.empty()) {
            sql += " WHERE " + where;
        }

        try {
            auto pstmt = conn.prepareStatement(sql);

            const auto& params = query.getParams();
            for (size_t i = 0; i < params.size(); ++i) {
                uORM::bindSqlValue(pstmt.get(), static_cast<int>(i + 1), params[i]);
            }

            auto res = pstmt->executeQuery();
            if (res->next()) {
                return res->getInt64("count_val");
            }
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("Count查询失败: ") + e.what());
        }
        return 0;
    }

    static long long count(const Query& query = Query()) {
        auto conn = ConnectionPool::instance().getConnection();
        return count(*conn, query);
    }

private:
    static bool hasDefaultConstraint(const char* constraints) {
        std::string s(constraints);
        return s.find("DEFAULT") != std::string::npos;
    }

    template<typename Field>
    static bool shouldSkipInsert(const Field& field, const T& entity) {
        if (isAutoIncrement(field.constraint_sql)) return true;
        return skipsEmptyDefault(field, entity);
    }

    template<typename Field>
    static bool shouldSkipUpdate(const Field& field, const T& entity) {
        if (isPrimaryKey(field.constraint_sql) || isAutoIncrement(field.constraint_sql)) return true;
        return skipsEmptyDefault(field, entity);
    }

    template<typename Field>
    static bool skipsEmptyDefault(const Field& field, const T& entity) {
        using FieldType = typename std::decay_t<Field>::Type;
        if constexpr (std::is_same_v<FieldType, std::string>) {
            const auto& value = entity.*(field.member_ptr);
            if (value.empty() && hasDefaultConstraint(field.constraint_sql)) return true;
        }
        return false;
    }

    template<typename Fields>
    static std::string findAutoIncrementColumn(const Fields& fields) {
        std::string col;
        std::apply([&](auto&&... field) {
            ((  (!col.empty() || !isAutoIncrement(field.constraint_sql) ? 0
                : (col = field.column_name, 0)) ), ...);
        }, fields);
        return col;
    }

    template<typename Fields>
    static void writeBackAutoIncrement(const Fields& fields, T& entity, const SqlValue& v) {
        if (isNullValue(v)) return;
        std::apply([&](auto&&... field) {
            ((  (!isAutoIncrement(field.constraint_sql) ? 0
                : (assignGeneratedValue(entity.*(field.member_ptr), v), 0)) ), ...);
        }, fields);
    }

    template<typename V>
    static void assignGeneratedValue(V& target, const SqlValue& v) {
        if constexpr (std::is_integral_v<V>) target = static_cast<V>(valueToInt64(v));
        else if constexpr (std::is_floating_point_v<V>) target = static_cast<V>(valueToDouble(v));
        else if constexpr (std::is_same_v<V, std::string>) target = valueToString(v);
        else { /* 不支持的类型：忽略 */ }
    }

    static T mapRow(IResultSet* res) {
        T entity;
        auto fields = TableMeta<T>::get_fields();
        std::apply([&](auto&&... field) {
            ((
                entity.*(field.member_ptr) = getValue<typename std::decay_t<decltype(field)>::Type>(res, field.column_name)
            ), ...);
        }, fields);
        return entity;
    }

    template<typename... Args>
    static std::vector<T> queryRows(IConnection& conn, const std::string& sql, Args&&... args) {
        std::vector<T> results;
        try {
            auto pstmt = conn.prepareStatement(sql);

            int index = 1;
            (uORM::bindValue(pstmt.get(), index++, args), ...);

            auto res = pstmt->executeQuery();
            while (res->next()) {
                results.push_back(mapRow(res.get()));
            }
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("查询失败: ") + e.what());
        }
        return results;
    }

    static std::vector<T> queryRowsWithParams(IConnection& conn, const std::string& sql,
                                              const std::vector<SqlValue>& params) {
        std::vector<T> results;
        try {
            auto pstmt = conn.prepareStatement(sql);

            for (size_t i = 0; i < params.size(); ++i) {
                uORM::bindSqlValue(pstmt.get(), static_cast<int>(i + 1), params[i]);
            }

            auto res = pstmt->executeQuery();
            while (res->next()) {
                results.push_back(mapRow(res.get()));
            }
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("查询失败: ") + e.what());
        }
        return results;
    }

    // 检查约束中是否包含 AUTO_INCREMENT
    static bool isAutoIncrement(const char* constraints) {
        std::string s(constraints);
        return s.find("AUTO_INCREMENT") != std::string::npos;
    }

    // 检查约束中是否包含 PRIMARY KEY
    static bool isPrimaryKey(const char* constraints) {
        std::string s(constraints);
        return s.find("PRIMARY KEY") != std::string::npos;
    }

    // 从结果集获取值并转换为 C++ 类型
    template<typename V>
    static V getValue(IResultSet* res, const char* colName) {
        SqlValue v = res->getSqlValue(std::string(colName));
        if constexpr (std::is_same_v<V, bool>) return valueToBool(v);
        else if constexpr (std::is_floating_point_v<V>) return static_cast<V>(valueToDouble(v));
        else if constexpr (std::is_integral_v<V>) return static_cast<V>(valueToInt64(v));
        else if constexpr (std::is_same_v<V, std::string>) return valueToString(v);
        else return V{};
    }
};

} // namespace uORM
