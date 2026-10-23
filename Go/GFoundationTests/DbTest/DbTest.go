package main

import (
	"GFoundationApi"
	"GFoundationTests/StressKit"
	"bytes"
	"fmt"
	"math/rand"
	"os"
	"strings"
	"time"
)

type dbPeer struct {
	id, connectionID, issued, completed uint64
	session                             GFoundationApi.IDatabaseSession
	pending                             int
	retired                             bool
	expires, nextDue                    time.Time
}
type DbTest struct {
	StressKit.Base
	host, user, dbname string
	port               uint16
	window             int
	sequence           uint64
	active             map[uint64]*dbPeer
	random             *rand.Rand
	loading            bool
	preflight          GFoundationApi.IDatabaseSession
	checking           map[GFoundationApi.IDatabaseSession]bool
}

func (m *DbTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	if !m.Init(api, "DbTest", "db", 100, 100) {
		return false
	}
	m.host = StressKit.Text(api, "db", "host", "127.0.0.1")
	m.user = StressKit.Text(api, "db", "user", "root")
	m.dbname = StressKit.Text(api, "db", "name", "")
	p, err := StressKit.Int(api, "db", "port", 3306, 1, 65535)
	if err != nil {
		fmt.Println(err)
		return false
	}
	m.port = uint16(p)
	m.window, err = StressKit.Int(api, "db", "window", 64, 1, 10000)
	if err != nil {
		fmt.Println(err)
		return false
	}
	m.active = map[uint64]*dbPeer{}
	m.checking = map[GFoundationApi.IDatabaseSession]bool{}
	m.random = m.C.Random()
	return true
}
func (m *DbTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if !m.Start(map[string]any{"host": m.host, "port": m.port, "username": m.user, "database": m.dbname, "window_per_session": m.window, "queries": "read-only SELECT; CONNECTION_ID checked on every response"}) {
		return
	}
	m.Deadline = time.Now().Add(20 * time.Second)
	m.runChecks()
}
func (m *DbTest) beginLoad() {
	if m.R.Finishing {
		return
	}
	clear(m.checking)
	m.preflight = nil
	m.loading = true
	m.P = StressKit.NewPacer(time.Now(), m.C)
	m.R.Count("load_start_offset_ns", uint64(time.Since(m.R.Started)))
	m.Deadline = time.Now().Add(time.Duration(m.C.Seconds+m.C.MaxLife+m.C.DrainSeconds) * time.Second)
	m.API.GetTimerApi().StartTimer(m.OnTimerConnectDB, m, nil, 10, GFoundationApi.Unlimited, 10)
}
func (m *DbTest) OnTimerConnectDB(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	n, end := m.P.Tick(m.R, count)
	for i := 0; i < n; i++ {
		if len(m.active) >= m.C.MaxActive {
			m.R.Count("admissions_skipped_capacity", uint64(n-i))
			break
		}
		m.connect()
	}
	if end {
		m.API.GetTimerApi().StopTimer(m.OnTimerConnectDB, m)
	}
}
func (m *DbTest) connect() {
	m.sequence++
	c := &dbPeer{id: m.sequence}
	m.active[c.id] = c
	sample := m.R.Begin("CONNECT", c.id, "call_to_authenticated_connection_callback")
	m.R.Count("admissions_issued", 1)
	m.R.Peak("peak_sessions", uint64(len(m.active)))
	m.API.GetDbProxyApi().CreateDatabaseSession(GFoundationApi.DatabaseTypeMysql, m.host, m.port, m.user, os.Getenv("DB_PASSWORD"), m.dbname, func(err error, s GFoundationApi.IDatabaseSession) {
		if m.R.Finishing {
			if err == nil && s != nil {
				s.Release()
			}
			return
		}
		if err == nil && s == nil {
			err = fmt.Errorf("nil database session")
		}
		m.R.Complete(sample, err)
		if err != nil {
			delete(m.active, c.id)
			return
		}
		c.session = s
		c.expires = time.Now().Add(m.C.Lifetime(m.random))
		c.nextDue = time.Now().Add(time.Duration(m.C.PeriodMS) * time.Millisecond)
		m.query(c, true)
		m.API.GetTimerApi().StartTimer(m.OnTimerDBQuery, s, c, m.C.PeriodMS, GFoundationApi.Unlimited, m.C.PeriodMS)
	})
}
func (m *DbTest) query(c *dbPeer, first bool) {
	c.issued++
	seq := c.issued
	id := (c.id << 32) | seq
	payload := StressKit.Payload(id, m.C.Payload)
	op := "QUERY"
	if first {
		op = "FIRST_QUERY"
	}
	negative := !first && seq%20 == 0
	if negative {
		op = "QUERY_SQL_ERROR"
	}
	sample := m.R.Begin(op, c.id, "query_to_callback")
	c.pending++
	query := "SELECT CONNECTION_ID(), ?, ? UNION ALL SELECT CONNECTION_ID(), ?, ?"
	args := []any{id, payload, id + 1, payload}
	if negative {
		query = "SELECT gfoundation_stress_nonexistent_column"
		args = nil
	}
	c.session.Query(func(err error, rows GFoundationApi.IDbRows) {
		if m.R.Finishing {
			return
		}
		c.completed++
		c.pending--
		if negative {
			if err == nil || !strings.Contains(err.Error(), "1054") {
				err = fmt.Errorf("expected MySQL unknown-column error, got %v", err)
			} else {
				err = nil
			}
		} else if err == nil {
			if rows == nil {
				err = fmt.Errorf("nil rows")
			} else {
				for i := uint64(0); i < 2; i++ {
					var connectionID, got uint64
					var value []byte
					if !rows.Next() {
						err = fmt.Errorf("missing row %d", i)
						break
					}
					if err = rows.Scan(&connectionID, &got, &value); err != nil {
						break
					}
					if c.connectionID == 0 {
						c.connectionID = connectionID
						m.R.Count("physical_connections_observed", 1)
					}
					if connectionID != c.connectionID || got != id+i || !bytes.Equal(value, payload) {
						err = fmt.Errorf("connection/result mismatch connection=%d expected=%d request=%d", connectionID, c.connectionID, id)
						break
					}
				}
				if err == nil && rows.Next() {
					err = fmt.Errorf("unexpected extra row")
				}
			}
		}
		if c.completed != seq {
			err = fmt.Errorf("session FIFO violated callback=%d want=%d", c.completed, seq)
		}
		m.R.Complete(sample, err)
	}, query, args...)
}
func (m *DbTest) OnTimerDBQuery(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	c := context.(*dbPeer)
	if time.Now().After(c.expires) {
		c.retired = true
		m.API.GetTimerApi().StopTimer(m.OnTimerDBQuery, data)
		return
	}
	period := time.Duration(m.C.PeriodMS) * time.Millisecond
	now := time.Now()
	m.R.Observe("QUERY_TIMER_LAG", c.id, "scheduled_to_callback", c.nextDue, nil)
	if lag := now.Sub(c.nextDue); lag >= period {
		missed := uint64(lag / period)
		m.R.Count("query_periods_missed", missed)
		c.nextDue = c.nextDue.Add(time.Duration(missed) * period)
	}
	c.nextDue = c.nextDue.Add(period)
	for i := 0; i < m.C.PerTick; i++ {
		if c.pending >= m.window {
			m.R.Count("commands_skipped_window", uint64(m.C.PerTick-i))
			break
		}
		m.query(c, false)
	}
}
func (m *DbTest) Update(api GFoundationApi.GFoundationApi) {
	if m.R == nil {
		return
	}
	m.Poll()
	if m.R.Finishing {
		return
	}
	if m.Expired() {
		m.R.Fail("database callback/drain deadline exceeded")
		m.Release(api)
		return
	}
	for id, c := range m.active {
		if c.retired && c.pending == 0 {
			sample := m.R.Begin("RELEASE_RETURN", c.id, "call_to_return_not_disconnect")
			c.session.Release()
			c.session.Release()
			m.R.Complete(sample, nil)
			api.GetTimerApi().StopTimer(m.OnTimerDBQuery, c.session)
			delete(m.active, id)
		}
	}
	if m.loading && m.P.Closed && len(m.active) == 0 {
		if m.R.Counters["commands_skipped_window"]+m.R.Counters["query_periods_missed"] > 0 {
			m.R.Fail("DB load not fully delivered; see counters")
		}
		m.Finish()
	}
}
func (m *DbTest) Release(api GFoundationApi.GFoundationApi) {
	if m.R == nil || m.R.Finishing {
		return
	}
	api.GetTimerApi().StopTimer(m.OnTimerConnectDB, m)
	if m.preflight != nil {
		m.preflight.Release()
	}
	for session := range m.checking {
		session.Release()
	}
	for _, c := range m.active {
		if c.session != nil {
			api.GetTimerApi().StopTimer(m.OnTimerDBQuery, c.session)
			c.session.Release()
		}
	}
	m.Finish()
}
func GetModule() GFoundationApi.IModule { return &DbTest{} }
