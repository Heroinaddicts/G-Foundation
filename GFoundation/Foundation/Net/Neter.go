package Net

import (
	"GFoundation/Api"
	"GFoundation/Utils"
	"fmt"
	"time"
)

const (
	NeterEventAccept     uint8 = 0
	NeterEventConnect    uint8 = 1
	NeterEventDisconnect uint8 = 2
	NeterEventRecv       uint8 = 3
)

type NeterEvent struct {
	EventType uint8
	Server    *TcpServer
	Session   *TcpSession
	Code      error
}

type Neter struct {
	events *Utils.SPSCQueue[NeterEvent]
}

func NewNeter() *Neter {
	return &Neter{
		events: Utils.NewSPSCQueue[NeterEvent](16384),
	}
}

func (r *Neter) PushEvent(event NeterEvent) {
	r.events.Push(event)
}

func (r *Neter) Update() {

	last := time.Now().UnixMicro()
	for {
		v, ok := r.events.Pop()
		if !ok {
			break
		}

		switch v.EventType {
		case NeterEventAccept:
			if v.Code == nil {
				session := v.Session
				if v.Server.connected != nil {
					v.Server.connected(session)
				}
				// Accepted sessions need the same read/send loops as connected clients.
				session.OnConnected(true)
			} else {
				if v.Server != nil && v.Server.err != nil {
					v.Server.err(v.Code)
				}
			}
		case NeterEventConnect:
			if v.Code == nil {
				v.Session.OnConnected(true)
			} else {
				v.Session.OnConnected(false)
			}
		case NeterEventDisconnect:
			v.Session.OnDisconnected()
		case NeterEventRecv:
			v.Session.OnRecive()
		default:
			fmt.Printf("Unknown event type: %d\n", v.EventType)
		}

		if time.Now().UnixMicro()-last > 10000 {
			break
		}
	}

}
func (r *Neter) LaunchTcpServer(ip string, port uint16, accepted func(session Api.ITcpSession), err func(err error)) Api.ITcpServer {
	s := NewTcpServer(ip, port, accepted, err, r)
	return s
}

func (r *Neter) LaunchTcpClient(ip string, port uint16) Api.ITcpSession {
	// Implementation for launching TCP client
	return nil
}
