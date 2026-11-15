// Workflow list_wifi, node work (imported by tools/importer).
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
	devID := wm.Int(wmIn["device_id"])
	devs, derr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": devID}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if derr != nil {
		return wm.Fail(derr)
	}
	if len(devs) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	saved := devs[0]["wifi_state"]
	if saved == nil {
		saved = []any{}
	}
	rows, err := wm.QueryOpts("wifi_ops", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": devID}, {"column": "status", "op": "in", "value": []any{"pending", "failed"}}},
		"order_by": "id", "limit": 50})
	if err != nil {
		return wm.Fail(err)
	}
	ops := []any{}
	for _, r := range rows {
		// Never the password.
		ops = append(ops, map[string]any{"id": wm.Int(r["id"]), "op": wm.Str(r["op"]), "ssid": wm.Str(r["ssid"]),
			"status": wm.Str(r["status"]), "result": wm.Str(r["result"])})
	}
	return map[string]any{"status": 200, "body": map[string]any{"saved": saved, "ops": ops}}
}
