// Workflow list_dev_keys, node work (imported by tools/importer).
package work

import "espos/internal/wm"

// The caller's developer keys for Tài khoản › Khoá nhà phát triển: what tells them apart, never the hash.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "Cần đăng nhập", "code": "not_signed_in"}}
	}
	rows, err := wm.QueryOpts("dev_keys", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
		"order_by": "id", "desc": true, "limit": 20,
	})
	if err != nil {
		return wm.Fail(err)
	}
	out := []any{}
	for _, r := range rows {
		out = append(out, map[string]any{
			"id": wm.Int(r["id"]), "label": wm.Str(r["label"]), "hint": wm.Str(r["hint"]),
			"created_at": wm.Str(r["created_at"]), "last_used_ms": wm.Int(r["last_used_ms"]),
		})
	}
	return map[string]any{"status": 200, "body": map[string]any{"keys": out, "max": 5}}
}
