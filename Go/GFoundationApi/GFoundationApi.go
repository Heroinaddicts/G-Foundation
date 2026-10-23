package GFoundationApi

type GFoundationApi interface {
	GetNetApi() INetApi
	GetTimerApi() ITimerApi
	GetTaskApi() ITaskApi
	GetDbProxyApi() IDbProxyApi
	GetHttpApi() IHttpApi
	GetRedisApi() IRedisApi

	GetLaunchArgument(name string) string

	GetModule(name string) IModule
}

type IModule interface {
	Initialize(api GFoundationApi) bool
	Launch(api GFoundationApi) bool
	LaunchFinished(api GFoundationApi)
	Release(api GFoundationApi)
	Update(api GFoundationApi)
}

type GetModule func() IModule
