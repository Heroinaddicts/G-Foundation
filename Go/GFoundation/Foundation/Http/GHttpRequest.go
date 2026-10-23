package Http

import (
	"GFoundationApi"
	"GFoundationUtils"
	"encoding/json"
	"net/http"
	"net/url"
	"sync/atomic"
	"time"
)

type GHttpResponse struct {
	request *GHttpRequest
	code    int
	header  *http.Header
	err     error
}

// 获取 HTTP 状态码，例如 200、404、500
func (r *GHttpResponse) GetStatusCode() int {
	return r.code
}

// 获取响应头
func (r *GHttpResponse) GetHeader() *http.Header {
	return r.header
}

// 获取响应体
func (r *GHttpResponse) GetBody() []byte {
	if nil == r.request || nil == r.request.responseBuffer {
		return nil
	}

	return r.request.responseBuffer.GetDataSlice()
}

// 获取请求是否成功执行
func (r *GHttpResponse) GetError() error {
	return r.err
}

func (r *GHttpRequest) buildURL() (string, error) {
	u, err := url.Parse(r.url)
	if err != nil {
		return "", err
	}

	query := u.Query()
	for key, value := range r.params {
		query.Set(key, value)
	}
	u.RawQuery = query.Encode()

	return u.String(), nil
}

type GHttpRequest struct {
	isCanceled     atomic.Bool
	method         string
	url            string
	params         map[string]string
	headers        map[string]string
	body           []byte
	timeOut        time.Duration
	callback       GFoundationApi.OnHttpResponseCallback
	context        any
	mask           uint64
	ghttp          *GHttp
	responseBuffer *GFoundationUtils.SPSCBuffer
	streamCallback GFoundationApi.OnHttpStreamCallback
}

func NewGHttpRequest(ghttp *GHttp) *GHttpRequest {
	return &GHttpRequest{
		isCanceled:     atomic.Bool{},
		params:         nil,
		headers:        nil,
		body:           nil,
		timeOut:        3 * time.Second,
		callback:       nil,
		context:        nil,
		mask:           GFoundationApi.UnorderHttpRequest,
		ghttp:          ghttp,
		responseBuffer: GFoundationUtils.NewSPSCBuffer(1024, 1024),
		streamCallback: nil,
	}
}

func (r *GHttpRequest) IsCanceled() bool {
	return r.isCanceled.Load()
}

// 设置请求方法
func (r *GHttpRequest) SetMethod(method string) {
	r.method = method
}

// 设置请求 URL
func (r *GHttpRequest) SetURL(url string) {
	r.url = url
}

// 添加 URL 查询参数
func (r *GHttpRequest) AppendParam(key string, value string) {
	if nil == r.params {
		r.params = make(map[string]string)
	}
	r.params[key] = value
}

// 添加请求头
func (r *GHttpRequest) AppendHeader(key string, value string) {
	if nil == r.headers {
		r.headers = make(map[string]string)
	}
	r.headers[key] = value
}

// 删除指定请求头
func (r *GHttpRequest) RemoveHeader(key string) {
	delete(r.headers, key)
}

// 设置请求体
func (r *GHttpRequest) SetBody(body []byte) error {
	r.body = body
	return nil
}

// 设置 JSON 请求体
func (r *GHttpRequest) SetJsonBody(body any) error {
	data, err := json.Marshal(body)
	if err != nil {
		return err
	}

	r.body = data
	if nil == r.headers {
		r.headers = make(map[string]string)
	}
	r.headers["Content-Type"] = "application/json"
	return nil
}

func (r *GHttpRequest) SetTimeout(timeout time.Duration) {
	r.timeOut = timeout
}

func (r *GHttpRequest) SetContext(ctx any) {
	r.context = ctx
}

func (r *GHttpRequest) GetContext() any {
	return r.context
}

func (r *GHttpRequest) SetHttpStreamCallback(callback GFoundationApi.OnHttpStreamCallback) {
	r.streamCallback = callback
}

func (r *GHttpRequest) SetHttpResponseCallback(callback GFoundationApi.OnHttpResponseCallback) {
	r.callback = callback
}

// 设置排队mask, 填UnorderRequest代表对时间序无要求, 否则相同的mask, 先调用的会先执行,并且先返回
func (r *GHttpRequest) SetOrderMask(mask uint64) {
	r.mask = mask
}

func (r *GHttpRequest) Send() {
	r.ghttp.PushRequest(r)
}

func (r *GHttpRequest) Cancel() {
	r.isCanceled.Store(true)
}
