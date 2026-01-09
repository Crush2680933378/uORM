#pragma once
// 文件说明：
// MySQLAdapter 是适配器接口的MySQL具体实现，
// 负责将通用的Table/Insert/Update/Select规格转换为MySQL语法与绑定，执行并返回结果。
// 依赖MySQLConnectionPool进行连接管理。

#include "orm/Adapter.h"
#include "orm/MySQLConnectionPool.h"
#include <memory>
namespace uORM {
class MySQLAdapter : public Adapter {
public:
    // 构造函数：当前无状态，使用连接池获取连接
    MySQLAdapter() = default;
    // 自动建表（CREATE TABLE IF NOT EXISTS）
    virtual bool createTable(const TableSpec& spec) override;
    // 插入：返回LAST_INSERT_ID（若存在）
    virtual std::optional<int64_t> insert(const InsertSpec& spec) override;
    // 更新：根据主键条件更新非主键列
    virtual bool update(const UpdateSpec& spec) override;
    // 删除：根据主键条件删除
    virtual bool deleteByPk(const std::string& table, const std::string& pkName, const Value& pkValue) override;
    // 查询一行：可选主键条件
    virtual std::optional<std::vector<Value>> selectOne(const SelectSpec& spec) override;
    // 查询多行：返回所有记录
    virtual std::vector<std::vector<Value>> selectAll(const SelectSpec& spec) override;
private:
    // 类型映射：将通用SqlType转换为MySQL字段类型字符串
    std::string typeToMySQL(const ColumnSpec& c) const;
    // 参数绑定：将Value绑定到预编译语句的指定位置
    void bindValue(sql::PreparedStatement* ps, int idx, const ColumnSpec& c, const Value& v) const;
    // 读取结果：从结果集指定列读取并转换为Value
    Value readValue(sql::ResultSet* rs, int idx, const ColumnSpec& c) const;
};
} // namespace uORM
