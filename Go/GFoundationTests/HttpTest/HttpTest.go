package main

import (
	"GFoundationApi"
	"GFoundationTests/StressKit"
	"bytes"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"reflect"
	"strconv"
	"strings"
	"time"
)

type httpCall struct {
	id               uint64
	kind             int
	req              GFoundationApi.IHttpRequest
	sample           *StressKit.Sample
	expected, stream []byte
	order            uint64
}
type HttpTest struct {
	StressKit.Base
	url                                string
	sequence, orderedSent, orderedDone uint64
	active                             map[uint64]*httpCall
}

func (m *HttpTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	if !m.Init(api, "HttpTest", "http", 1000, 1000) {
		return false
	}
	m.url = strings.TrimRight(StressKit.Text(api, "http", "url", "http://127.0.0.1:18888"), "/")
	m.active = map[uint64]*httpCall{}
	return true
}
func (m *HttpTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	if m.Start(map[string]any{"fixture_url": m.url}) {
		m.Deadline = m.R.Started.Add(time.Duration(m.C.Seconds+m.C.DrainSeconds) * time.Second)
		api.GetTimerApi().StartTimer(m.OnTimerRequest, m, nil, 10, GFoundationApi.Unlimited, 10)
	}
}
func (m *HttpTest) OnTimerRequest(state uint8, count int, data, context any, natural bool) {
	if state != GFoundationApi.TimerStateBeat || m.R.Finishing {
		return
	}
	n, end := m.P.Tick(m.R, count)
	for i := 0; i < n; i++ {
		if len(m.active) >= m.C.MaxActive {
			m.R.Count("admissions_skipped_capacity", uint64(n-i))
			break
		}
		m.send()
	}
	if end {
		m.API.GetTimerApi().StopTimer(m.OnTimerRequest, m)
	}
}
func (m *HttpTest) send() {
	m.sequence++
	c := &httpCall{id: m.sequence, kind: int((m.sequence - 1) % 9)}
	r := m.API.GetHttpApi().CreateHttpRequest()
	c.req = r
	op := []string{"GET", "POST", "JSON", "STREAM", "ORDERED", "CANCEL_QUEUED", "TIMEOUT", "STATUS_503", "CANCEL_RUNNING"}[c.kind]
	m.active[c.id] = c
	m.R.Count("admissions_issued", 1)
	m.R.Peak("peak_active", uint64(len(m.active)))
	r.SetMethod(http.MethodGet)
	r.SetURL(m.url + "/echo")
	r.SetTimeout(time.Duration(m.C.TimeoutMS) * time.Millisecond)
	r.SetOrderMask(GFoundationApi.UnorderHttpRequest)
	r.SetContext(c)
	r.AppendParam("id", strconv.FormatUint(c.id, 10))
	r.AppendParam("size", strconv.Itoa(m.C.Payload))
	r.AppendParam("echo", "空 格+&=/?")
	r.AppendHeader("X-Stress-Id", strconv.FormatUint(c.id, 10))
	r.AppendHeader("X-Stress-Remove", "must-not-arrive")
	r.RemoveHeader("X-Stress-Remove")
	c.expected = StressKit.Payload(c.id, m.C.Payload)
	switch c.kind {
	case 1, 4:
		r.SetMethod(http.MethodPost)
		if err := r.SetBody(c.expected); err != nil {
			m.R.Fail(err.Error())
		}
		if c.kind == 4 {
			r.SetOrderMask(37)
			m.orderedSent++
			c.order = m.orderedSent
		}
	case 2:
		r.SetMethod(http.MethodPost)
		r.SetURL(m.url + "/json")
		body := map[string]any{"id": c.id, "payload": c.expected}
		var err error
		c.expected, err = json.Marshal(body)
		if err != nil {
			m.R.Fail(err.Error())
		}
		if err = r.SetJsonBody(body); err != nil {
			m.R.Fail(err.Error())
		}
	case 3:
		r.SetURL(m.url + "/stream")
		r.SetHttpStreamCallback(func(req GFoundationApi.IHttpRequest, b []byte, off, n int) int {
			if m.R.Finishing {
				return n
			}
			if req.GetContext() != c {
				m.R.Fail("stream context mismatch")
			}
			if len(c.stream)+n > m.C.Payload {
				m.R.Fail("stream body exceeds expected size")
			} else {
				c.stream = append(c.stream, b[off:off+n]...)
			}
			m.R.Count("stream_callbacks", 1)
			return n
		})
	case 5:
		r.Cancel()
	case 6:
		r.SetURL(m.url + "/delay")
		r.AppendParam("ms", "40")
		r.SetTimeout(5 * time.Millisecond)
	case 7:
		r.SetURL(m.url + "/status")
	case 8:
		r.SetURL(m.url + "/stream")
		r.AppendParam("chunk_delay_ms", "10")
		// Cancel on the main thread after stream data actually arrives.
		r.SetHttpStreamCallback(func(req GFoundationApi.IHttpRequest, b []byte, off, n int) int {
			if m.R.Finishing {
				return n
			}
			if !req.IsCanceled() {
				req.Cancel()
				m.R.Count("running_cancellations", 1)
			}
			return n
		})
	}
	r.SetHttpResponseCallback(func(req GFoundationApi.IHttpRequest, resp GFoundationApi.IHttpResponse) {
		if m.R.Finishing {
			return
		}
		var err error
		if resp != nil && reflect.ValueOf(resp).Kind() == reflect.Pointer && reflect.ValueOf(resp).IsNil() {
			m.R.Fail(fmt.Sprintf("HTTP callback contains typed-nil %T", resp))
			resp = nil
		}
		switch c.kind {
		case 5, 8:
			// Cancel is best effort: a completed response is also a valid outcome.
			if c.kind == 5 && !req.IsCanceled() {
				err = fmt.Errorf("Cancel did not set the request flag")
			}
			if resp == nil {
				m.R.Count("cancel_nil_responses", 1)
			} else if resp.GetError() != nil {
				m.R.Count("cancel_error_responses", 1)
			} else if resp.GetStatusCode() != http.StatusOK {
				err = fmt.Errorf("cancel response status=%d want=200", resp.GetStatusCode())
			} else {
				m.R.Count("cancel_completed_responses", 1)
			}
		case 6:
			if resp == nil || resp.GetError() == nil {
				err = fmt.Errorf("expected request timeout")
			} else if ne, ok := resp.GetError().(net.Error); !ok || !ne.Timeout() {
				err = fmt.Errorf("expected timeout, received %v", resp.GetError())
			}
		default:
			status := 200
			if c.kind == 7 {
				status = 503
			}
			if resp == nil {
				err = fmt.Errorf("nil HTTP response")
			} else if resp.GetError() != nil {
				err = resp.GetError()
			} else if resp.GetStatusCode() != status {
				err = fmt.Errorf("status=%d want=%d", resp.GetStatusCode(), status)
			} else {
				h := resp.GetHeader()
				if h == nil || h.Get("X-Stress-Id") != strconv.FormatUint(c.id, 10) {
					err = fmt.Errorf("response header mismatch")
				}
				body := resp.GetBody()
				if c.kind == 3 {
					body = append(c.stream, body...)
				}
				if !bytes.Equal(body, c.expected) {
					err = fmt.Errorf("response body mismatch len=%d want=%d", len(body), len(c.expected))
				}
			}
		}
		if req.GetContext() != c {
			err = fmt.Errorf("request context mismatch")
		}
		if c.kind == 4 {
			m.orderedDone++
			if c.order != m.orderedDone {
				err = fmt.Errorf("ordered HTTP callback out of order")
			}
		}
		m.R.Complete(c.sample, err)
		delete(m.active, c.id)
	})
	c.sample = m.R.Begin(op, c.id, "send_to_callback")
	r.Send()
}
func (m *HttpTest) Update(api GFoundationApi.GFoundationApi) {
	if m.R == nil {
		return
	}
	m.Poll()
	if m.R.Finishing {
		return
	}
	if m.Expired() {
		m.R.Fail("HTTP drain deadline exceeded")
		m.Release(api)
		return
	}
	if m.P.Closed && len(m.active) == 0 {
		m.Finish()
	}
}
func (m *HttpTest) Release(api GFoundationApi.GFoundationApi) {
	if m.R == nil || m.R.Finishing {
		return
	}
	api.GetTimerApi().StopTimer(m.OnTimerRequest, m)
	for _, c := range m.active {
		c.req.Cancel()
	}
	m.Finish()
}

// Preserve the existing Interface.IHttpTest contract used by FindModuleTest.
func (m *HttpTest) Test() int           { return 1000 }
func GetModule() GFoundationApi.IModule { return &HttpTest{} }
