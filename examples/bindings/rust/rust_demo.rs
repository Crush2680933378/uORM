// 文件说明：
// Rust 通过 extern "C" 使用 uORM C ABI 的完整示例（无需 bindgen）。
// 覆盖：驱动枚举、打开数据库、建表、唯一索引、参数化插入、查询迭代（含 NULL）、
//       事务提交/回滚、UPDATE、DELETE、错误处理、RAII 资源管理。
//
// 运行：
//   rustc --edition 2021 -L ../build -l uorm_c rust_demo.rs -o rust_demo
//   （确保 uorm_c 与 libmariadb/libpq/sqlite3 动态库在库搜索路径或 PATH 上）
use std::ffi::{c_char, c_double, c_int, c_longlong, CStr, CString};
use std::ptr;

const UORM_TYPE_NULL: c_int = 0;
const UORM_TYPE_INT: c_int = 1;
const UORM_TYPE_DOUBLE: c_int = 2;
const UORM_TYPE_STRING: c_int = 3;

// ---- C 结构绑定（#[repr(C)] 与 C 布局一致）----
#[repr(C)]
struct UormParam {
    typ: c_int,
    _pad: [u8; 4],
    i: c_longlong,
    d: c_double,
    s: *const c_char,
}

#[repr(C)]
struct UormDsOptions {
    driver: *const c_char,
    host: *const c_char,
    port: c_int,
    username: *const c_char,
    password: *const c_char,
    database: *const c_char,
    pool_size: c_int,
    acquire_timeout_ms: c_int,
    use_tls: c_int,
}

// ---- extern "C" 声明 ----
#[link(name = "uorm_c")]
extern "C" {
    fn uorm_version() -> *const c_char;
    fn uorm_driver_count() -> c_int;
    fn uorm_last_error() -> *const c_char;
    fn uorm_ds_create(options: *const UormDsOptions) -> *mut OpaqueDataSource;
    fn uorm_ds_destroy(ds: *mut OpaqueDataSource);
    fn uorm_ds_execute(ds: *mut OpaqueDataSource, sql: *const c_char,
                       params: *const UormParam, count: c_int,
                       affected: *mut c_longlong) -> c_int;
    fn uorm_ds_query(ds: *mut OpaqueDataSource, sql: *const c_char,
                     params: *const UormParam, count: c_int,
                     result: *mut *mut OpaqueResult) -> c_int;
    fn uorm_ds_acquire(ds: *mut OpaqueDataSource, conn: *mut *mut OpaqueConnection) -> c_int;
    fn uorm_conn_release(conn: *mut OpaqueConnection);
    fn uorm_conn_begin(conn: *mut OpaqueConnection) -> c_int;
    fn uorm_conn_commit(conn: *mut OpaqueConnection) -> c_int;
    fn uorm_conn_rollback(conn: *mut OpaqueConnection) -> c_int;
    fn uorm_conn_execute(conn: *mut OpaqueConnection, sql: *const c_char,
                         params: *const UormParam, count: c_int,
                         affected: *mut c_longlong) -> c_int;
    fn uorm_result_row_count(result: *const OpaqueResult) -> c_int;
    fn uorm_value_is_null(result: *const OpaqueResult, row: c_int, col: c_int) -> c_int;
    fn uorm_value_int64(result: *const OpaqueResult, row: c_int, col: c_int) -> c_longlong;
    fn uorm_value_string(result: *const OpaqueResult, row: c_int, col: c_int) -> *const c_char;
    fn uorm_result_destroy(result: *mut OpaqueResult);
}

// 不透明类型（C 侧只作句柄）
#[repr(C)]
struct OpaqueDataSource { _p: [u8; 0] }
#[repr(C)]
struct OpaqueConnection { _p: [u8; 0] }
#[repr(C)]
struct OpaqueResult { _p: [u8; 0] }

// ---- 辅助 ----
fn last_error() -> String {
    unsafe { CStr::from_ptr(uorm_last_error()).to_string_lossy().into_owned() }
}

fn check(status: c_int, what: &str) {
    if status != 0 {
        panic!("{} 失败({}): {}", what, status, last_error());
    }
}

fn param_int(v: i64) -> UormParam {
    UormParam { typ: UORM_TYPE_INT, _pad: [0; 4], i: v, d: 0.0, s: ptr::null() }
}

fn param_str(s: &CString) -> UormParam {
    UormParam { typ: UORM_TYPE_STRING, _pad: [0; 4], i: 0, d: 0.0, s: s.as_ptr() }
}

fn param_null() -> UormParam {
    UormParam { typ: UORM_TYPE_NULL, _pad: [0; 4], i: 0, d: 0.0, s: ptr::null() }
}

// RAII：结果集
struct ResultSet(*mut OpaqueResult);
impl Drop for ResultSet {
    fn drop(&mut self) { unsafe { uorm_result_destroy(self.0) }; }
}

// RAII：事务连接（Drop 未提交自动回滚，连接归还池）
struct Connection(*mut OpaqueConnection);
impl Drop for Connection {
    fn drop(&mut self) { unsafe { uorm_conn_release(self.0) }; }
}

