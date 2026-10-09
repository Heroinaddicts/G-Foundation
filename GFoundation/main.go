package main

import (
	"GFoundation/Foundation"
	"database/sql"
	"fmt"
	"log"
	"net/http"
	"runtime"
	"time"

	_ "net/http/pprof"

	_ "github.com/go-sql-driver/mysql"
)

func startProfiler() {
	go func() {
		log.Println(http.ListenAndServe("127.0.0.1:6060", nil))
	}()
}

func main() {
	startProfiler()

	runtime.LockOSThread()

	Foundation.LauncherParamsInstance()
	Foundation.GFoundationInstance().Launch()

	for {
		tick := time.Now()
		Foundation.GFoundationInstance().Update()
		if time.Since(tick) <= 100*time.Microsecond {
			time.Sleep(time.Microsecond * 1000)
		}
	}

	//
	dsn := "用户名:密码@tcp(127.0.0.1:3306)/数据库名?parseTime=true"

	db, err := sql.Open("mysql", dsn)
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	// sql.Open 不一定会立即连接；Ping 用来检查连接是否可用。
	if err := db.Ping(); err != nil {
		log.Fatal(err)
	}

	rows, err := db.Query("SELECT id, name FROM account")
	if err != nil {
		log.Fatal(err)
	}
	defer rows.Close()

	for rows.Next() {
		var id int64
		var name string

		if err := rows.Scan(&id, &name); err != nil {
			log.Fatal(err)
		}
		fmt.Printf("id=%d name=%s\n", id, name)
	}

	if err := rows.Err(); err != nil {
		log.Fatal(err)
	}
}
