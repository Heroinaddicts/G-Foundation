// Run with: cd GFoundationTests/Regression && go run -race .
package main

import (
	"GFoundation/Foundation"
	"GFoundation/Foundation/Http"
	"GFoundationApi"
	"fmt"
	"runtime"
	"time"
)

func check(ok bool, format string, args ...any) {
	if !ok {
		panic(fmt.Sprintf(format, args...))
	}
}

func pump(update func(), done func() bool) {
	deadline := time.Now().Add(5 * time.Second)
	for !done() {
		check(time.Now().Before(deadline), "regression timed out")
		update()
		time.Sleep(100 * time.Microsecond)
	}
}

func timerDueResume() {
	g := Foundation.NewGTimer()
	defer g.Clear()
	target := new(int)
	beats, ends := 0, 0
	var resumed time.Time
	var callback GFoundationApi.TimerCallback
	callback = func(state uint8, count int, data, context any, natural bool) {
		if state == GFoundationApi.TimerStateBeat {
			beats++
			if beats == 2 {
				check(!resumed.IsZero(), "due timer fired while paused")
				check(time.Since(resumed) < 100*time.Millisecond, "due Resume waited for wheel wrap: %s", time.Since(resumed))
			}
		}
		if state == GFoundationApi.TimerStateEnd {
			ends++
			check(natural && beats == 2, "due timer End mismatch")
		}
	}
	g.StartTimer(callback, target, nil, 10, 2, 10)
	g.StartTimer(func(state uint8, count int, data, context any, natural bool) {
		if state != GFoundationApi.TimerStateBeat {
			return
		}
		if count == 0 {
			check(beats == 1, "control must pause the pending second Beat")
			g.PauseTimer(callback, target)
		} else {
			resumed = time.Now()
			g.ResumeTimer(callback, target)
		}
	}, target, nil, 20, 2, 20)
	pump(g.Update, func() bool { return ends == 1 })
	fmt.Println("PASS Timer due Resume avoids wheel wrap")
}

func timerPauseInCallback(pauseAtStart bool) {
	g := Foundation.NewGTimer()
	defer g.Clear()
	target := new(int)
	beats, starts, ends := 0, 0, 0
	var resumed time.Time
	var callback GFoundationApi.TimerCallback
	pause := func() {
		g.PauseTimer(callback, target)
		g.StartTimer(func(state uint8, count int, data, context any, natural bool) {
			if state == GFoundationApi.TimerStateBeat {
				resumed = time.Now()
				g.ResumeTimer(callback, target)
			}
		}, target, nil, 20, 1, 1)
	}
	callback = func(state uint8, count int, data, context any, natural bool) {
		switch state {
		case GFoundationApi.TimerStateStart:
			starts++
			if pauseAtStart {
				pause()
			}
		case GFoundationApi.TimerStateBeat:
			check(count == beats, "Beat count mismatch: %d/%d", count, beats)
			beats++
			if pauseAtStart {
				check(!resumed.IsZero(), "Beat ran after Pause in Start")
				if beats == 1 {
					check(time.Since(resumed) < 100*time.Millisecond, "first Beat delayed after Resume")
				}
			} else if beats == 1 {
				pause()
			} else {
				check(!resumed.IsZero(), "Beat ran while paused")
				gap := time.Since(resumed)
				check(gap >= 25*time.Millisecond && gap < 100*time.Millisecond, "next interval not preserved: %s", gap)
			}
		case GFoundationApi.TimerStateEnd:
			ends++
			check(natural && starts == 1 && beats == 2, "callback pause End mismatch")
		}
	}
	g.StartTimer(callback, target, nil, 10, 2, 40)
	pump(g.Update, func() bool { return ends == 1 })
	fmt.Printf("PASS Timer Pause in callback start=%t\n", pauseAtStart)
}

