package StressKit

import (
	"GFoundationApi"
	"encoding/binary"
	"fmt"
	"math/rand"
	"time"
)

type tcpPeer struct {
	id                          uint64
	session                     GFoundationApi.ITcpSession
	connect, close              *Sample
	requests                    map[uint64]*Sample
	order                       []uint64
	readIndex                   int
	connected, retired, closing bool
	expires, nextDue            time.Time
}
type tcpAccepted struct {
	id      uint64
	session GFoundationApi.ITcpSession
}

// TCP drives clients with INetApi in both modes. Server mode additionally
// loads INetApi's listener/accept/echo path; client mode uses Fixtures' echo.
type TCP struct {
	Base
	Host                            string
	Port                            uint16
	Window                          int
	SelfServer                      bool
	server                          GFoundationApi.ITcpServer
	serverClosing                   bool
	clients                         map[uint64]*tcpPeer
	accepted                        map[GFoundationApi.ITcpSession]*tcpAccepted
	sequence, requestID, acceptedID uint64
	random                          *rand.Rand
}

func (m *TCP) InitTCP(api GFoundationApi.GFoundationApi, name, prefix string, server bool) bool {
	if !m.Init(api, name, prefix, 1000, 1000) {
		return false
	}
	m.SelfServer = server
	m.Host = Text(api, prefix, "host", "127.0.0.1")
	port := 18889
	if server {
		port = 18890
	}
	p, err := Int(api, prefix, "port", port, 1, 65535)
	if err != nil {
		fmt.Println(err)
		return false
	}
	m.Port = uint16(p)
	m.Window, err = Int(api, prefix, "window", 64, 1, 100000)
	if err != nil {
		fmt.Println(err)
		return false
	}
	m.clients = map[uint64]*tcpPeer{}
	m.accepted = map[GFoundationApi.ITcpSession]*tcpAccepted{}
	m.random = m.C.Random()
	return true
}
func (m *TCP) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if !m.Start(map[string]any{"host": m.Host, "port": m.Port, "window_per_session": m.Window, "framework_server": m.SelfServer}) {
		return
	}
	if m.SelfServer {
		m.server = api.GetNetApi().LaunchTcpServer(m.Host, m.Port, m.onAccept, func(err error) {
			if !m.serverClosing {
				m.R.Fail("TCP server: " + err.Error())
			}
		})
		if m.server == nil || m.R.Faults > 0 {
			m.Finish()
			return
		}
	}
	api.GetTimerApi().StartTimer(m.OnTimerConnectTCP, m, nil, 10, GFoundationApi.Unlimited, 10)
}
func (m *TCP) onAccept(session GFoundationApi.ITcpSession) {
	if m.R.Finishing {
		session.Close()
		return
	}
	m.acceptedID++
	a := &tcpAccepted{id: m.acceptedID, session: session}
	m.accepted[session] = a
	session.SetContext(a)
	m.R.Count("server_accepted", 1)
	m.R.Peak("peak_server_sessions", uint64(len(m.accepted)))
	session.SetConnectedCallback(func(ok bool, s GFoundationApi.ITcpSession) {
		if !ok || s.GetContext() != a {
			m.R.Fail("accepted session connect/context mismatch")
		}
	})
	session.SetDisconnectedCallback(func(s GFoundationApi.ITcpSession) { delete(m.accepted, s); m.R.Count("server_disconnected", 1) })
	session.SetReceivedCallback(func(data []byte, offset, n int, s GFoundationApi.ITcpSession) int {
		if m.R.Finishing {
			return n
		}
		return m.frames(data, offset, n, func(b []byte) error {
			id := binary.BigEndian.Uint64(b[4:])
			sample := m.R.Begin("SERVER_ECHO_ENQUEUE", id, "receive_to_send_return")
			err := CheckFrame(b)
			if err == nil {
				s.Send(b, true)
				m.R.Count("server_received_bytes", uint64(len(b)))
			}
			m.R.Complete(sample, err)
			return err
		})
	})
}
func (m *TCP) frames(data []byte, offset, n int, consume func([]byte) error) int {
	b := data[offset : offset+n]
	used := 0
	for len(b) >= 4 {
		size := int(binary.BigEndian.Uint32(b))
		if size < 24 || size > m.C.Payload+64 {
			m.R.Fail(fmt.Sprintf("invalid TCP frame length %d", size))
			return n
		}
		if len(b) < size {
			break
		}
		if err := consume(b[:size]); err != nil {
			m.R.Fail(err.Error())
		}
		b = b[size:]
		used += size
	}
	return used
}
func (m *TCP) OnTimerConnectTCP(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	n, end := m.P.Tick(m.R, count)
	for i := 0; i < n; i++ {
		if len(m.clients) >= m.C.MaxActive {
			m.R.Count("admissions_skipped_capacity", uint64(n-i))
			break
		}
		m.connect()
	}
	if end {
		m.API.GetTimerApi().StopTimer(m.OnTimerConnectTCP, m)
	}
}
func (m *TCP) connect() {
	m.sequence++
	c := &tcpPeer{id: m.sequence, requests: map[uint64]*Sample{}}
	c.connect = m.R.Begin("CONNECT", c.id, "call_to_connected_callback")
	m.clients[c.id] = c
	m.R.Count("admissions_issued", 1)
	m.R.Peak("peak_client_sessions", uint64(len(m.clients)))
	s := m.API.GetNetApi().LaunchTcpClient(m.Host, m.Port)
	c.session = s
	s.SetContext(c)
	s.SetConnectedCallback(func(ok bool, session GFoundationApi.ITcpSession) {
		if m.R.Finishing {
			session.Close()
			return
		}
		if !ok {
			m.R.Complete(c.connect, fmt.Errorf("TCP connect failed"))
			session.Close()
			delete(m.clients, c.id)
			return
		}
		if session.GetContext() != c {
			m.R.Fail("client context mismatch")
		}
		c.connected = true
		m.R.Complete(c.connect, nil)
		c.expires = time.Now().Add(m.C.Lifetime(m.random))
		c.nextDue = time.Now().Add(time.Duration(m.C.PeriodMS) * time.Millisecond)
		m.API.GetTimerApi().StartTimer(m.OnTimerTCPSend, session, c, m.C.PeriodMS, GFoundationApi.Unlimited, m.C.PeriodMS)
	})
	s.SetReceivedCallback(func(data []byte, offset, n int, session GFoundationApi.ITcpSession) int {
		if m.R.Finishing {
			return n
		}
		return m.frames(data, offset, n, func(b []byte) error {
			id := binary.BigEndian.Uint64(b[4:])
			sample, ok := c.requests[id]
			if !ok {
				return fmt.Errorf("unexpected/duplicate TCP response id=%d", id)
			}
			err := CheckFrame(b)
			if c.readIndex >= len(c.order) || c.order[c.readIndex] != id {
				err = fmt.Errorf("TCP response order mismatch id=%d", id)
			}
			c.readIndex++
			delete(c.requests, id)
			m.R.Complete(sample, err)
			m.R.Count("received_bytes", uint64(len(b)))
			if c.readIndex == len(c.order) {
				c.order = c.order[:0]
				c.readIndex = 0
			}
			return nil
		})
	})
	s.SetDisconnectedCallback(func(session GFoundationApi.ITcpSession) {
		if m.R.Finishing {
			return
		}
		m.API.GetTimerApi().StopTimer(m.OnTimerTCPSend, session)
		if !c.closing {
			m.R.Fail(fmt.Sprintf("unexpected disconnect session=%d pending=%d", c.id, len(c.requests)))
		}
		for _, sample := range c.requests {
			m.R.Complete(sample, fmt.Errorf("disconnected before echo"))
		}
		clear(c.requests)
		if c.close != nil {
			m.R.Complete(c.close, nil)
		}
		delete(m.clients, c.id)
		m.R.Count("client_disconnected", 1)
	})
}
func (m *TCP) OnTimerTCPSend(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	s := data.(GFoundationApi.ITcpSession)
	c := context.(*tcpPeer)
	if time.Now().After(c.expires) {
		c.retired = true
		m.API.GetTimerApi().StopTimer(m.OnTimerTCPSend, s)
		return
	}
	period := time.Duration(m.C.PeriodMS) * time.Millisecond
	now := time.Now()
	m.R.Observe("SEND_TIMER_LAG", c.id, "scheduled_to_callback", c.nextDue, nil)
	if lag := now.Sub(c.nextDue); lag >= period {
		missed := uint64(lag / period)
		m.R.Count("send_periods_missed", missed)
		c.nextDue = c.nextDue.Add(time.Duration(missed) * period)
	}
	c.nextDue = c.nextDue.Add(period)
	for i := 0; i < m.C.PerTick; i++ {
		if len(c.requests) >= m.Window {
			m.R.Count("commands_skipped_window", uint64(m.C.PerTick-i))
			break
		}
		m.requestID++
		id := m.requestID
		frame := Frame(id, m.C.Payload+int(id%3)*8)
		sample := m.R.Begin("ECHO_RTT", c.id, "send_to_echo_callback")
		c.requests[id] = sample
		c.order = append(c.order, id)
		if id%2 == 0 {
			half := len(frame) / 2
			s.Send(frame[:half], false)
			s.Send(frame[half:], true)
		} else {
			s.Send(frame, true)
		}
		m.R.Count("sent_bytes", uint64(len(frame)))
	}
}
func (m *TCP) closePeer(c *tcpPeer) {
	if c.closing {
		return
	}
	c.closing = true
	m.API.GetTimerApi().StopTimer(m.OnTimerTCPSend, c.session)
	c.close = m.R.Begin("CLOSE", c.id, "call_to_disconnected_callback")
	c.session.Close()
	c.session.Close()
	// Post-close Send must be a harmless no-op in this API (no return value).
	c.session.Send(Frame(0, 24), true)
}
func (m *TCP) Update(api GFoundationApi.GFoundationApi) {
	if m.R == nil {
		return
	}
	m.Poll()
	if m.R.Finishing {
		return
	}
	for _, c := range m.clients {
		if c.retired && !c.closing && len(c.requests) == 0 {
			m.closePeer(c)
		}
	}
	if m.Expired() {
		m.R.Fail("TCP drain deadline exceeded")
		m.Release(api)
		return
	}
	if m.P.Closed && len(m.clients) == 0 && len(m.accepted) == 0 {
		if m.R.Counters["commands_skipped_window"]+m.R.Counters["send_periods_missed"] > 0 {
			m.R.Fail("TCP load was not fully delivered; see counters")
		}
		m.closeServer()
		m.Finish()
	}
}
func (m *TCP) closeServer() {
	if m.server != nil && !m.serverClosing {
		m.serverClosing = true
		m.server.Close()
	}
}
func (m *TCP) Release(api GFoundationApi.GFoundationApi) {
	if m.R == nil || m.R.Finishing {
		return
	}
	api.GetTimerApi().StopTimer(m.OnTimerConnectTCP, m)
	for _, c := range m.clients {
		api.GetTimerApi().StopTimer(m.OnTimerTCPSend, c.session)
		c.session.Close()
	}
	for s := range m.accepted {
		s.Close()
	}
	m.closeServer()
	m.Finish()
}
