#pragma once
// 文件说明：
// 统一的参数绑定辅助：SqlValue / C++ 任意可绑定类型 -> IPreparedStatement

#include "uORM/orm/SqlValue.h"
#include "uORM/driver/DBInterfaces.h"
#include <string>
#include <type_traits>

namespace uORM {

// SqlValue 绑定（NULL 语义完整）
inline void bindSqlValue(IPreparedStatement* pstmt, int index, const SqlValue& val) {
    std::visit([&](auto&& arg) {
        using ArgType = std::decay_t<decltype(arg)>;
        if constexpr (std::is_null_pointer_v<ArgType>) {
            pstmt->setNull(index);
        } else if constexpr (std::is_same_v<ArgType, long long>) {
            pstmt->setInt64(index, arg);
        } else if constexpr (std::is_same_v<ArgType, double>) {
            pstmt->setDouble(index, arg);
        } else if constexpr (std::is_same_v<ArgType, std::string>) {
            pstmt->setString(index, arg);
        }
    }, val);
}

// 任意 C++ 类型绑定（类型分流）
template<typename V>
inline void bindValue(IPreparedStatement* pstmt, int index, V&& val) {
    using D = std::decay_t<V>;
    if constexpr (std::is_same_v<D, SqlValue>) {
        bindSqlValue(pstmt, index, val);
    } else if constexpr (std::is_null_pointer_v<D>) {
        pstmt->setNull(index);
    } else if constexpr (std::is_enum_v<D>) {
        pstmt->setInt64(index, static_cast<long long>(val));
    } else if constexpr (std::is_integral_v<D>) {
        pstmt->setInt64(index, static_cast<long long>(val));
    } else if constexpr (std::is_floating_point_v<D>) {
        pstmt->setDouble(index, static_cast<double>(val));
    } else if constexpr (std::is_convertible_v<D, std::string>) {
        pstmt->setString(index, std::string(val));
    } else {
        static_assert(sizeof(D) == 0, "uORM: unsupported parameter type for binding");
    }
}

} // namespace uORM
