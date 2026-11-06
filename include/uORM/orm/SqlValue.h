#pragma once
#include <variant>
#include <string>
#include <type_traits>
#include <vector>

namespace uORM {

// 统一的 SQL 值类型：NULL / 整数 / 浮点 / 字符串
// 说明：所有整型（int/long/unsigned...）统一存储为 long long，
//       浮点统一为 double，由驱动层负责与数据库类型的精确转换。
using SqlValue = std::variant<std::nullptr_t, long long, double, std::string>;

// 任意可绑定类型 -> SqlValue 的统一归一化转换。
// 背景：GCC 8 的 std::variant 转换构造（P0608R3 之前）对 int/bool 等
// 同时可转为 long long 与 double 的类型会判为二义而拒绝，必须先归一化。
template <typename V>
inline SqlValue toSqlValue(V&& v) {
    using D = std::decay_t<V>;
    if constexpr (std::is_same_v<D, SqlValue>) {
        return std::forward<V>(v);
    } else if constexpr (std::is_null_pointer_v<D>) {
        return SqlValue(nullptr);
    } else if constexpr (std::is_enum_v<D> || std::is_integral_v<D>) {
        return SqlValue(static_cast<long long>(v));
    } else if constexpr (std::is_floating_point_v<D>) {
        return SqlValue(static_cast<double>(v));
    } else {
        return SqlValue(std::string(std::forward<V>(v)));
    }
}

} // namespace uORM
