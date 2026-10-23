package main

import (
	"GFoundationApi"
	"GFoundationTests/StressKit"
)

type TcpClientTest struct{ StressKit.TCP }

func (m *TcpClientTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	return m.InitTCP(api, "TcpClientTest", "tcp_client", false)
}
func GetModule() GFoundationApi.IModule { return &TcpClientTest{} }
