#pragma once
// 文件说明：
// Mapper 提供实体对象的 CRUD 操作（v2）。
// - 方言从连接动态获取（同一进程可混用多种数据库）
// - NULL 语义完整（SqlValue）
// - save 自动写回自增主键（MySQL: LAST_INSERT_ID；PG/SQLite: RETURNING）

#include "uORM/orm/Reflection.h"
#include "uORM/driver/ConnectionPool.h"
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
    // 保存实体到数据库 (INSERT)；成功后自增主键自动写回 entity
    static bool save(T& entity) {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();

        std::stringstream ss;
        ss << "INSERT INTO " << dialect->quoteIdentifier(TableMeta<T>::name) << " (";

        auto fields = TableMeta<T>::get_fields();
        bool first = true;

        // 构建列名列表，跳过自增列
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

        // PG/SQLite 3.35+: RETURNING 主键列
        std::string returningColumn;
        if (dialect->supportsReturningId()) {
            returningColumn = findAutoIncrementColumn(fields);
            if (!returningColumn.empty()) {
                ss << " RETURNING " << dialect->quoteIdentifier(returningColumn);
            }
        }

        try {
            auto pstmt = connPtr->prepareStatement(ss.str());

            int index = 1;
            std::apply([&](auto&&... field) {
                ((
                    (!shouldSkipInsert(field, entity) ? (
                        bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
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
                long long newId = connPtr->lastInsertId();
                if (newId > 0) writeBackAutoIncrement(fields, entity, newId);
            }
            return true;
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("保存失败: ") + e.what());
        }
    }

    // 兼容临时对象传参：save({0, "name", ...})
    static bool save(T&& entity) { return save(entity); }

    // 更新实体 (UPDATE)：按主键更新其余字段
    static bool update(const T& entity) {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();

        std::stringstream ss;
        ss << "UPDATE " << dialect->quoteIdentifier(TableMeta<T>::name) << " SET ";

        auto fields = TableMeta<T>::get_fields();
        bool first = true;

        std::apply([&](auto&&... field) {
            ((
                (!isPrimaryKey(field.constraint_sql) ? (
                    ss << (first ? "" : ", ") << dialect->quoteIdentifier(field.column_name) << " = ?",
                    first = false
                ) : 0)
            ), ...);
        }, fields);

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
            auto pstmt = connPtr->prepareStatement(ss.str());

            int index = 1;
            std::apply([&](auto&&... field) {
                ((
                    (!isPrimaryKey(field.constraint_sql) ? (
                        bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
                    ) : 0)
                ), ...);
            }, fields);

            std::apply([&](auto&&... field) {
                ((
                    (isPrimaryKey(field.constraint_sql) ? (
                        bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
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

    // 删除实体 (DELETE)：按主键删除
    static bool remove(const T& entity) {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();

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
            auto pstmt = connPtr->prepareStatement(ss.str());

            int index = 1;
            std::apply([&](auto&&... field) {
                ((
                    (isPrimaryKey(field.constraint_sql) ? (
                        bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
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

    // 清空表数据 (TRUNCATE)：SQLite 无 TRUNCATE，退化为 DELETE FROM
    static bool truncate() {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();
        std::string sql;
        if (dialect->kind() == DialectKind::SQLite) {
            sql = "DELETE FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        } else {
            sql = "TRUNCATE TABLE " + dialect->quoteIdentifier(TableMeta<T>::name);
        }
        try {
            auto stmt = connPtr->createStatement();
            stmt->execute(sql);
            return true;
        } catch (const uORM::Exception&) {
            throw;
        } catch (const std::exception& e) {
            throw SqlError(std::string("清空表失败: ") + e.what());
        }
    }

    // 查询所有实体
    static std::vector<T> findAll() {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();
        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        return executeQuery(connPtr, sql);
    }

    // 根据条件查询单个实体 (支持占位符)
    // 例如: findOne("username = ?", "Alice")
    template<typename... Args>
    static std::optional<T> findOne(const std::string& whereClause, Args&&... args) {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();

        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        if (!whereClause.empty()) {
            sql += " WHERE " + whereClause;
        }
        sql += " LIMIT 1";

        auto list = executeQuery(connPtr, sql, std::forward<Args>(args)...);
        if (list.empty()) return std::nullopt;
        return list[0];
    }

    // 根据条件查询列表 (支持占位符)
    template<typename... Args>
    static std::vector<T> find(const std::string& whereClause, Args&&... args) {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();

        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        if (!whereClause.empty()) {
            sql += " WHERE " + whereClause;
        }
        return executeQuery(connPtr, sql, std::forward<Args>(args)...);
    }

    // 使用 Query 构造器查询列表
    static std::vector<T> select(const Query& query) {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();

        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);

        std::string where = query.getWhere();
        if (!where.empty()) {
            sql += " WHERE " + where;
        }

        sql += query.getOrderBy();
        sql += query.getLimit();
        sql += query.getOffset();

        return executeQueryWithParams(connPtr, sql, query.getParams());
    }

    // 使用 Query 构造器查询单个实体
    static std::optional<T> selectOne(const Query& query) {
        auto results = select(query);
        if (results.empty()) return std::nullopt;
        return results[0];
    }

    // 统计记录数
    static long long count(const Query& query = Query()) {
        auto connPtr = ConnectionPool::instance().getConnection();
        auto dialect = connPtr->dialect();

        std::string sql = "SELECT COUNT(*) AS count_val FROM " + dialect->quoteIdentifier(TableMeta<T>::name);

        std::string where = query.getWhere();
        if (!where.empty()) {
            sql += " WHERE " + where;
        }

        try {
            auto pstmt = connPtr->prepareStatement(sql);

            const auto& params = query.getParams();
            for (size_t i = 0; i < params.size(); ++i) {
                bindSqlValue(pstmt.get(), i + 1, params[i]);
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

private:
    static bool hasDefaultConstraint(const char* constraints) {
        std::string s(constraints);
        return s.find("DEFAULT") != std::string::npos;
    }

    template<typename Field>
    static bool shouldSkipInsert(const Field& field, const T& entity) {
        if (isAutoIncrement(field.constraint_sql)) return true;
        using FieldType = typename std::decay_t<Field>::Type;
        if constexpr (std::is_same_v<FieldType, std::string>) {
            const auto& value = entity.*(field.member_ptr);
            if (value.empty() && hasDefaultConstraint(field.constraint_sql)) return true;
        }
        return false;
    }

    // 在字段元数据中查找自增列名
    template<typename Fields>
    static std::string findAutoIncrementColumn(const Fields& fields) {
        std::string col;
        std::apply([&](auto&&... field) {
            ((  (!col.empty() || !isAutoIncrement(field.constraint_sql) ? 0
                : (col = field.column_name, 0)) ), ...);
        }, fields);
        return col;
    }

    // 将自增主键值写回实体
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
    static std::vector<T> executeQuery(
        std::unique_ptr<IConnection, std::function<void(IConnection*)>>& connPtr,
        const std::string& sql, Args&&... args) {
        std::vector<T> results;
        try {
            auto pstmt = connPtr->prepareStatement(sql);

            int index = 1;
            (bindValue(pstmt.get(), index++, args), ...);

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

    static std::vector<T> executeQueryWithParams(
        std::unique_ptr<IConnection, std::function<void(IConnection*)>>& connPtr,
        const std::string& sql, const std::vector<SqlValue>& params) {
        std::vector<T> results;
        try {
            auto pstmt = connPtr->prepareStatement(sql);

            for (size_t i = 0; i < params.size(); ++i) {
                bindSqlValue(pstmt.get(), i + 1, params[i]);
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

    // 将 C++ 值绑定到 PreparedStatement（统一入口，类型分流）
    template<typename V>
    static void bindValue(IPreparedStatement* pstmt, int index, V&& val) {
        using D = std::decay_t<V>;
        if constexpr (std::is_same_v<D, SqlValue>) {
            bindSqlValue(pstmt, index, val);
        } else if constexpr (std::is_null_pointer_v<D>) {
            pstmt->setNull(index);
        } else if constexpr (std::is_enum_v<D>) {
            pstmt->setInt64(index, static_cast<long long>(val));
        } else if constexpr (std::is_integral_v<D>) {
            pstmt->setInt64(index, static_cast<long long>(val));
        } else if constexpr (std::is_floating_point_v<D>) {
            pstmt->setDouble(index, static_cast<double>(val));
        } else if constexpr (std::is_convertible_v<D, std::string>) {
            pstmt->setString(index, std::string(val));
        } else {
            static_assert(sizeof(D) == 0, "uORM: unsupported parameter type for binding");
        }
    }

    static void bindSqlValue(IPreparedStatement* pstmt, int index, const SqlValue& val) {
        std::visit([&](auto&& arg) {
            using ArgType = std::decay_t<decltype(arg)>;
            if constexpr (std::is_null_pointer_v<ArgType>) {
                pstmt->setNull(index);
            } else if constexpr (std::is_same_v<ArgType, long long>) {
                pstmt->setInt64(index, arg);
            } else if constexpr (std::is_same_v<ArgType, double>) {
                pstmt->setDouble(index, arg);
            } else if constexpr (std::is_same_v<ArgType, std::string>) {
                pstmt->setString(index, arg);
            }
        }, val);
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
