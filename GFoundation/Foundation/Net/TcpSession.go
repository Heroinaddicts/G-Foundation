package Net

import (
	"GFoundation/Api"
	"GFoundation/Utils"
	"fmt"
	"net"
	"time"
)

const (
	NeedSend uint8 = 0
	OutSend  uint8 = 1
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

	sendChannel chan uint8
}

func NewTcpSession(con net.Conn, server *TcpServer, neter *Neter) *TcpSession {
	s := &TcpSession{
		recver:          Utils.NewSPSCBuffer(16384, 16384),
		sender:          Utils.NewSPSCBuffer(16384, 16384),
		con:             con,
		neter:           neter,
		server:          server,
		connectCallback: nil,
		recvCallback:    nil,
		disconCallback:  nil,
		sendChannel:     make(chan uint8),
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

func (s *TcpSession) sendLoop() {
	for {
		sending := <-s.sendChannel
		if sending == 0 {
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
					break
				}
			}
		} else {
			return
		}
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

	go s.readLoop()
	go s.sendLoop()
}

func (s *TcpSession) OnDisconnected() {
	if s.disconCallback != nil {
		s.sendChannel <- OutSend
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
		s.sendChannel <- NeedSend
	}
}

func (s *TcpSession) Close() {
	s.sendChannel <- OutSend
	s.con.Close()
}
