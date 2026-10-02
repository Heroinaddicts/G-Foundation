package Api

import (
	"plugin"
)

type GFoundationApi interface {
	GetNetApi() INetApi

	Update()
}

func CreateApi(path string) GFoundationApi {
	p, err := plugin.Open(path)
	if err != nil {
		panic(err)
	}

	symbol, err := p.Lookup("CreateGFoundationApi")
	if err != nil {
		panic(err)
	}

	getApi := symbol.(func() GFoundationApi)

	api := getApi()
	return api
}
