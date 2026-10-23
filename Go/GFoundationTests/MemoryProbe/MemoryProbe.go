package main

import (
	"GFoundationApi"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"runtime/pprof"
	"strconv"
	"time"
)

type memoryWorkload interface {
	GFoundationApi.IModule
	MemoryPrepare()
	MemoryDone() (bool, bool)
	MemoryReset()
}

type snapshot struct {
	Cycle      int              `json:"cycle"`
	Phase      string           `json:"phase"`
	UTC        string           `json:"utc"`
	Goroutines int              `json:"goroutines"`
	FDCount    int              `json:"fd_count"`
	Stats      runtime.MemStats `json:"stats"`
	GCForced   bool             `json:"gc_forced"`
	Error      string           `json:"error,omitempty"`
}

type MemoryProbe struct {
	api                                     GFoundationApi.GFoundationApi
	target                                  memoryWorkload
	name, directory, stage                  string
	cycles, cycle, failures                 int
	seconds, idle, finalIdle                int
	started, next, roundStarted, nextSample time.Time
	loadTaken                               bool
	collecting                              bool
	results                                 chan snapshot
	snapshots                               []snapshot
	metrics                                 *os.File
}

func (m *MemoryProbe) Initialize(api GFoundationApi.GFoundationApi) bool {
	m.api = api
	m.name = api.GetLaunchArgument("memory_module")
	m.directory = api.GetLaunchArgument("memory_output")
	m.cycles, _ = strconv.Atoi(api.GetLaunchArgument("memory_cycles"))
	m.seconds, _ = strconv.Atoi(api.GetLaunchArgument("stress_seconds"))
	m.idle, _ = strconv.Atoi(api.GetLaunchArgument("memory_idle"))
	m.finalIdle, _ = strconv.Atoi(api.GetLaunchArgument("memory_final_idle"))
	if m.finalIdle < 1 {
		m.finalIdle = 10
	}
	if api.GetLaunchArgument("memory_probe") != "1" || m.name == "" || m.directory == "" || m.cycles < 1 || m.idle < 1 || m.seconds < 1 {
		fmt.Println("[MemoryProbe] invalid memory parameters")
		return false
	}
	if err := os.MkdirAll(m.directory, 0755); err != nil {
		panic(err)
	}
	var err error
	m.metrics, err = os.Create(filepath.Join(m.directory, "metrics.jsonl"))
	if err != nil {
		panic(err)
	}
	m.results = make(chan snapshot, 1)
	m.started = time.Now()
	m.stage = "baseline_wait"
	m.next = time.Now().Add(time.Duration(m.idle) * time.Second)
	return true
}

func (m *MemoryProbe) Launch(api GFoundationApi.GFoundationApi) bool { return true }
func (m *MemoryProbe) LaunchFinished(api GFoundationApi.GFoundationApi) {
	var ok bool
	m.target, ok = api.GetModule(m.name).(memoryWorkload)
	if !ok {
		panic("memory workload hooks missing: " + m.name)
	}
}
func (m *MemoryProbe) Release(api GFoundationApi.GFoundationApi) {
	if m.metrics != nil {
		m.metrics.Close()
	}
}

// The collector only reads runtime diagnostics and writes files. All workload
// lifecycle/API calls and state transitions remain on the framework main thread.
func (m *MemoryProbe) capture(phase string, forceGC bool) {
	if m.collecting {
		panic("overlapping memory snapshots")
	}
	m.collecting = true
	cycle, directory := m.cycle, m.directory
	go func() {
		s := snapshot{Cycle: cycle, Phase: phase, GCForced: forceGC}
		setError := func(err error) {
			if err != nil && s.Error == "" {
				s.Error = err.Error()
			}
		}
		if forceGC {
			runtime.GC()
			time.Sleep(100 * time.Millisecond)
			runtime.GC()
		}
		s.UTC = time.Now().UTC().Format(time.RFC3339Nano)
		runtime.ReadMemStats(&s.Stats)
		s.Goroutines = runtime.NumGoroutine()
		if entries, err := os.ReadDir("/dev/fd"); err == nil {
			s.FDCount = len(entries)
		}
		dir := filepath.Join(directory, fmt.Sprintf("cycle-%02d", cycle), phase)
		setError(os.MkdirAll(dir, 0755))
		for _, item := range []struct {
			name, file string
			debug      int
		}{
			{"heap", "heap.pprof", 0}, {"allocs", "allocs.pprof", 0},
			{"goroutine", "goroutine.pprof", 0}, {"goroutine", "goroutine.txt", 2},
		} {
			f, err := os.Create(filepath.Join(dir, item.file))
			if err != nil {
				setError(err)
				continue
			}
			setError(pprof.Lookup(item.name).WriteTo(f, item.debug))
			setError(f.Close())
		}
		b, err := json.MarshalIndent(s, "", "  ")
		setError(err)
		setError(os.WriteFile(filepath.Join(dir, "memory.json"), append(b, '\n'), 0644))
		m.results <- s
	}()
}

