package Redis

import (
	"GFoundationApi"
	"GFoundationUtils"
	"context"
	"net"
	"strconv"
	"time"
)

const (
	redisConnect    uint8 = 0
	redisOpt        uint8 = 1
	redisDisconnect uint8 = 2
)

type redisResponse struct {
	responsetype    uint8
	request         *redisRequest
	err             error
	data            []byte
	session         *RedisSession
	sessioncallback GFoundationApi.OnRedisConnectCallback
}

type RedisCli struct {
	responseQueue *GFoundationUtils.SPSCQueue[*redisResponse]
}

func NewRedisCli() *RedisCli {
	return &RedisCli{
		responseQueue: GFoundationUtils.NewSPSCQueue[*redisResponse](4096),
	}
}

func (r *RedisCli) Connect(ip string, port uint16, password string, dbindex int, callback GFoundationApi.OnRedisConnectCallback) {
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		rdb, err := dialRedisConnection(ctx, net.JoinHostPort(ip, strconv.Itoa(int(port))), password, dbindex)
		if err != nil {
			r.responseQueue.Push(&redisResponse{
				responsetype:    redisConnect,
				err:             err,
				session:         nil,
				sessioncallback: callback,
			})
			return
		}
		session := NewRedisSession(r, rdb)
		r.responseQueue.Push(&redisResponse{
			responsetype:    redisConnect,
			err:             nil,
			session:         session,
			sessioncallback: callback,
		})
	}()
}

// Update must be called on the main thread to deliver Redis callbacks.
func (r *RedisCli) Update() {
	for {
		response, ok := r.responseQueue.Pop()
		if !ok {
			return
		}
		switch response.responsetype {
		case redisConnect:
			if response.sessioncallback != nil {
				// Pass an actual nil interface when connecting failed.
				if response.session == nil {
					response.sessioncallback(response.err, nil)
				} else {
					response.sessioncallback(response.err, response.session)
				}
			} else if response.session != nil {
				response.session.Close()
			}
		case redisOpt:
			req := response.request
			if req == nil {
				continue
			}
			success := response.err == nil
			switch req.requesttype {
			case redisRequestSet:
				if req.setcallback != nil {
					req.setcallback(success, response.err)
				}
			case redisRequestGet:
				if req.getcallback != nil {
					req.getcallback(success, response.err, response.data)
				}
			case redisRequestEval:
				if req.evalinfo != nil && req.evalinfo.callback != nil {
					req.evalinfo.callback(success, response.err, response.data)
				}
			}
		case redisDisconnect:
			if response.session != nil {
				callback := response.session.disconnectedCallback
				response.session.disconnectedCallback = nil
				if callback != nil {
					callback(response.err == nil, response.err, response.session)
				}
			}
		}
	}
}
