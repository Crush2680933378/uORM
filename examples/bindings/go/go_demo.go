// 文件说明：
// Go 通过 cgo 使用 uORM C ABI 的完整示例。
// 覆盖：驱动枚举、打开数据库、建表、唯一索引、参数化插入、查询迭代（含 NULL）、
//       事务提交/回滚、UPDATE、DELETE、错误处理。
//
// 运行前置（MSYS2 MinGW 示例）：
//   export CGO_ENABLED=1
//   export PATH=/mingw64/bin:$PATH
//   动态库搜索：将 uorm_c.dll 及 libmariadb/libpq/sqlite3 所在目录加入 PATH
//   go mod init uorm-go-demo && go run go_demo.go
package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -L${SRCDIR}/../../build -luorm_c -lmariadb -lpq -lsqlite3 -lws2_32
#include <uORM/abi/uorm_c.h>
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// ---- 与 C 结构对应的绑定 ----

type dsOptions struct {
	driver           *C.char
	host             *C.char
	port             C.int
	username         *C.char
	password         *C.char
	database         *C.char
	poolSize         C.int
	acquireTimeoutMs C.int
	useTLS           C.int
}

// uorm_param：{int type; long long i; double d; const char* s;}（8 字节对齐）
type param struct {
	typ C.int
	_   [4]byte
	i64 C.longlong
	d   C.double
	s   *C.char
}

func pInt(v int) param {
	return param{typ: C.UORM_TYPE_INT, i64: C.longlong(v)}
}
func pStr(s string) param {
	return param{typ: C.UORM_TYPE_STRING, s: C.CString(s)}
}

func check(st C.int, what string) {
	if st != C.UORM_OK {
		panic(fmt.Sprintf("%s 失败(%d): %s", what, st, C.GoString(C.uorm_last_error())))
	}
}

func main() {
	fmt.Println("uORM 版本:", C.GoString(C.uorm_version()))

	// 1. 打开 SQLite 数据源
	opt := dsOptions{
		driver:   C.CString("sqlite"),
		database: C.CString("uorm_go_demo.db"),
		poolSize: 2,
	}
	defer func() {
		C.free(unsafe.Pointer(opt.driver))
		C.free(unsafe.Pointer(opt.database))
	}()
	ds := C.uorm_ds_create((*C.uorm_ds_options)(unsafe.Pointer(&opt)))
	if ds == nil {
		panic("打开数据源失败: " + C.GoString(C.uorm_last_error()))
	}
	defer C.uorm_ds_destroy(ds)

	// 2. 建表 + 唯一索引
	check(C.uorm_ds_execute(ds, C.CString(
		"CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INT, city TEXT)"),
		nil, 0, nil), "建表")
	check(C.uorm_ds_execute(ds, C.CString(
		"CREATE UNIQUE INDEX IF NOT EXISTS uq_users_name ON users (name)"),
		nil, 0, nil), "建索引")

	// 3. 参数化插入（绑定顺序即 ? 顺序）
	p1 := pStr("Tom")
	p2 := pInt(25)
	p3 := pStr("北京")
	ins := [3]param{p1, p2, p3}
	var affected C.longlong
	check(C.uorm_ds_execute(ds,
		C.CString("INSERT INTO users (name, age, city) VALUES (?, ?, ?)"),
		&ins[0], 3, &affected), "插入 Tom")
	defer C.free(unsafe.Pointer(p1.s))
	defer C.free(unsafe.Pointer(p3.s))
	fmt.Println("插入行数:", affected)

	p4 := pStr("Jerry")
	p5 := pInt(30)
	p6 := pStr("上海")
	ins2 := [3]param{p4, p5, p6}
	check(C.uorm_ds_execute(ds,
		C.CString("INSERT INTO users (name, age, city) VALUES (?, ?, ?)"),
		&ins2[0], 3, &affected), "插入 Jerry")
	defer C.free(unsafe.Pointer(p4.s))
	defer C.free(unsafe.Pointer(p6.s))

	// 4. 查询迭代（含 NULL 处理）
	minAge := pInt(18)
	var rs *C.uorm_result
	check(C.uorm_ds_query(ds,
		C.CString("SELECT id, name, age, city FROM users WHERE age >= ? ORDER BY id"),
		&minAge, 1, &rs), "查询")
	for r := 0; r < int(C.uorm_result_row_count(rs)); r++ {
		cr, cn := C.int(r), C.int(r)
		city := "(NULL)"
		if C.uorm_value_is_null(rs, cr, 3) == 0 {
			city = C.GoString(C.uorm_value_string(rs, cr, 3))
		}
		fmt.Printf("  %d | %s | %d | %s\n",
			int(C.uorm_value_int64(rs, cr, 0)),
			C.GoString(C.uorm_value_string(rs, cr, 1)),
			int(C.uorm_value_int64(rs, cr, 2)),
			city)
	}
	C.uorm_result_destroy(rs)

	// 5. 事务：提交
	var conn *C.uorm_connection
	check(C.uorm_ds_acquire(ds, &conn), "借出连接")
	check(C.uorm_conn_begin(conn), "begin")
	txName := pStr("TxUser")
	txAge := pInt(40)
	txp := [2]param{txName, txAge}
	check(C.uorm_conn_execute(conn,
		C.CString("INSERT INTO users (name, age) VALUES (?, ?)"),
		&txp[0], 2, nil), "事务内插入")
	C.free(unsafe.Pointer(txName.s))
	check(C.uorm_conn_commit(conn), "commit")

	// 6. 事务：回滚
	check(C.uorm_conn_begin(conn), "begin2")
	ghost := pStr("ghost")
	gp := [1]param{ghost}
	check(C.uorm_conn_execute(conn,
		C.CString("INSERT INTO users (name) VALUES (?)"), &gp[0], 1, nil), "事务内插入2")
	check(C.uorm_conn_rollback(conn), "rollback")
	C.free(unsafe.Pointer(ghost.s))

	// 7. UPDATE / DELETE
	newAge := pInt(26)
	tomName := pStr("Tom")
	upd := [2]param{newAge, tomName}
	check(C.uorm_conn_execute(conn, C.CString("UPDATE users SET age = ? WHERE name = ?"),
		&upd[0], 2, nil), "更新")
	jerry := pStr("Jerry")
	jp := [1]param{jerry}
	check(C.uorm_ds_execute(ds, C.CString("DELETE FROM users WHERE name = ?"),
		&jp[0], 1, nil), "删除")
	C.free(unsafe.Pointer(tomName.s))
	C.free(unsafe.Pointer(jerry.s))
	C.uorm_conn_release(conn)

	// 8. 错误处理
	var rs2 *C.uorm_result
	st := C.uorm_ds_query(ds, C.CString("SELECT * FROM no_such_table"), nil, 0, &rs2)
	if st != C.UORM_OK {
		fmt.Printf("预期错误(%d): %s\n", st, C.GoString(C.uorm_last_error()))
	}

	fmt.Println("Go 绑定演示完成")
}
