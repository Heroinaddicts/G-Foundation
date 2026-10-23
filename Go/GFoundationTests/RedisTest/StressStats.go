package main

import (
	"encoding/csv"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"sync/atomic"
	"time"
)

const (
	stressConnect = iota
	stressSet
	stressGet
	stressEval
	stressCleanup
	stressClose
	stressOperationCount
)

var stressOperationNames = [...]string{"CONNECT", "SET", "GET", "EVAL", "CLEANUP", "CLOSE"}

var stressLatencyBandLabels = [...]string{"0-10ms", "10-20ms", "20-30ms", "30-40ms", "40-50ms", ">50ms"}

// Fine buckets near the normal latency range, with an explicit overflow bucket.
var stressLatencyBounds = func() []time.Duration {
	var bounds []time.Duration
	for _, band := range []struct{ end, step time.Duration }{
		{time.Millisecond, 10 * time.Microsecond},
		{10 * time.Millisecond, 100 * time.Microsecond},
		{100 * time.Millisecond, time.Millisecond},
		{time.Second, 10 * time.Millisecond},
		{10 * time.Second, 100 * time.Millisecond},
		{60 * time.Second, time.Second},
		{600 * time.Second, 10 * time.Second},
	} {
		from := time.Duration(0)
		if len(bounds) != 0 {
			from = bounds[len(bounds)-1]
		}
		for value := from + band.step; value <= band.end; value += band.step {
			bounds = append(bounds, value)
		}
	}
	return bounds
}()

type stressHistogram struct {
	counts []uint64
	n      uint64
	min    time.Duration
	max    time.Duration
	mean   float64
	m2     float64
}

func (h *stressHistogram) observe(elapsed time.Duration) {
	if h.counts == nil {
		h.counts = make([]uint64, len(stressLatencyBounds)+1)
	}
	// Binary search avoids scanning hundreds of buckets on each callback.
	lo, hi := 0, len(stressLatencyBounds)
	for lo < hi {
		mid := (lo + hi) / 2
		if elapsed <= stressLatencyBounds[mid] {
			hi = mid
		} else {
			lo = mid + 1
		}
	}
	h.counts[lo]++
	h.n++
	if h.n == 1 || elapsed < h.min {
		h.min = elapsed
	}
	if elapsed > h.max {
		h.max = elapsed
	}
	delta := float64(elapsed) - h.mean
	h.mean += delta / float64(h.n)
	h.m2 += delta * (float64(elapsed) - h.mean)
}

type stressBucket struct {
	LowerNS    int64   `json:"lower_exclusive_ns"`
	UpperNS    *int64  `json:"upper_inclusive_ns"`
	Count      uint64  `json:"count"`
	Percent    float64 `json:"percent"`
	Cumulative float64 `json:"cumulative_percent"`
}

type stressLatencyReport struct {
	Samples      uint64           `json:"samples"`
	MinNS        int64            `json:"min_ns"`
	MeanNS       float64          `json:"mean_ns"`
	StddevNS     float64          `json:"stddev_ns"`
	MaxNS        int64            `json:"max_ns"`
	PercentileNS map[string]int64 `json:"percentile_upper_bounds_ns"`
	Buckets      []stressBucket   `json:"nonzero_buckets"`
	Bands        []stressBucket   `json:"latency_bands"`
}

