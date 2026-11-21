// Workflow desk_app_auth, node who (imported by tools/importer).
package who

import (
	"encoding/json"
	"espos/internal/wm"
	"regexp"
	"strings"
)

// After require_device: a device app's call on /v1/device/app/<app id>/... (desktop-protocol §9.0; lead's final call
// 2026-11-21). The S3's OS adds the device credential only for the calling app's own path (net:espos, the espos: URL
// scheme), so the path's app id is the caller; it must be a package installed on this device (its last report, or the
// store's wanted list while it installs; "rm" = not). JS never sees a token.

var appRe = regexp.MustCompile(`^/v1/device/app/([a-z][a-z0-9_]*(\.[a-z0-9_]+){1,5})(/|$)`)

func no(status int, code string) map[string]any {
	return map[string]any{"ok": false, "status": status, "error": code, "device_id": 0, "user_id": 0}
}

func installed(report any, wanted string, id string) bool {
	r, _ := report.(map[string]any)
	if s, isText := report.(string); isText && s != "" {
		json.Unmarshal([]byte(s), &r)
	}
	if r != nil {
		list, _ := r["installed"].([]any)
		for _, x := range list {
			if p, _ := x.(map[string]any); p != nil && wm.Str(p["id"]) == id {
				return true
			}
		}
	}
	var w []any
	if json.Unmarshal([]byte(wanted), &w) == nil {
		for _, x := range w {
			if p, _ := x.(map[string]any); p != nil && wm.Str(p["id"]) == id && p["rm"] != true {
				return true
			}
		}
	}
	return false
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	d, _ := in["device"].(map[string]any)
	devID, uid := wm.Int(d["device_id"]), wm.Int(d["user_id"])
	if devID == 0 || uid == 0 {
		return no(401, "unauthorized")
	}
	m := appRe.FindStringSubmatch(strings.TrimSpace(wm.Str(in["path"])))
	if m == nil {
		return no(404, "not_found")
	}
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": devID}}, "limit": 1, "with_total": false},
		{"table": "device_settings", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": devID},
			{"column": "skey", "op": "eq", "value": "s3.pkgs"}}, "limit": 1, "with_total": false},
	})
	if err != nil {
		return wm.Fail(err)
	}
	at := func(i int) []map[string]any {
		out := []map[string]any{}
		switch r := res[i]["rows"].(type) {
		case []map[string]any:
			return r
		case []any:
			for _, x := range r {
				if mm, ok := x.(map[string]any); ok {
					out = append(out, mm)
				}
			}
		}
		return out
	}
	devs, sets := at(0), at(1)
	if len(devs) != 1 || wm.Bool(devs[0]["revoked"]) || wm.Int(devs[0]["user_id"]) != uid {
		return no(401, "unauthorized")
	}
	wanted := ""
	if len(sets) == 1 {
		wanted = wm.Str(sets[0]["svalue"])
	}
	if !installed(devs[0]["pkgs"], wanted, m[1]) {
		return no(403, "app_not_installed")
	}
	return map[string]any{"ok": true, "status": 200, "error": "", "device_id": devID, "user_id": uid, "pkg_id": m[1],
		"dev_name": wm.Str(devs[0]["name"])}
}
