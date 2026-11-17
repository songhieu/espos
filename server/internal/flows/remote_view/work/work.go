// Workflow remote_view, node work (imported by tools/importer).
package work

import (
	"inkdesk/internal/wm"
	"time"
)

// GET /v1/devices/:id/remote?after=SEQ: the owner's browser asks for the screen (about 4 times a second). Each call
// keeps the device sending for 12 s more (watch_until). The frame comes only when newer than `after`.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	in := wmIn
	p, _ := in["params"].(map[string]any)
	q, _ := in["query"].(map[string]any)
	dev := wm.Int(p["id"])
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": dev}, {"column": "user_id", "op": "eq", "value": uid},
		{"column": "revoked", "op": "eq", "value": false}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(devs) == 0 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "no such device"}}
	}
	now := time.Now().UnixMilli()
	rows, err := wm.QueryOpts("remote_sessions", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": dev}}, "limit": 1,
	})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) == 0 {
		return map[string]any{"status": 200, "body": map[string]any{"on": false, "seq": 0}}
	}
	r := rows[0]
	if _, err := wm.Update("remote_sessions", r["id"], map[string]any{"watch_until": now + 12000}); err != nil {
		return wm.Fail(err)
	}
	seen := int64(wm.Int(r["seen_at"]))
	out := map[string]any{
		"on": wm.Bool(r["enabled"]), "seq": wm.Int(r["seq"]), "seen_ago": now - seen,
		"w": wm.Int(r["w"]), "h": wm.Int(r["h"]), "frame_ago": now - int64(wm.Int(r["frame_at"])),
	}
	if wm.Int(r["seq"]) > wm.Int(q["after"]) && wm.Str(r["frame"]) != "" {
		out["frame"] = wm.Str(r["frame"])
	}
	return map[string]any{"status": 200, "body": out}
}
