#pragma once
#include "orm/Adapter.h"
#include "orm/MySQLConnectionPool.h"
#include <memory>
namespace uORM {
class MySQLAdapter : public Adapter {
public:
    MySQLAdapter() = default;
    virtual bool createTable(const TableSpec& spec) override;
    virtual std::optional<int64_t> insert(const InsertSpec& spec) override;
    virtual bool update(const UpdateSpec& spec) override;
    virtual bool deleteByPk(const std::string& table, const std::string& pkName, const Value& pkValue) override;
    virtual std::optional<std::vector<Value>> selectOne(const SelectSpec& spec) override;
    virtual std::vector<std::vector<Value>> selectAll(const SelectSpec& spec) override;
private:
    std::string typeToMySQL(const ColumnSpec& c) const;
    void bindValue(sql::PreparedStatement* ps, int idx, const ColumnSpec& c, const Value& v) const;
    Value readValue(sql::ResultSet* rs, int idx, const ColumnSpec& c) const;
};
} // namespace uORM
