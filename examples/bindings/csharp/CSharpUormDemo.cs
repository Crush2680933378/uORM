// 文件说明：
// C# 通过 P/Invoke 使用 uORM C ABI 的完整示例（无需额外依赖，.NET 6+ 直接运行）。
// 覆盖：驱动枚举、打开数据库、建表、唯一索引、参数化插入、查询迭代（含 NULL）、
//       事务提交/回滚、UPDATE、DELETE、错误处理。
//
// 运行：
//   dotnet run（或 csc 编译后运行）
//   确保 uorm_c.dll 与驱动 dll 在输出目录或 PATH 上
using System;
using System.Runtime.InteropServices;

public static class UormLib
{
    // ---- 常量 ----
    public const int TYPE_NULL = 0, TYPE_INT = 1, TYPE_DOUBLE = 2, TYPE_STRING = 3;

    // ---- 结构（与 uorm_c.h 布局一致）----
    [StructLayout(LayoutKind.Sequential)]
    public struct Param
    {
        public int type;
        public long i;
        public double d;
        public string s;

        public static Param Int(long v) => new Param { type = TYPE_INT, i = v };
        public static Param Double(double v) => new Param { type = TYPE_DOUBLE, d = v };
        public static Param Str(string v) => new Param { type = TYPE_STRING, s = v };
        public static Param Null() => new Param { type = TYPE_NULL };
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DsOptions
    {
        public string driver;
        public string host;
        public int port;
        public string username;
        public string password;
        public string database;
        public int poolSize;
        public int acquireTimeoutMs;
        [MarshalAs(UnmanagedType.I1)] public bool useTLS;
    }

    // ---- C 函数导入 ----
    const string Lib = "uorm_c";

    [DllImport(Lib)] public static extern IntPtr uorm_version();
    [DllImport(Lib)] public static extern int uorm_driver_count();
    [DllImport(Lib)] public static extern IntPtr uorm_last_error();

    [DllImport(Lib)] public static extern IntPtr uorm_ds_create(ref DsOptions options);
    [DllImport(Lib)] public static extern void uorm_ds_destroy(IntPtr ds);

    [DllImport(Lib)] public static extern int uorm_ds_execute(IntPtr ds, string sql,
        [In] Param[]? params, int count, out long affected);

    [DllImport(Lib)] public static extern int uorm_ds_query(IntPtr ds, string sql,
        [In] Param[]? params, int count, out IntPtr result);

    [DllImport(Lib)] public static extern int uorm_result_row_count(IntPtr result);
    [DllImport(Lib)] public static extern int uorm_result_column_count(IntPtr result);
    [DllImport(Lib)] public static extern IntPtr uorm_result_column_name(IntPtr result, int column);
    [DllImport(Lib)] public static extern int uorm_value_is_null(IntPtr result, int row, int column);
    [DllImport(Lib)] public static extern long uorm_value_int64(IntPtr result, int row, int column);
    [DllImport(Lib)] public static extern double uorm_value_double(IntPtr result, int row, int column);
    [DllImport(Lib, CharSet = CharSet.Ansi)] public static extern string uorm_value_string(IntPtr result, int row, int column);
    [DllImport(Lib)] public static extern void uorm_result_destroy(IntPtr result);

    [DllImport(Lib)] public static extern int uorm_ds_acquire(IntPtr ds, out IntPtr conn);
    [DllImport(Lib)] public static extern void uorm_conn_release(IntPtr conn);
    [DllImport(Lib)] public static extern int uorm_conn_begin(IntPtr conn);
    [DllImport(Lib)] public static extern int uorm_conn_commit(IntPtr conn);
    [DllImport(Lib)] public static extern int uorm_conn_rollback(IntPtr conn);
    [DllImport(Lib)] public static extern int uorm_conn_execute(IntPtr conn, string sql,
        [In] Param[]? params, int count, out long affected);

