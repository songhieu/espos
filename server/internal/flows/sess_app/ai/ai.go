// Workflow sess_app, node ai (imported by tools/importer).
package ai

import (
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"regexp"
	"strings"
)

// The model call of POST suggest / compose (desktop-protocol §9.4), after admin_lic_core counted it as one AI turn:
// the dog's model and key (ESPOS_AI_KEY is filled on the platform; it never enters this code). Nothing is
// sent to the session from here: the app shows the text and the person taps Send.

const model = "vx/gemini-3.5-flash-lite"

// risky chips are dropped (the same shell/delete/force shapes the send route asks about)
var chipRisky = regexp.MustCompile(`(?i)^\s*!|\brm\s+-[a-z]*[rf]|\bgit\s+(push\b.*(-f\b|--force)|reset\s+--hard)|\bsudo\b|\b(drop|truncate)\s+table\b|^\s*/(clear|exit|quit|logout)\b`)

func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body}
}

// short: at most n letters, cut on a word, never "…".
func short(s string, n int) string {
	r := []rune(strings.Join(strings.Fields(s), " "))
	if len(r) <= n {
		return string(r)
	}
	c := string(r[:n])
	if i := strings.LastIndex(c, " "); i > n/2 {
		c = c[:i]
	}
	return strings.TrimRight(c, " ,.;:-")
}

func parse(content string) map[string]any {
	content = strings.TrimSpace(content)
	content = strings.TrimPrefix(strings.TrimPrefix(content, "```json"), "```")
	content = strings.TrimSuffix(content, "```")
	if i, j := strings.Index(content, "{"), strings.LastIndex(content, "}"); i >= 0 && j > i {
		var m map[string]any
		if json.Unmarshal([]byte(content[i:j+1]), &m) == nil {
			return m
		}
	}
	return nil
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	a, _ := in["ai"].(map[string]any)
	op := wm.Str(a["op"])
	msgs, _ := a["messages"].([]any)
	if len(msgs) == 0 {
		return answer(422, map[string]any{"error": "bad_args"})
	}
	req := map[string]any{"model": model, "messages": msgs, "max_tokens": 300, "temperature": 0.4, "stream": false,
		"response_format": map[string]any{"type": "json_object"}, "reasoning_effort": "low"}
	var got map[string]any
	lastErr := ""
	for try := 0; try < 2 && got == nil; try++ {
		r, ferr := wm.AIChat(req)
		if ferr != nil {
			lastErr = ferr.Error()
			continue
		}
		if wm.Int(r["status"]) != 200 {
			lastErr = fmt.Sprintf("router status %d", wm.Int(r["status"]))
			continue
		}
		body, _ := r["body"].(map[string]any)
		ch, _ := body["choices"].([]any)
		if len(ch) == 0 {
			lastErr = "no choices"
			continue
		}
		c0, _ := ch[0].(map[string]any)
		msg, _ := c0["message"].(map[string]any)
		got = parse(wm.Str(msg["content"]))
		if got == nil {
			lastErr = "not json"
		}
	}
	if got == nil {
		wm.Log("model failed: ", lastErr)
		return answer(502, map[string]any{"error": "ai_failed"})
	}
	if op == "compose" {
		t := strings.TrimSpace(wm.Str(got["text"]))
		if t == "" {
			return answer(502, map[string]any{"error": "ai_failed"})
		}
		r := []rune(t)
		if len(r) > 2000 {
			t = short(t, 2000)
		}
		return answer(200, map[string]any{"text": t})
	}
	chips := []any{}
	seen := map[string]bool{}
	raw, _ := got["chips"].([]any)
	for _, x := range raw {
		c := short(wm.Str(x), 40)
		if c == "" || seen[strings.ToLower(c)] || chipRisky.MatchString(c) || len(chips) == 3 {
			continue
		}
		seen[strings.ToLower(c)] = true
		chips = append(chips, c)
	}
	forSeq := wm.Num(a["for_seq"])
	if id := wm.Int(a["row_id"]); id > 0 {
		if _, uerr := wm.Update("desk_screens", id, map[string]any{"sugg_key": wm.Str(a["key"]),
			"sugg": map[string]any{"chips": chips, "for_seq": forSeq}}); uerr != nil {
			wm.Log("cache: ", uerr.Error())
		}
	}
	if len(chips) == 0 {
		return answer(204, map[string]any{})
	}
	return answer(200, map[string]any{"chips": chips, "for_seq": forSeq})
}
