package main

import (
	"GFoundationApi"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"os"
	"strconv"
	"time"
)

type redisCase struct {
	name string
	run  func(done func(error))
}

type RedisTest struct {
	redis       GFoundationApi.IRedisApi
	timer       GFoundationApi.ITimerApi
	load        stressConfig
	stress      *redisStress
	session     GFoundationApi.IRedisSession
	host        string
	port        uint16
	db          int
	timeout     int
	key         string
	cases       []redisCase
	index       int
	passed      int
	failed      int
	current     string
	deadline    time.Time
	finished    bool
	released    bool
	memoryArmed bool
}

func (m *RedisTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	m.redis = api.GetRedisApi()
	if m.redis == nil {
		fmt.Println("[RedisTest] FAIL: GetRedisApi() returned nil")
		return false
	}
	m.host = api.GetLaunchArgument("redis_host")
	if m.host == "" {
		m.host = "172.16.0.20"
	}
	port, err := intArgument(api, "redis_port", 6379, 1, 65535)
	if err != nil {
		fmt.Printf("[RedisTest] FAIL: %v\n", err)
		return false
	}
	m.port = uint16(port)
	m.db, err = intArgument(api, "redis_db", 0, 0, 2147483647)
	if err != nil {
		fmt.Printf("[RedisTest] FAIL: %v\n", err)
		return false
	}
	m.timeout, err = intArgument(api, "redis_timeout", 3, 1, 60)
	if err != nil {
		fmt.Printf("[RedisTest] FAIL: %v\n", err)
		return false
	}
	if err := m.configureStress(api); err != nil {
		fmt.Printf("[RedisTest] FAIL: %v\n", err)
		return false
	}
	var id [12]byte
	if _, err := rand.Read(id[:]); err != nil {
		fmt.Printf("[RedisTest] FAIL: generate test key: %v\n", err)
		return false
	}
	m.key = "gfoundation:redis-test:" + hex.EncodeToString(id[:])
	return true
}

func (m *RedisTest) Launch(api GFoundationApi.GFoundationApi) bool {
	return true
}

func (m *RedisTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if api.GetLaunchArgument("memory_probe") == "1" && !m.memoryArmed {
		return
	}
	m.current = "Connect"
	m.deadline = time.Now().Add(15 * time.Second)
	fmt.Printf("[RedisTest] connecting to %s:%d db=%d\n", m.host, m.port, m.db)
	m.redis.Connect(m.host, m.port, os.Getenv("REDIS_PASSWORD"), m.db, func(err error, session GFoundationApi.IRedisSession) {
		if m.finished || m.released {
			if session != nil {
				session.Close()
			}
			return
		}
		if err != nil || session == nil {
			m.record("Connect", fmt.Errorf("connection failed: err=%v sessionIsNil=%t", err, session == nil))
			m.finish()
			return
		}
		m.session = session
		session.SetTimeOut(m.timeout)
		m.record("Connect", nil)
		m.cases = m.testCases()
		if m.load.enabled {
			m.cases = append(m.cases, redisCase{"TimerApi stress", m.testStress})
		}
		m.next()
	})
}

func (m *RedisTest) Release(api GFoundationApi.GFoundationApi) {
	m.released = true
	if m.stress != nil {
		m.stress.abort(fmt.Errorf("module released"))
		m.stress.forceFinish()
	}
	if m.session != nil {
		m.session.Close()
	}
}

func (m *RedisTest) Update(api GFoundationApi.GFoundationApi) {
	if m.stress != nil {
		m.stress.poll()
	}
	if !m.finished && !m.released && !m.deadline.IsZero() && time.Now().After(m.deadline) {
		if m.stress != nil && !m.stress.finished {
			if !m.stress.aborting {
				m.stress.abort(fmt.Errorf("stress test timed out"))
				m.deadline = time.Now().Add(time.Duration(m.load.drainSeconds) * time.Second)
			} else {
				m.stress.forceFinish()
			}
			return
		}
		m.record(m.current, fmt.Errorf("callback timed out"))
		m.finish()
	}
}

func (m *RedisTest) next() {
	if m.finished || m.released {
		return
	}
	if m.index == len(m.cases) {
		m.finish()
		return
	}
	test := m.cases[m.index]
	m.index++
	m.current = test.name
	m.deadline = time.Now().Add(time.Duration(4*m.timeout+5) * time.Second)
	completed := false
	test.run(func(err error) {
		if completed || m.finished || m.released {
			return
		}
		completed = true
		m.record(test.name, err)
		m.next()
	})
}

func (m *RedisTest) record(name string, err error) {
	if err != nil {
		m.failed++
		fmt.Printf("[RedisTest] FAIL %s: %v\n", name, err)
	} else {
		m.passed++
		fmt.Printf("[RedisTest] PASS %s\n", name)
	}
}

func (m *RedisTest) finish() {
	if m.finished {
		return
	}
	m.finished = true
	if m.session != nil {
		m.session.Close()
	}
	fmt.Printf("[RedisTest] COMPLETE passed=%d failed=%d\n", m.passed, m.failed)
}

func intArgument(api GFoundationApi.GFoundationApi, name string, fallback, min, max int) (int, error) {
	text := api.GetLaunchArgument(name)
	if text == "" {
		return fallback, nil
	}
	value, err := strconv.Atoi(text)
	if err != nil || value < min || value > max {
		return 0, fmt.Errorf("--%s must be an integer between %d and %d", name, min, max)
	}
	return value, nil
}

func GetModule() GFoundationApi.IModule {
	return &RedisTest{}
}