// All five finite boundaries coincide with the detailed histogram boundaries,
// so merging its buckets gives exact counts, including values on each boundary.
func stressLatencyBands(r stressLatencyReport) []stressBucket {
	bands := make([]stressBucket, len(stressLatencyBandLabels))
	for i := range bands {
		bands[i].LowerNS = int64(time.Duration(i) * 10 * time.Millisecond)
		if i < len(bands)-1 {
			upper := int64(time.Duration(i+1) * 10 * time.Millisecond)
			bands[i].UpperNS = &upper
		}
	}
	for _, bucket := range r.Buckets {
		index := len(bands) - 1
		if bucket.UpperNS != nil {
			for i := 0; i < len(bands)-1; i++ {
				if *bucket.UpperNS <= *bands[i].UpperNS {
					index = i
					break
				}
			}
		}
		bands[index].Count += bucket.Count
	}
	var accumulated uint64
	for i := range bands {
		accumulated += bands[i].Count
		if r.Samples != 0 {
			bands[i].Percent = 100 * float64(bands[i].Count) / float64(r.Samples)
			bands[i].Cumulative = 100 * float64(accumulated) / float64(r.Samples)
		}
	}
	return bands
}

func (h *stressHistogram) report() stressLatencyReport {
	r := stressLatencyReport{Samples: h.n, MinNS: int64(h.min), MeanNS: h.mean, MaxNS: int64(h.max), PercentileNS: make(map[string]int64)}
	if h.n == 0 {
		r.Bands = stressLatencyBands(r)
		return r
	}
	r.StddevNS = math.Sqrt(h.m2 / float64(h.n))
	for name, quantile := range map[string]float64{"p50": .50, "p90": .90, "p95": .95, "p99": .99, "p999": .999} {
		target, accumulated := uint64(math.Ceil(float64(h.n)*quantile)), uint64(0)
		for i, count := range h.counts {
			accumulated += count
			if accumulated >= target {
				upper := h.max
				if i < len(stressLatencyBounds) && stressLatencyBounds[i] < upper {
					upper = stressLatencyBounds[i]
				}
				r.PercentileNS[name] = int64(upper)
				break
			}
		}
	}
	var accumulated uint64
	for i, count := range h.counts {
		accumulated += count
		if count == 0 {
			continue
		}
		bucket := stressBucket{Count: count, Percent: 100 * float64(count) / float64(h.n), Cumulative: 100 * float64(accumulated) / float64(h.n)}
		if i != 0 {
			bucket.LowerNS = int64(stressLatencyBounds[i-1])
		}
		if i < len(stressLatencyBounds) {
			upper := int64(stressLatencyBounds[i])
			bucket.UpperNS = &upper
		}
		r.Buckets = append(r.Buckets, bucket)
	}
	r.Bands = stressLatencyBands(r)
	return r
}

type stressOperationStats struct {
	Issued, Completed, Success, Failed, Invalid, Unresolved uint64
	All, OK, Errors                                         stressHistogram
}

type stressOperationReport struct {
	Issued     uint64              `json:"issued"`
	Completed  uint64              `json:"completed"`
	Success    uint64              `json:"success"`
	Failed     uint64              `json:"failed"`
	Invalid    uint64              `json:"invalid_data"`
	Unresolved uint64              `json:"unresolved"`
	All        stressLatencyReport `json:"all_latency"`
	OK         stressLatencyReport `json:"success_latency"`
	Errors     stressLatencyReport `json:"error_latency"`
}

func (s *stressOperationStats) report() stressOperationReport {
	return stressOperationReport{s.Issued, s.Completed, s.Success, s.Failed, s.Invalid, s.Unresolved, s.All.report(), s.OK.report(), s.Errors.report()}
}

type stressSample struct {
	ID, Connection uint64
	Operation      int
	Phase          string
	CallNS         int64
	CallbackNS     int64
	LatencyNS      int64
	LifetimeMS     int
	Complete       bool
	Success        bool
	Error          string
}

type stressReport struct {
	Model         string                           `json:"model"`
	StartedUTC    string                           `json:"started_utc"`
	ElapsedNS     int64                            `json:"elapsed_ns"`
	Configuration stressConfig                     `json:"configuration"`
	Counters      map[string]uint64                `json:"counters"`
	Operations    map[string]stressOperationReport `json:"operations"`
	Lifetime      stressLatencyReport              `json:"actual_session_lifetime"`
	SchedulerLag  stressLatencyReport              `json:"timer_scheduler_lag"`
	Errors        []string                         `json:"errors"`
	CSVRows       uint64                           `json:"csv_rows_written"`
	CSVDropped    uint64                           `json:"csv_rows_dropped"`
	CSVPath       string                           `json:"csv_path"`
	ReportPath    string                           `json:"report_path"`
}

