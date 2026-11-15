// Workflow list_devices, node work (imported by tools/importer).
package work

import (
	"inkdesk/internal/wm"
	"strconv"
	"strings"
)

// "inkdesk-1.2.3" -> 10203 (0 when unreadable), as the device computes it.
func versionCode(v string) int {
	if i := strings.LastIndex(v, "-"); i >= 0 {
		v = v[i+1:]
	}
	p := strings.Split(v, ".")
	if len(p) < 2 {
		return 0
	}
	n := 0
	for i := 0; i < 3; i++ {
		x := 0
		if i < len(p) {
			x, _ = strconv.Atoi(p[i])
		}
		n = n*100 + x
	}
	return n
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	rows, err := wm.QueryOpts("devices", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": uid}, {"column": "revoked", "op": "eq", "value": false}},
		"order_by": "id", "limit": 50,
	})
	if err != nil {
		return wm.Fail(err)
	}
	// The newest firmware on the server (what "Cập nhật qua WiFi" installs).
	var latest map[string]any
	fw, ferr := wm.QueryOpts("firmware", map[string]any{"order_by": "code", "desc": true, "limit": 1})
	if ferr != nil {
		return wm.Fail(ferr)
	}
	if len(fw) == 1 {
		f := fw[0]
		latest = map[string]any{
			"version": wm.Str(f["version"]), "code": wm.Int(f["code"]), "notes": wm.Str(f["notes"]),
			"released": wm.Str(f["released"]), "size": wm.Int(f["size"]),
		}
	}
	out := []any{}
	for _, r := range rows {
		out = append(out, map[string]any{
			"id": wm.Int(r["id"]), "name": wm.Str(r["name"]), "last_sync": wm.Str(r["last_sync"]),
			"battery": r["battery"], "fw": wm.Str(r["fw"]), "fw_code": versionCode(wm.Str(r["fw"])),
			"fw_req": wm.Int(r["fw_req"]), "fw_auto": r["fw_auto"] == true, "ota_state": r["ota_state"],
			"created_at": wm.Str(r["created_at"]),
		})
	}
	return map[string]any{"status": 200, "body": map[string]any{"devices": out, "firmware": latest}}
}
