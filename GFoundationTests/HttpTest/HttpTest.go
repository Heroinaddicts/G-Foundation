package main

import (
	GFoundationApi "GFoundationApi"
	GFoundationUtils "GFoundationUtils"
	"encoding/json"
	"fmt"
	"os"
	"time"
)

type HttpTest struct {
	api          GFoundationApi.GFoundationApi
	streamParser DeepSeekSSEParser
}

func (m *HttpTest) Initialize(api GFoundationApi.GFoundationApi) bool {
	m.api = api
	return true
}

func (m *HttpTest) Launch(api GFoundationApi.GFoundationApi) bool {
	return true
}

func (m *HttpTest) LaunchFinished(api GFoundationApi.GFoundationApi) {
	request := api.GetHttpApi().CreateHttpRequest()

	request.SetMethod("POST")
	request.SetURL("https://api.deepseek.com/chat/completions")
	request.AppendHeader("Authorization", "Bearer "+os.Getenv("DEEPSEEK_API_KEY"))
	request.AppendHeader("Content-Type", "application/json")
	request.SetTimeout(120 * time.Second)

	body := map[string]any{
		"model": "deepseek-flash",
		"messages": []map[string]string{
			{
				"role":    "system",
				"content": "我要你辅助我开始机器人自研。",
			},
			{
				"role":    "user",
				"content": "如果你是机器人大脑里的模型矩阵中的主推力大模型,你该如何驱动和控制机器人行走,空间构建和寻路算法使用slam",
			},
		},
		// 显式开启思考模式
		"thinking": map[string]string{
			"type": "enabled",
		},

		// 设置思考强度
		"reasoning_effort": "high",

		// 开启流式响应
		"stream": true,
	}

	bodyBytes, err := json.Marshal(body)
	if err != nil {
		panic(err)
	}

	request.SetBody(bodyBytes)

	request.SetHttpStreamCallback(func(
		request GFoundationApi.IHttpRequest,
		data []byte,
		offset int,
		length int,
	) int {
		chunk := data[offset : offset+length]
		fmt.Printf("%s\n", string(chunk))

		file, err := os.OpenFile(
			GFoundationUtils.GetCurrentExeDir()+"/Log.log",
			os.O_CREATE|os.O_WRONLY|os.O_APPEND,
			0644,
		)
		if err != nil {
			panic(err)
		}
		defer file.Close()

		_, err = file.Write(chunk)
		if err != nil {
			panic(err)
		}

		return length
	})

	request.SetHttpResponseCallback(func(
		request GFoundationApi.IHttpRequest,
		response GFoundationApi.IHttpResponse,
	) {
		if response == nil {
			fmt.Println("\n请求被取消,或者没有获得响应。")
			return
		}

		if response.GetError() != nil {
			fmt.Printf("\nHTTP 请求失败：%v\n", response.GetError())
			return
		}

		fmt.Printf(
			"\n\nHTTP 请求完成，状态码：%d\n%s\n",
			response.GetStatusCode(),
			string(response.GetBody()),
		)
	})

	//request.Send()
}

func (m *HttpTest) Release(api GFoundationApi.GFoundationApi) {

}

func (m *HttpTest) Update(api GFoundationApi.GFoundationApi) {

}

func (m *HttpTest) Test() int {
	return 1000
}

func GetModule() GFoundationApi.IModule {
	return &HttpTest{}
}