func (m *MemoryProbe) beginRound() {
	m.cycle++
	m.target.MemoryPrepare()
	m.target.LaunchFinished(m.api)
	m.roundStarted = time.Now()
	m.loadTaken = false
	m.stage = "load"
	fmt.Printf("[MemoryProbe] ROUND module=%s cycle=%d/%d\n", m.name, m.cycle, m.cycles)
}

func (m *MemoryProbe) Update(api GFoundationApi.GFoundationApi) {
	if m.stage == "done" {
		return
	}
	now := time.Now()
	if now.After(m.nextSample) {
		var stats runtime.MemStats
		runtime.ReadMemStats(&stats)
		b, _ := json.Marshal(struct {
			Elapsed                                                   float64 `json:"elapsed_seconds"`
			Cycle                                                     int     `json:"cycle"`
			Phase                                                     string  `json:"phase"`
			HeapAlloc, HeapInuse, HeapObjects, TotalAlloc, StackInuse uint64
			Goroutines                                                int `json:"goroutines"`
		}{time.Since(m.started).Seconds(), m.cycle, m.stage, stats.HeapAlloc, stats.HeapInuse, stats.HeapObjects, stats.TotalAlloc, stats.StackInuse, runtime.NumGoroutine()})
		if _, err := m.metrics.Write(append(b, '\n')); err != nil {
			panic(err)
		}
		m.nextSample = now.Add(time.Second)
	}
	select {
	case s := <-m.results:
		m.collecting = false
		m.snapshots = append(m.snapshots, s)
		if s.Error != "" {
			panic(s.Error)
		}
		fmt.Printf("[MemoryProbe] SNAPSHOT cycle=%d phase=%s heap=%d objects=%d goroutines=%d forced_gc=%t\n", s.Cycle, s.Phase, s.Stats.HeapAlloc, s.Stats.HeapObjects, s.Goroutines, s.GCForced)
		switch s.Phase {
		case "baseline":
			m.beginRound()
		case "drained":
			m.target.MemoryReset()
			m.stage = "released_wait"
			m.next = time.Now().Add(time.Duration(m.idle) * time.Second)
		case "released":
			if m.cycle < m.cycles {
				if !m.target.Initialize(m.api) || !m.target.Launch(m.api) {
					panic("memory round reinitialize failed")
				}
				m.beginRound()
			} else {
				m.stage = "final_wait"
				m.next = time.Now().Add(time.Duration(m.finalIdle) * time.Second)
			}
		case "final":
			b, err := json.MarshalIndent(map[string]any{"module": m.name, "cycles": m.cycles, "workload_failed_rounds": m.failures, "snapshots": m.snapshots, "mem_profile_rate": runtime.MemProfileRate}, "", "  ")
			if err != nil {
				panic(err)
			}
			if err := os.WriteFile(filepath.Join(m.directory, "memory-report.json"), append(b, '\n'), 0644); err != nil {
				panic(err)
			}
			m.metrics.Close()
			m.stage = "done"
			fmt.Printf("[MemoryProbe] COMPLETE module=%s cycles=%d failed_rounds=%d\n", m.name, m.cycle, m.failures)
		}
	default:
	}
	if m.collecting {
		return
	}
	switch m.stage {
	case "baseline_wait":
		if now.After(m.next) {
			m.stage = "baseline_capture"
			m.capture("baseline", true)
		}
	case "load":
		if !m.loadTaken && time.Since(m.roundStarted) >= time.Duration(m.seconds)*time.Second/2 {
			m.loadTaken = true
			m.capture("load", false)
			return
		}
		if done, failed := m.target.MemoryDone(); done {
			if failed {
				m.failures++
			}
			m.target.Release(m.api)
			m.stage = "drained_wait"
			m.next = now.Add(time.Duration(m.idle) * time.Second)
		}
	case "drained_wait":
		if now.After(m.next) {
			m.stage = "drained_capture"
			m.capture("drained", true)
		}
	case "released_wait":
		if now.After(m.next) {
			m.stage = "released_capture"
			m.capture("released", true)
		}
	case "final_wait":
		if now.After(m.next) {
			m.stage = "final_capture"
			m.capture("final", true)
		}
	}
}

func GetModule() GFoundationApi.IModule { return &MemoryProbe{} }
