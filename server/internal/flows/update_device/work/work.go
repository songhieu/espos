// Workflow update_device, node work (imported by tools/importer).
package work

import (
	"inkdesk/internal/wm"
	"strings"
	"time"
)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	b, _ := wmIn["body"].(map[string]any)
	id := wm.Int(b["id"])
	rows, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	vals := map[string]any{}
	if v, ok := b["name"]; ok {
		n := strings.TrimSpace(wm.Str(v))
		if n == "" || len([]rune(n)) > 40 {
			return map[string]any{"status": 422, "body": map[string]any{"error": "Tên máy: 1–40 ký tự"}}
		}
		vals["name"] = n
		vals["name_ms"] = time.Now().UnixMilli() // a colour desk's own rename wins only when newer (device_sync)
	}
	if v, ok := b["revoked"]; ok && wm.Bool(v) {
		vals["revoked"] = true
	}
	if len(vals) > 0 {
		if _, uerr := wm.Update("devices", id, vals); uerr != nil {
			return wm.Fail(uerr)
		}
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
}
