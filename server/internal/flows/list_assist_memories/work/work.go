// Workflow list_assist_memories, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"strings"
	"time"
)

// GET /v1/assist/memories: {memories: [{id, fact, kind, dog, created_at}], chats: how many sessions are kept,
// sessions: [{id, summary, turns, started_ms, last_ms, source, device}]}, newest first.
// A session's words (its turns) are kept 24 hours, its summary 7 days (assist_reply, assist_sweep); memories until
// the person deletes them. "turns" is how many raw turns it still holds (0: words already deleted).

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmIn["uid"])
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	since := time.Now().UnixMilli() - 7*86400000
	res, err := wm.QueryMany([]map[string]any{
		{"table": "assist_memories", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
			"order_by": "id", "desc": true, "limit": 100, "with_total": false},
		{"table": "assist_convos", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid},
			{"column": "last_ms", "op": "gte", "value": since}}, "order_by": "last_ms", "desc": true, "limit": 40, "with_total": false},
		{"table": "devices", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 50, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return map[string]any{"status": 500, "body": map[string]any{"error": "server_error"}}
	}
	out := []any{}
	for _, m := range rowsOf(res, 0) {
		out = append(out, map[string]any{"id": wm.Int(m["id"]), "fact": wm.Str(m["fact"]), "kind": wm.Str(m["kind"]),
			"dog": wm.Str(m["dog"]), "created_at": wm.Str(m["created_at"])})
	}
	names := map[int]string{}
	for _, d := range rowsOf(res, 2) {
		names[wm.Int(d["id"])] = wm.Str(d["name"])
	}
	sessions := []any{}
	for _, c := range rowsOf(res, 1) {
		turns, _ := c["turns"].([]any)
		started := int64(wm.Num(c["started_ms"]))
		if started == 0 && len(turns) > 0 {
			if t, ok := turns[0].(map[string]any); ok {
				started = int64(wm.Num(t["t"]))
			}
		}
		sessions = append(sessions, map[string]any{"id": wm.Int(c["id"]), "summary": strings.TrimSpace(wm.Str(c["summary"])),
			"turns": len(turns), "started_ms": started, "last_ms": int64(wm.Num(c["last_ms"])), "source": wm.Str(c["source"]),
			"device": names[wm.Int(c["device_id"])]})
	}
	return map[string]any{"status": 200, "body": map[string]any{"memories": out, "chats": len(sessions), "sessions": sessions}}
}

func rowsOf(res []map[string]any, i int) []map[string]any {
	if i >= len(res) {
		return nil
	}
	switch r := res[i]["rows"].(type) {
	case []map[string]any:
		return r
	case []any:
		out := []map[string]any{}
		for _, x := range r {
			if m, ok := x.(map[string]any); ok {
				out = append(out, m)
			}
		}
		return out
	}
	return nil
}
