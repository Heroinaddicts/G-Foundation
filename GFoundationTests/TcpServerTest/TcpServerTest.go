package main

import (
	"GFoundationApi"
	"fmt"
)

type TcpServerTest struct {
	api       GFoundationApi.GFoundationApi
	tcpServer GFoundationApi.ITcpServer
}

type SessionData struct {
	recivesize    int64
	lastprintsize int64
}

var (
	totalSessionCount int
)

func (m *TcpServerTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	m.api = api
	return true
}

func (m *TcpServerTest) Launch(api GFoundationApi.GFoundationApi) bool {
	return true
}

func (m *TcpServerTest) OnTimer(state uint8, count int, data any, context any, notmurder bool) {
	session, ok := data.(GFoundationApi.ITcpSession)
	if GFoundationApi.TimerStateBeat == state {
		if ok {
			session.Send([]byte("hello worldhello worldhello worldhello worldhello worldhello worldhello world"), true)
		} else {
			panic("error")
		}
	}

	if GFoundationApi.TimerStateEnd == state && notmurder {
		session.Close()
	}
}

func (m *TcpServerTest) OnTcpSessionReciver(data []byte, offset int, len int, session GFoundationApi.ITcpSession) int {
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

func (m *TcpServerTest) OnTcpSessionConnected(session GFoundationApi.ITcpSession) {
	session.SetConnectedCallback(func(success bool, session GFoundationApi.ITcpSession) {
		m.api.GetTimerApi().StartTimer(m.OnTimer, session, nil, 100, 1000, 100)
		totalSessionCount++
		fmt.Printf("total session count %d\n", totalSessionCount)
	})

	session.SetDisconnectedCallback(func(session GFoundationApi.ITcpSession) {
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

func (m *TcpServerTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	m.tcpServer = api.GetNetApi().LaunchTcpServer(
		"0.0.0.0",
		8888,
		m.OnTcpSessionConnected,
		func(err error) {

		},
	)
}

func (m *TcpServerTest) Release(api GFoundationApi.GFoundationApi) {

}

func (m *TcpServerTest) Update(api GFoundationApi.GFoundationApi) {

}

func GetModule() GFoundationApi.IModule {
	return &TcpServerTest{}
}
