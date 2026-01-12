#pragma once 
#include "uORM/orm/Reflection.h" 
#include "uORM/driver/ConnectionPool.h" 
#include <string> 
#include <vector> 
#include <sstream> 
#include <iostream> 
#include <optional> 
#include "uORM/orm/Query.h"

namespace uORM { 

// Mapper 类提供实体对象的 CRUD 操作
template<typename T> 
class Mapper { 
public: 
    // 保存实体到数据库 (INSERT)
    // 返回 true 表示成功。
    static bool save(const T& entity) { 
        auto dialect = ConnectionPool::instance().getDialect(); 
        if (!dialect) return false; 

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
        
        // 构建参数占位符 (?)
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
        
        // 处理 RETURNING id (PostgreSQL) 
        if (dialect->supportsReturningId()) { 
            ss << " " << dialect->getLastInsertIdSql(); 
        } 
        
        try { 
            auto connPtr = ConnectionPool::instance().getConnection(); 
            auto pstmt = connPtr->prepareStatement(ss.str()); 
            
            // 绑定参数值
            int index = 1; 
            std::apply([&](auto&&... field) { 
                (( 
                    (!shouldSkipInsert(field, entity) ? ( 
                        bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0 // 修复: 逗号表达式确保返回 void 兼容类型或整数
                    ) : 0) 
                ), ...); 
            }, fields); 
            
            if (dialect->supportsReturningId()) { 
                auto res = pstmt->executeQuery(); 
                // PG: 这里可以获取 ID
            } else { 
                pstmt->executeUpdate(); 
            } 
            return true; 
        } catch (const uORM::Exception& e) {
            throw; // Re-throw uORM exceptions
        } catch (const std::exception& e) { 
            throw SqlError(std::string("保存失败: ") + e.what());
        } 
    } 

    // 更新实体 (UPDATE)
    // 根据主键更新所有字段 (除主键外)
    static bool update(const T& entity) {
        auto dialect = ConnectionPool::instance().getDialect(); 
        if (!dialect) return false; 

        std::stringstream ss; 
        ss << "UPDATE " << dialect->quoteIdentifier(TableMeta<T>::name) << " SET "; 
        
        auto fields = TableMeta<T>::get_fields(); 
        bool first = true; 
        
        // SET clause
        std::apply([&](auto&&... field) { 
            (( 
                (!isPrimaryKey(field.constraint_sql) ? ( 
                    ss << (first ? "" : ", ") << dialect->quoteIdentifier(field.column_name) << " = ?", 
                    first = false 
                ) : 0) 
            ), ...); 
        }, fields); 
        
        // WHERE clause
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
            auto connPtr = ConnectionPool::instance().getConnection(); 
            auto pstmt = connPtr->prepareStatement(ss.str()); 
            
            int index = 1; 
            // Bind SET values
            std::apply([&](auto&&... field) { 
                (( 
                    (!isPrimaryKey(field.constraint_sql) ? ( 
                        bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
                    ) : 0) 
                ), ...); 
            }, fields); 
            
            // Bind WHERE values (PKs)
            std::apply([&](auto&&... field) { 
                (( 
                    (isPrimaryKey(field.constraint_sql) ? ( 
                        bindValue(pstmt.get(), index++, entity.*(field.member_ptr)), 0
                    ) : 0) 
                ), ...); 
            }, fields); 
            
            pstmt->executeUpdate(); 
            return true; 
        } catch (const uORM::Exception& e) {
            throw; // Re-throw uORM exceptions
        } catch (const std::exception& e) { 
            throw SqlError(std::string("更新失败: ") + e.what());
        }
    }

