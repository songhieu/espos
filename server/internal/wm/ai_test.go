package wm

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
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
	// a model that refuses the dog's temperature, and OpenAI's reasoning models' max_completion_tokens
	req2 := map[string]any{"messages": []any{}, "temperature": 0.7, "max_tokens": 700}
	t.Setenv("ESPOS_AI_TEMPERATURE", "off")
	t.Setenv("ESPOS_AI_TOKENS_PARAM", "max_completion_tokens")
	if _, err := AIChat(req2); err != nil {
		t.Fatal(err)
	}
	if _, ok := got["temperature"]; ok || got["max_completion_tokens"] != float64(700) || got["max_tokens"] != nil {
		t.Fatalf("temperature off, max_completion_tokens: %v", got)
	}
	t.Setenv("ESPOS_AI_TEMPERATURE", "1")
	t.Setenv("ESPOS_AI_TOKENS_PARAM", "")
	AIChat(req2)
	if got["temperature"] != float64(1) || got["max_tokens"] != float64(700) || req2["temperature"] != 0.7 {
		t.Fatalf("temperature 1: %v", got)
	}
	t.Setenv("ESPOS_AI_TEMPERATURE", "")

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

// The provider presets: one variable sets a provider's address, model and quirks; a variable set by hand wins.
func TestAIPresets(t *testing.T) {
	var got map[string]any
	var path string
	answer := `{"choices":[{"finish_reason":"stop","message":{"content":"{\"reply\":\"gâu\",\"guess\":null,\"actions\":[{\"do\":\"alarm_add\",\"h\":6,\"m\":null}]}"}}]}`
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		path = r.URL.Path
		got = nil
		json.NewDecoder(r.Body).Decode(&got)
		io.WriteString(w, answer)
	}))
	defer srv.Close()
	for _, k := range []string{"ESPOS_AI_URL", "ESPOS_AI_KEY", "ESPOS_AI_MODEL", "ESPOS_AI_REASONING", "ESPOS_AI_JSON", "ESPOS_AI_TEMPERATURE",
		"ESPOS_AI_TOKENS_PARAM", "ESPOS_AI_MAX_TOKENS", "ESPOS_AI_AUDIO", "ESPOS_AI_PROMPT", "ESPOS_AI_TIMEOUT", "ESPOS_AI_PROVIDER"} {
		t.Setenv(k, "")
	}
	dog := func() map[string]any {
		return map[string]any{"messages": []any{}, "max_tokens": 700, "temperature": 0.7, "reasoning_effort": "low",
			"response_format": map[string]any{"type": "json_object"}}
	}

	// no provider, no address: Gemini, as the hosted service
	if AIProvider() != "gemini" || AIModel() != "gemini-3.5-flash-lite" || !AIHearsAudio() || aiURL() != DefaultAIURL {
		t.Fatalf("default: %s %s %v %s", AIProvider(), AIModel(), AIHearsAudio(), aiURL())
	}

	// Anthropic: json_schema (strict), no temperature, no audio, its address and model
	t.Setenv("ESPOS_AI_PROVIDER", "anthropic")
	t.Setenv("ESPOS_AI_KEY", "k")
	t.Setenv("ESPOS_AI_URL", srv.URL+"/v1") // in place of https://api.anthropic.com/v1
	if AIHearsAudio() {
		t.Fatal("anthropic does not take audio")
	}
	r, err := AIChat(dog())
	if err != nil {
		t.Fatal(err)
	}
	rf, _ := got["response_format"].(map[string]any)
	js, _ := rf["json_schema"].(map[string]any)
	if path != "/v1/chat/completions" || rf["type"] != "json_schema" || js["strict"] != true || js["schema"] == nil ||
		got["model"] != "claude-haiku-4-5-20251001" || got["temperature"] != nil || got["max_tokens"] != float64(2048) {
		t.Fatalf("anthropic request: %v", got)
	}
	if _, ok := got["reasoning_effort"]; ok {
		t.Fatal("anthropic: reasoning_effort sent")
	}
	// the schema's nulls are gone from the answer the assistant reads
	msg := r["body"].(map[string]any)["choices"].([]any)[0].(map[string]any)["message"].(map[string]any)
	if c := Str(msg["content"]); strings.Contains(c, "null") || !strings.Contains(c, `"h":6`) {
		t.Fatalf("nulls left: %s", c)
	}
	sch, _ := json.Marshal(answerSchema)
	for _, f := range []string{`"additionalProperties":false`, `"actions"`, `"summary"`, `"prev_summary"`, `"heard"`} {
		if !strings.Contains(string(sch), f) {
			t.Fatalf("schema lacks %s", f)
		}
	}

	// OpenAI: max_completion_tokens, no temperature; a hand-set temperature wins
	t.Setenv("ESPOS_AI_PROVIDER", "openai")
	AIChat(dog())
	if got["max_completion_tokens"] != float64(2048) || got["max_tokens"] != nil || got["temperature"] != nil || got["model"] != "gpt-5.4-mini" {
		t.Fatalf("openai request: %v", got)
	}
	t.Setenv("ESPOS_AI_TEMPERATURE", "0.5")
	t.Setenv("ESPOS_AI_MAX_TOKENS", "900")
	AIChat(dog())
	if got["temperature"] != 0.5 || got["max_completion_tokens"] != float64(900) {
		t.Fatalf("hand-set values: %v", got)
	}
	t.Setenv("ESPOS_AI_TEMPERATURE", "")
	t.Setenv("ESPOS_AI_MAX_TOKENS", "")

	// DeepSeek: thinking off (reasoning_effort none), json_object
	t.Setenv("ESPOS_AI_PROVIDER", "deepseek")
	AIChat(dog())
	if got["reasoning_effort"] != "none" || got["model"] != "deepseek-flash" || got["temperature"] != 0.7 {
		t.Fatalf("deepseek request: %v", got)
	}

	// local (the AI kit): no key needed, the short prompt, a longer wait
	t.Setenv("ESPOS_AI_PROVIDER", "local")
	t.Setenv("ESPOS_AI_KEY", "")
	t.Setenv("ESPOS_AI_URL", "")
	if !AIConfigured() || !AIPromptShort() || AIModel() != "gemma-4-e2b" || aiURL() != "http://127.0.0.1:8080/v1" || aiClient().Timeout != 60*time.Second {
		t.Fatal("local preset")
	}
	t.Setenv("ESPOS_AI_AUDIO", "1")
	if !AIHearsAudio() {
		t.Fatal("ESPOS_AI_AUDIO=1 wins")
	}

	// Azure has no address of its own
	t.Setenv("ESPOS_AI_PROVIDER", "azure")
	t.Setenv("ESPOS_AI_KEY", "k")
	if _, err := AIChat(dog()); err == nil || !strings.Contains(err.Error(), "ESPOS_AI_URL") {
		t.Fatalf("azure without an address: %v", err)
	}
}

// An answer cut at the token limit, or empty, is said in the log, not lost.
func TestAIChatCutAnswerIsLogged(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, `{"choices":[{"finish_reason":"length","message":{"content":""}}]}`)
	}))
	defer srv.Close()
	t.Setenv("ESPOS_AI_PROVIDER", "")
	t.Setenv("ESPOS_AI_URL", srv.URL)
	t.Setenv("ESPOS_AI_KEY", "")
	var buf bytes.Buffer
	log.SetOutput(&buf)
	defer log.SetOutput(os.Stderr)
	if _, err := AIChat(map[string]any{"messages": []any{}}); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(buf.String(), "ESPOS_AI_MAX_TOKENS") {
		t.Fatalf("logged %q", buf.String())
	}
}
