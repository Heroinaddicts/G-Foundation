package main

import (
	"GFoundationApi"
	"GFoundationTests/Interface"
	"GFoundationTests/StressKit"
	"fmt"
	"time"
)

type FindModuleTest struct {
	StressKit.Base
	name, modulesArgument string
	target                GFoundationApi.IModule
	sequence              uint64
}

func (m *FindModuleTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	if !m.Init(api, "FindModuleTest", "lookup", 100000, 100000) {
		return false
	}
	m.name = StressKit.Text(api, "lookup", "module", "FindModuleTest")
	m.modulesArgument = api.GetLaunchArgument("modules")
	return true
}
func (m *FindModuleTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if !m.Start(map[string]any{"target_module": m.name, "measurement": "synchronous call duration; no async callback"}) {
		return
	}
	m.target = api.GetModule(m.name)
	if m.target == nil {
		m.R.Fail("target module is not loaded: " + m.name)
		m.Finish()
		return
	}
	if m.name == "HttpTest" {
		if provider, ok := m.target.(Interface.IHttpTest); !ok || provider.Test() != 1000 {
			m.R.Fail("IHttpTest interface/type identity mismatch")
		}
	}
	m.Deadline = m.R.Started.Add(time.Duration(m.C.Seconds+m.C.DrainSeconds) * time.Second)
	api.GetTimerApi().StartTimer(m.OnTimerLookup, m, nil, 10, GFoundationApi.Unlimited, 10)
}
func (m *FindModuleTest) OnTimerLookup(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	n, end := m.P.Tick(m.R, count)
	for i := 0; i < n; i++ {
		m.sequence++
		m.R.Count("admissions_issued", 1)
		ticket := m.R.Begin("GET_MODULE", m.sequence, "call_to_return")
		var err error
		if m.API.GetModule(m.name) != m.target {
			err = fmt.Errorf("GetModule identity mismatch")
		}
		m.R.Complete(ticket, err)
		ticket = m.R.Begin("GET_ARGUMENT", m.sequence, "call_to_return")
		err = nil
		if m.API.GetLaunchArgument("modules") != m.modulesArgument || m.API.GetLaunchArgument("gfoundation_missing_argument_8cd9") != "" {
			err = fmt.Errorf("GetLaunchArgument mismatch")
		}
		m.R.Complete(ticket, err)
		ticket = m.R.Begin("GET_APIS", m.sequence, "six_getters_call_to_return")
		err = nil
		if m.API.GetNetApi() == nil || m.API.GetTimerApi() == nil || m.API.GetTaskApi() == nil || m.API.GetDbProxyApi() == nil || m.API.GetHttpApi() == nil || m.API.GetRedisApi() == nil {
			err = fmt.Errorf("nil API getter")
		}
		m.R.Complete(ticket, err)
		ticket = m.R.Begin("GET_MISSING_MODULE", m.sequence, "call_to_return")
		err = nil
		if m.API.GetModule("gfoundation_missing_module_8cd9") != nil {
			err = fmt.Errorf("missing module is not nil")
		}
		m.R.Complete(ticket, err)
	}
	if end {
		m.API.GetTimerApi().StopTimer(m.OnTimerLookup, m)
		m.Finish()
	}
}
func (m *FindModuleTest) Update(api GFoundationApi.GFoundationApi) {
	if m.R == nil {
		return
	}
	m.Poll()
	if !m.R.Finishing && m.Expired() {
		m.R.Fail("lookup run deadline exceeded")
		m.Release(api)
	}
}
func (m *FindModuleTest) Release(api GFoundationApi.GFoundationApi) {
	if m.R == nil || m.R.Finishing {
		return
	}
	api.GetTimerApi().StopTimer(m.OnTimerLookup, m)
	m.Finish()
}
func GetModule() GFoundationApi.IModule { return &FindModuleTest{} }
