#pragma once
// 文件说明：
// Query：流式查询构造器（v2）。
// - 条件：eq/ne/gt/lt/ge/le/like/in/notIn/between/isNull/isNotNull + and_/or_ + 括号分组
// - 投影：select(cols)/selectRaw()/distinct()
// - 聚合：groupBy/having
// - 连接：innerJoin/leftJoin/rightJoin/crossJoin（结果建议配合 Mapper::selectDynamic）
// - 排序分页：orderBy/limit/offset

#include <string>
#include <vector>
#include <sstream>
#include <initializer_list>
#include "SqlValue.h"

namespace uORM {

class Query {
public:
    // ---------------- 逻辑连接符 ----------------
    Query& or_() {
        nextConnector_ = "OR";
        return *this;
    }

    Query& and_() {
        nextConnector_ = "AND";
        return *this;
    }

    // ---------------- 括号分组 ----------------
    // 例: q.eq("a",1).or_().beginGroup().eq("b",2).or_().eq("c",3).endGroup()
    Query& beginGroup() {
        appendConnector();
        whereClause_ += "(";
        groupDepth_++;
        pendingGroupTerm_ = true; // 组内第一个条件不再添加连接符
        return *this;
    }

    Query& endGroup() {
        if (groupDepth_ > 0) {
            whereClause_ += ")";
            groupDepth_--;
        }
        return *this;
    }

    // ---------------- 基本比较 ----------------
    Query& eq(const std::string& col, const SqlValue& val) { return appendCondition(col, "=", val); }
    Query& ne(const std::string& col, const SqlValue& val) { return appendCondition(col, "!=", val); }
    Query& gt(const std::string& col, const SqlValue& val) { return appendCondition(col, ">", val); }
    Query& lt(const std::string& col, const SqlValue& val) { return appendCondition(col, "<", val); }
    Query& ge(const std::string& col, const SqlValue& val) { return appendCondition(col, ">=", val); }
    Query& le(const std::string& col, const SqlValue& val) { return appendCondition(col, "<=", val); }
    Query& like(const std::string& col, const std::string& val) { return appendCondition(col, "LIKE", val); }
    Query& notLike(const std::string& col, const std::string& val) { return appendCondition(col, "NOT LIKE", val); }

    // ---------------- 空值 ----------------
    Query& isNull(const std::string& col) {
        appendConditionNoVal(col, "IS NULL");
        return *this;
    }

    Query& isNotNull(const std::string& col) {
        appendConditionNoVal(col, "IS NOT NULL");
        return *this;
    }

    // ---------------- 范围/集合 ----------------
    Query& between(const std::string& col, const SqlValue& min, const SqlValue& max) {
        beginTerm();
        whereClause_ += col + " BETWEEN ? AND ?";
        params_.push_back(min);
        params_.push_back(max);
        return *this;
    }

    template<typename T>
    Query& in(const std::string& col, const std::vector<T>& values) {
        beginTerm();
        if (values.empty()) {
            whereClause_ += "1=0"; // 空 IN 恒假
            return *this;
        }
        whereClause_ += col + " IN (";
        for (size_t i = 0; i < values.size(); ++i) {
            whereClause_ += (i == 0 ? "?" : ", ?");
            params_.push_back(values[i]);
        }
        whereClause_ += ")";
        return *this;
    }

    template<typename T>
    Query& notIn(const std::string& col, const std::vector<T>& values) {
        beginTerm();
        if (values.empty()) {
            whereClause_ += "1=1"; // 空 NOT IN 恒真
            return *this;
        }
        whereClause_ += col + " NOT IN (";
        for (size_t i = 0; i < values.size(); ++i) {
            whereClause_ += (i == 0 ? "?" : ", ?");
            params_.push_back(values[i]);
        }
        whereClause_ += ")";
        return *this;
    }

    // ---------------- 投影 ----------------
    Query& select(std::initializer_list<std::string> cols) {
        columns_.assign(cols);
        return *this;
    }

    // 原样输出投影表达式（含聚合/函数），不经转义
    Query& selectRaw(const std::string& expr) {
        selectRaw_ = expr;
        return *this;
    }

    Query& distinct() {
        distinct_ = true;
        return *this;
    }

    // ---------------- 聚合 ----------------
    Query& groupBy(const std::string& col) {
        if (groupByClause_.empty()) groupByClause_ = " GROUP BY " + col;
        else groupByClause_ += ", " + col;
        return *this;
    }

