// Workflow list_devices, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"strconv"
	"strings"
	"time"
)

// "espos-x4-1.2.3" -> 10203 (0 when unreadable), as the device computes it.
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

// remoteOf: the S3's remote control (its Settings › Điều khiển từ xa). on: the device called device_remote in the
// last minute with the switch on (it calls every few seconds while on): the web can watch it over the internet.
// url, pin: its page in the home network, as its last sync told (diag rc_url, rc_pin; under 3 hours old).
// boardOf: which S3 board, as its firmware found it at boot and sends in every sync (diag.board; s3desk/docs/boards.md).
// "" for an X4, or an S3 that has not synced with that firmware yet.
func boardOf(r map[string]any) string {
	d, ok := r["diag"].(map[string]any)
	if !ok {
		if s, isStr := r["diag"].(string); isStr && s != "" {
			_ = json.Unmarshal([]byte(s), &d)
		}
	}
	if b := wm.Str(d["board"]); b == "es3c35p" || b == "es3c28p" {
		return b
	}
	return ""
}

// pingOf: how often (seconds) the device asks whether the web changed something (GET /v1/device/ping, S3 firmware
// from 2026-11-17 on, told in every sync as diag.ping), so the web can say a change arrives within that; 0 when its
// firmware does not ask (it gets changes at its next sync).
func pingOf(r map[string]any) int {
	d, ok := r["diag"].(map[string]any)
	if !ok {
		if s, isStr := r["diag"].(string); isStr && s != "" {
			_ = json.Unmarshal([]byte(s), &d)
		}
	}
	if p := wm.Int(d["ping"]); p > 0 && p <= 600 {
		return p
	}
	return 0
}

func remoteOf(r map[string]any, sess map[string]any, now int64) map[string]any {
	on := sess != nil && wm.Bool(sess["enabled"]) && now-int64(wm.Int(sess["seen_at"])) < 60000
	d, ok := r["diag"].(map[string]any)
	if !ok {
		if s, isStr := r["diag"].(string); isStr && s != "" {
			_ = json.Unmarshal([]byte(s), &d)
		}
	}
	u := wm.Str(d["rc_url"])
	if at := int64(wm.Int(r["diag_at"])); at == 0 || now-at > 3*3600*1000 {
		u = ""
	}
	if !on && u == "" {
		return nil
	}
	out := map[string]any{"on": on}
	if u != "" {
		out["url"] = u
		out["pin"] = wm.Str(d["rc_pin"])
	}
	return out
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
	// The newest firmware per board (what "Cập nhật qua WiFi" installs on that kind of device).
	fwRows, ferr := wm.QueryOpts("firmware", map[string]any{"order_by": "code", "desc": true, "limit": 50})
	if ferr != nil {
		return wm.Fail(ferr)
	}
	latestBy := map[string]map[string]any{}
	for _, f := range fwRows {
		k := fwKind(f)
		if latestBy[k] != nil {
			continue
		}
		latestBy[k] = map[string]any{
			"version": wm.Str(f["version"]), "code": wm.Int(f["code"]), "notes": wm.Str(f["notes"]),
			"released": wm.Str(f["released"]), "size": wm.Int(f["size"]),
		}
	}
	latest := latestBy["x4"]
	sessRows, serr := wm.QueryOpts("remote_sessions", map[string]any{
		"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 50,
	})
	if serr != nil {
		return wm.Fail(serr)
	}
	sessBy := map[int]map[string]any{}
	for _, x := range sessRows {
		sessBy[wm.Int(x["device_id"])] = x
	}
	now := time.Now().UnixMilli()
	out := []any{}
	for _, r := range rows {
		out = append(out, map[string]any{
			"id": wm.Int(r["id"]), "name": wm.Str(r["name"]), "last_sync": wm.Str(r["last_sync"]),
			"battery": r["battery"], "fw": wm.Str(r["fw"]), "fw_code": versionCode(wm.Str(r["fw"])),
			"fw_req": wm.Int(r["fw_req"]), "fw_auto": r["fw_auto"] == true, "ota_state": r["ota_state"],
			"created_at": wm.Str(r["created_at"]),
			"kind":       devKind(r), "firmware": latestBy[devKind(r)], "remote": remoteOf(r, sessBy[wm.Int(r["id"])], now),
			"board": boardOf(r), "ping": pingOf(r),
		})
	}
	return map[string]any{"status": 200, "body": map[string]any{"devices": out, "firmware": latest}}
}
