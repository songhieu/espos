package server

import (
	"encoding/base64"
	"io"
	"net/http/httptest"
	"strings"
	"testing"

	"espos/internal/engine"
)

func TestRequestInputBodies(t *testing.T) {
	s := &Server{}
	in := func(ctype, body string) map[string]any {
		r := httptest.NewRequest("POST", "/v1/device/assist?stream=1", strings.NewReader(body))
		if ctype != "" {
			r.Header.Set("Content-Type", ctype)
		}
		got, err := s.requestInput(httptest.NewRecorder(), r, Route{})
		if err != nil {
			t.Fatal(err)
		}
		return got
	}
	if b := in("application/json", `{"text":"chào"}`)["body"].(map[string]any); b["text"] != "chào" {
		t.Fatalf("json body: %v", b)
	}
	audio := "RIFF\x00\x01\xff\xfe-wav"
	got := in("audio/wav", audio)
	if got["body"] != nil || got["raw_body_base64"] != base64.StdEncoding.EncodeToString([]byte(audio)) {
		t.Fatalf("binary body: %v", got)
	}
	if got := in("", `{"a":1}`); got["body"].(map[string]any)["a"] != float64(1) || got["raw_body_base64"] != nil {
		t.Fatalf("no content type: %v", got)
	}
}

func TestWriteResultStreams(t *testing.T) {
	w := httptest.NewRecorder()
	writeResult(w, &engine.Result{Status: 200, Headers: map[string]string{"Content-Type": "audio/pcm"},
		Stream: io.NopCloser(strings.NewReader("pcm-bytes"))})
	if w.Body.String() != "pcm-bytes" || w.Header().Get("Content-Type") != "audio/pcm" || !w.Flushed {
		t.Fatalf("stream: %q %v flushed=%v", w.Body.String(), w.Header(), w.Flushed)
	}
}
