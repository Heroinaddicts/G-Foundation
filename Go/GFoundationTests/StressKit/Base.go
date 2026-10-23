package StressKit

import (
	"GFoundationApi"
	"fmt"
	"time"
)

type Base struct {
	API                     GFoundationApi.GFoundationApi
	C                       Config
	R                       *Recorder
	P                       *Pacer
	Name, Prefix            string
	Deadline, nextProgress  time.Time
	memoryHeld, memoryArmed bool
}

func (b *Base) Init(api GFoundationApi.GFoundationApi, name, prefix string, rate, capacity int) bool {
	b.API = api
	b.Name = name
	b.Prefix = prefix
	b.memoryHeld = api.GetLaunchArgument("memory_probe") == "1"
	var err error
	b.C, err = Parse(api, prefix, rate, capacity)
	if err != nil {
		fmt.Printf("[%s] FAIL configuration: %v\n", name, err)
		return false
	}
	return true
}
func (b *Base) Start(extra map[string]any) bool {
	if b.memoryHeld && !b.memoryArmed {
		return false
	}
	if extra == nil {
		extra = map[string]any{}
	}
	extra["load"] = b.C
	var err error
	b.R, err = New(b.API, b.Name, b.Prefix, extra)
	if err != nil {
		fmt.Printf("[%s] COMPLETE passed=0 failed=1 export=%v\n", b.Name, err)
		return false
	}
	b.P = NewPacer(b.R.Started, b.C)
	b.Deadline = b.R.Started.Add(time.Duration(b.C.Seconds+b.C.MaxLife+b.C.DrainSeconds) * time.Second)
	b.nextProgress = b.R.Started.Add(time.Second)
	b.R.Count("arrival_epochs_planned", uint64(b.C.Seconds*100))
	b.R.Count("admissions_planned", uint64(b.C.Seconds*b.C.Rate))
	return true
}
func (b *Base) Poll() {
	if b.R == nil {
		return
	}
	b.R.Poll()
	if !b.R.Finishing && time.Now().After(b.nextProgress) {
		b.R.Progress()
		b.nextProgress = time.Now().Add(time.Second)
	}
}
func (b *Base) Expired() bool { return !b.Deadline.IsZero() && time.Now().After(b.Deadline) }
func (b *Base) Finish() {
	if b.R == nil || b.R.Finishing {
		return
	}
	b.P.Stop(b.R)
	if b.R.Counters["admissions_skipped_scheduler"] > 0 {
		b.R.Fail("arrival scheduler missed planned admissions; see counters")
	}
	b.R.Finish()
}
func (b *Base) Launch(api GFoundationApi.GFoundationApi) bool { return true }

// MemoryProbe drives repeated, fully drained rounds on the main thread.
func (b *Base) MemoryPrepare() { b.memoryArmed = true }
func (b *Base) MemoryDone() (bool, bool) {
	return b.R != nil && b.R.Done, b.R != nil && b.R.report != nil && b.R.report.Status != "passed"
}
