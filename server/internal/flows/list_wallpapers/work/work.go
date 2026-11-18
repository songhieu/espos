// Workflow list_wallpapers, node work (imported by tools/importer).
package work

import "espos/internal/wm"

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	rows, err := wm.QueryOpts("wallpapers", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": uid}, {"column": "status", "op": "eq", "value": "ready"}},
		"order_by": "pos", "limit": 50,
	})
	if err != nil {
		return wm.Fail(err)
	}
	out := []any{}
	for _, r := range rows {
		out = append(out, map[string]any{"id": wm.Int(r["id"]), "orient": wm.Str(r["orient"]), "thumb": wm.Str(r["thumb"]), "size": wm.Int(r["size"])})
	}
	return map[string]any{"status": 200, "body": map[string]any{"wallpapers": out}}
}
