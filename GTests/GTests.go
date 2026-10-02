package main

import (
	"GFoundation/Api"
	"fmt"
)

type GTests struct {
}

func (m *GTests) Initialize(api Api.GFoundationApi) bool {
	return true
}

func (m *GTests) Launch(api Api.GFoundationApi) bool {
	api.GetNetApi().LaunchTcpServer("0.0.0.0", 8888, m.onTcpSessionConnected, m.onTcpServerError)
	fmt.Printf("LaunchTcpServer 0.0.0.0:8888\n")
	return true
}

func (m *GTests) LaunchFinished(api Api.GFoundationApi) {
}

func (m *GTests) Release(api Api.GFoundationApi) {

}

func (m *GTests) Update(api Api.GFoundationApi) {

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
		},
	)

	fmt.Printf("onTcpSessionConnected\n")
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