    static string LastError() => Marshal.PtrToStringAnsi(uorm_last_error()) ?? "";

    static void Check(int status, string what)
    {
        if (status != 0) throw new Exception($"{what} 失败({status}): {LastError()}");
    }

    public static int Main()
    {
        Console.WriteLine("uORM 版本: " + Marshal.PtrToStringAnsi(uorm_version()));

        // 1. 打开 SQLite 数据源
        var opt = new DsOptions
        {
            driver = "sqlite",
            database = "uorm_csharp_demo.db",
            poolSize = 2,
        };
        IntPtr ds = uorm_ds_create(ref opt);
        if (ds == IntPtr.Zero) throw new Exception("打开失败: " + LastError());

        // 2. 建表 + 唯一索引
        Check(uorm_ds_execute(ds,
            "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INT)",
            null, 0, out _), "建表");
        Check(uorm_ds_execute(ds,
            "CREATE UNIQUE INDEX IF NOT EXISTS uq_users_name ON users (name)",
            null, 0, out _), "建索引");

        // 3. 参数化插入
        Check(uorm_ds_execute(ds,
            "INSERT INTO users (name, age, city) VALUES (?, ?, ?)",
            new[] { Param.Str("Tom"), Param.Int(25), Param.Str("北京") }, 3, out long affected), "插入");
        Console.WriteLine($"插入 {affected} 行");
        Check(uorm_ds_execute(ds,
            "INSERT INTO users (name, age, city) VALUES (?, ?, ?)",
            new[] { Param.Str("Jerry"), Param.Int(30), Param.Str("上海") }, 3, out _), "插入 Jerry");

        // 4. 查询迭代（含 NULL）
        Check(uorm_ds_query(ds,
            "SELECT id, name, age, city FROM users WHERE age >= ? ORDER BY id",
            new[] { Param.Int(18) }, 1, out IntPtr rs), "查询");
        int rows = uorm_result_row_count(rs);
        for (int r = 0; r < rows; ++r)
        {
            string city = uorm_value_is_null(rs, r, 3) != 0 ? "(NULL)"
                        : uorm_value_string(rs, r, 3);
            Console.WriteLine($"  {uorm_value_int64(rs, r, 0)} | " +
                $"{uorm_value_string(rs, r, 1)} | {uorm_value_int64(rs, r, 2)} | {city}");
        }
        uorm_result_destroy(rs);

        // 5. 事务：提交 + 回滚
        Check(uorm_ds_acquire(ds, out IntPtr conn), "借出连接");
        Check(uorm_conn_begin(conn), "begin");
        Check(uorm_conn_execute(conn,
            "INSERT INTO users (name, age) VALUES (?, ?)",
            new[] { Param.Str("TxUser"), Param.Int(40) }, 2, out _), "事务内插入");
        Check(uorm_conn_commit(conn), "commit");

        Check(uorm_conn_begin(conn), "begin2");
        Check(uorm_conn_execute(conn,
            "INSERT INTO users (name) VALUES (?)",
            new[] { Param.Str("Ghost") }, 1, out _), "事务内插入2");
        Check(uorm_conn_rollback(conn), "rollback");
        uorm_conn_release(conn);

        // 6. 更新 / 删除
        Check(uorm_ds_execute(ds,
            "UPDATE users SET age = ? WHERE name = ?",
            new[] { Param.Int(26), Param.Str("Tom") }, 2, out _), "更新");
        Check(uorm_ds_execute(ds,
            "DELETE FROM users WHERE name = ?",
            new[] { Param.Str("Jerry") }, 1, out _), "删除");

        // 7. 错误处理
        int st = uorm_ds_query(ds, "SELECT * FROM no_such_table", null, 0, out IntPtr rs2);
        if (st != 0) Console.WriteLine($"预期错误({st}): {LastError()}");

        uorm_ds_destroy(ds);
        Console.WriteLine("C# 绑定演示完成");
        return 0;
    }
}
