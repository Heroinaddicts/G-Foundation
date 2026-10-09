package main

import (
	"GFoundation/Api"
	"fmt"
	"strconv"
)

type SessionData struct {
	recivesize    int64
	lastprintsize int64
}

type TcpClientTest struct {
	api  Api.GFoundationApi
	port uint16
}

var (
	totalSessionCount int
)

func (m *TcpClientTest) OnTimerTcpSessionSend(state uint8, count int, data any, context any, notmurder bool) {
	session, ok := data.(Api.ITcpSession)
	if Api.TimerStateBeat == state {
		if ok {
			session.Send([]byte("hello worldhello worldhello worldhello worldhello worldhello worldhello worldhello worldhello worldhello world"), true)
		} else {
			panic("error")
			m.api.GetTimerApi().StopTimer(m.OnTimerTcpSessionSend, data)
		}
	}

	if Api.TimerStateEnd == state && notmurder {
		session.Close()
	}
}

func (m *TcpClientTest) OnTimerCreateTcpSession(state uint8, count int, data any, context any, notmurder bool) {
	if Api.TimerStateBeat == state {
		session := m.api.GetNetApi().LaunchTcpClient("127.0.0.1", m.port)
		session.SetConnectedCallback(func(success bool, session Api.ITcpSession) {
			if success {
				m.api.GetTimerApi().StartTimer(m.OnTimerTcpSessionSend, session, nil, 100, 1000, 100)
				session.SetContext(&SessionData{
					recivesize:    0,
					lastprintsize: 0,
				})

				totalSessionCount++
				fmt.Printf("total session count %d\n", totalSessionCount)
			}
		})

		session.SetDisconnectedCallback(func(session Api.ITcpSession) {
			m.api.GetTimerApi().StopTimer(m.OnTimerTcpSessionSend, session)
			totalSessionCount--
			fmt.Printf("total session count %d\n", totalSessionCount)
		})

		session.SetReceivedCallback(func(data []byte, offset, len int, session Api.ITcpSession) int {
			d, ok := session.GetContext().(*SessionData)
			if !ok {
				panic("error")
			}

			d.recivesize += int64(len)
			if d.recivesize-d.lastprintsize >= 10240 {
				fmt.Printf("session recive size %d kb, total session count %d\n", d.lastprintsize/1024, totalSessionCount)
				d.lastprintsize = d.recivesize
			}

			return len
		})
	}
}

func (m *TcpClientTest) Initialize(api Api.GFoundationApi) bool {
	m.api = api

	port, err := strconv.ParseUint(api.GetLaunchArgument("port"), 10, 16)
	if nil != err {
		panic("error")
	}

	m.port = uint16(port)
	api.GetTimerApi().StartTimer(m.OnTimerCreateTcpSession, m, nil, 100, 100000, 10)
	return true
}

func (m *TcpClientTest) Launch(api Api.GFoundationApi) bool {
	return true
}

func (m *TcpClientTest) LaunchFinished(api Api.GFoundationApi) {

}

func (m *TcpClientTest) Release(api Api.GFoundationApi) {

}

func (m *TcpClientTest) Update(api Api.GFoundationApi) {

}

func GetModule() Api.IModule {
	return &TcpClientTest{}
}
