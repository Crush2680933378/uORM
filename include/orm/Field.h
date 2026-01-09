#pragma once
// 文件说明：
// 该文件提供ORM模型字段的通用描述（AnyField），以及统一值封装类型（Value）。
// 通过makeField辅助函数，用户可以将结构体成员与ORM字段元数据绑定，生成getter/setter，
// 在DAO层实现通用的读写与绑定，无需手写序列化代码。
#include <string>
#include <variant>
#include <functional>
#include <optional>
#include <vector>
#include "orm/SqlTypes.h"
#include <type_traits>
namespace uORM {
using Value = std::variant<std::nullptr_t, int32_t, int64_t, double, bool, std::string>;
template<typename T> struct is_optional : std::false_type {};
template<typename U> struct is_optional<std::optional<U>> : std::true_type {};
template<typename T> constexpr bool is_optional_v = is_optional<T>::value;
struct AnyFieldBase {
    std::string name;
    SqlType type;
    std::size_t length;
    bool nullable;
    bool primaryKey;
    bool autoIncrement;
};
// 字段描述（模板）：绑定具体模型类型C的成员访问器
template<typename C>
struct AnyField : AnyFieldBase {
    // getter：从对象中取值并封装为Value
    std::function<Value(const C&)> getter;
    // setter：将Value写回对象成员（支持std::optional可空）
    std::function<void(C&, const Value&)> setter;
};
// 生成字段描述的辅助函数：
// name为列名；member为成员指针；type/length为SQL类型规格；nullable/primaryKey/autoIncrement为约束
// 自动检测std::optional并设置nullable为true
template<typename C, typename M>
AnyField<C> makeField(const std::string& name, M C::* member, SqlType type, std::size_t length, bool nullable, bool primaryKey, bool autoIncrement) {
    AnyField<C> f;
    f.name = name;
    f.type = type;
    f.length = length;
    f.nullable = nullable || is_optional_v<M>;
    f.primaryKey = primaryKey;
    f.autoIncrement = autoIncrement;
    // getter实现：读取成员值并转换为Value变体类型
    f.getter = [member](const C& c) -> Value {
        if constexpr (is_optional_v<M>) {
            if (!(c.*member).has_value()) return std::nullptr_t{};
            using T = typename M::value_type;
            if constexpr (std::is_same_v<T, int32_t>) return static_cast<int32_t>(*(c.*member));
            else if constexpr (std::is_same_v<T, int64_t>) return static_cast<int64_t>(*(c.*member));
            else if constexpr (std::is_same_v<T, double>) return static_cast<double>(*(c.*member));
            else if constexpr (std::is_same_v<T, bool>) return static_cast<bool>(*(c.*member));
            else if constexpr (std::is_same_v<T, std::string>) return std::string(*(c.*member));
            else return std::nullptr_t{};
        } else {
            if constexpr (std::is_same_v<M, int32_t>) return static_cast<int32_t>(c.*member);
            else if constexpr (std::is_same_v<M, int64_t>) return static_cast<int64_t>(c.*member);
            else if constexpr (std::is_same_v<M, double>) return static_cast<double>(c.*member);
            else if constexpr (std::is_same_v<M, bool>) return static_cast<bool>(c.*member);
            else if constexpr (std::is_same_v<M, std::string>) return std::string(c.*member);
            else return std::nullptr_t{};
        }
    };
    // setter实现：将Value中的值写回对象成员，支持设置为空（std::nullopt）
    f.setter = [member](C& c, const Value& v) {
        if constexpr (is_optional_v<M>) {
            using T = typename M::value_type;
            if (std::holds_alternative<std::nullptr_t>(v)) { c.*member = std::nullopt; return; }
            if constexpr (std::is_same_v<T, int32_t>) c.*member = std::get<int32_t>(v);
            else if constexpr (std::is_same_v<T, int64_t>) c.*member = std::get<int64_t>(v);
            else if constexpr (std::is_same_v<T, double>) c.*member = std::get<double>(v);
            else if constexpr (std::is_same_v<T, bool>) c.*member = std::get<bool>(v);
            else if constexpr (std::is_same_v<T, std::string>) c.*member = std::get<std::string>(v);
        } else {
            if constexpr (std::is_same_v<M, int32_t>) c.*member = std::get<int32_t>(v);
            else if constexpr (std::is_same_v<M, int64_t>) c.*member = std::get<int64_t>(v);
            else if constexpr (std::is_same_v<M, double>) c.*member = std::get<double>(v);
            else if constexpr (std::is_same_v<M, bool>) c.*member = std::get<bool>(v);
            else if constexpr (std::is_same_v<M, std::string>) c.*member = std::get<std::string>(v);
        }
    };
    return f;
}
} // namespace uORM

