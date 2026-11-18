// Workflow save_wifi, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"strings"
)

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
	b, _ := wmIn["body"].(map[string]any)
	op := wm.Str(b["op"])
	if op == "cancel" {
		id := wm.Int(b["id"])
		rows, err := wm.QueryOpts("wifi_ops", map[string]any{"filters": []map[string]any{
			{"column": "id", "op": "eq", "value": id}, {"column": "device_id", "op": "eq", "value": devID}}, "limit": 1})
		if err != nil {
			return wm.Fail(err)
		}
		if len(rows) == 1 {
			if derr := wm.Delete("wifi_ops", id); derr != nil {
				return wm.Fail(derr)
			}
		}
		return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
	}
	ssid := strings.TrimSpace(wm.Str(b["ssid"]))
	pass := wm.Str(b["pass"])
	if len(ssid) == 0 || len(ssid) > 32 {
		return map[string]any{"status": 422, "body": map[string]any{"error": "Tên WiFi từ 1 đến 32 ký tự"}}
	}
	if op != "add" && op != "remove" {
		return map[string]any{"status": 422, "body": map[string]any{"error": "bad op"}}
	}
	if op == "add" && pass != "" && (len(pass) < 8 || len(pass) > 63) {
		return map[string]any{"status": 422, "body": map[string]any{"error": "Mật khẩu WiFi từ 8 đến 63 ký tự (để trống nếu mạng không có mật khẩu)"}}
	}
	if op == "remove" {
		pass = ""
	}
	// A newer change for the same network replaces any that is still waiting.
	old, qerr := wm.QueryOpts("wifi_ops", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": devID}, {"column": "ssid", "op": "eq", "value": ssid},
		{"column": "status", "op": "in", "value": []any{"pending", "failed"}}}, "limit": 20})
	if qerr != nil {
		return wm.Fail(qerr)
	}
	for _, r := range old {
		if derr := wm.Delete("wifi_ops", wm.Int(r["id"])); derr != nil {
			return wm.Fail(derr)
		}
	}
	row, err := wm.Insert("wifi_ops", map[string]any{"device_id": devID, "user_id": uid, "op": op, "ssid": ssid, "pass": pass, "status": "pending"})
	if err != nil {
		return wm.Fail(err)
	}
	return map[string]any{"status": 201, "body": map[string]any{"id": wm.Int(row["id"])}}
}
