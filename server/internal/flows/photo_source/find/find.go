// Workflow photo_source, node find (imported by tools/importer).
package find

import "espos/internal/wm"

// The original of one of my pictures, for the browser to render it again at a new widget size.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	id := wm.Int(wmIn["id"])
	if uid == 0 || id <= 0 {
		return map[string]any{"ok": false}
	}
	rows, err := wm.QueryOpts("photos", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 || wm.Str(rows[0]["status"]) != "ready" {
		return map[string]any{"ok": false}
	}
	return map[string]any{"ok": true, "key": wm.Str(rows[0]["file_key"])}
}
