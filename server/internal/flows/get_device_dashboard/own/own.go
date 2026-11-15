// Workflow get_device_dashboard, node own (imported by tools/importer).
package own

import "inkdesk/internal/wm"

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"ok": false}
	}
	p, _ := wmIn["params"].(map[string]any)
	id := wm.Int(p["id"])
	rows, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid},
		{"column": "revoked", "op": "eq", "value": false}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return map[string]any{"ok": false}
	}
	return map[string]any{"ok": true, "device_id": id, "user_id": uid}
}
