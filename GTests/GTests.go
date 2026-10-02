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

func (m *GTests) OnTimer(state uint8, count int, data any, context any, murder bool) {
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

func (m *GTests) OnTimer2(state uint8, count int, data any, context any, murder bool) {
	s, ok := data.(Api.ITcpSession)
	if !ok || s == nil {
		return
	}

	if state == Api.TimerStateBeat {
		msg := []byte(strconv.Itoa(count) + "\n")
		for i := 0; i < 100; i++ {
			s.Send(msg, true)
		}

		fmt.Printf("Initialize Thread ID %d\n", Utils.GetThreadID())

		m.api.GetTaskApi().PushTask(
			func() (bool, any) {
				fmt.Printf("Task Thread ID %d\n", Utils.GetThreadID())
				return true, m
			},
			func(success bool, context any) {
				fmt.Printf("TaskCompleted Thread ID %d\n", Utils.GetThreadID())
			},
		)

		group := m.api.GetTaskApi().CreateTaskGroup()
		for i := 0; i < 100; i++ {
			taskIndex := i
			group.AddTask(
				func() (bool, any) {
					fmt.Printf("Group Task Thread ID %d\n", Utils.GetThreadID())
					return taskIndex%2 == 0, m
				},
				func(success bool, context any) {
					fmt.Printf("Group TaskCompleted Thread ID %d\n", Utils.GetThreadID())
				},
			)
		}

		group.SetAllCompletedCallback(
			func(success int, faild int) {
				fmt.Printf("TaskGroup AllCompletedCallback Thread ID %d success %d faild %d\n", Utils.GetThreadID(), success, faild)
			},
		)

		group.Start()
	}

	if state == Api.TimerStateEnd {
		s.Close()
	}
}

func (m *GTests) onTcpSessionConnected(session Api.ITcpSession) {
	session.SetReceivedCallback(m.onTcpRecive)
	session.SetConnectedCallback(
		func(success bool, session Api.ITcpSession) {
			fmt.Printf("session connected\n")
		},
	)
	session.SetDisconnectedCallback(
		func(session Api.ITcpSession) {
			fmt.Printf("session disconnected\n")
			m.api.GetTimerApi().StopTimer(m.OnTimer2, session)
		},
	)

	fmt.Printf("onTcpSessionConnected\n")

	m.api.GetTimerApi().StartTimer(
		m.OnTimer2,
		session,
		session,
		1000,
		1000000,
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
