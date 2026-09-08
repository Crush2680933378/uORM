#pragma once
// =====================================================================
// DAO 层：泛型基础 DAO
//
// EntityTraits —— 实体特征：声明主键类型与主键列，供 BaseDao 泛型化。
// BaseDao     —— 封装该实体的全部通用数据访问（CRUD / 分页 / 批量 / 计数 / 事务）。
//
// 业务代码只依赖 BaseDao 与具体 DAO，不直接接触 SQL 拼接与连接管理。
// =====================================================================
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <uORM/orm/ORM.h>

namespace demo {

// 实体特征主模板：未特化的实体无法实例化 DAO（编译期报错）
template<typename E>
struct EntityTraits;

// 特征定义辅助宏：主键类型 / 表名 / 主键列名 / 取键函数
#define DEMO_ENTITY_TRAITS(E, Table, KeyType_, KeyColumn, KeyGetter) \
    template<> struct EntityTraits<E> {                              \
        using KeyType = KeyType_;                                    \
        static constexpr const char* table      = Table;             \
        static constexpr const char* keyColumn  = KeyColumn;         \
        static KeyType keyOf(const E& e) { return KeyGetter; }       \
    };

template<typename E>
class BaseDao {
public:
    using Traits  = EntityTraits<E>;
    using KeyType = typename Traits::KeyType;

    explicit BaseDao(uORM::DataSource& ds) : ds_(ds) {}

    // ---------------- 查询 ----------------

    // 按主键查一条
    std::optional<E> findById(KeyType id) {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<E>::findOne(c, keyWhere(c), id);
        });
    }

    // 全量（建议配合 findPage 使用）
    std::vector<E> findAll() {
        return withConn([&](uORM::IConnection& c) { return uORM::Mapper<E>::findAll(c); });
    }

    // 条件查询（参数化占位符，杜绝注入）
    template<typename... Args>
    std::vector<E> findBy(const std::string& where, Args&&... args) {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<E>::find(c, where, std::forward<Args>(args)...);
        });
    }

    // 条件查询单条
    template<typename... Args>
    std::optional<E> findOneBy(const std::string& where, Args&&... args) {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<E>::findOne(c, where, std::forward<Args>(args)...);
        });
    }

    // 分页 + 排序（列名经方言引用）
    std::vector<E> findPage(int page, int pageSize,
                            const std::string& orderByColumn = {},
                            bool desc = false) {
        return withConn([&](uORM::IConnection& c) {
            uORM::Query q;
            if (!orderByColumn.empty())
                q.orderBy(c.dialect()->quoteIdentifier(orderByColumn), !desc);
            q.limit(pageSize).offset((page - 1) * pageSize);
            return uORM::Mapper<E>::select(c, q);
        });
    }

    // 计数；可带条件与参数：count("age > ? AND city = ?", 18, "北京")
    long long count() {
        return withConn([&](uORM::IConnection& c) { return uORM::Mapper<E>::count(c, uORM::Query{}); });
    }

    template<typename... Args>
    long long count(const std::string& where, Args&&... args) {
        return withConn([&](uORM::IConnection& c) {
            uORM::Query q;
            q.whereRaw(where);
            (q.param(uORM::SqlValue(std::forward<Args>(args))), ...);
            return uORM::Mapper<E>::count(c, q);
        });
    }

    // ---------------- 写入 ----------------

    // 插入，自增主键写回实体；返回主键
    KeyType insert(E& e) {
        return withConn([&](uORM::IConnection& c) {
            uORM::Mapper<E>::save(e, c);
            return Traits::keyOf(e);
        });
    }

    // 按主键整行更新
    bool update(const E& e) {
        return withConn([&](uORM::IConnection& c) { return uORM::Mapper<E>::update(e, c); });
    }

    // 部分更新：只更新指定的实体成员（成员指针，编译期检查），其余字段不动
    template<typename... Ms>
    bool updateFields(E& e, Ms... members) {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<E>::updateSome(e, c, members...);
        });
    }

    // 按主键删除
    bool removeById(KeyType id) {
        return withConn([&](uORM::IConnection& c) {
            auto dialect = c.dialect();
            auto sql = "DELETE FROM " + dialect->quoteIdentifier(Traits::table) +
                       " WHERE " + dialect->quoteIdentifier(Traits::keyColumn) + " = ?";
            auto stmt = c.prepareStatement(sql);
            uORM::bindSqlValue(stmt.get(), 1, uORM::SqlValue(static_cast<long long>(id)));
            stmt->executeUpdate();
            return true;
        });
    }

    // 批量插入（单条多行 VALUES，分块）
    bool insertRange(std::vector<E>& items) {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<E>::saveRange(items, c);
        });
    }

    // upsert：按主键存在则更新
    bool saveOrUpdate(E& e) {
        return withConn([&](uORM::IConnection& c) {
            return uORM::Mapper<E>::saveOrUpdate(e, c);
        });
    }

    // ---------------- 原生 SQL / 事务 ----------------

    uORM::QueryResult query(const std::string& sql, const std::vector<uORM::SqlValue>& params = {}) {
        return ds_.query(sql, params);
    }

    std::uint64_t execute(const std::string& sql, const std::vector<uORM::SqlValue>& params = {}) {
        return ds_.execute(sql, params);
    }

    // 事务：body 内可使用本 DAO 与其他 DAO（共享同一连接，异常自动回滚）
    template<typename F>
    auto transaction(F&& body) -> decltype(body(std::declval<uORM::IConnection&>())) {
        auto conn = ds_.getConnection();
        uORM::Transaction tx(*conn);
        using R = decltype(body(*conn));
        try {
            if constexpr (std::is_void_v<R>) {
                body(*conn);
                tx.commit();
            } else {
                R r = body(*conn);
                tx.commit();
                return r;
            }
        } catch (...) {
            tx.rollback();
            throw;
        }
    }

    uORM::DataSource& source() { return ds_; }

protected:
    // 主键条件（方言引用，防保留字冲突）
    static std::string keyWhere(uORM::IConnection& c) {
        return c.dialect()->quoteIdentifier(Traits::keyColumn) + " = ?";
    }

    template<typename F>
    auto withConn(F&& fn) -> decltype(fn(std::declval<uORM::IConnection&>())) {
        auto conn = ds_.getConnection();
        return fn(*conn);
    }

    uORM::DataSource& ds_;
};

} // namespace demo
