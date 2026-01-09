#pragma once
#include <string>
#include <vector>
#include <optional>
#include "orm/SqlTypes.h"
#include "orm/Field.h"
namespace uORM {
struct InsertSpec {
    std::string table;
    std::vector<ColumnSpec> columns;
    std::vector<Value> values;
};
struct UpdateSpec {
    std::string table;
    std::vector<ColumnSpec> columns;
    std::vector<Value> values;
    std::string pkName;
    Value pkValue;
};
struct SelectSpec {
    std::string table;
    std::vector<ColumnSpec> columns;
    std::optional<std::pair<std::string, Value>> wherePk;
};
class Adapter {
public:
    virtual ~Adapter() = default;
    virtual bool createTable(const TableSpec& spec) = 0;
    virtual std::optional<int64_t> insert(const InsertSpec& spec) = 0;
    virtual bool update(const UpdateSpec& spec) = 0;
    virtual bool deleteByPk(const std::string& table, const std::string& pkName, const Value& pkValue) = 0;
    virtual std::optional<std::vector<Value>> selectOne(const SelectSpec& spec) = 0;
    virtual std::vector<std::vector<Value>> selectAll(const SelectSpec& spec) = 0;
};
} // namespace uORM
