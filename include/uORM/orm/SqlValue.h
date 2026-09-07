#pragma once
#include <variant>
#include <string>
#include <vector>

namespace uORM {

// 统一的 SQL 值类型：NULL / 整数 / 浮点 / 字符串
// 说明：所有整型（int/long/unsigned...）统一存储为 long long，
//       浮点统一为 double，由驱动层负责与数据库类型的精确转换。
using SqlValue = std::variant<std::nullptr_t, long long, double, std::string>;

} // namespace uORM
