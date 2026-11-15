package wm

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"strings"
	"time"
)

var client = &http.Client{
	Timeout: 25 * time.Second,
	CheckRedirect: func(req *http.Request, via []*http.Request) error {
		if len(via) >= 5 {
			return errors.New("too many redirects")
		}
		if via[0].URL.Scheme == "https" && req.URL.Scheme != "https" {
			return errors.New("refused a redirect from https to " + req.URL.Scheme)
		}
		return nil
	},
}

// UserAgent is sent with every outbound request.
var UserAgent = "InkDesk (self-hosted)"

// Fetch makes an HTTP request. opts may hold "headers" (map) and "body" (a string, bytes,
// or any value sent as JSON). The result is {status, headers, body}; a JSON answer comes
// back decoded, anything else as a string.
func Fetch(method, url string, opts map[string]any) (map[string]any, error) {
	var body any
	var headers map[string]any
	if opts != nil {
		body = opts["body"]
		headers, _ = opts["headers"].(map[string]any)
		if h, ok := opts["headers"].(map[string]string); ok {
			headers = map[string]any{}
			for k, v := range h {
				headers[k] = v
			}
		}
	}
	return FetchWith(method, url, body, headers)
}

// FetchWith is Fetch with the body and headers as their own arguments.
func FetchWith(method, url string, body any, headers map[string]any) (map[string]any, error) {
	var rd io.Reader
	isJSON := false
	switch b := body.(type) {
	case nil:
	case string:
		rd = strings.NewReader(b)
	case []byte:
		rd = bytes.NewReader(b)
	default:
		enc, err := json.Marshal(b)
		if err != nil {
			return nil, err
		}
		rd = bytes.NewReader(enc)
		isJSON = true
	}
	req, err := http.NewRequest(method, url, rd)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", UserAgent)
	if isJSON {
		req.Header.Set("Content-Type", "application/json")
	}
	for k, v := range headers {
		req.Header.Set(k, Str(v))
	}
	res, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	raw, err := io.ReadAll(io.LimitReader(res.Body, 32<<20))
	if err != nil {
		return nil, err
	}
	hs := map[string]any{}
	for k, v := range res.Header {
		hs[strings.ToLower(k)] = strings.Join(v, ", ")
	}
	var parsed any = string(raw)
	trimmed := bytes.TrimSpace(raw)
	if len(trimmed) > 0 && (trimmed[0] == '{' || trimmed[0] == '[') {
		var v any
		if json.Unmarshal(trimmed, &v) == nil {
			parsed = v
		}
	}
	return map[string]any{"status": float64(res.StatusCode), "headers": hs, "body": parsed}, nil
}
