// Workflow device_firmware, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"strings"
	"time"
)

// devKind: the board of a device row ("" on rows made before the column = x4, unless its
// firmware says S3).
func devKind(r map[string]any) string {
	if k := wm.Str(r["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(wm.Str(r["fw"]), "espos-s3-") {
		return "s3"
	}
	return "x4"
}

// fwKind: the board of a firmware row (empty = x4).
func fwKind(f map[string]any) string {
	if k := wm.Str(f["kind"]); k != "" {
		return k
	}
	return "x4"
}

// Body: {"action": "update"} asks the device to install the newest firmware at its next sync;
// {"action": "cancel"} withdraws that; {"auto": true|false} turns auto-update (while charging) on or off.
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
		{"column": "id", "op": "eq", "value": devID}, {"column": "user_id", "op": "eq", "value": uid},
		{"column": "revoked", "op": "eq", "value": false}}, "limit": 1})
	if derr != nil {
		return wm.Fail(derr)
	}
	if len(devs) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	b, _ := wmIn["body"].(map[string]any)
	if auto, ok := b["auto"].(bool); ok {
		// fw_auto_ms: a colour desk's own switch wins only when newer (device_sync).
		if _, err := wm.Update("devices", devID, map[string]any{"fw_auto": auto, "fw_auto_ms": time.Now().UnixMilli()}); err != nil {
			return wm.Fail(err)
		}
		return map[string]any{"status": 200, "body": map[string]any{"ok": true, "fw_auto": auto}}
	}
	now := time.Now().UTC().Format(time.RFC3339)
	switch wm.Str(b["action"]) {
	case "update":
		all, ferr := wm.QueryOpts("firmware", map[string]any{"order_by": "code", "desc": true, "limit": 50})
		if ferr != nil {
			return wm.Fail(ferr)
		}
		fw := []map[string]any{}
		for _, f := range all {
			if fwKind(f) == devKind(devs[0]) {
				fw = append(fw, f)
				break
			}
		}
		if len(fw) != 1 {
			return map[string]any{"status": 409, "body": map[string]any{"error": "Chưa có bản firmware nào trên server"}}
		}
		state := map[string]any{"state": "requested", "version": wm.Str(fw[0]["version"]), "at": now}
		if _, err := wm.Update("devices", devID, map[string]any{"fw_req": wm.Int(fw[0]["code"]), "ota_state": state}); err != nil {
			return wm.Fail(err)
		}
		return map[string]any{"status": 200, "body": map[string]any{"ok": true, "ota_state": state}}
	case "cancel":
		if _, err := wm.Update("devices", devID, map[string]any{"fw_req": 0, "ota_state": map[string]any{"state": "", "at": now}}); err != nil {
			return wm.Fail(err)
		}
		return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
	}
	return map[string]any{"status": 422, "body": map[string]any{"error": "bad action"}}
}
