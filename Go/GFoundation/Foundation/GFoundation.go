package Foundation

import (
	"GFoundation/Foundation/Database"
	"GFoundation/Foundation/Http"
	"GFoundation/Foundation/Net"
	"GFoundation/Foundation/Redis"
	"GFoundationApi"
	"sync"
)

type GFoundation struct {
	neter   *Net.Neter
	logic   *GLogic
	timer   *GTimer
	task    *GTask
	dbproxy *Database.GDbProxy
	httper  *Http.GHttp
	redis   *Redis.RedisCli
}

var (
	gfinstance *GFoundation
	gfonce     sync.Once
)

func GFoundationInstance() *GFoundation {
	gfonce.Do(func() {
		gfinstance = &GFoundation{
			neter:   Net.NewNeter(),
			logic:   GLogicInstance(),
			timer:   NewGTimer(),
			task:    NewGTask(),
			dbproxy: Database.NewGDbProxy(),
			httper:  Http.NewGHttp(),
			redis:   Redis.NewRedisCli(),
		}
	})

	return gfinstance
}

func (f *GFoundation) GetNetApi() GFoundationApi.INetApi {
	return f.neter
}

func (f *GFoundation) GetTimerApi() GFoundationApi.ITimerApi {
	return f.timer
}

func (f *GFoundation) GetTaskApi() GFoundationApi.ITaskApi {
	return f.task
}

func (f *GFoundation) GetDbProxyApi() GFoundationApi.IDbProxyApi {
	return f.dbproxy
}

func (f *GFoundation) GetLaunchArgument(name string) string {
	return LauncherParamsInstance().GetString(name)
}

func (f *GFoundation) GetHttpApi() GFoundationApi.IHttpApi {
	return f.httper
}

func (f *GFoundation) GetRedisApi() GFoundationApi.IRedisApi {
	return f.redis
}

func (f *GFoundation) Launch() {
	f.logic.Launch()
}

func (f *GFoundation) Update() {
	f.neter.Update()
	f.timer.Update()
	f.task.Update()
	f.logic.Update()
	f.dbproxy.Update()
	f.httper.Update()
	f.redis.Update()
}

func (l *GFoundation) GetModule(name string) GFoundationApi.IModule {
	return l.logic.GetModule(name)
}
