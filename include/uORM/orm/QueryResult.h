#pragma once
// 文件说明：
// QueryResult：原生 SQL 查询的通用结果（列名 + SqlValue 行），Web 管理台
// 任意 SQL 执行与结果展示的基础。提供跨驱动的 executeQuery/executeUpdate 便捷函数。

#include "uORM/orm/SqlValue.h"
#include "uORM/orm/Bind.h"
#include "uORM/driver/DBInterfaces.h"
#include <string>
#include <vector>

namespace uORM {

struct QueryResult {
    std::vector<std::string> columns;
    std::vector<std::vector<SqlValue>> rows;

    std::size_t rowCount() const { return rows.size(); }
    std::size_t columnCount() const { return columns.size(); }
};

// 在指定连接上执行查询（结果集拉取到内存）
inline QueryResult executeQuery(IConnection& conn, const std::string& sql,
                                const std::vector<SqlValue>& params = {}) {
    auto pstmt = conn.prepareStatement(sql);
    for (std::size_t i = 0; i < params.size(); ++i) {
        bindSqlValue(pstmt.get(), static_cast<int>(i + 1), params[i]);
    }
    auto rs = pstmt->executeQuery();

    QueryResult out;
    const std::size_t n = rs->columnCount();
    out.columns.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.columns.push_back(rs->columnName(i));

    while (rs->next()) {
        std::vector<SqlValue> row;
        row.reserve(n);
        for (std::size_t i = 0; i < n; ++i) row.push_back(rs->getSqlValue(i));
        out.rows.push_back(std::move(row));
    }
    return out;
}

// 在指定连接上执行 DML/DDL，返回受影响行数
inline unsigned long long executeUpdate(IConnection& conn, const std::string& sql,
                                        const std::vector<SqlValue>& params = {}) {
    auto pstmt = conn.prepareStatement(sql);
    for (std::size_t i = 0; i < params.size(); ++i) {
        bindSqlValue(pstmt.get(), static_cast<int>(i + 1), params[i]);
    }
    return pstmt->executeUpdate();
}

// RAII 结果集读取：如需流式处理大结果，可直接使用 conn.prepareStatement + executeQuery

} // namespace uORM
