package main

import (
	"GFoundationApi"
	"bytes"
	"database/sql"
	"fmt"
	"os"
	"time"
)

func (m *DbTest) runChecks() {
	ticket := m.R.Begin("CHECK_CONNECT", 0, "real_authentication")
	m.API.GetDbProxyApi().CreateDatabaseSession(GFoundationApi.DatabaseTypeMysql, m.host, m.port, m.user, os.Getenv("DB_PASSWORD"), m.dbname, func(err error, s GFoundationApi.IDatabaseSession) {
		if m.R.Finishing {
			if err == nil && s != nil {
				s.Release()
			}
			return
		}
		if err == nil && s == nil {
			err = fmt.Errorf("nil session")
		}
		m.R.Complete(ticket, err)
		if err != nil {
			m.Finish()
			return
		}
		m.preflight = s
		m.checkTypes(func() {
			m.checkSnapshot(func() { m.checkEmptyBinary(func() { m.checkRelease(func() { m.checkDisconnect(m.beginLoad) }) }) })
		})
	})
}

func (m *DbTest) checkEmptyBinary(done func()) {
	ticket := m.R.Begin("CHECK_EMPTY_BINARY", 0, "empty_blob_is_not_null")
	m.preflight.Query(func(err error, r GFoundationApi.IDbRows) {
		if m.R.Finishing {
			return
		}
		if err == nil {
			var length int
			var isNull bool
			if r == nil || !r.Next() {
				err = fmt.Errorf("missing binary row")
			} else if err = r.Scan(&length, &isNull); err == nil && (length != 0 || isNull) {
				err = fmt.Errorf("empty binary parameter became NULL")
			}
		}
		m.R.Complete(ticket, err)
		done()
	}, "SELECT LENGTH(?), ? IS NULL", []byte{}, []byte{})
}

