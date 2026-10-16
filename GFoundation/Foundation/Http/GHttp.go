package Http

import (
	"GFoundationApi"
	"GFoundationUtils"
	"bytes"
	"context"
	"io"
	"net/http"
	"runtime"
	"time"
)

const (
	response_stream    uint8 = 0
	response_completed uint8 = 1
)

type responseInfo struct {
	responsetype uint8
	streamerr    error
	request      *GHttpRequest
	response     *GHttpResponse
}

type GHttp struct {
	unorderOffset       uint64
	requestChannelCount int
	requestChannel      []chan *GHttpRequest
	responseQueue       *GFoundationUtils.SPSCQueue[*responseInfo]
	httpClient          *http.Client
}

func NewGHttp() *GHttp {
	n := runtime.GOMAXPROCS(0)

	g := &GHttp{
		unorderOffset:       0,
		requestChannelCount: n,
		requestChannel:      make([]chan *GHttpRequest, n),
		responseQueue:       GFoundationUtils.NewSPSCQueue[*responseInfo](4096),
		httpClient:          &http.Client{},
	}

	for i := 0; i < g.requestChannelCount; i++ {
		g.requestChannel[i] = make(chan *GHttpRequest, 4096)
		go func(index int) {
			for {
				req := <-g.requestChannel[index]
				if req == nil {
					continue
				}

				resp := g.executeRequest(req)

				g.responseQueue.Push(&responseInfo{
					responsetype: response_completed,
					request:      req,
					response:     resp,
					streamerr:    nil,
				})
			}
		}(i)
	}

	return g
}

func (h *GHttp) executeRequest(req *GHttpRequest) *GHttpResponse {
	if req.IsCanceled() {
		return nil
	}

	method := req.method
	if method == "" {
		method = http.MethodGet
	}

	requestURL, err := req.buildURL()
	if err != nil {
		return &GHttpResponse{err: err}
	}

	var body io.Reader
	if req.body != nil {
		body = bytes.NewReader(req.body)
	}

	timeout := req.timeOut
	if timeout <= 0 {
		timeout = 3 * time.Second
	}

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	httpReq, err := http.NewRequestWithContext(ctx, method, requestURL, body)
	if err != nil {
		return &GHttpResponse{err: err}
	}

	for key, value := range req.headers {
		httpReq.Header.Set(key, value)
	}

	resp, err := h.httpClient.Do(httpReq)
	if err != nil {
		return &GHttpResponse{err: err}
	}
	defer resp.Body.Close()

	buf := make([]byte, 4096)
	for {
		if req.IsCanceled() {
			return nil
		}

		n, readErr := resp.Body.Read(buf)
		if n > 0 {
			req.responseBuffer.Write(buf, 0, n)
			h.responseQueue.Push(&responseInfo{
				responsetype: response_stream,
				request:      req,
				response:     nil,
				streamerr:    nil,
			})
		}

		if readErr != nil {
			if readErr != io.EOF {
				err = readErr
				h.responseQueue.Push(&responseInfo{
					responsetype: response_stream,
					request:      req,
					response:     nil,
					streamerr:    readErr,
				})
			}
			break
		}
	}

	return &GHttpResponse{
		request: req,
		code:    resp.StatusCode,
		header:  &resp.Header,
		err:     err,
	}
}

func (h *GHttp) PushRequest(request *GHttpRequest) {
	index := h.unorderOffset % uint64(h.requestChannelCount)
	h.unorderOffset++
	if request.mask != GFoundationApi.UnorderHttpRequest {
		index = request.mask % uint64(h.requestChannelCount)
	}

	h.requestChannel[int(index)] <- request
}

func (h *GHttp) CreateHttpRequest() GFoundationApi.IHttpRequest {
	return NewGHttpRequest(h)
}

func (h *GHttp) Update() {
	tick := time.Now().UnixMicro()

	for {
		respInfo, ok := h.responseQueue.Pop()
		if !ok {
			return
		}

		switch respInfo.responsetype {
		case response_stream:
			if nil == respInfo.request.streamCallback {
				continue
			}

			respInfo.request.responseBuffer.Read(
				func(data []byte, offset int, length int) int {
					return respInfo.request.streamCallback(
						respInfo.request,
						data,
						offset,
						length,
					)
				},
			)

		case response_completed:
			if respInfo.request.callback != nil {
				respInfo.request.callback(respInfo.request, respInfo.response)
			}
		}

		if time.Now().UnixMicro()-tick > 1000 {
			return
		}
	}
}
