#pragma once
// 文件说明：
// DAO模板为任意模型M提供通用的CRUD能力，基于适配器Adapter实现数据库无关的操作。
// - createTable：根据模型字段自动建表
// - insert/update/delete/findById/findAll：通用增删改查

#include <vector>
#include <optional>
#include <memory>
#include "orm/Adapter.h"
#include "orm/Model.h"
#include "orm/Field.h"
namespace uORM {

template<typename M>
class DAO {
public:
    // 构造：注入适配器（可为任意数据库的适配器实现）
    DAO(std::shared_ptr<Adapter> adapter) : adapter_(std::move(adapter)) {}
    // 自动建表：根据M::schema中的字段元数据生成表结构
    bool createTable() {
        TableSpec t;
        t.name = M::table();
        for (auto& f : M::schema()) {
            t.columns.push_back(ColumnSpec{f.name, f.type, f.length, f.nullable, f.primaryKey, f.autoIncrement});
        }
        return adapter_->createTable(t);
    }
    // 插入：将对象各字段通过getter转换为Value并绑定
    std::optional<int64_t> insert(const M& m) {
        auto s = M::schema();
        InsertSpec ins;
        ins.table = M::table();
        ins.columns.reserve(s.size());
        ins.values.reserve(s.size());
        for (auto& f : s) {
            ins.columns.push_back(ColumnSpec{f.name, f.type, f.length, f.nullable, f.primaryKey, f.autoIncrement});
            ins.values.push_back(f.getter(m));
        }
        return adapter_->insert(ins);
    }
    // 更新：根据主键条件更新对象其余字段
    bool update(const M& m) {
        auto s = M::schema();
        UpdateSpec upd;
        upd.table = M::table();
        for (auto& f : s) {
            upd.columns.push_back(ColumnSpec{f.name, f.type, f.length, f.nullable, f.primaryKey, f.autoIncrement});
            upd.values.push_back(f.getter(m));
            if (f.primaryKey) {
                upd.pkName = f.name;
                upd.pkValue = f.getter(m);
            }
        }
        return adapter_->update(upd);
    }
    // 删除：根据主键值删除
    template<typename PK>
    bool deleteById(const PK& pk) {
        auto s = M::schema();
        std::string pkName;
        for (auto& f : s) if (f.primaryKey) { pkName = f.name; break; }
        Value v;
        if constexpr (std::is_same_v<PK, int32_t>) v = pk;
        else if constexpr (std::is_same_v<PK, int64_t>) v = pk;
        else if constexpr (std::is_same_v<PK, std::string>) v = pk;
        else v = pk;
        return adapter_->deleteByPk(M::table(), pkName, v);
    }
    // 查询：按主键查找一条记录并反序列化为对象
    template<typename PK>
    std::optional<M> findById(const PK& pk) {
        auto s = M::schema();
        std::string pkName;
        for (auto& f : s) if (f.primaryKey) { pkName = f.name; break; }
        Value v;
        if constexpr (std::is_same_v<PK, int32_t>) v = pk;
        else if constexpr (std::is_same_v<PK, int64_t>) v = pk;
        else if constexpr (std::is_same_v<PK, std::string>) v = pk;
        else v = pk;
        SelectSpec sel;
        sel.table = M::table();
        for (auto& f : s) sel.columns.push_back(ColumnSpec{f.name, f.type, f.length, f.nullable, f.primaryKey, f.autoIncrement});
        sel.wherePk = std::make_pair(pkName, v);
        auto row = adapter_->selectOne(sel);
        if (!row.has_value()) return std::nullopt;
        M m{};
        for (size_t i = 0; i < s.size(); ++i) s[i].setter(m, (*row)[i]);
        return m;
    }
    // 查询全部：返回所有记录并反序列化
    std::vector<M> findAll() {
        auto s = M::schema();
        SelectSpec sel;
        sel.table = M::table();
        for (auto& f : s) sel.columns.push_back(ColumnSpec{f.name, f.type, f.length, f.nullable, f.primaryKey, f.autoIncrement});
        auto rows = adapter_->selectAll(sel);
        std::vector<M> out;
        for (auto& r : rows) {
            M m{};
            for (size_t i = 0; i < s.size(); ++i) s[i].setter(m, r[i]);
            out.push_back(std::move(m));
        }
        return out;
    }
private:
    // 底层数据库适配器
    std::shared_ptr<Adapter> adapter_;
};
} // namespace uORM