func (c stressConfig) MarshalJSON() ([]byte, error) {
	return json.Marshal(map[string]any{
		"new_connections_per_second": c.rate, "creation_seconds": c.seconds,
		"arrival_interval_ms":  stressArrivalPeriod / time.Millisecond,
		"arrival_batch_min":    c.rate / stressArrivalEpochsPerSecond,
		"arrival_batch_max":    (c.rate + stressArrivalEpochsPerSecond - 1) / stressArrivalEpochsPerSecond,
		"lifetime_min_seconds": c.minLife, "lifetime_max_seconds": c.maxLife,
		"sets_per_100ms": c.sets, "gets_per_100ms": c.gets, "evals_per_100ms": c.evals,
		"max_inflight_per_session": c.window, "max_open_or_connecting": c.maxActive,
		"max_connecting": c.maxConnecting, "drain_seconds": c.drainSeconds,
		"payload_bytes": c.payload, "random_seed": c.seed, "output_directory": c.output,
	})
}

type stressSinkError struct{ err error }
type stressSinkResult struct {
	report stressReport
	err    error
}

// The only extra goroutine in the load generator writes files. It calls no API.
type stressSink struct {
	records chan stressSample
	summary chan stressReport
	done    chan stressSinkResult
	err     atomic.Pointer[stressSinkError]
	path    string
}

func newStressSink(directory string) (*stressSink, error) {
	if err := os.MkdirAll(directory, 0755); err != nil {
		return nil, err
	}
	file, err := os.OpenFile(filepath.Join(directory, "samples.csv"), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
	if err != nil {
		return nil, err
	}
	sink := &stressSink{records: make(chan stressSample, 65536), summary: make(chan stressReport, 1), done: make(chan stressSinkResult, 1), path: directory}
	go func() {
		var firstError error
		setError := func(err error) {
			if err != nil && firstError == nil {
				firstError = err
				sink.err.Store(&stressSinkError{err})
			}
		}
		writer := csv.NewWriter(file)
		setError(writer.Write([]string{"request_id", "connection_id", "operation", "phase", "call_offset_ns", "callback_offset_ns", "latency_ns", "success", "status", "error", "planned_lifetime_ms"}))
		var rows uint64
		for sample := range sink.records {
			if firstError != nil {
				continue
			}
			callback, latency, status := "", "", "unresolved"
			if sample.Complete {
				callback, latency, status = strconv.FormatInt(sample.CallbackNS, 10), strconv.FormatInt(sample.LatencyNS, 10), "completed"
			}
			setError(writer.Write([]string{
				strconv.FormatUint(sample.ID, 10), strconv.FormatUint(sample.Connection, 10), stressOperationNames[sample.Operation], sample.Phase,
				strconv.FormatInt(sample.CallNS, 10), callback, latency, strconv.FormatBool(sample.Success), status, sample.Error, strconv.Itoa(sample.LifetimeMS),
			}))
			if firstError == nil {
				rows++
			}
		}
		writer.Flush()
		setError(writer.Error())
		setError(file.Sync())
		setError(file.Close())
		report := <-sink.summary
		report.CSVRows = rows
		if firstError != nil {
			report.Errors = append(report.Errors, fmt.Sprintf("sample export: %v", firstError))
		}
		data, err := json.MarshalIndent(report, "", "  ")
		setError(err)
		if err == nil {
			setError(os.WriteFile(report.ReportPath, append(data, '\n'), 0644))
		}
		sink.done <- stressSinkResult{report, firstError}
	}()
	return sink, nil
}
