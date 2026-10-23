package main

import (
	"GFoundationApi"
	"GFoundationTests/StressKit"
	"fmt"
	"time"
)

// Unlike the normal TCP workload, Close is called immediately after launch,
// before asynchronous dialing completes. No session references are retained.
type TcpLifecycleTest struct {
	StressKit.Base
	port                 uint16
	sequence             uint64
	settle               time.Time
	closeOnConnected     bool
	closeBeforeConnected bool
}

func (m *TcpLifecycleTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	if !m.Init(api, "TcpLifecycleTest", "tcp_lifecycle", 100, 1000) {
		return false
	}
	port, err := StressKit.Int(api, "tcp_client", "port", 18889, 1, 65535)
	if err != nil {
		fmt.Println(err)
		return false
	}
	m.port = uint16(port)
	m.closeOnConnected = api.GetLaunchArgument("tcp_lifecycle_close_on_connected") == "1"
	m.closeBeforeConnected = !m.closeOnConnected || api.GetLaunchArgument("tcp_lifecycle_close_before_connected") == "1"
	return true
}
func (m *TcpLifecycleTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if m.Start(map[string]any{"scenario": "TCP close lifecycle", "close_on_connected": m.closeOnConnected, "close_before_connected": m.closeBeforeConnected}) {
		api.GetTimerApi().StartTimer(m.OnTimerConnect, m, nil, 10, GFoundationApi.Unlimited, 10)
	}
}
func (m *TcpLifecycleTest) OnTimerConnect(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	n, end := m.P.Tick(m.R, count)
	for i := 0; i < n; i++ {
		m.sequence++
		m.R.Count("admissions_issued", 1)
		sample := m.R.Begin("CLOSE_DURING_CONNECT", m.sequence, "launch_to_connect_callback")
		s := m.API.GetNetApi().LaunchTcpClient("127.0.0.1", m.port)
		s.SetConnectedCallback(func(ok bool, session GFoundationApi.ITcpSession) {
			if m.R == nil || m.R.Finishing {
				return
			}
			if ok {
				m.R.Count("connect_success", 1)
			} else {
				m.R.Count("connect_failed", 1)
			}
			if m.closeOnConnected {
				session.Close()
				m.R.Count("closed_in_connect_callback", 1)
			} else if ok {
				m.R.Count("connected_after_close", 1)
			} else {
				m.R.Count("connect_rejected_after_close", 1)
			}
			m.R.Complete(sample, nil)
		})
		s.SetDisconnectedCallback(func(session GFoundationApi.ITcpSession) {
			if m.R != nil && !m.R.Finishing {
				m.R.Count("disconnect_callbacks", 1)
			}
		})
		if m.closeBeforeConnected {
			s.Close()
			m.R.Count("close_before_connect_calls", 1)
		}
	}
	if end {
		m.API.GetTimerApi().StopTimer(m.OnTimerConnect, m)
		m.settle = time.Now().Add(2 * time.Second)
	}
}
func (m *TcpLifecycleTest) Update(api GFoundationApi.GFoundationApi) {
	if m.R == nil {
		return
	}
	m.Poll()
	if m.R.Finishing {
		return
	}
	if m.P.Closed && time.Now().After(m.settle) && len(m.R.Pending) == 0 {
		m.Finish()
	}
	if m.Expired() {
		m.R.Fail("connect callbacks timed out")
		m.Release(api)
	}
}
func (m *TcpLifecycleTest) Release(api GFoundationApi.GFoundationApi) {
	if m.R == nil || m.R.Finishing {
		return
	}
	api.GetTimerApi().StopTimer(m.OnTimerConnect, m)
	m.Finish()
}
func (m *TcpLifecycleTest) MemoryReset() { *m = TcpLifecycleTest{} }
func GetModule() GFoundationApi.IModule  { return &TcpLifecycleTest{} }
