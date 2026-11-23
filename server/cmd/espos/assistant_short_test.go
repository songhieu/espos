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
// the hosted one (pbSystem's, from the v1 request). Vietnamese devices get the short Vietnamese prompt, every other
// language the short English one.
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
	device := func(t *testing.T, kind, lang string) int {
		dev, err := wm.Insert("devices", map[string]any{"user_id": 1, "name": "Desk", "kind": kind, "token_hash": "x" + kind + lang})
		if err != nil {
			t.Fatal(err)
		}
		id := wm.Int(dev["id"])
		if _, err := wm.Insert("device_settings", map[string]any{"device_id": id, "user_id": 1, "skey": "sys.lang", "svalue": lang}); err != nil {
			t.Fatal(err)
		}
		return id
	}
	// one turn as typed on the web (chat_device_dog), or a device turn with the news chip or the wake call
	// (assist_reply, as device_assist calls it); returns the system prompt the model got
	ask := func(t *testing.T, short bool, id int, text string, extra map[string]any) string {
		t.Helper()
		if short {
			t.Setenv("ESPOS_AI_PROMPT", "short")
		} else {
			t.Setenv("ESPOS_AI_PROMPT", "")
		}
		n := len(systems)
		if extra == nil {
			r, err := eng.Run("chat_device_dog", map[string]any{"user": map[string]any{"id": 1}, "params": map[string]any{"id": id},
				"body": map[string]any{"text": text, "cid": "new"}}, nil, "http://x")
			if err != nil || r.Status != 200 {
				t.Fatalf("chat: %+v %v", r, err)
			}
		} else {
			in := map[string]any{"device_id": id, "user_id": 1, "source": "device", "text": text, "cid": "new"}
			for k, v := range extra {
				in[k] = v
			}
			if _, err := eng.Run("assist_reply", in, nil, "http://x"); err != nil {
				t.Fatalf("assist_reply: %v", err)
			}
		}
		if len(systems) != n+1 {
			t.Fatalf("model calls: %d, want %d", len(systems), n+1)
		}
		return systems[n]
	}
	// before: the index of a in s is before b's (both there)
	before := func(s, a, b string) bool {
		i, j := strings.Index(s, a), strings.LastIndex(s, b)
		return i >= 0 && j >= 0 && i < j
	}
	type words struct {
		lang, text, newsText                     string
		fullRules, shortRules, data, now, memory string
		where, actions, alarms, json, jsonTurn   string
		chip, wake, ruleOneNews                  string
	}
	for _, w := range []words{
		{lang: "vi", text: "mấy giờ rồi", newsText: "Tin hôm nay", fullRules: "LUẬT NÓI:", shortRules: "\nLUẬT:\n", data: "\nDỮ LIỆU CỦA CHỦ (", now: "BÂY GIỜ:",
			memory: "TRÍ NHỚ CỦA", where: "BẠN ĐANG Ở:", actions: "VIỆC LÀM GIÙM CHỦ", alarms: "BÁO THỨC:",
			json: "TRẢ VỀ ĐÚNG MỘT ĐỐI TƯỢNG JSON", jsonTurn: "JSON LƯỢT NÀY:",
			chip: "LƯỢT NÀY: chủ vừa bấm nút", wake: "Đoạn ghi âm bắt đầu bằng lời gọi", ruleOneNews: "NGOẠI LỆ ở luật 1"},
		{lang: "en", text: "what time is it", newsText: "Today's news", fullRules: "SPEAKING RULES:", shortRules: "\nRULES:\n", data: "\nTHE PERSON'S DATA (", now: "NOW:",
			memory: "MEMORY: WHAT", where: "WHERE YOU ARE:", actions: "THINGS YOU DO FOR THE PERSON", alarms: "ALARMS:",
			json: "RETURN EXACTLY ONE JSON OBJECT", jsonTurn: "JSON THIS TURN:",
			chip: "THIS TURN: the person just pressed", wake: "The recording starts with the call that wakes you", ruleOneNews: "rule 1's EXCEPTION"},
	} {
		t.Run(w.lang, func(t *testing.T) {
			id := device(t, "s3", w.lang)
			full := ask(t, false, id, w.text, nil)
			short := ask(t, true, id, w.text, nil)
			if !strings.Contains(full, w.fullRules) || strings.Contains(full, w.shortRules) {
				t.Fatal("the full prompt should be the hosted one")
			}
			if !strings.Contains(short, w.shortRules) || strings.Contains(short, w.fullRules) {
				t.Fatalf("the short prompt should carry the short rules only:\n%s", short)
			}
			// the fixed part (the dog, the rules, the actions: everything before the person's data) is what got shorter
			fixed := func(s string) int { return len([]rune(s[:strings.Index(s, w.data)])) }
			if fixed(short) > fixed(full)*7/10 {
				t.Fatalf("short prompt's fixed part %d characters, full %d: not much shorter", fixed(short), fixed(full))
			}
			t.Logf("%s: whole prompt %d → %d characters, fixed part %d → %d", w.lang, len([]rune(full)), len([]rune(short)), fixed(full), fixed(short))
			// the time comes after the data, the memories and the conversation (only this turn's notes follow it)
			at := strings.Index(short, w.now)
			if strings.Count(short, w.now) != 1 || at < strings.Index(short, w.memory) || at < strings.Index(short, w.alarms) {
				t.Fatal("the time should come once, at the end of the short prompt")
			}
			if strings.Contains(short, `"prev_summary"`) && strings.LastIndex(short, w.jsonTurn) < at {
				t.Fatal("a turn that writes a summary repeats the answer's shape after the time")
			}
			for _, part := range []string{w.where, w.data, w.actions, w.alarms, w.json} {
				if !strings.Contains(short, part) {
					t.Fatalf("the short prompt lost %q", part)
				}
			}
			if !before(short, w.where, w.shortRules) {
				t.Fatal("where the device is belongs with the dog, before the rules")
			}
			if w.lang != "vi" && !strings.Contains(short, `"claims_done": false}`) {
				t.Fatal("the English answer's shape keeps claims_done (Run reads it in other languages)")
			}

			// the "Today's news" chip: the short rules' own news note at the end, not the full rules' rule 1
			news := ask(t, true, id, w.newsText, map[string]any{"intent": "news"})
			if !strings.Contains(news, w.shortRules) || strings.Contains(news, w.ruleOneNews) || !before(news, w.now, w.chip) {
				t.Fatalf("news chip in the short prompt:\n%s", news[max(0, len(news)-1500):])
			}
			// the wake call: its note moves to the end with the other per-turn parts
			wake := ask(t, true, id, w.text, map[string]any{"wake": true})
			if !strings.Contains(wake, w.shortRules) || strings.Count(wake, w.wake) != 1 || !before(wake, w.now, w.wake) {
				t.Fatalf("wake call in the short prompt:\n%s", wake[max(0, len(wake)-1500):])
			}
			// the other kinds (the bot's own speaking rule, the X4's settings) get the short prompt too, not the full one
			for _, kind := range []string{"bot", "x4"} {
				kid := device(t, kind, w.lang)
				full, short := ask(t, false, kid, w.text, nil), ask(t, true, kid, w.text, nil)
				if !strings.Contains(full, w.fullRules) || !strings.Contains(short, w.shortRules) || strings.Contains(short, w.fullRules) ||
					!before(short, w.alarms, w.now) {
					t.Fatalf("%s: the short prompt was not fitted:\n%s", kind, short[:min(len(short), 1500)])
				}
			}
		})
	}
}