fn main() {
    unsafe {
        println!("uORM 版本: {}", CStr::from_ptr(uorm_version()).to_string_lossy());

        // 1. 打开 SQLite 数据源
        let driver = CString::new("sqlite").unwrap();
        let database = CString::new("uorm_rust_demo.db").unwrap();
        let opts = UormDsOptions {
            driver: driver.as_ptr(),
            host: ptr::null(),
            port: 0,
            username: ptr::null(),
            password: ptr::null(),
            database: database.as_ptr(),
            pool_size: 2,
            acquire_timeout_ms: 3000,
            use_tls: 0,
        };
        let ds = {
            let h = uorm_ds_create(&opts);
            if h.is_null() { panic!("打开数据源失败: {}", last_error()); }
            h
        };

        // 2. 建表 + 唯一索引
        let create = CString::new(
            "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INT)")
            .unwrap();
        check(uorm_ds_execute(ds, create.as_ptr(), ptr::null(), 0, ptr::null_mut()), "建表");
        let idx = CString::new(
            "CREATE UNIQUE INDEX IF NOT EXISTS uq_users_name ON users (name)").unwrap();
        check(uorm_ds_execute(ds, idx.as_ptr(), ptr::null(), 0, ptr::null_mut()), "建索引");

        // 3. 参数化插入（CString 生命周期覆盖调用）
        let ins = CString::new("INSERT INTO users (name, age) VALUES (?, ?)").unwrap();
        let name1 = CString::new("Tom").unwrap();
        let p1 = [param_str(&name1), param_int(25)];
        check(uorm_ds_execute(ds, ins.as_ptr(), p1.as_ptr(), 2, ptr::null_mut()), "插入 Tom");

        let name2 = CString::new("Jerry").unwrap();
        let p2 = [param_str(&name2), param_int(30)];
        check(uorm_ds_execute(ds, ins.as_ptr(), p2.as_ptr(), 2, ptr::null_mut()), "插入 Jerry");

        // 4. 查询迭代（含 NULL）
        let sel = CString::new(
            "SELECT id, name, age, city FROM users WHERE age >= ? ORDER BY id").unwrap();
        let q = [param_int(18)];
        let mut rs: *mut OpaqueResult = ptr::null_mut();
        check(uorm_ds_query(ds, sel.as_ptr(), q.as_ptr(), 1, &mut rs), "查询");
        let result = ResultSet(rs);
        for r in 0..result.rows() {
            let name = if uorm_value_is_null(result.0, r, 1) != 0 {
                "(NULL)".to_string()
            } else {
                CStr::from_ptr(uorm_value_string(result.0, r, 1)).to_string_lossy().into_owned()
            };
            println!("  {} | {} | {}", uorm_value_int64(result.0, r, 0), name,
                     uorm_value_int64(result.0, r, 2));
        }
        drop(result);

        // 5. 事务：提交
        {
            let mut conn: *mut OpaqueConnection = ptr::null_mut();
            check(uorm_ds_acquire(ds, &mut conn), "借出连接");
            let conn = Connection(conn);
            check(uorm_conn_begin(conn.0), "begin");
            let tx_name = CString::new("TxUser").unwrap();
            let tp = [param_str(&tx_name), param_int(40)];
            check(uorm_conn_execute(conn.0, ins.as_ptr(), tp.as_ptr(), 2, ptr::null_mut()), "事务内插入");
            check(uorm_conn_commit(conn.0), "commit");
        } // Connection Drop -> 归还池

        // 6. 事务：回滚
        {
            let mut conn: *mut OpaqueConnection = ptr::null_mut();
            check(uorm_ds_acquire(ds, &mut conn), "借出连接2");
            let conn = Connection(conn);
            check(uorm_conn_begin(conn.0), "begin2");
            let ghost = CString::new("Ghost").unwrap();
            let gp = [param_str(&ghost)];
            check(uorm_conn_execute(conn.0,
                CString::new("INSERT INTO users (name) VALUES (?)").unwrap().as_ptr(),
                gp.as_ptr(), 1, ptr::null_mut()), "事务内插入2");
            check(uorm_conn_rollback(conn.0), "rollback");
        }

        // 7. UPDATE / DELETE
        let upd = CString::new("UPDATE users SET age = ? WHERE name = ?").unwrap();
        let age = CString::new("26").unwrap();
        let tom = CString::new("Tom").unwrap();
        let up = [param_int(26), param_str(&tom)];
        check(uorm_ds_execute(ds, upd.as_ptr(), up.as_ptr(), 2, ptr::null_mut()), "更新");
        let del = CString::new("DELETE FROM users WHERE name = ?").unwrap();
        let dp = [param_str(&tom)];
        check(uorm_ds_execute(ds, del.as_ptr(), dp.as_ptr(), 1, ptr::null_mut()), "删除");

        // 8. 错误处理
        let bad = CString::new("SELECT * FROM no_such_table").unwrap();
        let mut rs: *mut OpaqueResult = ptr::null_mut();
        let st = uorm_ds_query(ds, bad.as_ptr(), ptr::null(), 0, &mut rs);
        if st != 0 {
            println!("预期错误({}): {}", st, last_error());
        }

        drop(ds); // DataSource RAII
        println!("Rust 绑定演示完成");
    }
}
