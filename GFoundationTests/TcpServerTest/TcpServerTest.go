package main

import (
	"GFoundation/Api"
	"fmt"
)

type TcpServerTest struct {
	api       Api.GFoundationApi
	tcpServer Api.ITcpServer
}

type SessionData struct {
	recivesize    int64
	lastprintsize int64
}

var (
	totalSessionCount int
)

func (m *TcpServerTest) Initialize(api Api.GFoundationApi) bool {
	m.api = api
	return true
}

func (m *TcpServerTest) Launch(api Api.GFoundationApi) bool {
	return true
}

func (m *TcpServerTest) OnTimer(state uint8, count int, data any, context any, notmurder bool) {
	session, ok := data.(Api.ITcpSession)
	if Api.TimerStateBeat == state {
		if ok {
			session.Send([]byte("hello worldhello worldhello worldhello worldhello worldhello worldhello world"), true)
		} else {
			panic("error")
			m.api.GetTimerApi().StopTimer(m.OnTimer, data)
		}
	}

	if Api.TimerStateEnd == state && notmurder {
		session.Close()
	}
}

func (m *TcpServerTest) OnTcpSessionReciver(data []byte, offset int, len int, session Api.ITcpSession) int {
	sd, ok := session.GetContext().(*SessionData)
	if ok {
		sd.recivesize += int64(len)
	} else {
		panic("error")
	}

	if sd.recivesize-sd.lastprintsize >= 10240 {
		sd.lastprintsize = sd.recivesize
		fmt.Printf("session recive size %d kb, total session count %d\n", sd.lastprintsize/1024, totalSessionCount)
	}

	return len
}

func (m *TcpServerTest) OnTcpSessionConnected(session Api.ITcpSession) {
	session.SetConnectedCallback(func(success bool, session Api.ITcpSession) {
		m.api.GetTimerApi().StartTimer(m.OnTimer, session, nil, 100, 1000, 100)
		totalSessionCount++
		fmt.Printf("total session count %d\n", totalSessionCount)
	})

	session.SetDisconnectedCallback(func(session Api.ITcpSession) {
		m.api.GetTimerApi().StopTimer(m.OnTimer, session)
		totalSessionCount--
		fmt.Printf("total session count %d\n", totalSessionCount)
	})

	session.SetReceivedCallback(m.OnTcpSessionReciver)

	session.SetContext(&SessionData{
		recivesize:    0,
		lastprintsize: 0,
	})
}

func (m *TcpServerTest) LaunchFinished(api Api.GFoundationApi) {
	m.tcpServer = api.GetNetApi().LaunchTcpServer(
		"0.0.0.0",
		8888,
		m.OnTcpSessionConnected,
		func(err error) {

		},
	)
}

func (m *TcpServerTest) Release(api Api.GFoundationApi) {

}

func (m *TcpServerTest) Update(api Api.GFoundationApi) {

}

func GetModule() Api.IModule {
	return &TcpServerTest{}
}
