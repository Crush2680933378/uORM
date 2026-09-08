#pragma once
// 文件说明：
// TypedQuery —— 类型安全的流式查询：用成员指针指定列，编译期防拼错。
//
//   auto adults = db.query<User>()
//       .where(&User::age, uORM::GT, 18)
//       .like(&User::name, "%T%")
//       .orderByDesc(&User::id)
//       .limit(10)
//       .all();
//
// 同一组 where 条件可直接驱动 UPDATE / DELETE / COUNT：
//   db.query<User>().where(&User::active, uORM::EQ, 0).remove();
//   db.query<User>().where(&User::id, uORM::EQ, 3).set(&User::age, 20).update();
//
// 值绑定走既有 SqlValue 通道（预编译参数，杜绝注入）；列名经方言引用；
// 引用未注册成员会在运行时抛出 OrmError（列名来自注册元数据）。

#include "uORM/driver/DataSource.h"
#include "uORM/orm/Mapper.h"
#include "uORM/orm/Query.h"
#include "uORM/orm/SqlValue.h"
#include "uORM/orm/Error.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace uORM {

// 比较运算符
enum class Op { EQ, NE, GT, GE, LT, LE };

inline const char* opText(Op op) {
    switch (op) {
        case Op::EQ: return "=";
        case Op::NE: return "!=";
        case Op::GT: return ">";
        case Op::GE: return ">=";
        case Op::LT: return "<";
        case Op::LE: return "<=";
    }
    return "=";
}

template<typename T>
class TypedQuery {
public:
    explicit TypedQuery(DataSource& ds) : ds_(ds) {}

    // ---------------- 条件 ----------------
    template<typename M, typename V>
    TypedQuery& where(M member, Op op, V&& value) {
        appendCond(member, op, SqlValue(std::forward<V>(value)));
        return *this;
    }

    // 用 OR 连接的下一条条件
    template<typename M, typename V>
    TypedQuery& orWhere(M member, Op op, V&& value) {
        q_.or_();
        return where(member, op, std::forward<V>(value));
    }

    template<typename M>
    TypedQuery& like(M member, const std::string& pattern) {
        q_.like(quoted(member), pattern);
        return *this;
    }

    template<typename M, typename V>
    TypedQuery& in(M member, const std::vector<V>& values) {
        q_.in(quoted(member), values);
        return *this;
    }

    template<typename M, typename V>
    TypedQuery& in(M member, std::initializer_list<V> values) {
        return in(member, std::vector<V>(values));
    }

    template<typename M, typename V>
    TypedQuery& between(M member, V&& lo, V&& hi) {
        q_.between(quoted(member), SqlValue(std::forward<V>(lo)), SqlValue(std::forward<V>(hi)));
        return *this;
    }

    template<typename M>
    TypedQuery& isNull(M member) { q_.isNull(quoted(member)); return *this; }

    template<typename M>
    TypedQuery& isNotNull(M member) { q_.isNotNull(quoted(member)); return *this; }

    // 分组括号：beginGroup() ... endGroup()
    TypedQuery& beginGroup() { q_.beginGroup(); return *this; }
    TypedQuery& endGroup() { q_.endGroup(); return *this; }

    // ---------------- 投影/分组（动态结果） ----------------
    template<typename M>
    TypedQuery& groupBy(M member) { q_.groupBy(quoted(member)); return *this; }

    TypedQuery& selectRaw(const std::string& expr) { q_.selectRaw(expr); return *this; }
    TypedQuery& havingRaw(const std::string& expr) { q_.having(expr); return *this; }

    // ---------------- 排序分页 ----------------
    template<typename M>
    TypedQuery& orderByAsc(M member) { q_.orderBy(quoted(member), true); return *this; }

    template<typename M>
    TypedQuery& orderByDesc(M member) { q_.orderBy(quoted(member), false); return *this; }

    TypedQuery& limit(int n) { q_.limit(n); return *this; }
    TypedQuery& offset(int n) { q_.offset(n); return *this; }

