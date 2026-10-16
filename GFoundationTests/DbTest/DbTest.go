package main

import "GFoundationApi"

type DbTest struct {
	api GFoundationApi.GFoundationApi
}

func (m *DbTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	m.api = api
	return true
}

func (m *DbTest) Launch(api GFoundationApi.GFoundationApi) bool {
	return true
}

func (m *DbTest) LaunchFinished(api GFoundationApi.GFoundationApi) {

}

func (m *DbTest) Release(api GFoundationApi.GFoundationApi) {

}

func (m *DbTest) Update(api GFoundationApi.GFoundationApi) {

}

func GetModule() GFoundationApi.IModule {
	return &DbTest{}
}
