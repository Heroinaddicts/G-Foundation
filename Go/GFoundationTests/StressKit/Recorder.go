package StressKit

import (
	"GFoundationApi"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"time"
)

var bounds = func() []int64 {
	var b []int64
	for _, v := range []struct{ end, step time.Duration }{
		{time.Millisecond, 10 * time.Microsecond}, {10 * time.Millisecond, 100 * time.Microsecond},
		{100 * time.Millisecond, time.Millisecond}, {time.Second, 10 * time.Millisecond},
		{10 * time.Second, 100 * time.Millisecond}, {time.Minute, time.Second}, {time.Hour, time.Minute},
	} {
		from := int64(0)
		if len(b) > 0 {
			from = b[len(b)-1]
		}
		for x := from + int64(v.step); x <= int64(v.end); x += int64(v.step) {
			b = append(b, x)
		}
	}
	return b
}()

type Histogram struct {
	n        uint64
	min, max int64
	mean     float64
	counts   []uint64
	bands    [6]uint64
}

func (h *Histogram) Add(d time.Duration) {
	n := int64(d)
	if n < 0 {
		n = 0
	}
	if h.counts == nil {
		h.counts = make([]uint64, len(bounds)+1)
	}
	h.counts[sort.Search(len(bounds), func(i int) bool { return n <= bounds[i] })]++
	b := 0
	if n > 0 {
		b = int((n - 1) / int64(10*time.Millisecond))
	}
	if b > 5 {
		b = 5
	}
	h.bands[b]++
	h.n++
	if h.n == 1 || n < h.min {
		h.min = n
	}
	if n > h.max {
		h.max = n
	}
	h.mean += (float64(n) - h.mean) / float64(h.n)
}

type Band struct {
	Range   string  `json:"range"`
	Count   uint64  `json:"count"`
	Percent float64 `json:"percent"`
}
type Latency struct {
	Samples   uint64           `json:"samples"`
	Min       int64            `json:"min_ns"`
	Mean      float64          `json:"mean_ns"`
	Max       int64            `json:"max_ns"`
	Quantiles map[string]int64 `json:"percentile_upper_bounds_ns"`
	Bands     []Band           `json:"latency_bands"`
}

func (h *Histogram) Report() Latency {
	r := Latency{Samples: h.n, Min: h.min, Mean: h.mean, Max: h.max, Quantiles: map[string]int64{}}
	for _, q := range []struct {
		name string
		p    float64
	}{{"p50", .5}, {"p90", .9}, {"p95", .95}, {"p99", .99}, {"p999", .999}} {
		if h.n == 0 {
			break
		}
		target := uint64(math.Ceil(float64(h.n) * q.p))
		var n uint64
		for i, c := range h.counts {
			n += c
			if n >= target {
				v := h.max
				if i < len(bounds) && bounds[i] < v {
					v = bounds[i]
				}
				r.Quantiles[q.name] = v
				break
			}
		}
	}
	for i, label := range []string{"0-10ms", "10-20ms", "20-30ms", "30-40ms", "40-50ms", ">50ms"} {
		b := Band{Range: label, Count: h.bands[i]}
		if h.n > 0 {
			b.Percent = 100 * float64(b.Count) / float64(h.n)
		}
		r.Bands = append(r.Bands, b)
	}
	return r
}

type Sample struct {
	ID                 uint64
	Entity             uint64
	Operation, Phase   string
	CallNS, CallbackNS int64
	Success, Complete  bool
	Error              string
}
type Op struct {
	Issued       uint64  `json:"issued"`
	Completed    uint64  `json:"completed"`
	Success      uint64  `json:"success"`
	Failed       uint64  `json:"failed"`
	Unresolved   uint64  `json:"unresolved"`
	All          Latency `json:"all_latency"`
	OK           Latency `json:"success_latency"`
	Errors       Latency `json:"error_latency"`
	all, ok, bad Histogram
}
type Report struct {
	Module        string            `json:"module"`
	Started       string            `json:"started_utc"`
	ElapsedNS     int64             `json:"elapsed_ns"`
	Status        string            `json:"status"`
	Configuration any               `json:"configuration"`
	Operations    map[string]*Op    `json:"operations"`
	Counters      map[string]uint64 `json:"counters"`
	Errors        []string          `json:"errors"`
	CSVRows       uint64            `json:"csv_rows_written"`
	CSVDropped    uint64            `json:"csv_rows_dropped"`
	WriterError   string            `json:"writer_error,omitempty"`
}
type sinkResult struct {
	Report Report
	Err    error
}
type Recorder struct {
	Name, Directory   string
	Started           time.Time
	Config            any
	Ops               map[string]*Op
	Counters          map[string]uint64
	Pending           map[uint64]*Sample
	Errors            []string
	Faults            uint64
	sequence, dropped uint64
	rows              chan Sample
	summary           chan Report
	result            chan sinkResult
	Finishing, Done   bool
	report            *Report
}

