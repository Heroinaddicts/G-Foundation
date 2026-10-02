package main

import (
	"GFoundation/Foundation"
	"log"
	"net/http"
	"runtime"
	"time"

	_ "net/http/pprof"
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
}
