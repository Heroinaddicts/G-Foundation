// Reference services run separately and never call framework APIs.
package main

import (
	"GFoundationTests/StressKit"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"sync"
	"syscall"
	"time"
)

func main() {
	httpAddr := flag.String("http", "127.0.0.1:18888", "HTTP fixture address")
	tcpAddr := flag.String("tcp", "127.0.0.1:18889", "TCP echo fixture address")
	flag.Parse()
	hl, err := net.Listen("tcp", *httpAddr)
	if err != nil {
		panic(err)
	}
	tl, err := net.Listen("tcp", *tcpAddr)
	if err != nil {
		hl.Close()
		panic(err)
	}
	var mu sync.Mutex
	conns := map[net.Conn]bool{}
	go func() {
		for {
			c, err := tl.Accept()
			if err != nil {
				return
			}
			mu.Lock()
			conns[c] = true
			mu.Unlock()
			go func() { defer func() { c.Close(); mu.Lock(); delete(conns, c); mu.Unlock() }(); io.Copy(c, c) }()
		}
	}()
	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, "ok") })
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		id, err := strconv.ParseUint(r.URL.Query().Get("id"), 10, 64)
		if err != nil {
			http.Error(w, "bad id", 400)
			return
		}
		n, err := strconv.Atoi(r.URL.Query().Get("size"))
		if err != nil || n < 24 || n > 1048576 {
			http.Error(w, "bad size", 400)
			return
		}
		if r.Header.Get("X-Stress-Remove") != "" || r.Header.Get("X-Stress-Id") != strconv.FormatUint(id, 10) {
			http.Error(w, "bad request header", 400)
			return
		}
		if r.URL.Query().Get("echo") != "空 格+&=/?" {
			http.Error(w, "query encoding mismatch", 400)
			return
		}
		w.Header().Set("X-Stress-Id", strconv.FormatUint(id, 10))
		body := StressKit.Payload(id, n)
		if r.Method == http.MethodPost {
			body, err = io.ReadAll(io.LimitReader(r.Body, 2*1048576))
			if err != nil {
				http.Error(w, err.Error(), 400)
				return
			}
		}
		wait := func(ms int) bool {
			t := time.NewTimer(time.Duration(ms) * time.Millisecond)
			defer t.Stop()
			select {
			case <-t.C:
				return true
			case <-r.Context().Done():
				return false
			}
		}
		switch r.URL.Path {
		case "/delay":
			if !wait(40) {
				return
			}
		case "/status":
			w.WriteHeader(503)
		case "/stream":
			delay, _ := strconv.Atoi(r.URL.Query().Get("chunk_delay_ms"))
			if delay < 0 || delay > 100 {
				http.Error(w, "bad delay", 400)
				return
			}
			for len(body) > 0 {
				size := 32
				if size > len(body) {
					size = len(body)
				}
				if _, err := w.Write(body[:size]); err != nil {
					return
				}
				w.(http.Flusher).Flush()
				body = body[size:]
				if delay > 0 && !wait(delay) {
					return
				}
			}
			return
		case "/echo":
		case "/json":
			var value struct {
				ID      uint64 `json:"id"`
				Payload []byte `json:"payload"`
			}
			if r.Header.Get("Content-Type") != "application/json" || json.Unmarshal(body, &value) != nil || value.ID != id || len(value.Payload) != n {
				http.Error(w, "JSON/header mismatch", 400)
				return
			}
		default:
			http.NotFound(w, r)
			return
		}
		w.Write(body)
	})
	server := &http.Server{Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	go server.Serve(hl)
	fmt.Printf("FIXTURES_READY http=%s tcp=%s\n", hl.Addr(), tl.Addr())
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt, syscall.SIGTERM)
	<-quit
	tl.Close()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	server.Shutdown(ctx)
	mu.Lock()
	for c := range conns {
		c.Close()
	}
	mu.Unlock()
}
