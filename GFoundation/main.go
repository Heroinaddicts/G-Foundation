package main

import (
	"GFoundation/Foundation"
	"runtime"
	"time"
)

func main() {
	runtime.LockOSThread()

	Foundation.LauncherParamsInstance()
	Foundation.GFoundationInstance().Launch()

	for {
		tick := time.Microsecond
		Foundation.GFoundationInstance().Update()
		if time.Microsecond-tick <= 100 {
			time.Sleep(time.Microsecond * 1000)
		}
	}
}
