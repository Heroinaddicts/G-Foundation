package Redis

import (
	"GFoundationApi"
	"GFoundationUtils"
	"context"
	"fmt"
	"strconv"
	"time"

	"github.com/redis/go-redis/v9"
)

const (
	redisconnect uint8 = 0
	redisopt     uint8 = 1
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
		rdb := redis.NewClient(&redis.Options{
			Addr:     ip + ":" + strconv.Itoa(int(port)),
			Password: password,
			DB:       dbindex,
		})

		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel() // 一定要cancel释放资源
		// 测试连通性
		pong, err := rdb.Ping(ctx).Result()
		if err != nil {
			rdb.Close()
			r.responseQueue.Push(&redisResponse{
				responsetype:    redisconnect,
				err:             err,
				session:         nil,
				sessioncallback: callback,
			})
			return
		}
		fmt.Println("ping result:", pong)

		session := NewRedisSession(r, rdb)
		r.responseQueue.Push(&redisResponse{
			responsetype:    redisconnect,
			err:             nil,
			session:         session,
			sessioncallback: callback,
		})
	}()
}