    // ---------------- UPDATE 的 SET 子句 ----------------
    template<typename M, typename V>
    TypedQuery& set(M member, V&& value) {
        sets_.emplace_back(quoted(member), SqlValue(std::forward<V>(value)));
        return *this;
    }

    // ---------------- 执行 ----------------
    // SELECT * -> 实体列表
    std::vector<T> all() {
        return withConn([&](IConnection& c) { return Mapper<T>::select(c, q_); });
    }

    // SELECT -> 单条
    std::optional<T> first() {
        return withConn([&](IConnection& c) { return Mapper<T>::selectOne(c, q_); });
    }

    // COUNT(*)（忽略投影/分组）
    long long count() {
        return withConn([&](IConnection& c) { return Mapper<T>::count(c, q_); });
    }

    // SELECT 的动态结果（配合 selectRaw/groupBy/having）
    QueryResult collect() {
        return withConn([&](IConnection& c) { return Mapper<T>::selectDynamic(c, q_); });
    }

    // UPDATE ... SET（需先 set()），WHERE 取当前条件；无条件时拒绝执行（防全表误更新）
    bool update() {
        if (sets_.empty()) throw OrmError("TypedQuery::update: 未通过 set() 指定要更新的字段");
        ensureHasCondition("TypedQuery::update");
        return withConn([&](IConnection& c) {
            auto dialect = c.dialect();
            std::string sql = "UPDATE " + dialect->quoteIdentifier(TableMeta<T>::name) + " SET ";
            bool first = true;
            for (const auto& kv : sets_) {
                if (!first) sql += ", ";
                sql += kv.first + " = ?";
                first = false;
            }
            std::string where = q_.getWhere();
            if (!where.empty()) sql += " WHERE " + where;

            auto pstmt = c.prepareStatement(sql);
            int index = 1;
            for (const auto& kv : sets_) uORM::bindSqlValue(pstmt.get(), index++, kv.second);
            for (const auto& p : q_.getParams()) uORM::bindSqlValue(pstmt.get(), index++, p);
            pstmt->executeUpdate();
            return true;
        });
    }

    // DELETE（WHERE 取当前条件；无条件时拒绝执行，防全表误删）
    bool remove() {
        ensureHasCondition("TypedQuery::remove");
        return withConn([&](IConnection& c) {
            auto dialect = c.dialect();
            std::string sql = "DELETE FROM " + dialect->quoteIdentifier(TableMeta<T>::name) +
                              " WHERE " + q_.getWhere();
            auto pstmt = c.prepareStatement(sql);
            int index = 1;
            for (const auto& p : q_.getParams()) uORM::bindSqlValue(pstmt.get(), index++, p);
            pstmt->executeUpdate();
            return true;
        });
    }

private:
    // M 必须是 T 的数据成员指针；返回方言引用后的列名
    template<typename M>
    std::string quoted(M member) {
        static_assert(std::is_member_pointer_v<M>, "TypedQuery: 请传类的成员指针，如 &User::age");
        auto col = Mapper<T>::columnNameOf(member);
        if (col.empty()) throw OrmError("TypedQuery: 该成员未在 UORM 映射中注册");
        return ds_.dialect()->quoteIdentifier(col);
    }

    template<typename M>
    void appendCond(M member, Op op, SqlValue value) {
        const std::string col = quoted(member);
        switch (op) {
            case Op::EQ: q_.eq(col, value); break;
            case Op::NE: q_.ne(col, value); break;
            case Op::GT: q_.gt(col, value); break;
            case Op::GE: q_.ge(col, value); break;
            case Op::LT: q_.lt(col, value); break;
            case Op::LE: q_.le(col, value); break;
        }
    }

    void ensureHasCondition(const char* what) const {
        if (q_.getWhere().empty())
            throw OrmError(std::string(what) + ": 无 WHERE 条件，拒绝影响全表");
    }

    template<typename F>
    auto withConn(F&& fn) -> decltype(fn(std::declval<IConnection&>())) {
        auto conn = ds_.getConnection();
        return fn(*conn);
    }

    DataSource& ds_;
    Query q_;
    std::vector<std::pair<std::string, SqlValue>> sets_;
};

} // namespace uORM
