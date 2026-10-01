package main

import (
	"G-Foundation/Api"
	"G-Foundation/Foundation/Net"
	"G-Foundation/Utils"
	"fmt"
	"runtime"
	"time"
)

func main() {
	runtime.LockOSThread()
	netApi := Net.NewNeter()

	server := netApi.LaunchTcpServer(
		"0.0.0.0",
		8888,
		func(session Api.ITcpSession) {
			session.SetConnectedCallback(
				func(success bool, session Api.ITcpSession) {
					fmt.Printf("TcpSession Connected Thread %d\n", Utils.GetThreadID())
				},
			)

			session.SetDisconnectedCallback(
				func(session Api.ITcpSession) {
					fmt.Printf("TcpSession Disconnected Thread %d\n", Utils.GetThreadID())
				},
			)

			session.SetReceivedCallback(
				func(data []byte, offset int, len int, session Api.ITcpSession) int {
					fmt.Printf("recv: %s\n", data[offset:offset+len])
					session.Send(data[offset:offset+len], true)
					if len > 10 {
						session.Close()
					}
					return len
				},
			)
		},
		func(err error) {

		},
	)

	fmt.Printf("Current Thread ID %d", Utils.GetThreadID())

	for {
		netApi.Update()
		time.Sleep(time.Microsecond)
	}

	server.Close()
}
