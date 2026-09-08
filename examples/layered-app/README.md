# 分层架构示例（Model / DAO / Service）

展示基于 uORM 构建规范分层应用的组织方式，可直接作为业务项目脚手架。

## 目录结构

```
layered-app/
├── models/              # Model 层：纯数据 + 表映射（不含任何访问代码）
│   ├── User.hpp
│   ├── Order.hpp        # 演示外键与状态机
│   └── Product.hpp      # 演示库存字段
├── dao/
│   ├── BaseDao.hpp      # 泛型基础 DAO：CRUD/分页/批量/计数/事务（模板）
│   ├── UserDao.hpp      # User 专属：按邮箱/用户名查、模糊搜索、部分更新
│   └── OrderDao.hpp     # Order + Product 专属：按用户查订单、原子扣减/回补库存
├── service/
│   └── Service.hpp      # Service 层：业务规则与事务编排（下单/取消/注册）
└── main.cpp             # 组装层：创建 DataSource，装配并调用各层
```

## 分层规则

| 层 | 职责 | 依赖 |
|---|---|---|
| **Model** | 纯数据结构 + `UORM_*` 映射宏 | 仅 uORM 头 |
| **DAO** | 该实体的全部数据访问；`BaseDao<E>` 提供通用操作，具体 DAO 补充专属查询 | DataSource |
| **Service** | 业务规则、事务边界、跨 DAO 编排 | 多个 DAO + DataSource |
| **UI/Controller** | 只调用 Service | — |

## 关键设计

1. **EntityTraits**：声明实体的表名/主键类型/主键列，`BaseDao<E>` 据此泛型化
   `findById` / `removeById`；未注册特征的实体在编译期报错。
2. **BaseDao**：`insert`（自增 id 写回）/ `update` / `removeById` / `findById` /
   `findBy` / `findOneBy` / `findPage` / `count` / `insertRange` / `saveOrUpdate` /
   `updateFields`（部分更新）/ `transaction`。
3. **条件原子更新**：防超卖的库存扣减用
   `UPDATE ... SET stock = stock - ? WHERE id = ? AND stock >= ?`，
   以 `executeUpdate()` 返回值判断是否成功。
4. **事务边界在 Service**：`placeOrder` 在一个事务里完成"扣库存 + 建订单"，
   `cancelOrder` 在一个事务里完成"回补库存 + 状态改 CANCELLED"。
5. **业务异常**：`BusinessError` 与数据库异常（`uORM::SqlError`）分离，
   UI 层据此区分"业务拒绝"与"系统故障"。

## 运行

```bash
cmake --build build            # uORM_layered_demo 目标
cd build && rm -f layered_demo.db && ./uORM_layered_demo
```

预期输出包含：注册（含重名拒绝）、批量插入、下单（扣库存）、订单查询、
消费统计、分页、部分更新、库存不足回滚、取消订单回补、upsert。

## 扩展到自己的项目

1. `models/` 里为每个业务实体建头文件（纯结构体 + `UORM_REFLECTION_NAMED` 或
   `UORM_TABLE_BEGIN` 系列宏）。
2. 用 `DEMO_ENTITY_TRAITS` 注册实体的表名与主键。
3. 为每个实体建 `XxxDao : BaseDao<Xxx>`，补充专属查询/更新。
4. 在 Service 中用 `transaction(...)` 组合多个 DAO 操作保证一致性。
