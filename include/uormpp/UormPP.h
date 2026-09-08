/*
 * UormPP.h —— uORM 的 C++ 抽象接口（Pimpl / 编译防火墙）
 * =====================================================================
 * 对外只暴露：
 *   1. 抽象基类（纯虚接口）：IDatabase / IResult / ITransaction
 *   2. 工厂函数：openDatabase()
 *   3. 稳定值类型：Value / Param
 * 实际实现（uORM 内核 + 各数据库驱动）全部编译进 uormpp 动态库，
 * 继承这些接口的实现类只存在于库的编译单元中，外部不可见。
 *
 * 典型用法：
 *   auto db = uormpp::openDatabase(opts);
 *   db->execute("CREATE TABLE ...");
 *   auto rs = db->query("SELECT * FROM users WHERE name = ?",
 *                       {uormpp::Param::string("Tom")});
 *   for (int r = 0; r < rs->rowCount(); ++r)
 *       std::cout << rs->getString(r, 0);
 *   {
 *       auto tx = db->beginTransaction();
 *       tx->execute("INSERT ...");
 *       tx->commit();            // 未提交析构时自动回滚
 *   }
 */
#ifndef UORMPP_H
#define UORMPP_H

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/* ---- 导出宏 ---- */
#if defined(_WIN32) && defined(UORMPP_SHARED)
#  ifdef UORMPP_BUILDING
#    define UORMPP_API __declspec(dllexport)
#  else
#    define UORMPP_API __declspec(dllimport)
#  endif
#else
#  define UORMPP_API
#endif

/* ---- 版本 ---- */
#define UORMPP_VERSION_MAJOR 0
#define UORMPP_VERSION_MINOR 8
#define UORMPP_VERSION_PATCH 0
#define UORMPP_VERSION_STRING "0.8.0"

namespace uormpp {

/* =====================================================================
 * 值与参数（对外稳定类型）
 * ===================================================================== */
enum class ValueType { Null, Int, Double, String };

struct Value {
    ValueType type = ValueType::Null;
    std::int64_t i = 0;
    double d = 0.0;
    std::string s;

    static Value null() { return {}; }
    static Value fromInt(std::int64_t v) { Value x; x.type = ValueType::Int; x.i = v; return x; }
    static Value fromDouble(double v) { Value x; x.type = ValueType::Double; x.d = v; return x; }
    static Value fromString(std::string v) { Value x; x.type = ValueType::String; x.s = std::move(v); return x; }
    bool isNull() const { return type == ValueType::Null; }
};

struct Param {
    ValueType type = ValueType::Null;
    std::int64_t i = 0;
    double d = 0.0;
    std::string s;

    Param() = default;
    explicit Param(std::int64_t v) : type(ValueType::Int), i(v) {}
    explicit Param(double v) : type(ValueType::Double), d(v) {}
    explicit Param(const char* v) : type(ValueType::String), s(v ? v : "") {}
    explicit Param(std::string v) : type(ValueType::String), s(std::move(v)) {}

    static Param null() { return {}; }
};

/* =====================================================================
 * 异常（实现内部所有错误统一转换为该异常）
 * ===================================================================== */
class UORMPP_API Error : public std::runtime_error {
public:
    explicit Error(const std::string& msg) : std::runtime_error(msg) {}
};

/* =====================================================================
 * 抽象基类：查询结果
 * ===================================================================== */
class IResult {
public:
    virtual ~IResult() = default;

    virtual int rowCount() const = 0;
    virtual int columnCount() const = 0;
    virtual std::string columnName(int column) const = 0;

    virtual bool isNull(int row, int column) const = 0;
    virtual std::int64_t getInt64(int row, int column) const = 0;
    virtual double getDouble(int row, int column) const = 0;
    virtual std::string getString(int row, int column) const = 0;
};

/* =====================================================================
 * 抽象基类：事务作用域
 * 构造即 BEGIN；commit() 显式提交；未提交析构自动回滚；
 * 连接随对象析构自动归还内部连接池。
 * ===================================================================== */
class ITransaction {
public:
    virtual ~ITransaction() = default;

    virtual std::unique_ptr<IResult> query(const std::string& sql, const std::vector<Param>& params = {}) = 0;
    virtual std::uint64_t execute(const std::string& sql, const std::vector<Param>& params = {}) = 0;

    virtual void commit() = 0;
    virtual void rollback() = 0;
};

/* =====================================================================
 * 抽象基类：数据库（一个数据源 + 内部连接池，线程安全）
 * ===================================================================== */
class IDatabase {
public:
    virtual ~IDatabase() = default;

    /* ---- SQL ---- */
    virtual std::unique_ptr<IResult> query(const std::string& sql, const std::vector<Param>& params = {}) = 0;
    virtual std::uint64_t execute(const std::string& sql, const std::vector<Param>& params = {}) = 0;

    /* ---- 事务 ---- */
    virtual std::unique_ptr<ITransaction> beginTransaction() = 0;

    /* ---- Schema ---- */
    virtual bool tableExists(const std::string& table) = 0;
    virtual bool indexExists(const std::string& table, const std::string& indexName) = 0;
    virtual bool createIndex(const std::string& table, const std::string& indexName,
                             const std::vector<std::string>& columns, bool unique = false) = 0;
    virtual bool dropIndex(const std::string& table, const std::string& indexName) = 0;
    virtual bool addColumn(const std::string& table, const std::string& column,
                           const std::string& type, const std::string& constraints = "") = 0;
    virtual bool dropColumn(const std::string& table, const std::string& column) = 0;
    virtual bool renameColumn(const std::string& table, const std::string& oldName, const std::string& newName) = 0;
    virtual bool renameTable(const std::string& oldName, const std::string& newName) = 0;

    /* ---- 池状态 ---- */
    virtual void stats(int* idle, int* inUse, int* totalCreated) = 0;
};

/* =====================================================================
 * 工厂与全局
 * ===================================================================== */
struct Options {
    std::string driver;              /* "mysql" / "postgresql" / "sqlite" */
    std::string host = "127.0.0.1";
    int port = 0;                    /* 0 = 驱动默认端口 */
    std::string username;
    std::string password;
    std::string database;            /* 数据库名；SQLite 为文件路径 */
    int poolSize = 5;                /* <=0 取默认 5 */
    int acquireTimeoutMs = 3000;     /* <=0 取默认 3000 */
    bool useTLS = false;
};

/* 打开数据库；驱动未知/连接失败抛 Error。 */
UORMPP_API std::unique_ptr<IDatabase> openDatabase(const Options& options);

/* 已编入的驱动列表。 */
UORMPP_API std::vector<std::string> availableDrivers();

/* 库版本，如 "0.8.0"。 */
UORMPP_API std::string version();

} // namespace uormpp

#endif /* UORMPP_H */
