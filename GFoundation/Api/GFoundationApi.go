package Api

type GFoundationApi interface {
	GetNetApi() INetApi

	Update()
}

type IModule interface {
	Initialize(api GFoundationApi) bool
	Launch(api GFoundationApi) bool
	LaunchFinished(api GFoundationApi)
	Release(api GFoundationApi)
	Update(api GFoundationApi)
}

type GetModule func() IModule
