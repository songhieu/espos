package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"espos/internal/backend"
	"espos/internal/engine"
	"espos/internal/flows"
	"espos/internal/wm"
)

// ESPOS_AI_PROMPT=short (tools/importer/short_prompt.go.in): the same turn gets a much shorter system prompt, the
// fixed parts first and the time last, so a local model's prefix cache covers the next turn. Unset, the prompt is
// the hosted one.
func TestAssistantShortPrompt(t *testing.T) {
	var systems []string
	ai := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var req map[string]any
		json.NewDecoder(r.Body).Decode(&req)
		msgs, _ := req["messages"].([]any)
		sys, _ := msgs[0].(map[string]any)
		systems = append(systems, wm.Str(sys["content"]))
		ans, _ := json.Marshal(map[string]any{"heard": "mấy giờ rồi", "reply": "Gâu! Hai giờ rồi nè.", "mood": "happy", "topic": "time"})
		json.NewEncoder(w).Encode(map[string]any{"choices": []any{map[string]any{"message": map[string]any{"content": string(ans)}}}})
	}))
	defer ai.Close()
	t.Setenv("ESPOS_AI_URL", ai.URL+"/v1")
	t.Setenv("ESPOS_AI_KEY", "test")
	t.Setenv("ESPOS_STT_URL", "")
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
	ask := func() {
		r, err := eng.Run("chat_device_dog", map[string]any{"user": map[string]any{"id": 1}, "params": map[string]any{"id": id},
			"body": map[string]any{"text": "mấy giờ rồi", "cid": "new"}}, nil, "http://x")
		if err != nil || r.Status != 200 {
			t.Fatalf("chat: %+v %v", r, err)
		}
	}

	t.Setenv("ESPOS_AI_PROMPT", "")
	ask()
	t.Setenv("ESPOS_AI_PROMPT", "short")
	ask()
	if len(systems) != 2 {
		t.Fatalf("model calls: %d", len(systems))
	}
	full, short := systems[0], systems[1]
	if !strings.Contains(full, "LUẬT NÓI:") || strings.Contains(full, "\nLUẬT:\n") {
		t.Fatal("the full prompt should be the hosted one")
	}
	if !strings.Contains(short, "\nLUẬT:\n") || strings.Contains(short, "LUẬT NÓI:") || strings.Contains(short, "TRÒ CHUYỆN: bạn là bạn của chủ") {
		t.Fatal("the short prompt should carry the short rules only")
	}
	// the fixed part (the dog, the rules, the actions: everything before the person's data) is what got shorter
	fixed := func(s string) int { return len([]rune(s[:strings.Index(s, "DỮ LIỆU CỦA CHỦ")])) }
	if fixed(short) > fixed(full)*7/10 {
		t.Fatalf("short prompt's fixed part %d characters, full %d: not much shorter", fixed(short), fixed(full))
	}
	// the time comes after the data, the memories and the conversation (only this turn's notes follow it)
	at := strings.Index(short, "BÂY GIỜ:")
	if strings.Count(short, "BÂY GIỜ:") != 1 || at < strings.Index(short, "TRÍ NHỚ CỦA") || at < strings.Index(short, "BÁO THỨC:") {
		t.Fatal("the time should come once, at the end of the short prompt")
	}
	if strings.Contains(short, `"prev_summary"`) && strings.LastIndex(short, "JSON LƯỢT NÀY:") < at {
		t.Fatal("a turn that writes a summary repeats the answer's shape after the time")
	}
	for _, part := range []string{"BẠN ĐANG Ở:", "DỮ LIỆU CỦA CHỦ", "VIỆC LÀM GIÙM CHỦ", "BÁO THỨC:", "TRẢ VỀ ĐÚNG MỘT ĐỐI TƯỢNG JSON"} {
		if !strings.Contains(short, part) {
			t.Fatalf("the short prompt lost %q", part)
		}
	}
	if strings.Index(short, "BẠN ĐANG Ở:") > strings.Index(short, "\nLUẬT:\n") {
		t.Fatal("where the device is belongs with the dog, before the rules")
	}
}
