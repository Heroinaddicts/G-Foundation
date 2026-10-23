package GFoundationApi

import (
	"net/http"
	"time"
)

type OnHttpResponseCallback func(request IHttpRequest, response IHttpResponse)

// HTTP 响应接口
type IHttpResponse interface {
	// 获取 HTTP 状态码，例如 200、404、500
	GetStatusCode() int
	// 获取响应头
	GetHeader() *http.Header
	// 获取响应体
	GetBody() []byte
	// 获取请求是否成功执行
	GetError() error
}

const (
	UnorderHttpRequest uint64 = ^uint64(0)
)

type OnHttpStreamCallback func(request IHttpRequest, data []byte, offset int, len int) int

type IHttpRequest interface {
	IsCanceled() bool
	// 设置请求方法
	SetMethod(method string)
	// 设置请求 URL
	SetURL(rawURL string)
	// 添加 URL 查询参数
	AppendParam(key string, value string)
	// 添加请求头
	AppendHeader(key string, value string)
	// 删除指定请求头
	RemoveHeader(key string)
	// 设置请求体
	SetBody(body []byte) error
	// 设置 JSON 请求体
	SetJsonBody(body any) error
	// 设置超时时间
	SetTimeout(timeout time.Duration)
	// 设置上下文
	SetContext(ctx any)
	// 获取上下文
	GetContext() any
	// 设置请求完成回调
	SetHttpResponseCallback(callback OnHttpResponseCallback)
	// 设置Stream流回调
	SetHttpStreamCallback(callback OnHttpStreamCallback)
	//设置排队mask, 填UnorderRequest代表对时间序无要求, 否则相同的mask, 先调用的会先执行,并且先返回
	SetOrderMask(mask uint64)
	// 提交请求，异步执行
	Send()
	// 取消请求
	Cancel()
}

type IHttpApi interface {
	CreateHttpRequest() IHttpRequest
}
