#pragma once
#include <string>
#include <vector>
#include "orm/Field.h"
namespace uORM {
template<typename Derived>
struct Model {
    static std::string table();
    static std::vector<AnyField<Derived>> schema();
};
} // namespace uORM
