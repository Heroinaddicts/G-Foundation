package Api

type ITcpSession interface {
	SetConnectedCallback(callback func(success bool, session ITcpSession))
	SetReceivedCallback(callback func(data []byte, session ITcpSession) uint32)
	SetDisconnectedCallback(callback func(session ITcpSession))

	Send(data []byte, immediate bool)
	Close()
}

type ITcpServer interface {
	MallocSession() ITcpSession
}

type INetApi interface {
	LaunchTcpServer(server *ITcpServer, ip string, port uint16) bool
	LaunchTcpClient(session *ITcpSession, ip string, port uint16) bool
}
