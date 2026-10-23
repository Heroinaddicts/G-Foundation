package main

import (
	"GFoundationApi"
	"GFoundationTests/StressKit"
)

type TcpServerTest struct{ StressKit.TCP }

func (m *TcpServerTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	return m.InitTCP(api, "TcpServerTest", "tcp_server", true)
}
func GetModule() GFoundationApi.IModule { return &TcpServerTest{} }
