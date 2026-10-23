package main

import (
	"GFoundationApi"
	"bytes"
	"fmt"
	"strings"
)

func requestError(success bool, err error) error {
	if !success || err != nil {
		return fmt.Errorf("success=%t err=%v", success, err)
	}
	return nil
}

func expectData(done func(error), expected []byte) GFoundationApi.OnRedisEvalCallback {
	return func(success bool, err error, data []byte) {
		if failure := requestError(success, err); failure != nil {
			done(failure)
			return
		}
		if !bytes.Equal(data, expected) {
			done(fmt.Errorf("data=%q, expected=%q", data, expected))
			return
		}
		done(nil)
	}
}

func (m *RedisTest) testCases() []redisCase {
	return []redisCase{
		{"Set/Get binary and input snapshot", m.testBinary},
		{"Get missing key", func(done func(error)) {
			m.session.Get(m.key+":missing", func(success bool, err error, data []byte) {
				// IRedisSession currently forwards the driver's missing-key error.
				if success || err == nil || err.Error() != "redis: nil" || data != nil {
					done(fmt.Errorf("expected missing-key error, got success=%t err=%v data=%q", success, err, data))
					return
				}
				done(nil)
			})
		}},
		{"Eval string", func(done func(error)) {
			m.session.Eval("return ARGV[1]", nil, expectData(done, []byte("Redis 中文")), "Redis 中文")
		}},
		{"Eval binary argument snapshot", func(done func(error)) {
			value := []byte{0, 255, 'x'}
			m.session.Eval("return ARGV[1]", nil, expectData(done, bytes.Clone(value)), value)
			value[0] = 'X'
		}},
		{"Eval integer", func(done func(error)) {
			m.session.Eval("return tonumber(ARGV[1]) + tonumber(ARGV[2])", nil, expectData(done, []byte("42")), 19, 23)
		}},
		{"Eval array and key snapshot", func(done func(error)) {
			keys := []string{m.key}
			expected := fmt.Appendf(nil, "[%q,7,\"ok\"]", m.key)
			m.session.Eval("return {KEYS[1], tonumber(ARGV[1]), ARGV[2]}", keys, expectData(done, expected), 7, "ok")
			keys[0] = "changed-after-enqueue"
		}},
		{"Eval script error", func(done func(error)) {
			m.session.Eval("return redis.error_reply('ERR RedisTest expected error')", nil, func(success bool, err error, _ []byte) {
				if success || err == nil || !strings.Contains(err.Error(), "RedisTest expected error") {
					done(fmt.Errorf("expected script error, got success=%t err=%v", success, err))
					return
				}
				done(nil)
			})
		}},
		{"Request and callback ordering", m.testOrdering},
		{"SetTimeOut configuration", func(done func(error)) {
			// Verify settings do not break subsequent commands. Execution deadline
			// enforcement requires a controlled slow server, not a blocking Lua script.
			m.session.SetTimeOut(1)
			m.session.Eval("return 'timeout-configured'", nil, func(success bool, err error, data []byte) {
				m.session.SetTimeOut(0)
				m.session.SetTimeOut(m.timeout)
				expectData(done, []byte("timeout-configured"))(success, err, data)
			})
		}},
		{"Nil callbacks", func(done func(error)) {
			m.session.Get(m.key, nil)
			m.session.Eval("return 1", nil, nil)
			m.session.Eval("return 'barrier'", nil, expectData(done, []byte("barrier")))
		}},
		{"Cleanup test key", func(done func(error)) {
			m.session.Eval("return redis.call('DEL', KEYS[1])", []string{m.key}, expectData(done, []byte("1")))
		}},
		{"Close twice, disconnected callback and rejected requests", m.testClose},
	}
}

func (m *RedisTest) testBinary(done func(error)) {
	value := []byte{0, 255, 127, 'g', 'f'}
	expected := bytes.Clone(value)
	m.session.Set(m.key, value, func(success bool, err error) {
		if failure := requestError(success, err); failure != nil {
			done(failure)
			return
		}
		// Limit leftover test data if the host is stopped before cleanup.
		m.session.Eval("return redis.call('EXPIRE', KEYS[1], 60)", []string{m.key}, func(success bool, err error, data []byte) {
			if failure := requestError(success, err); failure != nil {
				done(failure)
				return
			}
			if string(data) != "1" {
				done(fmt.Errorf("could not set test key expiry: %q", data))
				return
			}
			m.session.Get(m.key, func(success bool, err error, data []byte) {
				expectData(done, expected)(success, err, data)
			})
		})
	})
	value[0] = 'X'
}

func (m *RedisTest) testOrdering(done func(error)) {
	position := 0
	var failure error
	check := func(expected int, err error) {
		if failure == nil && (position != expected || err != nil) {
			failure = fmt.Errorf("callback=%d expected=%d err=%v", position, expected, err)
		}
		position++
		if position == 4 {
			done(failure)
		}
	}
	setValue := func(value string, expected int) {
		// SET through Lua keeps this test key expiring during the ordering test.
		m.session.Eval("return redis.call('SET', KEYS[1], ARGV[1], 'EX', 60)", []string{m.key}, func(success bool, err error, data []byte) {
			expectData(func(err error) { check(expected, err) }, []byte("OK"))(success, err, data)
		}, value)
	}
	getValue := func(value string, expected int) {
		m.session.Get(m.key, func(success bool, err error, data []byte) {
			expectData(func(err error) { check(expected, err) }, []byte(value))(success, err, data)
		})
	}
	setValue("first", 0)
	getValue("first", 1)
	setValue("second", 2)
	getValue("second", 3)
}

func (m *RedisTest) testClose(done func(error)) {
	completed := 0
	var failure error
	check := func(err error) {
		if failure == nil {
			failure = err
		}
		completed++
		if completed == 4 {
			done(failure)
		}
	}
	m.session.SetDisconnectedCallback(func(success bool, err error, session GFoundationApi.IRedisSession) {
		if failure := requestError(success, err); failure != nil {
			check(failure)
		} else if session != m.session {
			check(fmt.Errorf("disconnect callback returned a different session"))
		} else {
			check(nil)
		}
	})
	m.session.Close()
	m.session.Close()
	rejected := func(success bool, err error) {
		if success || err == nil || err.Error() != "redis session is closed" {
			check(fmt.Errorf("closed session accepted request: success=%t err=%v", success, err))
			return
		}
		check(nil)
	}
	m.session.Set(m.key, []byte("must-not-be-written"), rejected)
	m.session.Get(m.key, func(success bool, err error, _ []byte) { rejected(success, err) })
	m.session.Eval("return 1", nil, func(success bool, err error, _ []byte) { rejected(success, err) })
}
