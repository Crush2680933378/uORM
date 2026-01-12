#pragma once
#include <variant>
#include <string>
#include <vector>

namespace uORM {

using SqlValue = std::variant<int, long, long long, unsigned int, unsigned long long, std::string, const char*, bool, double, std::nullptr_t>;

}
