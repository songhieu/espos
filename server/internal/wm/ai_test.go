package wm

import (
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
)

func TestAIChatFromEnv(t *testing.T) {
	var got map[string]any
	var auth, path string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		path, auth = r.URL.Path, r.Header.Get("Authorization")
		b, _ := io.ReadAll(r.Body)
		got = nil
		json.Unmarshal(b, &got)
		w.Header().Set("Content-Type", "application/json")
		io.WriteString(w, `{"choices":[{"message":{"content":"{\"reply\":\"gâu\"}"}}]}`)
	}))
	defer srv.Close()

	t.Setenv("ESPOS_AI_URL", "")
	t.Setenv("ESPOS_AI_KEY", "")
	if _, err := AIChat(map[string]any{"messages": []any{}}); err == nil {
		t.Fatal("no model set: want an error")
	}

	t.Setenv("ESPOS_AI_URL", srv.URL+"/v1/")
	t.Setenv("ESPOS_AI_KEY", "k-test")
	t.Setenv("ESPOS_AI_MODEL", "my-model")
	t.Setenv("ESPOS_AI_REASONING", "")
	req := map[string]any{"model": "hosted/model", "messages": []any{}, "reasoning_effort": "low",
		"response_format": map[string]any{"type": "json_object"}}
	r, err := AIChat(req)
	if err != nil || Int(r["status"]) != 200 {
		t.Fatalf("AIChat: %v %v", r, err)
	}
	if path != "/v1/chat/completions" || auth != "Bearer k-test" || got["model"] != "my-model" {
		t.Fatalf("sent %s %q %v", path, auth, got["model"])
	}
	if _, ok := got["reasoning_effort"]; ok {
		t.Fatal("reasoning_effort sent although ESPOS_AI_REASONING is unset")
	}
	if got["response_format"] == nil {
		t.Fatal("response_format dropped")
	}
	if req["model"] != "hosted/model" {
		t.Fatal("the caller's request was changed")
	}

	t.Setenv("ESPOS_AI_REASONING", "none")
	t.Setenv("ESPOS_AI_JSON", "0")
	t.Setenv("ESPOS_AI_KEY", "") // a local model: an address, no key
	if _, err := AIChat(req); err != nil {
		t.Fatal(err)
	}
	if got["reasoning_effort"] != "none" || got["response_format"] != nil || auth != "" {
		t.Fatalf("toggles: %v %v %q", got["reasoning_effort"], got["response_format"], auth)
	}
	t.Setenv("ESPOS_AI_REASONING", "off")
	if _, err := AIChat(req); err != nil {
		t.Fatal(err)
	}
	if _, ok := got["reasoning_effort"]; ok {
		t.Fatal("ESPOS_AI_REASONING=off still sent it")
	}
	t.Setenv("ESPOS_AI_MODEL", "")
	if AIModel() != "gemini-3.5-flash-lite" {
		t.Fatal("default model")
	}
}

func TestTranscribe(t *testing.T) {
	var model, lang, file string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/openai/v1/audio/transcriptions" || r.Header.Get("Authorization") != "Bearer s-key" {
			http.Error(w, "wrong path or key", 400)
			return
		}
		if err := r.ParseMultipartForm(1 << 20); err != nil {
			http.Error(w, err.Error(), 400)
			return
		}
		model, lang = r.FormValue("model"), r.FormValue("language")
		f, _, _ := r.FormFile("file")
		b, _ := io.ReadAll(f)
		file = string(b)
		io.WriteString(w, `{"text":"  mấy giờ rồi  "}`)
	}))
	defer srv.Close()
	t.Setenv("ESPOS_STT_URL", srv.URL+"/openai/v1")
	t.Setenv("ESPOS_STT_KEY", "s-key")
	t.Setenv("ESPOS_STT_MODEL", "whisper-large-v3-turbo")
	t.Setenv("ESPOS_STT_LANG", "vi")
	if !STTConfigured() {
		t.Fatal("STTConfigured")
	}
	text, err := Transcribe(base64.StdEncoding.EncodeToString([]byte("RIFF-fake-wav")))
	if err != nil || text != "mấy giờ rồi" {
		t.Fatalf("Transcribe: %q %v", text, err)
	}
	if model != "whisper-large-v3-turbo" || lang != "vi" || file != "RIFF-fake-wav" {
		t.Fatalf("sent model %q lang %q file %q", model, lang, file)
	}
	t.Setenv("ESPOS_STT_KEY", "wrong")
	if _, err := Transcribe(base64.StdEncoding.EncodeToString([]byte("x"))); err == nil {
		t.Fatal("a refused request must be an error")
	}
}

func TestSecretsFromEnv(t *testing.T) {
	t.Setenv("ESPOS_TTS_KEY", "tts-1")
	t.Setenv("ESPOS_SECRET_MAIL_KEY", "m-2")
	if s := FillSecrets("Bearer {{ secret.espos_tts_key }} / {{secret.mail_key}} / {{ secret.none }}"); s != "Bearer tts-1 / m-2 / " {
		t.Fatalf("FillSecrets: %q", s)
	}
}

func TestBatchRollsBack(t *testing.T) {
	dir := t.TempDir()
	decl := []Table{{Code: "notes", Columns: []Column{{Code: "user_id", Type: "integer", Indexed: true}, {Code: "text", Type: "text"}}}}
	if err := Open(filepath.Join(dir, "t.db"), decl); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { Close() })
	r, err := Batch([]map[string]any{
		{"op": "insert", "table": "notes", "values": map[string]any{"user_id": 1, "text": "a"}},
		{"op": "upsert", "table": "notes", "on": []any{"user_id"}, "values": map[string]any{"user_id": 1, "text": "b"}},
	})
	if err != nil || len(r["results"].([]any)) != 2 {
		t.Fatalf("batch: %v %v", r, err)
	}
	rows, _ := Query("notes", nil)
	if len(rows) != 1 || rows[0]["text"] != "b" {
		t.Fatalf("after upsert: %v", rows)
	}
	_, err = Batch([]map[string]any{
		{"op": "insert", "table": "notes", "values": map[string]any{"user_id": 2, "text": "c"}},
		{"op": "explode", "table": "notes"},
	})
	if err == nil {
		t.Fatal("a bad op must fail the batch")
	}
	if rows, _ := Query("notes", nil); len(rows) != 1 {
		t.Fatalf("not rolled back: %v", rows)
	}
}
