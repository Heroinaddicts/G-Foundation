package Redis

import (
	"GFoundationApi"

	"github.com/redis/go-redis/v9"
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
}

type RedisSession struct {
	rediscli    *RedisCli
	handle      *redis.Client
	requestchan chan *redisRequest
	timeout     int
	isclosed    bool
	stopchan    chan bool
}

func NewRedisSession(rediscli *RedisCli, handle *redis.Client) *RedisSession {
	rs := &RedisSession{
		rediscli:    rediscli,
		handle:      handle,
		requestchan: make(chan *redisRequest, 4096),
		timeout:     10,
		isclosed:    false,
		stopchan:    make(chan bool),
	}

	return rs
}

func (r *RedisSession) dealRequest(req *redisRequest) {
	switch req.requesttype {
	case redisRequestSet:
	case redisRequestGet:
	case redisRequestEval:
	default:
	}
}

func (r *RedisSession) OptLoop() {
	for {
		select {
		case req := <-r.requestchan:
			r.dealRequest(req)
		case <-r.stopchan:
			for req := range r.requestchan {
				r.dealRequest(req)
			}
		}
		if r.isclosed {
			return
		}
	}
}

func (r *RedisSession) Set(key string, data []byte, callback GFoundationApi.OnRedisSetCallback) {
	if r.isclosed {
		callback(false, nil)
		return
	}

	r.requestchan <- &redisRequest{
		requesttype: redisRequestSet,
		key:         key,
		data:        data,
		setcallback: callback,
		getcallback: nil,
		evalinfo:    nil,
	}
}

func (r *RedisSession) Get(key string, callback GFoundationApi.OnRedisGetCallback) {
	if r.isclosed {
		callback(false, nil, nil)
		return
	}

	r.requestchan <- &redisRequest{
		requesttype: redisRequestGet,
		key:         key,
		data:        nil,
		setcallback: nil,
		getcallback: callback,
		evalinfo:    nil,
	}
}

func (r *RedisSession) Eval(script string, keys []string, callback GFoundationApi.OnRedisEvalCallback, args ...any) {
	if r.isclosed {
		callback(false, nil, nil)
		return
	}

	r.requestchan <- &redisRequest{
		requesttype: redisRequestEval,
		data:        nil,
		setcallback: nil,
		getcallback: nil,
		evalinfo: &evalinfo{
			script:   script,
			keys:     keys,
			args:     args,
			callback: callback,
		},
	}
}

func (r *RedisSession) SetDisconnectedCallback(callback GFoundationApi.OnRedisDisconnectedCallback) {

}

// SetTimeOut sets the request timeout in seconds.
func (r *RedisSession) SetTimeOut(timeout int) {
	r.timeout = timeout
}

func (r *RedisSession) Close() {
	r.isclosed = true
	close(r.stopchan)
}
