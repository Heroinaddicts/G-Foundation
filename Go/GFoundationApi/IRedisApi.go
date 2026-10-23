package GFoundationApi

type OnRedisSetCallback func(success bool, err error)
type OnRedisGetCallback func(success bool, err error, data []byte)

type OnRedisEvalCallback func(success bool, err error, data []byte)
type OnRedisDisconnectedCallback func(success bool, err error, session IRedisSession)
type IRedisSession interface {
	Set(key string, data []byte, callback OnRedisSetCallback)
	Get(key string, callback OnRedisGetCallback)
	Eval(script string, keys []string, callback OnRedisEvalCallback, args ...any)

	SetDisconnectedCallback(callback OnRedisDisconnectedCallback)
	SetTimeOut(timeout int)
	Close()
}

type OnRedisConnectCallback func(err error, session IRedisSession)
type IRedisApi interface {
	Connect(ip string, port uint16, password string, dbindex int, callback OnRedisConnectCallback)
}