    // having 子句（表达式原样输出，参数照常追加到 params_）
    // 例: q.selectRaw("category, COUNT(*) AS cnt").groupBy("category")
    //       .having("COUNT(*) > ?").in(...)...
    Query& having(const std::string& rawExpr) {
        if (havingClause_.empty()) havingClause_ = " HAVING " + rawExpr;
        else havingClause_ += " AND " + rawExpr;
        return *this;
    }

    // having 参数（与 having 中 ? 占位符一一对应）
    Query& havingParam(const SqlValue& val) {
        params_.push_back(val);
        return *this;
    }

    // ---------------- 连接 ----------------
    Query& innerJoin(const std::string& table, const std::string& on) { return addJoin("INNER JOIN", table, on); }
    Query& leftJoin(const std::string& table, const std::string& on) { return addJoin("LEFT JOIN", table, on); }
    Query& rightJoin(const std::string& table, const std::string& on) { return addJoin("RIGHT JOIN", table, on); }
    Query& crossJoin(const std::string& table) { return addJoin("CROSS JOIN", table, ""); }

    // ---------------- 排序分页 ----------------
    Query& orderBy(const std::string& col, bool asc = true) {
        if (orderByClause_.empty()) {
            orderByClause_ = " ORDER BY " + col + (asc ? " ASC" : " DESC");
        } else {
            orderByClause_ += ", " + col + (asc ? " ASC" : " DESC");
        }
        return *this;
    }

    // ---------------- 原生片段 ----------------
    // 直接设置 WHERE 子句；参数用 param() 按序追加
    Query& whereRaw(const std::string& raw) {
        whereClause_ = raw;
        return *this;
    }

    // 追加绑定参数（与 whereRaw/having 中的 ? 按序对应）
    Query& param(SqlValue v) {
        params_.push_back(std::move(v));
        return *this;
    }

    Query& limit(int limit) {
        limitValue_ = limit;
        limitClause_ = " LIMIT " + std::to_string(limit);
        return *this;
    }

    Query& offset(int offset) {
        offsetValue_ = offset;
        offsetClause_ = " OFFSET " + std::to_string(offset);
        return *this;
    }

    // ---------------- 构建结果 ----------------
    std::string getWhere() const { return whereClause_; }
    std::string getOrderBy() const { return orderByClause_; }
    std::string getLimit() const { return limitClause_; }
    std::string getOffset() const { return offsetClause_; }
    std::string getGroupBy() const { return groupByClause_; }
    std::string getHaving() const { return havingClause_; }
    std::string getJoins() const { return joins_; }
    bool isDistinct() const { return distinct_; }

    // 结构化分页值（-1 = 未设置）：方言子句生成用
    int limitValue() const { return limitValue_; }
    int offsetValue() const { return offsetValue_; }

    const std::vector<std::string>& getColumns() const { return columns_; }
    const std::string& getSelectRaw() const { return selectRaw_; }

    const std::vector<SqlValue>& getParams() const { return params_; }

private:
    std::string whereClause_;
    std::string orderByClause_;
    std::string limitClause_;
    std::string offsetClause_;
    std::string groupByClause_;
    std::string havingClause_;
    std::string joins_;
    std::string selectRaw_;
    std::vector<std::string> columns_;
    std::vector<SqlValue> params_;
    std::string nextConnector_ = "AND";
    int groupDepth_ = 0;
    bool pendingGroupTerm_ = false;
    bool distinct_ = false;
    int limitValue_ = -1;
    int offsetValue_ = -1;

    // 条件起始：决定连接符与括号内首条件
    void beginTerm() {
        if (pendingGroupTerm_) {
            pendingGroupTerm_ = false;
            return;
        }
        appendConnector();
    }

    void appendConnector() {
        if (!whereClause_.empty() && whereClause_.back() != '(') {
            whereClause_ += " " + nextConnector_ + " ";
        }
        nextConnector_ = "AND";
    }

    Query& appendCondition(const std::string& col, const std::string& op, const SqlValue& val) {
        beginTerm();
        whereClause_ += col + " " + op + " ?";
        params_.push_back(val);
        return *this;
    }

    void appendConditionNoVal(const std::string& col, const std::string& op) {
        beginTerm();
        whereClause_ += col + " " + op;
    }

    Query& addJoin(const std::string& kind, const std::string& table, const std::string& on) {
        joins_ += " " + kind + " " + table;
        if (!on.empty()) joins_ += " ON " + on;
        return *this;
    }
};

} // namespace uORM