func timerPauseReentry() {
	g := Foundation.NewGTimer()
	defer g.Clear()
	target := new(int)
	beats, ends, pauses, resumes := 0, 0, 0, 0
	var callback GFoundationApi.TimerCallback
	callback = func(state uint8, count int, data, context any, natural bool) {
		switch state {
		case GFoundationApi.TimerStatePause:
			pauses++
			if pauses == 1 {
				g.ResumeTimer(callback, target)
			}
		case GFoundationApi.TimerStateResume:
			resumes++
			if resumes == 1 {
				g.PauseTimer(callback, target)
			}
		case GFoundationApi.TimerStateBeat:
			beats++
		case GFoundationApi.TimerStateEnd:
			ends++
			check(natural && beats == 1, "reentrant timer End mismatch")
		}
	}
	g.StartTimer(callback, target, nil, 20, 1, 20)
	g.PauseTimer(callback, target)
	check(pauses == 2 && resumes == 1 && beats == 0, "reentrant Pause/Resume mismatch")
	g.ResumeTimer(callback, target)
	pump(g.Update, func() bool { return ends == 1 })
	fmt.Println("PASS Timer Pause/Resume callback reentry")
}

func taskCompletions() {
	g := Foundation.NewGTask()
	const total = 12000
	seen := make([]bool, total)
	ordered := make([]int, 4)
	for i := range ordered {
		ordered[i] = i
	}
	done := 0
	callback := func(id int, orderedTask bool) GFoundationApi.TaskCompletedCallback {
		return func(success bool, context any) {
			check(success == (id%7 != 0) && context == id, "task result mismatch %d", id)
			check(!seen[id], "duplicate completion %d", id)
			seen[id] = true
			done++
			if orderedTask {
				mask := id % 4
				check(ordered[mask] == id, "ordered callback mismatch %d/%d", ordered[mask], id)
				ordered[mask] += 8
			}
		}
	}
	for id := 0; id < total; id++ {
		mask := GFoundationApi.Unorder
		if id%8 < 4 {
			mask = int64(id % 4)
		}
		g.PushTask(mask, func() (bool, any) { return id%7 != 0, id }, callback(id, mask != GFoundationApi.Unorder))
	}
	group := g.CreateTaskGroup()
	groupCallbacks, groupDone := 0, false
	for i := 0; i < 32; i++ {
		group.AddTask(func() (bool, any) { return i%2 == 0, i }, func(success bool, context any) {
			check(success == (context.(int)%2 == 0), "group result mismatch")
			groupCallbacks++
		})
	}
	group.SetAllCompletedCallback(func(ok, bad int) {
		check(!groupDone && ok == 16 && bad == 16 && groupCallbacks == 32, "group completion mismatch")
		groupDone = true
	})
	group.Start()
	pump(g.Update, func() bool { return done == total && groupDone })
	fmt.Println("PASS Task concurrent producers, FIFO and group completion")

	callbacks := 0
	g.PushTask(GFoundationApi.Unorder, func() (bool, any) { return true, nil }, func(bool, any) {
		callbacks++
		started := make(chan struct{})
		g.PushTask(GFoundationApi.Unorder, func() (bool, any) { close(started); return true, nil }, func(bool, any) { callbacks++ })
		select {
		case <-started:
		case <-time.After(time.Second):
			panic("nested task did not start")
		}
		time.Sleep(2 * time.Millisecond)
	})
	pump(g.Update, func() bool { return callbacks > 0 })
	check(callbacks == 1, "Update consumed a completion produced after entry")
	pump(g.Update, func() bool { return callbacks == 2 })
	fmt.Println("PASS Task Update defers new completions to the next batch")
}

func httpCanceledNil() {
	h := Http.NewGHttp()
	r := h.CreateHttpRequest()
	done := false
	r.SetHttpResponseCallback(func(req GFoundationApi.IHttpRequest, resp GFoundationApi.IHttpResponse) {
		check(req == r && req.IsCanceled() && resp == nil, "pre-canceled response is a typed nil")
		done = true
	})
	r.Cancel()
	r.Send()
	pump(h.Update, func() bool { return done })
	fmt.Println("PASS HTTP absent response is a nil interface")
}

func main() {
	runtime.LockOSThread()
	timerDueResume()
	timerPauseInCallback(false)
	timerPauseInCallback(true)
	timerPauseReentry()
	taskCompletions()
	httpCanceledNil()
	fmt.Println("REGRESSION PASS")
}
