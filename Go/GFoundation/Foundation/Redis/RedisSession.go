package Redis

import (
	"GFoundationApi"
	"encoding/json"
	"errors"
	"fmt"
	"sync"
	"time"
)

const (
	redisRequestGet  uint8 = 0
	redisRequestSet  uint8 = 1
	redisRequestEval uint8 = 2
)

type evalinfo struct {
	script   string
	keys     []string
	args     []any
	callback GFoundationApi.OnRedisEvalCallback
}

type redisRequest struct {
	requesttype uint8
	key         string
	data        []byte
	setcallback GFoundationApi.OnRedisSetCallback
	getcallback GFoundationApi.OnRedisGetCallback
	evalinfo    *evalinfo
	timeout     time.Duration
}

var ErrRedisSessionClosed = errors.New("redis session is closed")
var ErrRedisNil = errors.New("redis: nil")

// Session API methods are main-thread only. The worker consumes request snapshots
// and delivers results through RedisCli.Update instead of invoking callbacks.
type RedisSession struct {
	rediscli             *RedisCli
	handle               *redisConnection
	requestchan          chan *redisRequest
	timeout              int
	isclosed             bool
	loopOnce             sync.Once
	done                 chan struct{}
	disconnectedCallback GFoundationApi.OnRedisDisconnectedCallback
}

func NewRedisSession(rediscli *RedisCli, handle *redisConnection) *RedisSession {
	rs := &RedisSession{
		rediscli:    rediscli,
		handle:      handle,
		requestchan: make(chan *redisRequest, 4096),
		timeout:     10,
		isclosed:    false,
		done:        make(chan struct{}),
	}

	go rs.OptLoop()
	return rs
}

func (r *RedisSession) dealRequest(req *redisRequest) {
	if req == nil {
		return
	}

	timeout := req.timeout
	if timeout <= 0 {
		timeout = 10 * time.Second
	}
	deadline := time.Now().Add(timeout)

	response := &redisResponse{
		responsetype: redisOpt,
		request:      req,
		session:      r,
	}
	switch req.requesttype {
	case redisRequestSet:
		_, response.err = r.handle.do(deadline, "SET", req.key, req.data)
	case redisRequestGet:
		var result any
		result, response.err = r.handle.do(deadline, "GET", req.key)
		if response.err == nil {
			if result == nil {
				response.err = ErrRedisNil
			} else {
				response.data, _ = result.([]byte)
			}
		}
	case redisRequestEval:
		if req.evalinfo == nil {
			response.err = errors.New("redis eval request is missing script information")
			break
		}
		var result any
		args := make([]any, 0, 2+len(req.evalinfo.keys)+len(req.evalinfo.args))
		args = append(args, req.evalinfo.script, len(req.evalinfo.keys))
		for _, key := range req.evalinfo.keys {
			args = append(args, key)
		}
		args = append(args, req.evalinfo.args...)
		result, response.err = r.handle.do(deadline, "EVAL", args...)
		if response.err == nil {
			// Preserve binary/string results; encode numeric and aggregate results as JSON.
			switch value := result.(type) {
			case nil:
			case []byte:
				response.data = value
			case string:
				response.data = []byte(value)
			default:
				response.data, response.err = json.Marshal(redisEvalAggregate(value))
			}
		}
	default:
		response.err = fmt.Errorf("unknown redis request type: %d", req.requesttype)
	}
	r.rediscli.responseQueue.Push(response)
}

func (r *RedisSession) OptLoop() {
	r.loopOnce.Do(func() {
		for {
			req, ok := <-r.requestchan
			if ok {
				r.dealRequest(req)
			} else {
				// 用户关闭了 Session，且排队请求已处理完。
				err := r.handle.Close()
				r.rediscli.responseQueue.Push(&redisResponse{
					responsetype: redisDisconnect,
					err:          err,
					session:      r,
				})
				close(r.done)
				return
			}
		}
	})
}

// enqueue is called on the main thread, like the other session API methods.
func (r *RedisSession) enqueue(req *redisRequest) {
	if r.isclosed {
		r.rediscli.responseQueue.Push(&redisResponse{
			responsetype: redisOpt,
			request:      req,
			err:          ErrRedisSessionClosed,
			session:      r,
		})
		return
	}
	// Snapshot the main-thread setting instead of reading it from the worker.
	req.timeout = time.Duration(r.timeout) * time.Second
	r.requestchan <- req
}

func (r *RedisSession) Set(key string, data []byte, callback GFoundationApi.OnRedisSetCallback) {
	r.enqueue(&redisRequest{
		requesttype: redisRequestSet,
		key:         key,
		data:        append([]byte(nil), data...),
		setcallback: callback,
		getcallback: nil,
		evalinfo:    nil,
	})
}

func (r *RedisSession) Get(key string, callback GFoundationApi.OnRedisGetCallback) {
	r.enqueue(&redisRequest{
		requesttype: redisRequestGet,
		key:         key,
		data:        nil,
		setcallback: nil,
		getcallback: callback,
		evalinfo:    nil,
	})
}

// Eval returns string/binary results verbatim and other supported results as JSON.
func (r *RedisSession) Eval(script string, keys []string, callback GFoundationApi.OnRedisEvalCallback, args ...any) {
	requestArgs := append([]any(nil), args...)
	for i, arg := range requestArgs {
		if data, ok := arg.([]byte); ok {
			requestArgs[i] = append([]byte(nil), data...)
		}
	}
	r.enqueue(&redisRequest{
		requesttype: redisRequestEval,
		data:        nil,
		setcallback: nil,
		getcallback: nil,
		evalinfo: &evalinfo{
			script:   script,
			keys:     append([]string(nil), keys...),
			args:     requestArgs,
			callback: callback,
		},
	})
}

// SetDisconnectedCallback observes session closure after pending requests finish.
// Network/protocol errors invalidate the dedicated connection. Subsequent requests
// report that error until Close; a new connection requires a new RedisSession.
func (r *RedisSession) SetDisconnectedCallback(callback GFoundationApi.OnRedisDisconnectedCallback) {
	r.disconnectedCallback = callback
}

// SetTimeOut sets the execution timeout in seconds for subsequently queued requests.
// Non-positive values restore the default of 10 seconds; queueing time is excluded.
func (r *RedisSession) SetTimeOut(timeout int) {
	if timeout <= 0 {
		timeout = 10
	}
	r.timeout = timeout
}

// Close stops accepting requests and lets the worker finish queued requests
// before closing the client. Completion notifications are delivered by Update.
func (r *RedisSession) Close() {
	if r.isclosed {
		return
	}
	r.isclosed = true
	close(r.requestchan)
}
