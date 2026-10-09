package Api

type ITcpSession interface {
	SetConnectedCallback(callback func(success bool, session ITcpSession))
	SetReceivedCallback(callback func(data []byte, offset int, len int, session ITcpSession) int)
	SetDisconnectedCallback(callback func(session ITcpSession))

	Send(data []byte, immediate bool)
	Close()

	SetContext(data any)
	GetContext() any
}

type ITcpServer interface {
	Close()
}

type INetApi interface {
	LaunchTcpServer(ip string, port uint16, accepted func(session ITcpSession), err func(err error)) ITcpServer
	LaunchTcpClient(ip string, port uint16) ITcpSession
}
