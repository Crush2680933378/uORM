#include "orm/MySQLAdapter.h"
#include <sstream>
#include <iostream>
namespace uORM {
// 将通用列类型映射为MySQL具体类型字符串
std::string MySQLAdapter::typeToMySQL(const ColumnSpec& c) const {
    switch (c.type) {
        case SqlType::Int32: return "INT";
        case SqlType::BigInt: return "BIGINT";
        case SqlType::Double: return "DOUBLE";
        case SqlType::Bool: return "TINYINT(1)";
        case SqlType::VarChar: return c.length > 0 ? ("VARCHAR(" + std::to_string(c.length) + ")") : "VARCHAR(255)";
        case SqlType::Text: return "TEXT";
        case SqlType::DateTime: return "DATETIME";
    }
    return "TEXT";
}
// 根据表规格生成DDL并执行建表（若不存在）
bool MySQLAdapter::createTable(const TableSpec& spec) {
    std::ostringstream oss;
    oss << "CREATE TABLE IF NOT EXISTS `" << spec.name << "` (";
    bool hasPk = false;
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        const auto& c = spec.columns[i];
        oss << "`" << c.name << "` " << typeToMySQL(c);
        if (!c.nullable) oss << " NOT NULL";
        if (c.autoIncrement) oss << " AUTO_INCREMENT";
        if (i < spec.columns.size() - 1) oss << ", ";
        if (c.primaryKey) hasPk = true;
    }
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        const auto& c = spec.columns[i];
        if (c.primaryKey) {
            oss << ", PRIMARY KEY (`" << c.name << "`)";
            break;
        }
    }
    oss << ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    auto conn = MySQLConnectionPool::instance().getConnection();
    std::unique_ptr<sql::Statement> stmt(conn->createStatement());
    try {
        stmt->execute(oss.str());
        return true;
    } catch (const sql::SQLException& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
}
// 绑定参数：根据列类型将Value绑定到预编译语句
void MySQLAdapter::bindValue(sql::PreparedStatement* ps, int idx, const ColumnSpec& c, const Value& v) const {
    if (std::holds_alternative<std::nullptr_t>(v)) { ps->setNull(idx, 0); return; }
    switch (c.type) {
        case SqlType::Int32: ps->setInt(idx, std::get<int32_t>(v)); break;
        case SqlType::BigInt: ps->setInt64(idx, std::get<int64_t>(v)); break;
        case SqlType::Double: ps->setDouble(idx, std::get<double>(v)); break;
        case SqlType::Bool: ps->setBoolean(idx, std::get<bool>(v)); break;
        case SqlType::VarChar:
        case SqlType::Text:
        case SqlType::DateTime: ps->setString(idx, std::get<std::string>(v)); break;
    }
}
// 读取结果：从ResultSet指定列读取为Value
Value MySQLAdapter::readValue(sql::ResultSet* rs, int idx, const ColumnSpec& c) const {
    if (rs->isNull(idx)) return std::nullptr_t{};
    switch (c.type) {
        case SqlType::Int32: return static_cast<int32_t>(rs->getInt(idx));
        case SqlType::BigInt: return static_cast<int64_t>(rs->getInt64(idx));
        case SqlType::Double: return rs->getDouble(idx);
        case SqlType::Bool: return rs->getBoolean(idx);
        case SqlType::VarChar:
        case SqlType::Text:
        case SqlType::DateTime: return rs->getString(idx);
    }
    return std::nullptr_t{};
}
// 插入一行：忽略自增主键列的绑定，返回自增主键值（如存在）
std::optional<int64_t> MySQLAdapter::insert(const InsertSpec& spec) {
    std::ostringstream cols, vals;
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        const auto& c = spec.columns[i];
        if (c.autoIncrement && c.primaryKey) continue;
        cols << "`" << c.name << "`";
        vals << "?";
        if (i < spec.columns.size() - 1) { cols << ", "; vals << ", "; }
    }
    std::ostringstream sql;
    sql << "INSERT INTO `" << spec.table << "` (" << cols.str() << ") VALUES (" << vals.str() << ")";
    auto conn = MySQLConnectionPool::instance().getConnection();
    std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql.str()));
    int paramIdx = 1;
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        const auto& c = spec.columns[i];
        if (c.autoIncrement && c.primaryKey) continue;
        bindValue(ps.get(), paramIdx++, c, spec.values[i]);
    }
    try {
        ps->executeUpdate();
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery("SELECT LAST_INSERT_ID()"));
        if (rs->next()) return static_cast<int64_t>(rs->getInt64(1));
        return std::nullopt;
    } catch (const sql::SQLException& e) {
        std::cerr << e.what() << std::endl;
        return std::nullopt;
    }
}
// 更新：为非主键列绑定新值，最后绑定主键条件
bool MySQLAdapter::update(const UpdateSpec& spec) {
    std::ostringstream sets;
    size_t count = 0;
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        const auto& c = spec.columns[i];
        if (c.primaryKey) continue;
        if (count++) sets << ", ";
        sets << "`" << c.name << "`=?";
    }
    std::ostringstream sql;
    sql << "UPDATE `" << spec.table << "` SET " << sets.str() << " WHERE `" << spec.pkName << "`=?";
    auto conn = MySQLConnectionPool::instance().getConnection();
    std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql.str()));
    int idx = 1;
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        const auto& c = spec.columns[i];
        if (c.primaryKey) continue;
        bindValue(ps.get(), idx++, c, spec.values[i]);
    }
    bindValue(ps.get(), idx, ColumnSpec{spec.pkName, SqlType::BigInt, 0, false, true, false}, spec.pkValue);
    try {
        return ps->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
}
// 删除：绑定主键值作为条件，执行删除
bool MySQLAdapter::deleteByPk(const std::string& table, const std::string& pkName, const Value& pkValue) {
    std::ostringstream sql;
    sql << "DELETE FROM `" << table << "` WHERE `" << pkName << "`=?";
    auto conn = MySQLConnectionPool::instance().getConnection();
    std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql.str()));
    bindValue(ps.get(), 1, ColumnSpec{pkName, SqlType::BigInt, 0, false, true, false}, pkValue);
    try {
        return ps->executeUpdate() > 0;
    } catch (const sql::SQLException& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
}
// 查询一行：可选主键条件
std::optional<std::vector<Value>> MySQLAdapter::selectOne(const SelectSpec& spec) {
    std::ostringstream cols;
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        cols << "`" << spec.columns[i].name << "`";
        if (i < spec.columns.size() - 1) cols << ", ";
    }
    std::ostringstream sql;
    sql << "SELECT " << cols.str() << " FROM `" << spec.table << "`";
    bool hasWhere = spec.wherePk.has_value();
    if (hasWhere) sql << " WHERE `" << spec.wherePk->first << "`=?";
    auto conn = MySQLConnectionPool::instance().getConnection();
    std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql.str()));
    if (hasWhere) bindValue(ps.get(), 1, ColumnSpec{spec.wherePk->first, SqlType::BigInt, 0, false, true, false}, spec.wherePk->second);
    try {
        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        if (rs->next()) {
            std::vector<Value> row;
            for (size_t i = 0; i < spec.columns.size(); ++i) {
                row.push_back(readValue(rs.get(), static_cast<int>(i + 1), spec.columns[i]));
            }
            return row;
        }
        return std::nullopt;
    } catch (const sql::SQLException& e) {
        std::cerr << e.what() << std::endl;
        return std::nullopt;
    }
}
// 查询多行：读取全部记录
std::vector<std::vector<Value>> MySQLAdapter::selectAll(const SelectSpec& spec) {
    std::ostringstream cols;
    for (size_t i = 0; i < spec.columns.size(); ++i) {
        cols << "`" << spec.columns[i].name << "`";
        if (i < spec.columns.size() - 1) cols << ", ";
    }
    std::ostringstream sql;
    sql << "SELECT " << cols.str() << " FROM `" << spec.table << "`";
    auto conn = MySQLConnectionPool::instance().getConnection();
    std::unique_ptr<sql::PreparedStatement> ps(conn->prepareStatement(sql.str()));
    std::vector<std::vector<Value>> out;
    try {
        std::unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        while (rs->next()) {
            std::vector<Value> row;
            for (size_t i = 0; i < spec.columns.size(); ++i) {
                row.push_back(readValue(rs.get(), static_cast<int>(i + 1), spec.columns[i]));
            }
            out.push_back(std::move(row));
        }
        return out;
    } catch (const sql::SQLException& e) {
        std::cerr << e.what() << std::endl;
        return {};
    }
}
} // namespace uORM
