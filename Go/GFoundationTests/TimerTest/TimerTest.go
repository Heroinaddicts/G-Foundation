package main

import (
	"GFoundationApi"
	"GFoundationTests/StressKit"
	"fmt"
	"math/rand"
	"time"
)

type timerTarget struct {
	id                         uint64
	start, life                *StressKit.Sample
	beats, expected            int
	started, paused, stopEarly bool
	nextDue, pausedAt          time.Time
}
type TimerTest struct {
	StressKit.Base
	active   map[*timerTarget]bool
	random   *rand.Rand
	sequence uint64
}

func (m *TimerTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	if !m.Init(api, "TimerTest", "timer", 2000, 20000) {
		return false
	}
	m.active = map[*timerTarget]bool{}
	m.random = m.C.Random()
	return true
}
func (m *TimerTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if m.Start(nil) {
		api.GetTimerApi().StartTimer(m.OnTimerCreate, m, nil, 10, GFoundationApi.Unlimited, 10)
	}
}
func (m *TimerTest) OnTimerCreate(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	n, end := m.P.Tick(m.R, count)
	for i := 0; i < n; i++ {
		if len(m.active) >= m.C.MaxActive {
			m.R.Count("admissions_skipped_capacity", uint64(n-i))
			break
		}
		m.sequence++
		c := &timerTarget{id: m.sequence, stopEarly: m.sequence%10 == 0}
		c.expected = int(m.C.Lifetime(m.random) / (time.Duration(m.C.PeriodMS) * time.Millisecond))
		if c.expected < 1 {
			c.expected = 1
		}
		c.start = m.R.Begin("START", c.id, "call_to_start_callback")
		c.life = m.R.Begin("LIFETIME", c.id, "start_to_end_callback")
		c.nextDue = time.Now().Add(time.Duration(m.C.PeriodMS) * time.Millisecond)
		m.active[c] = true
		m.R.Count("admissions_issued", 1)
		m.R.Peak("peak_active", uint64(len(m.active)))
		m.API.GetTimerApi().StartTimer(m.OnTimerTarget, c, c, m.C.PeriodMS, c.expected, m.C.PeriodMS)
		if !c.stopEarly && c.expected >= 6 && c.id%4 == 0 {
			m.API.GetTimerApi().StartTimer(m.OnTimerPauseResume, c, c, 2*m.C.PeriodMS, 2, 20)
		}
	}
	if end {
		m.API.GetTimerApi().StopTimer(m.OnTimerCreate, m)
	}
}
func (m *TimerTest) OnTimerPauseResume(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	c := data.(*timerTarget)
	if !m.active[c] {
		return
	}
	if count == 0 {
		m.API.GetTimerApi().PauseTimer(m.OnTimerTarget, c)
	} else {
		m.API.GetTimerApi().ResumeTimer(m.OnTimerTarget, c)
	}
}
func (m *TimerTest) OnTimerTarget(state uint8, count int, data, context any, natural bool) {
	if m.R.Finishing {
		return
	}
	c := data.(*timerTarget)
	if context != c {
		m.R.Fail("timer context/target mismatch")
	}
	switch state {
	case GFoundationApi.TimerStateStart:
		if c.started {
			m.R.Fail("duplicate Start")
		}
		c.started = true
		m.R.Complete(c.start, nil)
		m.R.Count("state_start", 1)
	case GFoundationApi.TimerStateBeat:
		var err error
		if !c.started || c.paused || count != c.beats {
			err = fmt.Errorf("invalid Beat: started=%t paused=%t count=%d expected=%d", c.started, c.paused, count, c.beats)
		}
		now := time.Now()
		m.R.Observe("BEAT_LAG", c.id, "scheduled_to_callback", c.nextDue, err)
		period := time.Duration(m.C.PeriodMS) * time.Millisecond
		if lag := now.Sub(c.nextDue); lag >= period {
			missed := uint64(lag / period)
			m.R.Count("timer_periods_missed", missed)
			c.nextDue = c.nextDue.Add(time.Duration(missed) * period)
		}
		c.nextDue = c.nextDue.Add(period)
		c.beats++
		m.R.Count("state_beat", 1)
		if c.stopEarly && c.beats == 2 {
			m.API.GetTimerApi().StopTimer(m.OnTimerTarget, c)
		}
	case GFoundationApi.TimerStatePause:
		if c.paused {
			m.R.Fail("duplicate Pause")
		}
		c.paused = true
		c.pausedAt = time.Now()
		m.R.Count("state_pause", 1)
	case GFoundationApi.TimerStateResume:
		if !c.paused {
			m.R.Fail("Resume without Pause")
		}
		c.paused = false
		c.nextDue = c.nextDue.Add(time.Since(c.pausedAt))
		m.R.Count("state_resume", 1)
	case GFoundationApi.TimerStateEnd:
		var err error
		expected := c.expected
		wantNatural := true
		if c.stopEarly && c.expected > 1 {
			expected = 2
			wantNatural = false
		}
		if c.beats != expected || natural != wantNatural {
			err = fmt.Errorf("End beats=%d want=%d natural=%t want=%t", c.beats, expected, natural, wantNatural)
		}
		m.R.Complete(c.life, err)
		m.R.Count("state_end", 1)
		if natural {
			m.R.Count("natural_end", 1)
		} else {
			m.R.Count("stopped_end", 1)
		}
		delete(m.active, c)
		m.API.GetTimerApi().StopTimer(m.OnTimerPauseResume, c)
	}
}
func (m *TimerTest) Update(api GFoundationApi.GFoundationApi) {
	if m.R == nil {
		return
	}
	m.Poll()
	if m.R.Finishing {
		return
	}
	if m.Expired() {
		m.R.Fail("timer drain deadline exceeded")
		m.Release(api)
		return
	}
	if m.P.Closed && len(m.active) == 0 {
		if m.R.Counters["timer_periods_missed"] > 0 {
			m.R.Fail("timer missed scheduled periods")
		}
		m.Finish()
	}
}
func (m *TimerTest) Release(api GFoundationApi.GFoundationApi) {
	if m.R == nil || m.R.Finishing {
		return
	}
	api.GetTimerApi().StopTimer(m.OnTimerCreate, m)
	for c := range m.active {
		api.GetTimerApi().StopTimer(m.OnTimerPauseResume, c)
		api.GetTimerApi().StopTimer(m.OnTimerTarget, c)
	}
	m.Finish()
}
func GetModule() GFoundationApi.IModule { return &TimerTest{} }