func New(api GFoundationApi.GFoundationApi, name, prefix string, config any) (*Recorder, error) {
	root := Text(api, prefix, "output", "StressResults")
	dir, err := filepath.Abs(filepath.Join(root, name, time.Now().Format("20060102-150405.000000000")))
	if err != nil {
		return nil, err
	}
	if err = os.MkdirAll(dir, 0755); err != nil {
		return nil, err
	}
	f, err := os.OpenFile(filepath.Join(dir, "samples.csv"), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
	if err != nil {
		return nil, err
	}
	r := &Recorder{Name: name, Directory: dir, Started: time.Now(), Config: config, Ops: map[string]*Op{}, Counters: map[string]uint64{}, Pending: map[uint64]*Sample{}, rows: make(chan Sample, 65536), summary: make(chan Report, 1), result: make(chan sinkResult, 1)}
	go r.write(f)
	fmt.Printf("[%s] BEGIN config=%+v output=%s\n", name, config, dir)
	return r, nil
}
func (r *Recorder) op(name string) *Op {
	o := r.Ops[name]
	if o == nil {
		o = &Op{}
		r.Ops[name] = o
	}
	return o
}
func (r *Recorder) Begin(op string, entity uint64, phase string) *Sample {
	return r.BeginAt(op, entity, phase, time.Now())
}
func (r *Recorder) BeginAt(op string, entity uint64, phase string, at time.Time) *Sample {
	if r.Finishing {
		return nil
	}
	r.sequence++
	s := &Sample{ID: r.sequence, Entity: entity, Operation: op, Phase: phase, CallNS: at.Sub(r.Started).Nanoseconds()}
	r.Pending[s.ID] = s
	r.op(op).Issued++
	r.Peak("peak_pending", uint64(len(r.Pending)))
	return s
}
func (r *Recorder) Complete(s *Sample, err error) {
	if s == nil || r.Finishing {
		return
	}
	if s.Complete {
		r.Fail("duplicate callback for " + s.Operation)
		return
	}
	s.CallbackNS = time.Since(r.Started).Nanoseconds()
	s.Complete = true
	s.Success = err == nil
	o := r.op(s.Operation)
	o.Completed++
	o.all.Add(time.Duration(s.CallbackNS - s.CallNS))
	if err == nil {
		o.Success++
		o.ok.Add(time.Duration(s.CallbackNS - s.CallNS))
	} else {
		o.Failed++
		s.Error = err.Error()
		o.bad.Add(time.Duration(s.CallbackNS - s.CallNS))
		r.remember(s.Operation + ": " + err.Error())
	}
	delete(r.Pending, s.ID)
	r.export(*s)
}
func (r *Recorder) Observe(op string, entity uint64, phase string, scheduled time.Time, err error) {
	if now := time.Now(); scheduled.After(now) {
		scheduled = now
	}
	r.Complete(r.BeginAt(op, entity, phase, scheduled), err)
}
func (r *Recorder) export(s Sample) {
	select {
	case r.rows <- s:
	default:
		r.dropped++
	}
}
func (r *Recorder) remember(message string) {
	if len(r.Errors) < 32 {
		r.Errors = append(r.Errors, message)
	}
}
func (r *Recorder) Fail(message string) {
	if !r.Finishing {
		r.Faults++
		r.remember(message)
	}
}
func (r *Recorder) Count(name string, n uint64) {
	if !r.Finishing {
		r.Counters[name] += n
	}
}
func (r *Recorder) Peak(name string, n uint64) {
	if !r.Finishing && n > r.Counters[name] {
		r.Counters[name] = n
	}
}
func (r *Recorder) Finish() {
	if r.Finishing {
		return
	}
	r.Finishing = true
	for _, s := range r.Pending {
		r.op(s.Operation).Unresolved++
		s.Error = "callback not received before drain deadline"
		r.export(*s)
	}
	clear(r.Pending)
	failed := r.Faults + r.dropped
	for _, o := range r.Ops {
		o.All = o.all.Report()
		o.OK = o.ok.Report()
		o.Errors = o.bad.Report()
		failed += o.Failed + o.Unresolved
	}
	r.Counters["assertion_failures"] = r.Faults
	status := "passed"
	if failed != 0 {
		status = "failed"
	}
	r.summary <- Report{Module: r.Name, Started: r.Started.UTC().Format(time.RFC3339Nano), ElapsedNS: time.Since(r.Started).Nanoseconds(), Status: status, Configuration: r.Config, Operations: r.Ops, Counters: r.Counters, Errors: append([]string(nil), r.Errors...), CSVDropped: r.dropped}
	close(r.rows)
}
func (r *Recorder) Poll() {
	if !r.Finishing || r.Done {
		return
	}
	select {
	case v := <-r.result:
		r.Done = true
		r.report = &v.Report
		names := make([]string, 0, len(v.Report.Operations))
		for n := range v.Report.Operations {
			names = append(names, n)
		}
		sort.Strings(names)
		var passed, failed uint64
		failed = r.Faults + r.dropped
		for _, n := range names {
			o := v.Report.Operations[n]
			passed += o.Success
			failed += o.Failed + o.Unresolved
			fmt.Printf("[%s] SUMMARY %s issued=%d success=%d failed=%d unresolved=%d mean=%.3fms p99_upper=%.3fms max=%.3fms\n", r.Name, n, o.Issued, o.Success, o.Failed, o.Unresolved, o.All.Mean/1e6, float64(o.All.Quantiles["p99"])/1e6, float64(o.All.Max)/1e6)
			for _, b := range o.All.Bands {
				fmt.Printf("[%s] DISTRIBUTION %s %s count=%d percent=%.4f\n", r.Name, n, b.Range, b.Count, b.Percent)
			}
		}
		if v.Err != nil {
			failed++
			fmt.Printf("[%s] FAIL report export: %v\n", r.Name, v.Err)
		}
		for _, e := range v.Report.Errors {
			fmt.Printf("[%s] ERROR %s\n", r.Name, e)
		}
		fmt.Printf("[%s] REPORT %s\n[%s] COMPLETE passed=%d failed=%d\n", r.Name, filepath.Join(r.Directory, "report.json"), r.Name, passed, failed)
	default:
	}
}
func (r *Recorder) Progress() {
	fmt.Printf("[%s] PROGRESS elapsed=%.1fs pending=%d counters=%v\n", r.Name, time.Since(r.Started).Seconds(), len(r.Pending), r.Counters)
}
func (r *Recorder) write(f *os.File) {
	var first error
	set := func(e error) {
		if e != nil && first == nil {
			first = e
		}
	}
	w := csv.NewWriter(f)
	set(w.Write([]string{"request_id", "entity_id", "operation", "phase", "call_offset_ns", "callback_offset_ns", "latency_ns", "success", "status", "error"}))
	var rows uint64
	for s := range r.rows {
		if first != nil {
			continue
		}
		cb, lat, status := "", "", "unresolved"
		if s.Complete {
			cb = strconv.FormatInt(s.CallbackNS, 10)
			lat = strconv.FormatInt(s.CallbackNS-s.CallNS, 10)
			status = "completed"
		}
		set(w.Write([]string{strconv.FormatUint(s.ID, 10), strconv.FormatUint(s.Entity, 10), s.Operation, s.Phase, strconv.FormatInt(s.CallNS, 10), cb, lat, strconv.FormatBool(s.Success), status, s.Error}))
		if first == nil {
			rows++
		}
	}
	w.Flush()
	set(w.Error())
	set(f.Sync())
	set(f.Close())
	report := <-r.summary
	report.CSVRows = rows
	if first != nil {
		report.WriterError = first.Error()
		report.Status = "failed"
	}
	b, err := json.MarshalIndent(report, "", "  ")
	set(err)
	if err == nil {
		set(os.WriteFile(filepath.Join(r.Directory, "report.json"), append(b, '\n'), 0644))
	}
	r.result <- sinkResult{report, first}
}
