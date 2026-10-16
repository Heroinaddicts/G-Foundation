package main

import (
	GFoundationApi "GFoundationApi"
	"GFoundationTests/Interface"
	"fmt"
)

type FindModuleTest struct {
	api GFoundationApi.GFoundationApi
}

func (m *FindModuleTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	m.api = api
	return true
}

func (m *FindModuleTest) Launch(api GFoundationApi.GFoundationApi) bool {
	return true
}

func (m *FindModuleTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	i, ok := api.GetModule("HttpTest").(Interface.IHttpTest)
	if ok {
		fmt.Printf("%d\n", i.Test())
	} else {
		fmt.Printf("Get HttpTest Module error\n")
	}
}

func (m *FindModuleTest) Release(api GFoundationApi.GFoundationApi) {

}

func (m *FindModuleTest) Update(api GFoundationApi.GFoundationApi) {

}

func GetModule() GFoundationApi.IModule {
	return &FindModuleTest{}
}
