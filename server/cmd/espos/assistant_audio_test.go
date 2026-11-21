package main

import (
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"espos/internal/backend"
	"espos/internal/engine"
	"espos/internal/flows"
	"espos/internal/wm"
)

// A model that cannot hear a recording (ESPOS_AI_AUDIO=0, or a provider preset such as anthropic) never gets one:
// the words come from speech to text, in the device's language, and when there are none the dog asks again
// without calling the model.
func TestAssistantModelThatCannotHear(t *testing.T) {
	var asked []string
	ai := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		asked = append(asked, string(b))
		ans, _ := json.Marshal(map[string]any{"heard": "x", "reply": "Woof!", "mood": "happy", "topic": "chat"})
		json.NewEncoder(w).Encode(map[string]any{"choices": []any{map[string]any{"finish_reason": "stop",
			"message": map[string]any{"content": string(ans)}}}})
	}))
	defer ai.Close()
	var sttLang string
	sttUp := true
	stt := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !sttUp {
			http.Error(w, "down", 503)
			return
		}
		r.ParseMultipartForm(1 << 20)
		sttLang = r.FormValue("language")
		io.WriteString(w, `{"text":"what time is it"}`)
	}))
	defer stt.Close()
	t.Setenv("ESPOS_AI_PROVIDER", "anthropic")
	t.Setenv("ESPOS_AI_URL", ai.URL+"/v1")
	t.Setenv("ESPOS_AI_KEY", "test")
	t.Setenv("ESPOS_AI_AUDIO", "")
	t.Setenv("ESPOS_STT_URL", "")
	t.Setenv("ESPOS_STT_LANG", "")
	t.Setenv("ESPOS_TTS_URL", "")

	open(t.TempDir())
	t.Cleanup(func() { wm.Close() })
	eng, err := engine.Load(backend.FS, "workflows", flows.Nodes)
	if err != nil {
		t.Fatal(err)
	}
	dev, err := wm.Insert("devices", map[string]any{"user_id": 1, "name": "Desk", "kind": "s3", "token_hash": "x"})
	if err != nil {
		t.Fatal(err)
	}
	id := wm.Int(dev["id"])
	if _, err := wm.Insert("device_settings", map[string]any{"device_id": id, "user_id": 1, "skey": "sys.lang", "svalue": "de"}); err != nil {
		t.Fatal(err)
	}
	guard := map[string]any{"require_device": map[string]any{"device_id": id, "user_id": 1}}
	speak := func() map[string]any {
		r, err := eng.Run("device_assist", map[string]any{"guard": guard, "query": map[string]any{},
			"headers": map[string]any{"content-type": "audio/wav"}, "raw": "", "raw_body_base64": base64.StdEncoding.EncodeToString(wav(16000, 16000))}, nil, "http://x")
		if err != nil {
			t.Fatal(err)
		}
		b, _ := json.Marshal(r.Body)
		var body map[string]any
		json.Unmarshal(b, &body)
		return body
	}

	// 1. no speech to text at all: the model is not called with the recording; the dog asks again
	speak()
	if len(asked) != 0 {
		t.Fatalf("the model got a request without words: %d", len(asked))
	}

	// 2. speech to text: the words, in the device's language, and no input_audio
	t.Setenv("ESPOS_STT_URL", stt.URL+"/v1")
	speak()
	if len(asked) != 1 || strings.Contains(asked[0], "input_audio") || !strings.Contains(asked[0], "what time is it") {
		t.Fatalf("model request: %v", asked)
	}
	if sttLang != "de" {
		t.Fatalf("the device's language as the hint: %q", sttLang)
	}

	// 3. speech to text down: still no recording to the model
	sttUp = false
	speak()
	if len(asked) != 1 {
		t.Fatalf("the model was called with the recording after speech to text failed: %v", asked[len(asked)-1][:200])
	}
}
