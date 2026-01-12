#pragma once
#include <string>
#include <vector>
#include <sstream>
#include "SqlValue.h"

namespace uORM {

class Query {
public:
    // 逻辑连接符设置
    Query& or_() {
        nextConnector_ = "OR";
        return *this;
    }
    
    Query& and_() {
        nextConnector_ = "AND";
        return *this;
    }

    // 基本比较
    Query& eq(const std::string& col, const SqlValue& val) {
        appendCondition(col, "=", val);
        return *this;
    }

    Query& ne(const std::string& col, const SqlValue& val) {
        appendCondition(col, "!=", val);
        return *this;
    }

    Query& gt(const std::string& col, const SqlValue& val) {
        appendCondition(col, ">", val);
        return *this;
    }
    
    Query& lt(const std::string& col, const SqlValue& val) {
        appendCondition(col, "<", val);
        return *this;
    }

    Query& ge(const std::string& col, const SqlValue& val) {
        appendCondition(col, ">=", val);
        return *this;
    }

    Query& le(const std::string& col, const SqlValue& val) {
        appendCondition(col, "<=", val);
        return *this;
    }

    Query& like(const std::string& col, const std::string& val) {
        appendCondition(col, "LIKE", val);
        return *this;
    }

    // 空值检查
    Query& isNull(const std::string& col) {
        appendConditionNoVal(col, "IS NULL");
        return *this;
    }

    Query& isNotNull(const std::string& col) {
        appendConditionNoVal(col, "IS NOT NULL");
        return *this;
    }

    // 范围查询
    Query& between(const std::string& col, const SqlValue& min, const SqlValue& max) {
        appendConnector();
        whereClause_ += col + " BETWEEN ? AND ?";
        params_.push_back(min);
        params_.push_back(max);
        return *this;
    }

    // 集合查询
    template<typename T>
    Query& in(const std::string& col, const std::vector<T>& values) {
        if (values.empty()) {
            appendConnector();
            whereClause_ += "1=0"; // Empty IN list is always false
            return *this;
        }
        
        appendConnector();
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
        if (values.empty()) {
            appendConnector();
            whereClause_ += "1=1"; // Empty NOT IN list is always true
            return *this;
        }

        appendConnector();
        whereClause_ += col + " NOT IN (";
        for (size_t i = 0; i < values.size(); ++i) {
            whereClause_ += (i == 0 ? "?" : ", ?");
            params_.push_back(values[i]);
        }
        whereClause_ += ")";
        return *this;
    }

    // 排序分页
    Query& orderBy(const std::string& col, bool asc = true) {
        if (orderByClause_.empty()) {
            orderByClause_ = " ORDER BY " + col + (asc ? " ASC" : " DESC");
        } else {
            orderByClause_ += ", " + col + (asc ? " ASC" : " DESC");
        }
        return *this;
    }

    Query& limit(int limit) {
        limitClause_ = " LIMIT " + std::to_string(limit);
        return *this;
    }

    Query& offset(int offset) {
        offsetClause_ = " OFFSET " + std::to_string(offset);
        return *this;
    }

    // 获取构建结果
    std::string getWhere() const {
        return whereClause_;
    }

    std::string getOrderBy() const {
        return orderByClause_;
    }

    std::string getLimit() const {
        return limitClause_;
    }

    std::string getOffset() const {
        return offsetClause_;
    }

    const std::vector<SqlValue>& getParams() const {
        return params_;
    }

private:
    std::string whereClause_;
    std::string orderByClause_;
    std::string limitClause_;
    std::string offsetClause_;
    std::vector<SqlValue> params_;
    std::string nextConnector_ = "AND";

    void appendConnector() {
        if (!whereClause_.empty()) {
            whereClause_ += " " + nextConnector_ + " ";
        }
        nextConnector_ = "AND"; // Reset to default
    }

    void appendCondition(const std::string& col, const std::string& op, const SqlValue& val) {
        appendConnector();
        whereClause_ += col + " " + op + " ?";
        params_.push_back(val);
    }

    void appendConditionNoVal(const std::string& col, const std::string& op) {
        appendConnector();
        whereClause_ += col + " " + op;
    }
};

}
