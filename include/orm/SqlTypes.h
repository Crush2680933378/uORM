#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace uORM {
enum class SqlType { Int32, BigInt, Double, Bool, VarChar, Text, DateTime };
struct ColumnSpec {
    std::string name;
    SqlType type;
    std::size_t length;
    bool nullable;
    bool primaryKey;
    bool autoIncrement;
};
struct TableSpec {
    std::string name;
    std::vector<ColumnSpec> columns;
};
} // namespace uORM
