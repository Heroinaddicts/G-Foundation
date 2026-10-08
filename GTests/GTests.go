package main

import (
	"GFoundation/Api"
	"GFoundation/Utils"
	"fmt"
	"strconv"
	"time"
)

type GTests struct {
	api Api.GFoundationApi
}

func (m *GTests) Initialize(api Api.GFoundationApi) bool {
	m.api = api
	return true
}

func (m *GTests) OnTimer(state uint8, count int, data any, context any, notmurder bool) {
	fmt.Printf("Current tick %d, count %d\n", time.Now().UnixMilli(), count)
	if count == 8 {
		m.api.GetTimerApi().StopTimer(m.OnTimer, m)
	}
}

func (m *GTests) Launch(api Api.GFoundationApi) bool {
	api.GetNetApi().LaunchTcpServer("0.0.0.0", 8888, m.onTcpSessionConnected, m.onTcpServerError)
	fmt.Printf("LaunchTcpServer 0.0.0.0:8888\n")

	api.GetTimerApi().StartTimer(
		m.OnTimer,
		m,
		m,
		1000,
		10,
		1000,
	)

	return true
}

func (m *GTests) LaunchFinished(api Api.GFoundationApi) {

}

func (m *GTests) Release(api Api.GFoundationApi) {

}

func (m *GTests) Update(api Api.GFoundationApi) {

}

var (
	sessionCount int32 = 0
)

func (m *GTests) OnTimer2(state uint8, count int, data any, context any, notmurder bool) {
	s, ok := data.(Api.ITcpSession)
	if !ok || s == nil {
		return
	}

	if state == Api.TimerStateStart {
		fmt.Printf("Initialize Thread ID %d\n", Utils.GetThreadID())
	}

	if state == Api.TimerStateBeat {
		msg := []byte(strconv.Itoa(count) + "\n")
		s.Send(msg, true)

		m.api.GetTaskApi().PushTask(
			Api.Unorder,
			func() (bool, any) {
				//fmt.Printf("Task Thread ID %d\n", Utils.GetThreadID())
				return true, m
			},
			func(success bool, context any) {
				//fmt.Printf("TaskCompleted Thread ID %d\n", Utils.GetThreadID())
			},
		)

		group := m.api.GetTaskApi().CreateTaskGroup()
		for i := 0; i < 10; i++ {
			taskIndex := i
			group.AddTask(
				func() (bool, any) {
					//fmt.Printf("Group Task Thread ID %d\n", Utils.GetThreadID())
					return taskIndex%2 == 0, m
				},
				func(success bool, context any) {
					//fmt.Printf("Group TaskCompleted Thread ID %d\n", Utils.GetThreadID())
				},
			)
		}

		group.SetAllCompletedCallback(
			func(success int, faild int) {
				//fmt.Printf("TaskGroup AllCompletedCallback Thread ID %d success %d faild %d\n", Utils.GetThreadID(), success, faild)
			},
		)

		group.Start()
	}

	if state == Api.TimerStateEnd && notmurder {
		s.Close()
		fmt.Printf("session Close, session count %d\n", sessionCount)
	}
}

func (m *GTests) onTcpSessionConnected(session Api.ITcpSession) {
	session.SetReceivedCallback(m.onTcpRecive)
	session.SetConnectedCallback(
		func(success bool, session Api.ITcpSession) {
			sessionCount++
			fmt.Printf("session connected, session count %d\n", sessionCount)
		},
	)
	session.SetDisconnectedCallback(
		func(session Api.ITcpSession) {
			sessionCount--
			fmt.Printf("session disconnected, session count %d\n", sessionCount)
			m.api.GetTimerApi().StopTimer(m.OnTimer2, session)
		},
	)

	fmt.Printf("onTcpSessionConnected\n")

	m.api.GetTimerApi().StartTimer(
		m.OnTimer2,
		session,
		session,
		1000,
		1000,
		10,
	)
}

func (m *GTests) onTcpServerError(err error) {

}

func (m *GTests) onTcpRecive(data []byte, offset int, len int, session Api.ITcpSession) int {
	fmt.Printf("onTcpRecive %d\n", len)
	return len
}

func GetModule() Api.IModule {
	return &GTests{}
}
