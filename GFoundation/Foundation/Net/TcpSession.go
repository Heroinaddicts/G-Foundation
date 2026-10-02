package Net

import (
	"GFoundation/Api"
	"GFoundation/Utils"
	"fmt"
	"net"
	"sync/atomic"
	"time"
)

type TcpSession struct {
	recver *Utils.SPSCBuffer
	sender *Utils.SPSCBuffer
	con    net.Conn
	neter  *Neter
	server *TcpServer

	connectCallback func(success bool, session Api.ITcpSession)
	recvCallback    func(data []byte, offset int, len int, session Api.ITcpSession) int
	disconCallback  func(session Api.ITcpSession)

	isSending atomic.Bool
}

func NewTcpSession(con net.Conn, server *TcpServer, neter *Neter) *TcpSession {
	s := &TcpSession{
		recver:          Utils.NewSPSCBuffer(1024, 1024),
		sender:          Utils.NewSPSCBuffer(1024, 1024),
		con:             con,
		neter:           neter,
		server:          server,
		connectCallback: nil,
		recvCallback:    nil,
		disconCallback:  nil,
	}

	if con != nil {
		go s.readLoop()
	}

	return s
}

func (s *TcpSession) ConnectAsync(ip string, port uint16) {
	if s.con != nil {
		panic("TcpSession already connected")
		return
	}

	go func() {
		conn, err := net.Dial("tcp", fmt.Sprintf("%s:%d", ip, port))
		if err != nil {
			s.neter.PushEvent(NeterEvent{
				EventType: NeterEventConnect,
				Server:    s.server,
				Session:   s,
				Code:      err,
			})
			return
		}

		s.con = conn
		s.neter.PushEvent(NeterEvent{
			EventType: NeterEventConnect,
			Server:    s.server,
			Session:   s,
			Code:      nil,
		})
		go s.readLoop()
	}()
}

func (s *TcpSession) sendAsync() {
	for {
		for {
			if s.sender.Size() <= 0 {
				break
			}
			if s.sender.Read(
				func(data []byte, offset int, length int) int {
					n, err := s.con.Write(data[offset : offset+length])
					if err != nil {
						s.con.Close()
						return 0
					}

					return n
				},
			) == false {
				return
			}
		}

		if !s.isSending.CompareAndSwap(true, false) {
			panic("TcpSession: isSending state corrupted")
		}

		if s.sender.Size() > 0 {
			if s.isSending.CompareAndSwap(false, true) {
				continue
			}
		}

		return
	}
}

func (s *TcpSession) readLoop() {
	buffer := make([]byte, 4096)

	for {
		n, err := s.con.Read(buffer)

		if n > 0 {
			s.recver.Write(buffer, 0, n)
			s.neter.PushEvent(NeterEvent{
				EventType: NeterEventRecv,
				Server:    s.server,
				Session:   s,
				Code:      nil,
			})
		}

		if err != nil {
			s.neter.PushEvent(NeterEvent{
				EventType: NeterEventDisconnect,
				Server:    s.server,
				Session:   s,
				Code:      err,
			})
			s.con.Close()
			return
		}
	}
}

func (s *TcpSession) OnConnected(sucess bool) {
	if s.connectCallback != nil {
		s.connectCallback(sucess, s)
	}
}

func (s *TcpSession) OnDisconnected() {
	if s.disconCallback != nil {
		s.disconCallback(s)
	}
}

func (s *TcpSession) OnRecive() {
	if s.recvCallback != nil {
		for {
			last := time.Microsecond
			b := s.recver.Read(
				func(data []byte, offset int, length int) int {
					return s.recvCallback(data, offset, length, s)
				},
			)

			if b == false {
				return
			}

			if time.Microsecond-last > 1000 {
				s.neter.PushEvent(NeterEvent{
					EventType: NeterEventRecv,
					Server:    s.server,
					Session:   s,
					Code:      nil,
				})
				return
			}
		}
	}
}

func (s *TcpSession) SetConnectedCallback(callback func(success bool, session Api.ITcpSession)) {
	s.connectCallback = callback
}

func (s *TcpSession) SetReceivedCallback(callback func(data []byte, offset int, len int, session Api.ITcpSession) int) {
	s.recvCallback = callback
}

func (s *TcpSession) SetDisconnectedCallback(callback func(session Api.ITcpSession)) {
	s.disconCallback = callback
}

func (s *TcpSession) Send(data []byte, immediate bool) {
	s.sender.Write(data, 0, len(data))
	if immediate {
		if s.isSending.CompareAndSwap(false, true) {
			go s.sendAsync()
		}
	}
}

func (s *TcpSession) Close() {
	s.con.Close()
}
