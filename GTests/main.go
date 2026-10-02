package main

import (
	"GFoundation/Api"
	"time"
)

func main() {
	api := Api.CreateApi("/Users/max/Documents/GitHub/G-Foundation/GFoundation/GFoundation.so")

	api.GetNetApi().LaunchTcpServer("0.0.0.0", 8888,
		func(session Api.ITcpSession) {
			session.SetConnectedCallback(
				func(success bool, session Api.ITcpSession) {

				},
			)

			session.SetDisconnectedCallback(
				func(session Api.ITcpSession) {

				},
			)

			session.SetReceivedCallback(
				func(data []byte, offset, len int, session Api.ITcpSession) int {
					return len
				},
			)
		},
		func(err error) {

		},
	)

	for {
		api.Update()

		time.Sleep(time.Millisecond)
	}
}
