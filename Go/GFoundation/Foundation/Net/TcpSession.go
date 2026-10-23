package Net

import (
	"GFoundationApi"
	"GFoundationUtils"
	"fmt"
	"net"
	"sync"
	"time"
)

const (
	NeedSend uint8 = 0
)

type TcpSession struct {
	recver *GFoundationUtils.SPSCBuffer
	sender *GFoundationUtils.SPSCBuffer
	con    net.Conn
	neter  *Neter
	server *TcpServer

	connectCallback func(success bool, session GFoundationApi.ITcpSession)
	recvCallback    func(data []byte, offset int, len int, session GFoundationApi.ITcpSession) int
	disconCallback  func(session GFoundationApi.ITcpSession)

	sendChannel chan uint8
	closed      chan struct{}
	closeOnce   sync.Once

	context     any
	isConnected bool
}

func NewTcpSession(con net.Conn, server *TcpServer, neter *Neter) *TcpSession {
	s := &TcpSession{
		recver:          GFoundationUtils.NewSPSCBuffer(16384, 16384),
		sender:          GFoundationUtils.NewSPSCBuffer(16384, 16384),
		con:             con,
		neter:           neter,
		server:          server,
		connectCallback: nil,
		recvCallback:    nil,
		disconCallback:  nil,
		sendChannel:     make(chan uint8, 1),
		closed:          make(chan struct{}),
		context:         nil,
		isConnected:     false,
	}

	return s
}

func (s *TcpSession) ConnectAsync(ip string, port uint16) {
	if s.con != nil {
		panic("TcpSession already connected")
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
	}()
}

func (s *TcpSession) sendLoop(sendChan chan uint8) {
	for {
		select {
		case <-s.closed:
			return
		case <-sendChan:
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
	if s.isConnected {
		return
	}

	s.isConnected = true

	if s.connectCallback != nil {
		s.connectCallback(sucess, s)
		s.connectCallback = nil

		if sucess {
			go s.readLoop()
			go s.sendLoop(s.sendChannel)
		}
	}
}

func (s *TcpSession) OnDisconnected() {
	if false == s.isConnected {
		return
	}

	fmt.Print("TcpSession OnDisconnected\n")
	s.Close()
	if s.disconCallback != nil {
		s.disconCallback(s)
		s.disconCallback = nil
	}
	s.isConnected = false
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

func (s *TcpSession) SetConnectedCallback(callback func(success bool, session GFoundationApi.ITcpSession)) {
	s.connectCallback = callback
}

func (s *TcpSession) SetReceivedCallback(callback func(data []byte, offset int, len int, session GFoundationApi.ITcpSession) int) {
	s.recvCallback = callback
}

func (s *TcpSession) SetDisconnectedCallback(callback func(session GFoundationApi.ITcpSession)) {
	s.disconCallback = callback
}

func (s *TcpSession) Send(data []byte, immediate bool) {
	if nil == s.sendChannel {
		return
	}

	s.sender.Write(data, 0, len(data))
	if immediate {
		select {
		case s.sendChannel <- NeedSend:
		default: // A pending notification already covers the queued data.
		}
	}
}

func (s *TcpSession) Close() {
	if false == s.isConnected {
		return
	}

	s.closeOnce.Do(func() {
		close(s.closed)
		close(s.sendChannel)
		s.sendChannel = nil
		if s.con != nil {
			_ = s.con.Close()
		}
	})
}

func (s *TcpSession) SetContext(context any) {
	s.context = context
}

func (s *TcpSession) GetContext() any {
	return s.context
}