    // 删除实体 (DELETE)
    // 根据主键删除
    static bool remove(const T& entity) {
        auto dialect = ConnectionPool::instance().getDialect(); 
        if (!dialect) return false; 

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
            auto connPtr = ConnectionPool::instance().getConnection(); 
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
        } catch (const uORM::Exception& e) {
            throw; // Re-throw uORM exceptions
        } catch (const std::exception& e) { 
            throw SqlError(std::string("删除失败: ") + e.what());
        }
    }

    // 清空表数据 (TRUNCATE)
    static bool truncate() {
        auto dialect = ConnectionPool::instance().getDialect();
        if (!dialect) return false;

        std::string sql = "TRUNCATE TABLE " + dialect->quoteIdentifier(TableMeta<T>::name);
        
        try {
            auto connPtr = ConnectionPool::instance().getConnection();
            auto stmt = connPtr->createStatement();
            stmt->execute(sql);
            return true;
        } catch (const uORM::Exception& e) {
            throw; // Re-throw uORM exceptions
        } catch (const std::exception& e) {
            throw SqlError(std::string("清空表失败: ") + e.what());
        }
    }

    // 查询所有实体
    static std::vector<T> findAll() { 
        auto dialect = ConnectionPool::instance().getDialect(); 
        if (!dialect) return {}; 

        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name); 
        return executeQuery(sql);
    } 
    
    // 根据条件查询单个实体 (支持占位符)
    // 例如: findOne("username = ?", "Alice")
    template<typename... Args>
    static std::optional<T> findOne(const std::string& whereClause, Args&&... args) {
        auto dialect = ConnectionPool::instance().getDialect();
        if (!dialect) return std::nullopt;
        
        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        if (!whereClause.empty()) {
            sql += " WHERE " + whereClause;
        }
        sql += " LIMIT 1";
        
        auto list = executeQuery(sql, std::forward<Args>(args)...);
        if (list.empty()) return std::nullopt;
        return list[0];
    }
    
    // 根据条件查询列表 (支持占位符)
    // 例如: find("age > ? AND gender = ?", 18, "male")
    template<typename... Args>
    static std::vector<T> find(const std::string& whereClause, Args&&... args) {
        auto dialect = ConnectionPool::instance().getDialect();
        if (!dialect) return {};
        
        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        if (!whereClause.empty()) {
            sql += " WHERE " + whereClause;
        }
        return executeQuery(sql, std::forward<Args>(args)...);
    }

    // 使用 Query 构造器查询列表
    static std::vector<T> select(const Query& query) {
        auto dialect = ConnectionPool::instance().getDialect();
        if (!dialect) return {};
        
        std::string sql = "SELECT * FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        
        std::string where = query.getWhere();
        if (!where.empty()) {
            sql += " WHERE " + where;
        }
        
        sql += query.getOrderBy();
        sql += query.getLimit();
        sql += query.getOffset();
        
        return executeQueryWithParams(sql, query.getParams());
    }

    // 使用 Query 构造器查询单个实体
    static std::optional<T> selectOne(const Query& query) {
        auto results = select(query); // 注意：如果 query 没有 limit 1，这里可能会查询多条，性能稍差。建议 query.limit(1)
        if (results.empty()) return std::nullopt;
        return results[0];
    }

    // 统计记录数
    static long long count(const Query& query = Query()) {
        auto dialect = ConnectionPool::instance().getDialect();
        if (!dialect) return 0;

        std::string sql = "SELECT COUNT(*) FROM " + dialect->quoteIdentifier(TableMeta<T>::name);
        
        std::string where = query.getWhere();
        if (!where.empty()) {
            sql += " WHERE " + where;
        }

        try {
            auto connPtr = ConnectionPool::instance().getConnection();
            auto pstmt = connPtr->prepareStatement(sql);
            
            const auto& params = query.getParams();
            for (size_t i = 0; i < params.size(); ++i) {
                bindSqlValue(pstmt.get(), i + 1, params[i]);
            }
            
            auto res = pstmt->executeQuery();
            if (res->next()) {
                // index 1 for the first column
                return res->getInt64(1);
            }
        } catch (const uORM::Exception& e) {
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
    static std::vector<T> executeQuery(const std::string& sql, Args&&... args) {
        std::vector<T> results;
        try {
            auto connPtr = ConnectionPool::instance().getConnection();
            auto pstmt = connPtr->prepareStatement(sql);
            
            int index = 1;
            (bindValue(pstmt.get(), index++, args), ...);
            
            auto res = pstmt->executeQuery();
            while (res->next()) {
                results.push_back(mapRow(res.get()));
            }
        } catch (const uORM::Exception& e) {
            throw; // Re-throw uORM exceptions
        } catch (const std::exception& e) {
            throw SqlError(std::string("查询失败: ") + e.what());
        }
        return results;
    }

    static std::vector<T> executeQueryWithParams(const std::string& sql, const std::vector<SqlValue>& params) {
        std::vector<T> results;
        try {
            auto connPtr = ConnectionPool::instance().getConnection();
            auto pstmt = connPtr->prepareStatement(sql);
            
            for (size_t i = 0; i < params.size(); ++i) {
                bindSqlValue(pstmt.get(), i + 1, params[i]);
            }
            
            auto res = pstmt->executeQuery();
            while (res->next()) {
                results.push_back(mapRow(res.get()));
            }
        } catch (const uORM::Exception& e) {
            throw; // Re-throw uORM exceptions
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

    // 辅助函数：将 C++ 值绑定到 PreparedStatement
    static void bindValue(IPreparedStatement* pstmt, int index, const int& val) { pstmt->setInt(index, val); } 
    static void bindValue(IPreparedStatement* pstmt, int index, const long& val) { pstmt->setInt64(index, val); } 
    static void bindValue(IPreparedStatement* pstmt, int index, const long long& val) { pstmt->setInt64(index, val); } 
    static void bindValue(IPreparedStatement* pstmt, int index, const unsigned int& val) { pstmt->setUInt(index, val); } 
    static void bindValue(IPreparedStatement* pstmt, int index, const unsigned long long& val) { pstmt->setInt64(index, static_cast<long long>(val)); } // MySQL Connector C++ doesn't have setUInt64 in older versions or wrapper needs it
    static void bindValue(IPreparedStatement* pstmt, int index, const std::string& val) { pstmt->setString(index, val); } 
    static void bindValue(IPreparedStatement* pstmt, int index, const char* val) { pstmt->setString(index, val); } 
    static void bindValue(IPreparedStatement* pstmt, int index, const bool& val) { pstmt->setBoolean(index, val); } 
    static void bindValue(IPreparedStatement* pstmt, int index, const double& val) { pstmt->setDouble(index, val); } 
    // 如有需要可添加更多重载 

    static void bindSqlValue(IPreparedStatement* pstmt, int index, const SqlValue& val) {
        std::visit([&](auto&& arg) {
            using ArgType = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<ArgType, std::nullptr_t>) {
                // Not supported
            } else {
                bindValue(pstmt, index, arg);
            }
        }, val);
    }

    // 辅助函数：从 ResultSet 获取值并转换为 C++ 类型
    template<typename V> 
    static V getValue(IResultSet* res, const char* colName) { 
        if constexpr (std::is_same_v<V, int>) return res->getInt(colName); 
        else if constexpr (std::is_same_v<V, long>) return res->getInt64(colName); 
        else if constexpr (std::is_same_v<V, long long>) return res->getInt64(colName); 
        else if constexpr (std::is_same_v<V, unsigned long long>) return static_cast<unsigned long long>(res->getInt64(colName));
        else if constexpr (std::is_same_v<V, std::string>) return res->getString(colName); 
        else if constexpr (std::is_same_v<V, bool>) return res->getBoolean(colName); 
        else if constexpr (std::is_same_v<V, double>) return res->getDouble(colName); 
        else return V{}; 
    } 
}; 

} // namespace uORM 
