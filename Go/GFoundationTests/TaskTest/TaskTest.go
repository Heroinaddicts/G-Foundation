package main

import (
	"GFoundationApi"
	"GFoundationTests/StressKit"
	"fmt"
	"time"
)

type taskResult struct {
	ID    uint64
	Value uint64
}
type TaskTest struct {
	StressKit.Base
	sequence                     uint64
	iterations, groupSize, masks int
	orderedSent, orderedDone     []uint64
	expected                     []uint64
}

func (m *TaskTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	if !m.Init(api, "TaskTest", "task", 10000, 4000) {
		return false
	}
	for _, p := range []struct {
		name          string
		dst           *int
		def, min, max int
	}{
		{"iterations", &m.iterations, 2000, 1, 10000000}, {"group_size", &m.groupSize, 16, 1, 1000}, {"masks", &m.masks, 8, 1, 1000},
	} {
		v, err := StressKit.Int(api, "task", p.name, p.def, p.min, p.max)
		if err != nil {
			fmt.Println(err)
			return false
		}
		*p.dst = v
	}
	if m.C.MaxActive < m.groupSize+1 {
		fmt.Println("[TaskTest] max_active must exceed group_size")
		return false
	}
	m.orderedSent = make([]uint64, m.masks)
	m.orderedDone = make([]uint64, m.masks)
	// Prepare the oracle before timing starts; submitting work stays O(1).
	m.expected = make([]uint64, 4096)
	for i := range m.expected {
		m.expected[i] = calculate(uint64(i), m.iterations)
	}
	return true
}
func (m *TaskTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if m.Start(map[string]any{"iterations": m.iterations, "group_size": m.groupSize, "masks": m.masks, "expected_values_precomputed": true}) {
		m.Deadline = m.R.Started.Add(time.Duration(m.C.Seconds+m.C.DrainSeconds) * time.Second)
		api.GetTimerApi().StartTimer(m.OnTimerSubmit, m, nil, 10, GFoundationApi.Unlimited, 10)
	}
}
func calculate(id uint64, iterations int) uint64 {
	v := id ^ 0x9e3779b97f4a7c15
	for i := 0; i < iterations; i++ {
		v = (v^(v>>13))*0xff51afd7ed558ccd + uint64(i)
	}
	return v
}
func (m *TaskTest) newTask(op string, group bool) (GFoundationApi.TaskFunction, GFoundationApi.TaskCompletedCallback) {
	m.sequence++
	id := m.sequence
	iterations := m.iterations
	input := id % uint64(len(m.expected))
	expected := m.expected[input] ^ id
	wantSuccess := !(group && id%7 == 0)
	phase := "submit_to_callback"
	if group {
		phase = "add_to_callback"
	}
	sample := m.R.Begin(op, id, phase)
	fn := func() (bool, any) { return wantSuccess, taskResult{id, calculate(input, iterations) ^ id} }
	cb := func(success bool, ctx any) {
		v, ok := ctx.(taskResult)
		var err error
		if !ok || v.ID != id || v.Value != expected || success != wantSuccess {
			err = fmt.Errorf("task result mismatch id=%d success=%t", id, success)
		}
		m.R.Complete(sample, err)
	}
	return fn, cb
}
func (m *TaskTest) OnTimerSubmit(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	n, end := m.P.Tick(m.R, count)
	for i := 0; i < n; i++ {
		kind := m.sequence % 3
		need := 1
		if kind == 2 {
			need = m.groupSize + 1
		}
		if len(m.R.Pending)+need > m.C.MaxActive {
			m.R.Count("admissions_skipped_capacity", uint64(n-i))
			break
		}
		m.R.Count("admissions_issued", 1)
		if kind == 2 {
			g := m.API.GetTaskApi().CreateTaskGroup()
			sample := m.R.Begin("GROUP_ALL", m.sequence+1, "start_to_all_completed")
			wantOK, wantFail, callbacks := 0, 0, 0
			for j := 0; j < m.groupSize; j++ {
				fn, cb := m.newTask("GROUP_TASK", true)
				if m.sequence%7 == 0 {
					wantFail++
				} else {
					wantOK++
				}
				g.AddTask(fn, func(ok bool, ctx any) { cb(ok, ctx); callbacks++ })
			}
			g.SetAllCompletedCallback(func(ok, bad int) {
				var err error
				if ok != wantOK || bad != wantFail || callbacks != m.groupSize {
					err = fmt.Errorf("group totals ok=%d/%d bad=%d/%d callbacks=%d", ok, wantOK, bad, wantFail, callbacks)
				}
				m.R.Complete(sample, err)
			})
			sample.CallNS = time.Since(m.R.Started).Nanoseconds()
			g.Start()
			m.R.Count("groups_started", 1)
		} else {
			op := "UNORDERED"
			mask := GFoundationApi.Unorder
			if kind == 0 {
				op = "ORDERED"
				mask = int64(m.sequence % uint64(m.masks))
			}
			fn, cb := m.newTask(op, false)
			if mask != GFoundationApi.Unorder {
				index := int(mask)
				m.orderedSent[index]++
				seq := m.orderedSent[index]
				inner := cb
				cb = func(ok bool, ctx any) {
					m.orderedDone[index]++
					if m.orderedDone[index] != seq {
						m.R.Fail(fmt.Sprintf("mask %d callback out of order", mask))
					}
					inner(ok, ctx)
				}
			}
			m.API.GetTaskApi().PushTask(mask, fn, cb)
		}
	}
	if end {
		m.API.GetTimerApi().StopTimer(m.OnTimerSubmit, m)
	}
}
func (m *TaskTest) Update(api GFoundationApi.GFoundationApi) {
	if m.R == nil {
		return
	}
	m.Poll()
	if m.R.Finishing {
		return
	}
	if m.Expired() {
		m.R.Fail("task drain deadline exceeded")
		m.Release(api)
		return
	}
	if m.P.Closed && len(m.R.Pending) == 0 {
		m.Finish()
	}
}
func (m *TaskTest) Release(api GFoundationApi.GFoundationApi) {
	if m.R == nil || m.R.Finishing {
		return
	}
	api.GetTimerApi().StopTimer(m.OnTimerSubmit, m)
	m.Finish()
}
func GetModule() GFoundationApi.IModule { return &TaskTest{} }
