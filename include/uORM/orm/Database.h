#pragma once
// 文件说明：
// Database —— 绑定数据源的实例化门面（推荐入口）。
// 相比静态 Mapper（绑定全局连接池），Database 把"一个数据源"作为对象持有：
//
//   uORM::DataSource ds(cfg);
//   uORM::Database db(ds);
//   db.createTable<User>();
//   User u{0, "Tom", 18};
//   db.save(u);                                   // u.id 自动写回
//   auto tom = db.findById<User>(u.id);
//   auto adults = db.find<User>("age >= ?", 18);
//
// 每个操作自动借还连接；db.tx(lambda) 提供事务。
// 全局池用户：uORM::Database db(uORM::ConnectionPool::instance().source());
//
// 命名说明：不能叫 Orm.h —— Windows 文件系统大小写不敏感，会与伞头文件 ORM.h 冲突。

#include "uORM/driver/DataSource.h"
#include "uORM/orm/Mapper.h"
#include "uORM/orm/Schema.h"
#include "uORM/orm/Transaction.h"
#include "uORM/orm/TypedQuery.h"
#include "uORM/orm/QueryResult.h"

#include <optional>
#include <string>
#include <vector>

namespace uORM {

class Database {
public:
    explicit Database(DataSource& ds) : ds_(ds) {}
    explicit Database(DataSource* ds) : ds_(*ds) {}

    // ---------------- Schema ----------------
    template<typename T>
    bool createTable() { return withConn([&](IConnection& c) { return Schema::createTable<T>(c); }); }

    template<typename T>
    bool dropTable() { return withConn([&](IConnection& c) { return Schema::dropTable<T>(c); }); }

    // 轻量迁移：缺表建表 / 缺列补列 / 补建索引
    template<typename T>
    bool syncTable() { return withConn([&](IConnection& c) { return Schema::syncTable<T>(c); }); }

    bool tableExists(const std::string& table) { return withConn([&](IConnection& c) { return Schema::tableExists(c, table); }); }
    bool indexExists(const std::string& table, const std::string& indexName) {
        return withConn([&](IConnection& c) { return Schema::indexExists(c, table, indexName); });
    }
    bool createIndex(const std::string& table, const std::string& indexName,
                     const std::vector<std::string>& columns, bool unique = false) {
        return withConn([&](IConnection& c) { return Schema::createIndex(c, table, indexName, columns, unique); });
    }
    bool dropIndex(const std::string& table, const std::string& indexName) {
        return withConn([&](IConnection& c) { return Schema::dropIndex(c, table, indexName); });
    }
    bool addColumn(const std::string& table, const std::string& column,
                   const std::string& type, const std::string& constraints = "") {
        return withConn([&](IConnection& c) { return Schema::addColumn(c, table, column, type, constraints); });
    }
    bool dropColumn(const std::string& table, const std::string& column) {
        return withConn([&](IConnection& c) { return Schema::dropColumn(c, table, column); });
    }
    bool renameColumn(const std::string& table, const std::string& oldName, const std::string& newName) {
        return withConn([&](IConnection& c) { return Schema::renameColumn(c, table, oldName, newName); });
    }
    bool renameTable(const std::string& oldName, const std::string& newName) {
        return withConn([&](IConnection& c) { return Schema::renameTable(c, oldName, newName); });
    }

    // ---------------- 单行 CRUD ----------------
    template<typename T>
    bool save(T& entity) { return withConn([&](IConnection& c) { return Mapper<T>::save(entity, c); }); }

    // 批量插入：分块的单条多行 VALUES 语句，自增主键按序写回。
    // 注意：批量走统一列集（除自增主键），不做逐行"空串默认值跳过"。
    template<typename T>
    bool saveRange(std::vector<T>& items) { return withConn([&](IConnection& c) { return Mapper<T>::saveRange(items, c); }); }

    template<typename T>
    bool saveOrUpdate(T& entity) { return withConn([&](IConnection& c) { return Mapper<T>::saveOrUpdate(entity, c); }); }

    template<typename T>
    bool update(const T& entity) { return withConn([&](IConnection& c) { return Mapper<T>::update(entity, c); }); }

    template<typename T>
    bool remove(const T& entity) { return withConn([&](IConnection& c) { return Mapper<T>::remove(entity, c); }); }

    template<typename T>
    bool truncate() { return withConn([&](IConnection& c) { return Mapper<T>::truncate(c); }); }

    // ---------------- 查询 ----------------
    // 按主键查一条（实体须有 PRIMARY KEY 字段）
    template<typename T, typename ID>
    std::optional<T> findById(ID id) {
        return withConn([&](IConnection& c) {
            auto pk = Mapper<T>::primaryKeyColumn();
            if (pk.empty()) return std::optional<T>();
            return Mapper<T>::findOne(c, c.dialect()->quoteIdentifier(pk) + " = ?", id);
        });
    }

    template<typename T, typename... Args>
    std::optional<T> findOne(const std::string& whereClause, Args&&... args) {
        return withConn([&](IConnection& c) { return Mapper<T>::findOne(c, whereClause, std::forward<Args>(args)...); });
    }

    template<typename T, typename... Args>
    std::vector<T> find(const std::string& whereClause, Args&&... args) {
        return withConn([&](IConnection& c) { return Mapper<T>::find(c, whereClause, std::forward<Args>(args)...); });
    }

    template<typename T>
    std::vector<T> select(const Query& query) { return withConn([&](IConnection& c) { return Mapper<T>::select(c, query); }); }

    template<typename T>
    std::optional<T> selectOne(const Query& query) { return withConn([&](IConnection& c) { return Mapper<T>::selectOne(c, query); }); }

    template<typename T>
    long long count(const Query& query = Query()) { return withConn([&](IConnection& c) { return Mapper<T>::count(c, query); }); }

    template<typename T>
    SqlValue aggregate(const std::string& funcExpr, const Query& query = Query()) {
        return withConn([&](IConnection& c) { return Mapper<T>::aggregate(c, funcExpr, query); });
    }

    template<typename T>
    QueryResult selectDynamic(const Query& query) { return withConn([&](IConnection& c) { return Mapper<T>::selectDynamic(c, query); }); }

    // ---------------- 原生 SQL ----------------
    QueryResult query(const std::string& sql, const std::vector<SqlValue>& params = {}) {
        return ds_.query(sql, params);
    }

    unsigned long long execute(const std::string& sql, const std::vector<SqlValue>& params = {}) {
        return ds_.execute(sql, params);
    }

    // ---------------- 类型安全查询（推荐） ----------------
    // 成员指针指定列，编译期防拼错：
    //   auto rows = db.query<User>()
    //       .where(&User::age, uORM::GT, 18)
    //       .orderByDesc(&User::id).limit(10).all();
    template<typename T>
    TypedQuery<T> query() { return TypedQuery<T>(ds_); }

    // ---------------- RAII 事务作用域 ----------------
    // 借出连接 + BEGIN；commit() 显式提交，析构未提交自动回滚，连接自动归还。
    //   {
    //       auto tx = db.txBegin();
    //       Mapper<User>::save(u, *tx);
    //       tx->commit();
    //   }
    TxScope txBegin() { return TxScope(ds_); }

    // ---------------- 事务（lambda 风格） ----------------
    template<typename F>
    auto tx(F&& fn) { return withTransaction(ds_, std::forward<F>(fn)); }

    DataSource& source() { return ds_; }

private:
    template<typename F>
    auto withConn(F&& fn) -> decltype(fn(std::declval<IConnection&>())) {
        auto conn = ds_.getConnection();
        return fn(*conn);
    }

    DataSource& ds_;
};

} // namespace uORM
