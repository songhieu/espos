// Workflow list_photos, node work (imported by tools/importer).
package work

import "inkdesk/internal/wm"

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	rows, err := wm.QueryOpts("photos", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": uid}, {"column": "status", "op": "eq", "value": "ready"}},
		"order_by": "pos", "desc": true, "limit": 100,
	})
	if err != nil {
		return wm.Fail(err)
	}
	out := []any{}
	for _, r := range rows {
		out = append(out, map[string]any{"id": wm.Int(r["id"]), "thumb": wm.Str(r["thumb"]), "w": wm.Int(r["w"]), "h": wm.Int(r["h"])})
	}
	return map[string]any{"status": 200, "body": map[string]any{"photos": out}}
}
