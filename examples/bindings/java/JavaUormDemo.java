// 文件说明：
// Java 通过 JNA 使用 uORM C ABI 的完整示例（无需写 JNI、无需编译本地代码）。
// 覆盖：驱动枚举、打开数据库、建表、唯一索引、参数化插入、查询迭代（含 NULL）、
//       事务提交/回滚、UPDATE、错误处理。
//
// 运行前置：
//   1. 下载 jna.jar（https://github.com/java-native-access/jna）放入本目录
//   2. uorm_c.dll 与驱动 dll（libmariadb-2.dll、libpq.dll、sqlite3.dll）在 java.library.path
//      或 -Djna.library.path=../build
//   编译运行：
//   javac -cp jna.jar JavaUormDemo.java
//   java  -cp jna.jar:. -Djna.library.path=../build JavaUormDemo
import com.sun.jna.;
import com.sun.jna.ptr.;

public class JavaUormDemo {

    // ---- 与 uorm_c.h 对应的结构映射 ----
    public static class UormParam extends Structure {
        public int type;
        public long i;
        public double d;
        public String s;

        @Override protected java.util.List<String> getFieldOrder() {
            return java.util.Arrays.asList("type", "i", "d", "s");
        }

        public static UormParam ofInt(long v) {
            UormParam p = new UormParam(); p.type = 1; p.i = v; return p;
        }
        public static UormParam ofDouble(double v) {
            UormParam p = new UormParam(); p.type = 2; p.d = v; return p;
        }
        public static UormParam ofString(String v) {
            UormParam p = new UormParam(); p.type = 3; p.s = v; return p;
        }
        public static UormParam ofNull() { return new UormParam(); }
    }

    public static class UormDsOptions extends Structure {
        public String driver;
        public String host;
        public int port;
        public String username;
        public String password;
        public String database;
        public int poolSize;
        public int acquireTimeoutMs;
        public int useTLS;

        @Override protected java.util.List<String> getFieldOrder() {
            return java.util.Arrays.asList("driver", "host", "port", "username",
                    "password", "database", "poolSize", "acquireTimeoutMs", "useTLS");
        }
    }

    // ---- C 函数声明 ----
    public interface UormLib extends Library {
        UormLib INSTANCE = Native.load("uorm_c", UormLib.class);

        String uorm_version();
        int uorm_driver_count();
        String uorm_last_error();
        Pointer uorm_ds_create(UormDsOptions options);
        void uorm_ds_destroy(Pointer ds);
        int uorm_ds_execute(Pointer ds, String sql, UormParam[] params, int count,
                            LongByReference affected);
        int uorm_ds_query(Pointer ds, String sql, UormParam[] params, int count,
                          PointerByReference result);
        int uorm_result_row_count(Pointer result);
        int uorm_result_column_count(Pointer result);
        String uorm_result_column_name(Pointer result, int column);
        int uorm_value_is_null(Pointer result, int row, int column);
        long uorm_value_int64(Pointer result, int row, int column);
        double uorm_value_double(Pointer result, int row, int column);
        String uorm_value_string(Pointer result, int row, int column);
        void uorm_result_destroy(Pointer result);
        int uorm_ds_acquire(Pointer ds, PointerByReference conn);
        void uorm_conn_release(Pointer conn);
        int uorm_conn_begin(Pointer conn);
        int uorm_conn_commit(Pointer conn);
        int uorm_conn_rollback(Pointer conn);
    }

    static void check(int status, String what) {
        if (status != 0) {
            throw new RuntimeException(what + " 失败(" + status + "): "
                    + UormLib.INSTANCE.uorm_last_error());
        }
    }

    public static void main(String[] args) {
        UormLib lib = UormLib.INSTANCE;
        System.out.println("uORM 版本: " + lib.uorm_version());

        // 1. 打开 SQLite 数据源
        UormDsOptions opt = new UormDsOptions();
        opt.driver = "sqlite";
        opt.database = "uorm_java_demo.db";
        opt.poolSize = 2;
        Pointer ds = lib.uorm_ds_create(opt);
        if (ds == null) throw new RuntimeException("打开失败: " + lib.uorm_last_error());

        // 2. 建表 + 唯一索引
        check(lib.uorm_ds_execute(ds,
                "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INT)",
                null, 0, null), "建表");
        check(lib.uorm_ds_execute(ds,
                "CREATE UNIQUE INDEX IF NOT EXISTS uq_users_name ON users (name)",
                null, 0, null), "建索引");

        // 3. 参数化插入
        UormParam[] ins = { UormParam.ofString("Tom"), UormParam.ofInt(25) };
        check(lib.uorm_ds_execute(ds,
                "INSERT INTO users (name, age) VALUES (?, ?)", ins, 2, null), "插入 Tom");
        UormParam[] ins2 = { UormParam.ofString("Jerry"), UormParam.ofInt(30) };
        check(lib.uorm_ds_execute(ds,
                "INSERT INTO users (name, age) VALUES (?, ?)", ins2, 2, null), "插入 Jerry");

        // 4. 查询迭代
        PointerByReference rsRef = new PointerByReference();
        UormParam[] sel = { UormParam.ofInt(18) };
        check(lib.uorm_ds_query(ds,
                "SELECT id, name, age FROM users WHERE age >= ? ORDER BY id", sel, 1, rsRef), "查询");
        Pointer rs = rsRef.getValue();
        int rows = lib.uorm_result_row_count(rs);
        for (int r = 0; r < rows; ++r) {
            System.out.printf("  %d | %s | %d%n",
                    lib.uorm_value_int64(rs, r, 0),
                    lib.uorm_value_is_null(rs, r, 1) != 0 ? "(NULL)"
                            : lib.uorm_value_string(rs, r, 1),
                    lib.uorm_value_int64(rs, r, 2));
        }
        lib.uorm_result_destroy(rs);

        // 5. 事务：提交 + 回滚
        PointerByReference connRef = new PointerByReference();
        check(lib.uorm_ds_acquire(ds, connRef), "借出连接");
        Pointer conn = connRef.getValue();
        check(lib.uorm_conn_begin(conn), "begin");
        UormParam[] txIns = { UormParam.ofString("TxUser"), UormParam.ofInt(40) };
        check(lib.uorm_conn_execute(conn,
                "INSERT INTO users (name, age) VALUES (?, ?)", txIns, 2, null), "事务内插入");
        check(lib.uorm_conn_commit(conn), "commit");

        check(lib.uorm_conn_begin(conn), "begin2");
        UormParam[] ghost = { UormParam.ofString("Ghost") };
        check(lib.uorm_conn_execute(conn,
                "INSERT INTO users (name) VALUES (?)", ghost, 1, null), "事务内插入2");
        check(lib.uorm_conn_rollback(conn), "rollback");
        lib.uorm_conn_release(conn);

        // 6. 更新 / 删除 / 错误处理
        check(lib.uorm_ds_execute(ds,
                "UPDATE users SET age = ? WHERE name = ?",
                new UormParam[]{ UormParam.ofInt(26), UormParam.ofString("Tom") }, 2, null), "更新");
        check(lib.uorm_ds_execute(ds,
                "DELETE FROM users WHERE name = ?",
                new UormParam[]{ UormParam.ofString("Jerry") }, 1, null), "删除");

        int st = lib.uorm_ds_query(ds, "SELECT * FROM no_such_table", null, 0, rsRef);
        if (st != 0) System.out.println("预期错误(" + st + "): " + lib.uorm_last_error());

        lib.uorm_ds_destroy(ds);
        System.out.println("Java 绑定演示完成");
    }
}
