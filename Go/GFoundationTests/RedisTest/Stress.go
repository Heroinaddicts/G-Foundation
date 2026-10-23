package main

import (
	"GFoundationApi"
	"bytes"
	"encoding/binary"
	"fmt"
	"math/rand"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

const (
	stressPeriod                 = 100 * time.Millisecond
	stressArrivalPeriod          = 10 * time.Millisecond
	stressArrivalEpochsPerSecond = int(time.Second / stressArrivalPeriod)
)

type stressConfig struct {
	enabled                                                    bool
	rate, seconds, minLife, maxLife, sets, gets, evals, window int
	maxActive, maxConnecting, drainSeconds, payload            int
	seed                                                       int64
	output                                                     string
}

func (m *RedisTest) configureStress(api GFoundationApi.GFoundationApi) error {
	enabled, err := intArgument(api, "redis_stress", 0, 0, 1)
	if err != nil || enabled == 0 {
		return err
	}
	m.load.enabled = true
	m.timer = api.GetTimerApi()
	if m.timer == nil {
		return fmt.Errorf("GetTimerApi() returned nil")
	}
	for _, setting := range []struct {
		name               string
		value              *int
		fallback, min, max int
	}{
		{"redis_stress_connect_rate", &m.load.rate, 1000, 1, 100000},
		{"redis_stress_seconds", &m.load.seconds, 30, 1, 86400},
		{"redis_stress_lifetime_min", &m.load.minLife, 5, 1, 3600},
		{"redis_stress_lifetime_max", &m.load.maxLife, 15, 1, 3600},
		{"redis_stress_sets_per_100ms", &m.load.sets, 1, 0, 1024},
		{"redis_stress_gets_per_100ms", &m.load.gets, 1, 0, 1024},
		{"redis_stress_evals_per_100ms", &m.load.evals, 1, 0, 1024},
		{"redis_stress_window", &m.load.window, 256, 1, 1024},
		{"redis_stress_max_active", &m.load.maxActive, 4000, 1, 1000000},
		{"redis_stress_max_connecting", &m.load.maxConnecting, 10000, 1, 100000},
		{"redis_stress_drain_seconds", &m.load.drainSeconds, 30, 1, 3600},
		{"redis_stress_payload", &m.load.payload, 256, 16, 65536},
	} {
		*setting.value, err = intArgument(api, setting.name, setting.fallback, setting.min, setting.max)
		if err != nil {
			return err
		}
	}
	if m.load.minLife > m.load.maxLife {
		return fmt.Errorf("redis_stress_lifetime_min must not exceed redis_stress_lifetime_max")
	}
	if m.load.sets+m.load.gets+m.load.evals == 0 {
		return fmt.Errorf("at least one SET/GET/EVAL per 100ms is required")
	}
	// Reject the previous fixed-pool model's options instead of silently ignoring them.
	for _, old := range []string{"redis_stress_connections", "redis_stress_requests", "redis_stress_batch", "redis_stress_interval"} {
		if api.GetLaunchArgument(old) != "" {
			return fmt.Errorf("--%s belongs to the replaced fixed-pool test; use the connection-churn options", old)
		}
	}
	m.load.seed = time.Now().UnixNano()
	if value := api.GetLaunchArgument("redis_stress_seed"); value != "" {
		m.load.seed, err = strconv.ParseInt(value, 10, 64)
		if err != nil {
			return fmt.Errorf("redis_stress_seed must be an int64")
		}
	}
	m.load.output = api.GetLaunchArgument("redis_stress_output")
	if m.load.output == "" {
		executable, err := os.Executable()
		if err != nil {
			return err
		}
		m.load.output = filepath.Join(filepath.Dir(executable), "RedisTestResults")
	}
	return nil
}

type stressSchedule struct {
	nextSlot int
	running  bool
}

type stressConnection struct {
	id          uint64
	session     GFoundationApi.IRedisSession
	key         string
	value       []byte
	version     uint64
	lifeTicks   int
	connectedAt time.Time
	pending     int
	remaining   int
	closing     bool
	schedules   [3]stressSchedule
}

type redisStress struct {
	module                       *RedisTest
	done                         func(error)
	rng                          *rand.Rand
	sink                         *stressSink
	connections                  map[uint64]*stressConnection
	pending                      map[uint64]*stressSample
	stats                        [stressOperationCount]stressOperationStats
	counters                     map[string]uint64
	lifetime, schedulerLag       stressHistogram
	started, abortDeadline       time.Time
	requestID, connectionID      uint64
	connecting, active, draining int
	open                         int // Successful connects, released only by the disconnect callback.
	lastEpoch                    int
	firstError, fatal            error
	arrivalsDone, aborting       bool
	finalizing, finished         bool
	csvDropped                   uint64
}

func (m *RedisTest) testStress(done func(error)) {
	name := time.Now().Format("20060102-150405") + "-" + strings.TrimPrefix(m.key, "gfoundation:redis-test:")
	sink, err := newStressSink(filepath.Join(m.load.output, name))
	if err != nil {
		done(fmt.Errorf("open stress output: %w", err))
		return
	}
	s := &redisStress{
		module: m, done: done, rng: rand.New(rand.NewSource(m.load.seed)), sink: sink,
		connections: make(map[uint64]*stressConnection), pending: make(map[uint64]*stressSample),
		counters: make(map[string]uint64), started: time.Now(), lastEpoch: -1,
	}
	m.stress = s
	m.deadline = s.started.Add(time.Duration(m.load.seconds+m.load.maxLife+15+m.load.drainSeconds) * time.Second)
	s.counters["planned_connections"] = uint64(m.load.rate) * uint64(m.load.seconds)
	s.counters["arrival_epochs_planned"] = uint64(m.load.seconds * stressArrivalEpochsPerSecond)
	fmt.Printf("[RedisTest] CHURN BEGIN new_connections_per_sec=%d arrival_interval_ms=%d batch=%d..%d creation_seconds=%d lifetime_seconds=%d..%d per_100ms_set=%d get=%d eval=%d max_active=%d max_connecting=%d window=%d seed=%d output=%s\n",
		m.load.rate, stressArrivalPeriod/time.Millisecond, m.load.rate/stressArrivalEpochsPerSecond, (m.load.rate+stressArrivalEpochsPerSecond-1)/stressArrivalEpochsPerSecond,
		m.load.seconds, m.load.minLife, m.load.maxLife, m.load.sets, m.load.gets, m.load.evals, m.load.maxActive, m.load.maxConnecting, m.load.window, m.load.seed, sink.path)
	m.timer.StartTimer(s.OnTimerConnectRedis, s, nil, int(stressArrivalPeriod/time.Millisecond), m.load.seconds*stressArrivalEpochsPerSecond, int(stressArrivalPeriod/time.Millisecond))
	m.timer.StartTimer(s.onTimerReport, s, nil, 1000, GFoundationApi.Unlimited, 1000)
}

// Spread the configured rate over 10ms batches, without replaying missed slots.
// Fractional batches retain the exact per-second total for rates such as 101/s.
func (c stressConfig) arrivalsBefore(epoch int) uint64 {
	return uint64(epoch) * uint64(c.rate) / uint64(stressArrivalEpochsPerSecond)
}

func (s *redisStress) missedArrivals(first, end int) {
	if end <= first {
		return
	}
	skipped := s.module.load.arrivalsBefore(end) - s.module.load.arrivalsBefore(first)
	s.counters["arrival_epochs_missed"] += uint64(end - first)
	s.counters["connections_skipped_scheduler"] += skipped
	if skipped > 0 && !s.aborting {
		s.fail(fmt.Errorf("connection timer missed %d arrival epochs (%d connections)", end-first, skipped))
	}
}

// The session timers below all use the actual
// IRedisSession as target and *stressConnection as context.
func (s *redisStress) OnTimerConnectRedis(state uint8, count int, _ any, _ any, _ bool) {
	if s.finished || s.finalizing {
		return
	}
	switch state {
	case GFoundationApi.TimerStateStart:
		s.counters["connect_timer_start"]++
	case GFoundationApi.TimerStateBeat:
		if s.aborting {
			return
		}
		totalEpochs := s.module.load.seconds * stressArrivalEpochsPerSecond
		epoch := max(count, int(time.Since(s.started)/stressArrivalPeriod)-1)
		if epoch >= totalEpochs {
			s.module.timer.StopTimer(s.OnTimerConnectRedis, s)
			return
		}
		if epoch <= s.lastEpoch {
			return
		}
		s.missedArrivals(s.lastEpoch+1, epoch)
		s.lastEpoch = epoch
		s.counters["arrival_epochs_delivered"]++
		batch := int(s.module.load.arrivalsBefore(epoch+1) - s.module.load.arrivalsBefore(epoch))
		for i := 0; i < batch; i++ {
			// Reserve a slot before asynchronous Connect. Closing sessions retain
			// their slot until disconnect, so neither pending dials nor draining
			// sessions can make the actual total exceed the configured limit.
			if s.open+s.connecting >= s.module.load.maxActive || s.connecting >= s.module.load.maxConnecting {
				s.counters["connections_skipped_capacity"] += uint64(batch - i)
				s.counters["arrival_epochs_throttled"]++
				return // Keep the timer running; retry capacity on the next beat.
			}
			s.connect()
			s.peak("peak_arrival_batch", i+1)
		}
		if epoch == totalEpochs-1 && count < totalEpochs-1 {
			s.module.timer.StopTimer(s.OnTimerConnectRedis, s)
		}
	case GFoundationApi.TimerStateEnd:
		s.counters["connect_timer_end"]++
		s.missedArrivals(s.lastEpoch+1, s.module.load.seconds*stressArrivalEpochsPerSecond)
		s.arrivalsDone = true
		s.checkDone()
	}
}

func (s *redisStress) connect() {
	s.connectionID++
	cfg := s.module.load
	c := &stressConnection{id: s.connectionID, key: fmt.Sprintf("%s:churn:%d", s.module.key, s.connectionID)}
	c.lifeTicks = cfg.minLife*10 + s.rng.Intn((cfg.maxLife-cfg.minLife)*10+1)
	s.connections[c.id] = c
	s.connecting++
	s.peak("peak_connecting", s.connecting)
	s.peak("peak_open_or_connecting", len(s.connections))
	row := s.begin(c, stressConnect, "connect")
	s.module.redis.Connect(s.module.host, s.module.port, os.Getenv("REDIS_PASSWORD"), s.module.db, func(err error, session GFoundationApi.IRedisSession) {
		callbackNS := int64(time.Since(s.started))
		if s.finished || s.finalizing {
			if session != nil {
				session.Close()
			}
			return
		}
		s.connecting--
		if err == nil && session == nil {
			err = fmt.Errorf("Connect returned a nil session without an error")
		}
		s.complete(row, err == nil, err, false, callbackNS)
		if err != nil {
			if session != nil {
				session.Close()
			}
			delete(s.connections, c.id)
			s.checkDone()
			return
		}
		c.session, c.connectedAt = session, time.Now()
		s.open++
		s.peak("peak_open", s.open)
		s.active++
		s.peak("peak_active", s.active)
		session.SetTimeOut(s.module.timeout)
		if s.aborting {
			s.closeConnection(c, "abort")
			return
		}
		c.value = make([]byte, cfg.payload)
		binary.BigEndian.PutUint64(c.value[:8], c.id)
		for i := 16; i < len(c.value); i++ {
			c.value[i] = byte(int(c.id) + i)
		}
		// The seed SET is also timed/exported, with phase=seed. Periodic GET/EVAL
		// are queued after it, so read-only configurations still have valid data.
		s.counters["seed_set_calls"]++
		s.issue(c, stressSet, "seed")
		for op := 0; op < 3; op++ {
			if s.perPeriod(op) == 0 {
				continue
			}
			c.schedules[op].running = true
			c.remaining++
			s.counters[s.opKey(op, "planned_period_commands")] += uint64(c.lifeTicks * s.perPeriod(op))
		}
		for op := 0; op < 3; op++ {
			if c.schedules[op].running {
				s.module.timer.StartTimer(s.callback(op), session, c, 100, c.lifeTicks, 100)
			}
		}
	})
}

func (s *redisStress) OnTimerRedisSessionSet(state uint8, count int, target any, context any, natural bool) {
	s.sessionTimer(0, state, count, target, context, natural)
}

func (s *redisStress) OnTimerRedisSessionGet(state uint8, count int, target any, context any, natural bool) {
	s.sessionTimer(1, state, count, target, context, natural)
}

func (s *redisStress) OnTimerRedisSessionEval(state uint8, count int, target any, context any, natural bool) {
	s.sessionTimer(2, state, count, target, context, natural)
}

func (s *redisStress) callback(op int) GFoundationApi.TimerCallback {
	switch op {
	case 0:
		return s.OnTimerRedisSessionSet
	case 1:
		return s.OnTimerRedisSessionGet
	default:
		return s.OnTimerRedisSessionEval
	}
}

func (s *redisStress) perPeriod(op int) int {
	return [...]int{s.module.load.sets, s.module.load.gets, s.module.load.evals}[op]
}

func (s *redisStress) opKey(op int, suffix string) string {
	return strings.ToLower(stressOperationNames[stressSet+op]) + "_" + suffix
}

func (s *redisStress) sessionTimer(op int, state uint8, count int, target, context any, natural bool) {
	if s.finished || s.finalizing {
		return
	}
	session, ok := target.(GFoundationApi.IRedisSession)
	c, valid := context.(*stressConnection)
	if !ok || !valid || session != c.session {
		s.fatal = fmt.Errorf("invalid Redis timer target/context")
		return
	}
	schedule := &c.schedules[op]
	switch state {
	case GFoundationApi.TimerStateStart:
		s.counters[s.opKey(op, "timer_start")]++
	case GFoundationApi.TimerStateBeat:
		s.counters[s.opKey(op, "timer_beat")]++
		if s.aborting || c.closing {
			return
		}
		// Do not replay missed periods in a catch-up burst: account for every
		// omitted command and fail the offered-load target explicitly.
		expected := c.connectedAt.Add(time.Duration(schedule.nextSlot+1) * stressPeriod)
		lag := time.Since(expected)
		if lag < 0 {
			lag = 0
		}
		s.schedulerLag.observe(lag)
		skipped := int(lag / stressPeriod)
		remaining := c.lifeTicks - schedule.nextSlot
		if skipped >= remaining {
			s.missed(op, remaining)
			schedule.nextSlot = c.lifeTicks
			s.module.timer.StopTimer(s.callback(op), session)
			return
		}
		if skipped > 0 {
			s.missed(op, skipped)
		}
		schedule.nextSlot += skipped + 1
		for i := 0; i < s.perPeriod(op); i++ {
			if c.pending >= s.module.load.window {
				s.counters[s.opKey(op, "skipped_window")] += uint64(s.perPeriod(op) - i)
				s.fail(fmt.Errorf("per-session inflight window prevented configured work"))
				break
			}
			s.counters[s.opKey(op, "period_calls")]++
			s.issue(c, stressSet+op, "periodic")
		}
		// The time wheel can skip intervals under load. Preserve the absolute
		// planned lifetime instead of extending it by the number of missed beats.
		if schedule.nextSlot == c.lifeTicks && count < c.lifeTicks-1 {
			s.module.timer.StopTimer(s.callback(op), session)
		}
	case GFoundationApi.TimerStateEnd:
		if !schedule.running {
			return
		}
		schedule.running = false
		s.counters[s.opKey(op, "timer_end")]++
		if natural {
			s.counters[s.opKey(op, "timer_natural_end")]++
		} else {
			s.counters[s.opKey(op, "timer_forced_end")]++
		}
		remaining := c.lifeTicks - schedule.nextSlot
		if s.aborting {
			s.counters[s.opKey(op, "cancelled_commands")] += uint64(remaining * s.perPeriod(op))
		} else if remaining > 0 {
			s.missed(op, remaining)
		}
		schedule.nextSlot = c.lifeTicks
		c.remaining--
		if c.remaining == 0 {
			reason := "lifetime"
			if s.aborting {
				reason = "abort"
			}
			s.closeConnection(c, reason)
		}
	case GFoundationApi.TimerStatePause:
		s.counters[s.opKey(op, "timer_pause")]++
	case GFoundationApi.TimerStateResume:
		s.counters[s.opKey(op, "timer_resume")]++
	}
}

func (s *redisStress) missed(op, periods int) {
	if periods <= 0 {
		return
	}
	s.counters[s.opKey(op, "missed_periods")] += uint64(periods)
	s.counters[s.opKey(op, "skipped_scheduler")] += uint64(periods * s.perPeriod(op))
	s.fail(fmt.Errorf("main-thread timer scheduling could not sustain offered workload"))
}

func (s *redisStress) issue(c *stressConnection, op int, phase string) {
	c.pending++
	s.counters["inflight_commands"]++
	if n := s.counters["inflight_commands"]; n > s.counters["peak_inflight_commands"] {
		s.counters["peak_inflight_commands"] = n
	}
	if op == stressSet {
		c.version++
		binary.BigEndian.PutUint64(c.value[8:16], c.version)
	}
	expectedVersion := c.version
	row := s.begin(c, op, phase)
	callback := func(success bool, err error, data []byte) {
		callbackNS := int64(time.Since(s.started))
		if s.finished || s.finalizing {
			return
		}
		c.pending--
		s.counters["inflight_commands"]--
		invalid := false
		if success && err == nil && op != stressSet {
			invalid = len(data) != len(c.value) || len(data) < 16 ||
				binary.BigEndian.Uint64(data[:8]) != c.id || binary.BigEndian.Uint64(data[8:16]) != expectedVersion ||
				!bytes.Equal(data[16:], c.value[16:])
			if invalid {
				err = fmt.Errorf("data/version mismatch: connection=%d expected_version=%d bytes=%d", c.id, expectedVersion, len(data))
			}
		}
		s.complete(row, success && err == nil, err, invalid, callbackNS)
	}
	switch op {
	case stressSet:
		c.session.Set(c.key, c.value, func(success bool, err error) { callback(success, err, nil) })
	case stressGet:
		c.session.Get(c.key, callback)
	case stressEval:
		c.session.Eval("return redis.call('GET', KEYS[1])", []string{c.key}, callback)
	}
}

func (s *redisStress) closeConnection(c *stressConnection, reason string) {
	if c.closing || c.session == nil {
		return
	}
	c.closing = true
	s.active--
	s.draining++
	s.peak("peak_draining", s.draining)
	s.lifetime.observe(time.Since(c.connectedAt))
	// The implementation drains accepted requests on Close. Queue the DEL
	// after the last operation, before Close, and wait for its close callback.
	cleanup := s.begin(c, stressCleanup, reason)
	c.session.Eval("return redis.call('DEL', KEYS[1])", []string{c.key}, func(success bool, err error, data []byte) {
		callbackNS := int64(time.Since(s.started))
		if s.finished || s.finalizing {
			return
		}
		if success && err == nil && string(data) != "0" && string(data) != "1" {
			err = fmt.Errorf("cleanup DEL returned %q", data)
		}
		s.complete(cleanup, success && err == nil, err, false, callbackNS)
	})
	var closeRow *stressSample
	c.session.SetDisconnectedCallback(func(success bool, err error, session GFoundationApi.IRedisSession) {
		callbackNS := int64(time.Since(s.started))
		if s.finished || s.finalizing {
			return
		}
		if session != c.session || c.pending != 0 {
			err = fmt.Errorf("disconnect callback: wrong_session=%t pending_commands=%d", session != c.session, c.pending)
		}
		s.complete(closeRow, success && err == nil, err, false, callbackNS)
		s.open--
		s.draining--
		delete(s.connections, c.id)
		s.checkDone()
	})
	closeRow = s.begin(c, stressClose, reason)
	c.session.Close()
}

func (s *redisStress) begin(c *stressConnection, op int, phase string) *stressSample {
	s.requestID++
	row := &stressSample{ID: s.requestID, Connection: c.id, Operation: op, Phase: phase, LifetimeMS: c.lifeTicks * 100}
	s.stats[op].Issued++
	s.pending[row.ID] = row
	// Capture immediately before the public API call; monotonic offsets survive
	// wall-clock adjustments and include all client queue/callback dispatch time.
	row.CallNS = int64(time.Since(s.started))
	return row
}

func (s *redisStress) complete(row *stressSample, success bool, err error, invalid bool, callbackNS int64) {
	if row == nil {
		s.fatal = fmt.Errorf("callback has no matching invocation")
		return
	}
	if _, exists := s.pending[row.ID]; !exists {
		s.fatal = fmt.Errorf("duplicate callback for request %d", row.ID)
		return
	}
	row.CallbackNS = callbackNS
	row.LatencyNS, row.Complete, row.Success = row.CallbackNS-row.CallNS, true, success && err == nil
	stats := &s.stats[row.Operation]
	stats.Completed++
	elapsed := time.Duration(row.LatencyNS)
	stats.All.observe(elapsed)
	if row.Success {
		stats.Success++
		stats.OK.observe(elapsed)
	} else {
		stats.Failed++
		stats.Errors.observe(elapsed)
		row.Error = fmt.Sprintf("success=%t err=%v", success, err)
		s.fail(fmt.Errorf("%s: %s", stressOperationNames[row.Operation], row.Error))
		if s.counters["printed_request_errors"] < 5 {
			s.counters["printed_request_errors"]++
			fmt.Printf("[RedisTest] CHURN request error operation=%s connection=%d err=%s\n", stressOperationNames[row.Operation], row.Connection, row.Error)
		}
	}
	if invalid {
		stats.Invalid++
	}
	delete(s.pending, row.ID)
	s.export(*row)
}

func (s *redisStress) export(row stressSample) {
	select {
	case s.sink.records <- row:
	default:
		s.csvDropped++
		s.fatal = fmt.Errorf("sample writer cannot keep up; raw latency samples were lost")
	}
}

func (s *redisStress) peak(name string, value int) {
	if uint64(value) > s.counters[name] {
		s.counters[name] = uint64(value)
	}
}

func (s *redisStress) fail(err error) {
	if err != nil && s.firstError == nil {
		s.firstError = err
	}
}

func (s *redisStress) onTimerReport(state uint8, _ int, _ any, _ any, _ bool) {
	if state != GFoundationApi.TimerStateBeat || s.finalizing || s.finished {
		return
	}
	set, get, eval := s.stats[stressSet], s.stats[stressGet], s.stats[stressEval]
	fmt.Printf("[RedisTest] CHURN progress elapsed=%s attempted=%d connected=%d connect_failed=%d connecting=%d active=%d draining=%d open=%d reserved=%d peak_active=%d throttled_epochs=%d SET=%d/%d GET=%d/%d EVAL=%d/%d command_failed=%d inflight=%d csv_dropped=%d\n",
		time.Since(s.started).Round(time.Millisecond), s.stats[stressConnect].Issued, s.stats[stressConnect].Success, s.stats[stressConnect].Failed,
		s.connecting, s.active, s.draining, s.open, s.open+s.connecting, s.counters["peak_active"], s.counters["arrival_epochs_throttled"], set.Completed, set.Issued, get.Completed, get.Issued,
		eval.Completed, eval.Issued, set.Failed+get.Failed+eval.Failed, s.counters["inflight_commands"], s.csvDropped)
}

func (s *redisStress) checkDone() {
	if !s.finalizing && !s.finished && s.arrivalsDone && len(s.connections) == 0 {
		s.finalize()
	}
}

func (s *redisStress) abort(err error) {
	if s.finished || s.finalizing || s.aborting {
		return
	}
	s.fail(err)
	s.aborting = true
	s.abortDeadline = time.Now().Add(time.Duration(s.module.load.drainSeconds) * time.Second)
	s.module.timer.StopTimer(s.OnTimerConnectRedis, s)
	s.arrivalsDone = true
	for _, c := range s.connections {
		if c.session == nil || c.closing {
			continue
		}
		for op := 0; op < 3; op++ {
			if c.schedules[op].running {
				s.module.timer.StopTimer(s.callback(op), c.session)
			}
		}
		if !c.closing {
			s.closeConnection(c, "abort")
		}
	}
	s.checkDone()
}

func (s *redisStress) forceFinish() {
	if s.finished {
		return
	}
	s.fail(fmt.Errorf("drain/export deadline exceeded; unresolved calls are not latency samples"))
	if s.finalizing {
		s.finished = true
		s.done(s.firstError)
		return
	}
	if !s.aborting {
		s.abort(s.firstError)
	}
	s.finalize()
}

func (s *redisStress) finalize() {
	if s.finalizing || s.finished {
		return
	}
	s.finalizing = true
	s.module.timer.StopTimer(s.OnTimerConnectRedis, s)
	s.module.timer.StopTimer(s.onTimerReport, s)
	for _, c := range s.connections {
		for op := 0; op < 3; op++ {
			if c.schedules[op].running {
				s.module.timer.StopTimer(s.callback(op), c.session)
			}
		}
	}
	s.fail(s.fatal)
	if s.csvDropped != 0 {
		s.fail(fmt.Errorf("raw latency export dropped %d samples", s.csvDropped))
	}
	if len(s.pending) != 0 {
		s.fail(fmt.Errorf("%d invocations have no callback at finalization", len(s.pending)))
	}
	for id, row := range s.pending {
		s.stats[row.Operation].Unresolved++
		row.Error = "callback not received before drain deadline"
		s.export(*row)
		delete(s.pending, id)
	}
	for op := 0; op < 3; op++ {
		planned := s.counters[s.opKey(op, "planned_period_commands")]
		accounted := s.counters[s.opKey(op, "period_calls")] + s.counters[s.opKey(op, "skipped_window")] +
			s.counters[s.opKey(op, "skipped_scheduler")] + s.counters[s.opKey(op, "cancelled_commands")]
		if planned != accounted {
			s.fail(fmt.Errorf("%s offered-load accounting mismatch: planned=%d accounted=%d", stressOperationNames[stressSet+op], planned, accounted))
		}
	}
	accountedConnections := s.stats[stressConnect].Issued + s.counters["connections_skipped_capacity"] + s.counters["connections_skipped_scheduler"]
	if accountedConnections != s.counters["planned_connections"] {
		s.fail(fmt.Errorf("connection accounting mismatch: planned=%d accounted=%d", s.counters["planned_connections"], accountedConnections))
	}
	if s.counters["arrival_epochs_delivered"]+s.counters["arrival_epochs_missed"] != s.counters["arrival_epochs_planned"] {
		s.fail(fmt.Errorf("connection timer epoch accounting mismatch"))
	}
	if s.connecting != 0 || s.active != 0 || s.draining != 0 || s.open != 0 {
		s.fail(fmt.Errorf("unresolved connections: connecting=%d active=%d draining=%d open=%d", s.connecting, s.active, s.draining, s.open))
	}
	s.counters["remaining_open"] = uint64(s.open)
	s.counters["remaining_connecting"] = uint64(s.connecting)
	s.counters["remaining_active"] = uint64(s.active)
	s.counters["remaining_draining"] = uint64(s.draining)
	report := stressReport{
		Model: "timer_per_session_connection_churn", StartedUTC: s.started.UTC().Format(time.RFC3339Nano), ElapsedNS: int64(time.Since(s.started)),
		Configuration: s.module.load, Counters: s.counters, Operations: make(map[string]stressOperationReport),
		Lifetime: s.lifetime.report(), SchedulerLag: s.schedulerLag.report(), CSVDropped: s.csvDropped,
		CSVPath: filepath.Join(s.sink.path, "samples.csv"), ReportPath: filepath.Join(s.sink.path, "report.json"),
	}
	for op := 0; op < stressOperationCount; op++ {
		report.Operations[stressOperationNames[op]] = s.stats[op].report()
	}
	if s.firstError != nil {
		report.Errors = append(report.Errors, s.firstError.Error())
	}
	if s.fatal != nil {
		report.Errors = append(report.Errors, s.fatal.Error())
	}
	// Snapshot complete: no callback mutates the report after this point.
	s.sink.summary <- report
	close(s.sink.records)
	s.module.deadline = time.Now().Add(time.Duration(s.module.load.drainSeconds) * time.Second)
}

func (s *redisStress) poll() {
	if s.finished {
		return
	}
	if s.finalizing {
		select {
		case result := <-s.sink.done:
			s.finished = true
			s.fail(result.err)
			s.printReport(result.report)
			s.done(s.firstError)
		default:
		}
		return
	}
	if failure := s.sink.err.Load(); failure != nil {
		s.fatal = failure.err
	}
	if s.fatal != nil && !s.aborting {
		s.abort(s.fatal)
	}
	if s.aborting && !s.abortDeadline.IsZero() && time.Now().After(s.abortDeadline) {
		s.forceFinish()
	}
}

func (s *redisStress) printReport(report stressReport) {
	fmt.Printf("[RedisTest] CHURN RESULT planned=%d attempted=%d connected=%d connect_failed=%d closed=%d peak_active=%d peak_open=%d peak_reserved=%d peak_arrival_batch=%d throttled_epochs=%d skipped_capacity=%d skipped_scheduler=%d missed_epochs=%d csv_rows=%d csv_dropped=%d elapsed=%s\n",
		s.counters["planned_connections"], s.stats[stressConnect].Issued, s.stats[stressConnect].Success, s.stats[stressConnect].Failed,
		s.stats[stressClose].Success, s.counters["peak_active"], s.counters["peak_open"], s.counters["peak_open_or_connecting"], s.counters["peak_arrival_batch"], s.counters["arrival_epochs_throttled"], s.counters["connections_skipped_capacity"], s.counters["connections_skipped_scheduler"], s.counters["arrival_epochs_missed"], report.CSVRows, report.CSVDropped, time.Duration(report.ElapsedNS))
	fmt.Println("[RedisTest] 耗时口径：API 调用到回调，包含成功和失败；未完成请求不计入分布。0-10ms 包含两端，其余区间左开右闭。")
	for _, op := range []string{"SET", "GET", "EVAL"} {
		stats := report.Operations[op]
		latency := stats.All
		fmt.Printf("[RedisTest] ===== %s 耗时分析 =====\n", op)
		fmt.Printf("[RedisTest] 调用=%d 已回调=%d 成功=%d 失败=%d（含数据错误=%d） 未完成=%d\n",
			stats.Issued, stats.Completed, stats.Success, stats.Failed, stats.Invalid, stats.Unresolved)
		fmt.Printf("[RedisTest] 最小=%s 平均=%s 标准差=%s 最大=%s\n",
			time.Duration(latency.MinNS), time.Duration(latency.MeanNS), time.Duration(latency.StddevNS), time.Duration(latency.MaxNS))
		fmt.Printf("[RedisTest] 分位数上界：P50=%s P90=%s P95=%s P99=%s P99.9=%s\n",
			time.Duration(latency.PercentileNS["p50"]), time.Duration(latency.PercentileNS["p90"]), time.Duration(latency.PercentileNS["p95"]),
			time.Duration(latency.PercentileNS["p99"]), time.Duration(latency.PercentileNS["p999"]))
		fmt.Printf("[RedisTest] %-12s %12s %12s %12s\n", "耗时区间", "次数", "占比", "累计占比")
		for i, bucket := range latency.Bands {
			fmt.Printf("[RedisTest] %-16s %12d %11.4f%% %11.4f%%\n",
				stressLatencyBandLabels[i], bucket.Count, bucket.Percent, bucket.Cumulative)
		}
	}
	fmt.Printf("[RedisTest] CHURN FILES samples=%s report=%s\n", report.CSVPath, report.ReportPath)
}
