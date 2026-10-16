package main

import (
	"bytes"
	"encoding/json"
	"fmt"
)

type DeepSeekSSEParser struct{}

type DeepSeekStreamResponse struct {
	Choices []struct {
		Delta struct {
			Content string `json:"content"`
		} `json:"delta"`
	} `json:"choices"`
}

// Feed 解析完整的 SSE 消息，返回实际消费的字节数。
// data 中未处理完的尾部数据不会被复制，也不会被消费。
func (p *DeepSeekSSEParser) Feed(data []byte) int {
	consumed := 0

	for consumed < len(data) {
		remaining := data[consumed:]

		// 寻找 SSE 消息分隔符，兼容 LF 和 CRLF。
		end, delimiterLen := findSSEDelimiter(remaining)
		if end < 0 {
			// 没有完整消息，保留未处理数据。
			return consumed
		}

		message := remaining[:end]

		// 处理一条完整消息。
		p.parseMessage(message)

		// 消费消息内容及其分隔符。
		consumed += end + delimiterLen
	}

	return consumed
}

// 返回消息分隔符前的位置及分隔符长度。
func findSSEDelimiter(data []byte) (int, int) {
	lf := bytes.Index(data, []byte("\n\n"))
	crlf := bytes.Index(data, []byte("\r\n\r\n"))

	if lf < 0 && crlf < 0 {
		return -1, 0
	}

	if crlf >= 0 && (lf < 0 || crlf <= lf) {
		return crlf, 4
	}

	return lf, 2
}

func (p *DeepSeekSSEParser) parseMessage(message []byte) {
	// 逐行解析，不复制整个消息。
	for len(message) > 0 {
		lineEnd := bytes.IndexByte(message, '\n')

		var line []byte
		if lineEnd < 0 {
			line = message
			message = nil
		} else {
			line = message[:lineEnd]
			message = message[lineEnd+1:]
		}

		line = bytes.TrimSuffix(line, []byte("\r"))

		if !bytes.HasPrefix(line, []byte("data:")) {
			continue
		}

		payload := bytes.TrimSpace(line[len("data:"):])

		if bytes.Equal(payload, []byte("[DONE]")) {
			return
		}

		if len(payload) == 0 {
			continue
		}

		var response DeepSeekStreamResponse
		if err := json.Unmarshal(payload, &response); err != nil {
			fmt.Printf("\nDeepSeek JSON 解析失败: %v\n", err)
			continue
		}

		for _, choice := range response.Choices {
			fmt.Print(choice.Delta.Content)
		}
	}
}
