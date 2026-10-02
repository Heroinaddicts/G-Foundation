package Net

import (
	"GFoundation/Api"
	"fmt"
	"net"
)

type TcpServer struct {
	neter *Neter

	connected func(session Api.ITcpSession)
	err       func(err error)
	closed    bool
	listen    net.Listener
}

func NewTcpServer(ip string, port uint16, connected func(session Api.ITcpSession), err func(err error), neter *Neter) *TcpServer {
	s := &TcpServer{
		connected: connected,
		err:       err,
		neter:     neter,
	}

	listen, e := net.Listen("tcp", fmt.Sprintf("%s:%d", ip, port))
	if e != nil {
		err(e)
		return nil
	}

	s.listen = listen
	go s.AcceptLoop(ip, port)
	return s
}

func (s *TcpServer) Close() {
	s.closed = true
}

func (s *TcpServer) IsClosed() bool {
	return s.closed
}

func (s *TcpServer) AcceptLoop(ip string, port uint16) {
	for {
		conn, err := s.listen.Accept()
		if err != nil {
			s.neter.PushEvent(NeterEvent{
				EventType: NeterEventAccept,
				Server:    s,
				Session:   nil,
				Code:      err,
			})
			continue
		}

		s.neter.PushEvent(NeterEvent{
			EventType: NeterEventAccept,
			Server:    s,
			Session:   NewTcpSession(conn, s, s.neter),
			Code:      nil,
		})
	}
}
