// Package StressKit is shared by the API stress plugins. Framework API calls
// and the recorder belong to the main thread; only the CSV sink uses a worker.
package StressKit

import (
	"GFoundationApi"
	"fmt"
	"math/rand"
	"strconv"
	"time"
)

type Config struct {
	Seconds, Rate, MaxActive, MinLife, MaxLife          int
	PeriodMS, PerTick, Payload, TimeoutMS, DrainSeconds int
	Seed                                                int64
}

func Text(api GFoundationApi.GFoundationApi, prefix, name, fallback string) string {
	if v := api.GetLaunchArgument(prefix + "_" + name); v != "" {
		return v
	}
	if v := api.GetLaunchArgument("stress_" + name); v != "" {
		return v
	}
	return fallback
}

func Int(api GFoundationApi.GFoundationApi, prefix, name string, fallback, min, max int) (int, error) {
	v, err := strconv.Atoi(Text(api, prefix, name, strconv.Itoa(fallback)))
	if err != nil || v < min || v > max {
		return 0, fmt.Errorf("--%s_%s must be %d..%d", prefix, name, min, max)
	}
	return v, nil
}

func Parse(api GFoundationApi.GFoundationApi, prefix string, rate, capacity int) (Config, error) {
	c := Config{}
	for _, p := range []struct {
		name               string
		dst                *int
		fallback, min, max int
	}{
		{"seconds", &c.Seconds, 30, 1, 3600}, {"rate", &c.Rate, rate, 1, 1000000},
		{"max_active", &c.MaxActive, capacity, 1, 1000000},
		{"lifetime_min", &c.MinLife, 5, 1, 3600}, {"lifetime_max", &c.MaxLife, 15, 1, 3600},
		{"period_ms", &c.PeriodMS, 100, 1, 60000}, {"per_tick", &c.PerTick, 1, 1, 10000},
		{"payload", &c.Payload, 256, 24, 1048576}, {"timeout_ms", &c.TimeoutMS, 5000, 1, 60000},
		{"drain_seconds", &c.DrainSeconds, 20, 1, 3600},
	} {
		v, err := Int(api, prefix, p.name, p.fallback, p.min, p.max)
		if err != nil {
			return c, err
		}
		*p.dst = v
	}
	if c.MaxLife < c.MinLife {
		return c, fmt.Errorf("lifetime_max must be >= lifetime_min")
	}
	var err error
	c.Seed, err = strconv.ParseInt(Text(api, prefix, "seed", "1792166673896271000"), 10, 64)
	return c, err
}

func (c Config) Random() *rand.Rand { return rand.New(rand.NewSource(c.Seed)) }
func (c Config) Lifetime(r *rand.Rand) time.Duration {
	return time.Duration(c.MinLife*100+r.Intn((c.MaxLife-c.MinLife)*100+1)) * 10 * time.Millisecond
}

// Pacer spreads rate admissions across 100 ten-millisecond slots per second.
// Late slots are counted, never replayed as a burst. Capacity skips are counted
// by the caller separately from scheduler skips.
type Pacer struct {
	Start                time.Time
	Rate, Seconds, epoch int
	Closed               bool
}

func NewPacer(start time.Time, c Config) *Pacer {
	return &Pacer{Start: start, Rate: c.Rate, Seconds: c.Seconds, epoch: -1}
}
func (p *Pacer) quota(epoch int) int { return (epoch+1)*p.Rate/100 - epoch*p.Rate/100 }
func (p *Pacer) Tick(r *Recorder, callbackCount int) (int, bool) {
	if p.Closed {
		return 0, true
	}
	e := int(time.Since(p.Start)/(10*time.Millisecond)) - 1
	// Timer deadlines use the framework's millisecond clock. Its phase can be
	// slightly earlier than Start; never discard a delivered beat just because
	// a wall-clock division rounded it into the preceding ten-millisecond slot.
	if e < callbackCount {
		e = callbackCount
	}
	if e <= p.epoch {
		return 0, false
	}
	if e >= p.Seconds*100 {
		e = p.Seconds*100 - 1
	}
	if gap := e - p.epoch - 1; gap > 0 {
		r.Count("arrival_epochs_missed", uint64(gap))
		r.Count("admissions_skipped_scheduler", uint64(e*p.Rate/100-(p.epoch+1)*p.Rate/100))
	}
	p.epoch = e
	r.Count("arrival_epochs_delivered", 1)
	p.Closed = e == p.Seconds*100-1
	return p.quota(e), p.Closed
}

func (p *Pacer) Stop(r *Recorder) {
	if !p.Closed {
		r.Count("arrival_epochs_missed", uint64(p.Seconds*100-1-p.epoch))
		r.Count("admissions_skipped_scheduler", uint64(p.Seconds*p.Rate-(p.epoch+1)*p.Rate/100))
		p.Closed = true
	}
}
