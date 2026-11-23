// Workflow assist_sweep, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"fmt"
	"strings"
	"time"
)

// Every hour (2026-11-20): a dog-assistant session (assist_convos row) silent for 24 hours loses its words (the
// turns; its summary stays, and a session never summarized keeps only what it was about, from its turns' topics);
// one silent for 7 days is deleted. assist_reply does the same for the device it is talking to: this catches the
// devices and accounts that stopped talking. Memories (assist_memories) stay until the person deletes them.
// The assistant apps' call log (ai_calls: sizes, a hash, error codes, never words) is kept 7 days too (2026-11-22).
// The purge must match purgeValues in assist_reply/nodes/talk.go.

const rawMs = 24 * 3600e3
const sessionMs = 7 * 86400e3
const callsMs = 7 * 86400e3 // ai_calls

var topicVN = map[string]string{"gold": "giá vàng", "crypto": "tiền số", "stock": "chứng khoán", "fx": "ngoại tệ", "news": "tin tức",
	"weather": "thời tiết", "time": "ngày giờ", "calendar": "lịch", "alarm": "báo thức", "todo": "nhắc việc", "note": "ghi chú",
	"reading": "đọc sách", "memory": "những điều đã nhớ", "chat": "chuyện phiếm"}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	now := time.Now().UnixMilli()
	res, err := wm.QueryMany([]map[string]any{
		{"table": "assist_convos", "filters": []map[string]any{{"column": "last_ms", "op": "lt", "value": now - int64(sessionMs)}},
			"order_by": "last_ms", "limit": 200, "with_total": false},
		{"table": "assist_convos", "filters": []map[string]any{{"column": "last_ms", "op": "lt", "value": now - int64(rawMs)},
			{"column": "purged_ms", "op": "is_null"}}, "order_by": "last_ms", "limit": 200, "with_total": false},
		{"table": "ai_calls", "filters": []map[string]any{{"column": "at_ms", "op": "lt", "value": now - int64(callsMs)}},
			"order_by": "at_ms", "limit": 500, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return map[string]any{"error": "read"}
	}
	ops := []map[string]any{}
	deleted := map[int]bool{}
	for _, r := range rowsOf(res, 0) {
		deleted[wm.Int(r["id"])] = true
		ops = append(ops, map[string]any{"op": "delete", "table": "assist_convos", "id": wm.Int(r["id"])})
	}
	purged := 0
	for _, r := range rowsOf(res, 1) {
		if deleted[wm.Int(r["id"])] {
			continue
		}
		ops = append(ops, map[string]any{"op": "update", "table": "assist_convos", "id": wm.Int(r["id"]), "values": purgeValues(r, now)})
		purged++
	}
	calls := 0
	for _, r := range rowsOf(res, 2) {
		ops = append(ops, map[string]any{"op": "delete", "table": "ai_calls", "id": wm.Int(r["id"])})
		calls++
	}
	for i := 0; i < len(ops); i += 100 {
		j := i + 100
		if j > len(ops) {
			j = len(ops)
		}
		if _, berr := wm.Batch(ops[i:j]); berr != nil {
			wm.Log("save: ", berr.Error())
			return map[string]any{"error": "save"}
		}
	}
	return map[string]any{"deleted": len(deleted), "purged": purged, "calls_deleted": calls}
}

func turnsOf(row map[string]any) []any {
	t, _ := row["turns"].([]any)
	return t
}

// purgeValues: the same as assist_reply's.
func purgeValues(o map[string]any, nowMs int64) map[string]any {
	v := map[string]any{"turns": []any{}, "purged_ms": nowMs}
	rest := 0
	seen := map[string]bool{}
	about := []string{}
	for _, x := range turnsOf(o) {
		t, _ := x.(map[string]any)
		if t == nil || wm.Int(t["k"]) <= wm.Int(o["sum_k"]) {
			continue
		}
		rest++
		w := topicVN[wm.Str(t["o"])]
		if w == "" {
			w = topicVN["chat"]
		}
		if !seen[w] {
			seen[w] = true
			about = append(about, w)
		}
	}
	if rest == 0 {
		return v
	}
	sum := strings.TrimSpace(wm.Str(o["summary"]))
	if sum == "" {
		v["summary"] = fmt.Sprintf("Nói chuyện %d lượt, về %s.", rest, strings.Join(about, ", "))
	} else {
		v["summary"] = sum + " Sau đó nói thêm về " + strings.Join(about, ", ") + "."
	}
	return v
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