// Kill only a connection created by this test. A dead dedicated session must
// report errors on subsequent queries, rather than silently dial another one.
func (m *DbTest) checkDisconnect(done func()) {
	m.preflight = nil
	ticket := m.R.Begin("CHECK_DISCONNECT", 0, "no_automatic_redial")
	create := func(cb GFoundationApi.OnCreateDatabaseSessionCallback) {
		m.API.GetDbProxyApi().CreateDatabaseSession(GFoundationApi.DatabaseTypeMysql, m.host, m.port, m.user, os.Getenv("DB_PASSWORD"), m.dbname, func(err error, s GFoundationApi.IDatabaseSession) {
			if m.R.Finishing {
				if err == nil && s != nil {
					s.Release()
				}
				return
			}
			if err == nil && s != nil {
				m.checking[s] = true
			}
			cb(err, s)
		})
	}
	create(func(err error, victim GFoundationApi.IDatabaseSession) {
		if err != nil {
			m.R.Complete(ticket, err)
			done()
			return
		}
		victim.Query(func(err error, rows GFoundationApi.IDbRows) {
			if m.R.Finishing {
				return
			}
			var id uint64
			if err == nil {
				if rows == nil || !rows.Next() {
					err = fmt.Errorf("missing victim connection id")
				} else {
					err = rows.Scan(&id)
				}
			}
			if err != nil {
				victim.Release()
				m.R.Complete(ticket, err)
				done()
				return
			}
			create(func(err error, control GFoundationApi.IDatabaseSession) {
				if err != nil {
					victim.Release()
					m.R.Complete(ticket, err)
					done()
					return
				}
				control.Query(func(err error, rows GFoundationApi.IDbRows) {
					if m.R.Finishing {
						return
					}
					control.Release()
					if err != nil {
						victim.Release()
						m.R.Complete(ticket, err)
						done()
						return
					}
					remaining := 2
					var failure error
					for i := 0; i < 2; i++ {
						victim.Query(func(err error, rows GFoundationApi.IDbRows) {
							if m.R.Finishing {
								return
							}
							if err == nil || rows != nil {
								failure = fmt.Errorf("dead session reconnected or returned rows")
							}
							remaining--
							if remaining == 0 {
								victim.Release()
								m.R.Complete(ticket, failure)
								done()
							}
						}, "SELECT CONNECTION_ID()")
					}
				}, fmt.Sprintf("KILL CONNECTION %d", id))
			})
		}, "SELECT CONNECTION_ID()")
	})
}
func (m *DbTest) checkTypes(done func()) {
	ticket := m.R.Begin("CHECK_SCAN", 0, "types_null_empty_and_scan_errors")
	m.preflight.Query(func(err error, rows GFoundationApi.IDbRows) {
		if m.R.Finishing {
			return
		}
		if err == nil {
			var i int64
			var u uint64
			var f float64
			var b bool
			var text string
			var binary []byte
			var nullable sql.NullString
			var date time.Time
			var pointer *int
			if rows == nil || !rows.Next() {
				err = fmt.Errorf("missing typed row")
			} else if err = rows.Scan(&i, &u, &f, &b, &text, &binary, &nullable, &date, &pointer); err == nil {
				if i != -123 || u != 18446744073709551615 || f != 1.25 || !b || text != "text" || !bytes.Equal(binary, []byte{0, 255, 1}) || nullable.Valid || pointer != nil || date.Year() != 2026 {
					err = fmt.Errorf("Scan typed/NULL values mismatch")
				}
				if rows.Scan(&i) == nil {
					err = fmt.Errorf("Scan accepted incorrect destination count")
				}
				var tiny int8
				if e := rows.Scan(&tiny, &u, &f, &b, &text, &binary, &nullable, &date, &pointer); e != nil {
					err = e
				} else if tiny != -123 {
					err = fmt.Errorf("int8 conversion mismatch")
				}
				if rows.Next() {
					err = fmt.Errorf("unexpected second typed row")
				}
			}
		}
		m.R.Complete(ticket, err)
		empty := m.R.Begin("CHECK_EMPTY_ROWS", 0, "empty_result")
		m.preflight.Query(func(e error, r GFoundationApi.IDbRows) {
			if m.R.Finishing {
				return
			}
			if e == nil && (r == nil || r.Next()) {
				e = fmt.Errorf("expected empty rows")
			}
			m.R.Complete(empty, e)
			done()
		}, "SELECT 1 WHERE 0")
	}, "SELECT -123, CAST('18446744073709551615' AS UNSIGNED), 1.25, TRUE, 'text', ?, NULL, CAST('2026-10-17 01:02:03' AS DATETIME), NULL", []byte{0, 255, 1})
}
func (m *DbTest) checkSnapshot(done func()) {
	ticket := m.R.Begin("CHECK_INPUT_SNAPSHOT", 0, "mutated_args_after_query")
	input := []byte{0, 1, 255, 7}
	args := []any{input, "original"}
	m.preflight.Query(func(err error, r GFoundationApi.IDbRows) {
		if m.R.Finishing {
			return
		}
		if err == nil {
			var b []byte
			var s string
			if r == nil || !r.Next() {
				err = fmt.Errorf("missing snapshot row")
			} else if err = r.Scan(&b, &s); err == nil && (!bytes.Equal(b, []byte{0, 1, 255, 7}) || s != "original") {
				err = fmt.Errorf("query inputs were not snapshotted")
			}
		}
		m.R.Complete(ticket, err)
		done()
	}, "SELECT ?, ?", args...)
	input[0] = 99
	args[1] = "changed"
}
func (m *DbTest) checkRelease(done func()) {
	// Queue work then immediately release. Accepted queries must drain in FIFO
	// order; new work must fail through a main-thread callback without redialing.
	remaining := 17
	completed := 0
	var connection uint64
	finish := func() {
		remaining--
		if remaining == 0 {
			done()
		}
	}
	for i := 1; i <= 16; i++ {
		ticket := m.R.Begin("CHECK_DRAIN_FIFO", uint64(i), "release_with_pending_queries")
		m.preflight.Query(func(err error, r GFoundationApi.IDbRows) {
			if m.R.Finishing {
				return
			}
			completed++
			if err == nil {
				var cid, seq uint64
				if r == nil || !r.Next() {
					err = fmt.Errorf("missing FIFO row")
				} else if err = r.Scan(&cid, &seq); err == nil {
					if connection == 0 {
						connection = cid
					}
					if cid != connection || seq != uint64(i) || completed != i {
						err = fmt.Errorf("connection/FIFO changed during drain")
					}
				}
			}
			m.R.Complete(ticket, err)
			finish()
		}, "SELECT CONNECTION_ID(), ?", i)
	}
	m.preflight.Query(nil, "SELECT 1")
	m.preflight.Release()
	m.preflight.Release()
	ticket := m.R.Begin("CHECK_RELEASE_REJECT", 0, "query_after_release")
	m.preflight.Query(func(err error, r GFoundationApi.IDbRows) {
		if m.R.Finishing {
			return
		}
		var failure error
		if err == nil || r != nil {
			failure = fmt.Errorf("query after Release was not rejected with nil rows")
		}
		m.R.Complete(ticket, failure)
		finish()
	}, "SELECT 1")
}
