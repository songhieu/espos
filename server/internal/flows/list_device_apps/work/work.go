// Workflow list_device_apps, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"strings"
)

// GET /v1/devices/:id/apps[?pkg=<package id>] (end_user): the apps on the caller's device that have a settings schema
// (manifest v2 "settings", s3desk docs/manifest-driven.md §1), for the web's app settings page (web/app/src/n2/
// AppSettings.tsx, useAppSettings.ts):
//   {"apps": [{"id", "name", "version", "code", "settings": [...], "secrets": {"<key>": {"where", "set", "hint", "ms"}}}]}
// An app is on the device when its last report lists it or the store's wanted list (s3.pkgs) does. The schema comes
// from app_manifests. A server-kept secret says whether it is set and its last 4 characters (hint), never the value;
// a device-kept one says whether the device's store holds it (devices.app_secrets, from its sync), null until it says. The values themselves are the device
// settings app.<id>.<key> (GET /v1/devices/:id/settings).

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	p, _ := in["params"].(map[string]any)
	q, _ := in["query"].(map[string]any)
	deviceID := wm.Int(p["id"])
	only := strings.TrimSpace(wm.Str(q["pkg"]))
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": deviceID}, {"column": "user_id", "op": "eq", "value": uid},
		{"column": "revoked", "op": "eq", "value": false}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(devs) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	wanted, err := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": deviceID}, {"column": "skey", "op": "eq", "value": "s3.pkgs"}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	w := ""
	if len(wanted) == 1 {
		w = wm.Str(wanted[0]["svalue"])
	}
	names := map[string]string{}
	ids := []any{}
	for id, name := range installed(devs[0]["pkgs"], w) {
		if only == "" || id == only {
			names[id] = name
			ids = append(ids, id)
		}
	}
	apps := []any{}
	if len(ids) == 0 {
		return map[string]any{"status": 200, "body": map[string]any{"apps": apps}}
	}
	rows, err := wm.QueryOpts("app_manifests", map[string]any{"filters": []map[string]any{
		{"column": "pkg_id", "op": "in", "value": ids}, {"column": "user_id", "op": "in", "value": []any{0, uid}}}, "limit": 100})
	if err != nil {
		return wm.Fail(err)
	}
	secs, err := wm.QueryOpts("app_secrets", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 100})
	if err != nil {
		return wm.Fail(err)
	}
	// one row per package: the person's own upload of an id wins over the store's
	pick := map[string]map[string]any{}
	for _, own := range []bool{false, true} {
		for _, r := range rows {
			if (wm.Int(r["user_id"]) != 0) == own {
				pick[wm.Str(r["pkg_id"])] = r
			}
		}
	}
	mans := []map[string]any{}
	for _, r := range pick {
		mans = append(mans, r)
	}
	kept := map[string]map[string]any{}
	for _, s := range secs {
		if wm.Str(s["value"]) != "" { // an empty row is a removal the device has not acked yet
			kept[wm.Str(s["pkg_id"])+"."+wm.Str(s["skey"])] = s
		}
	}
	held, _ := devs[0]["app_secrets"].(map[string]any) // nil until the device reports its secrets
	for _, m := range mans {
		id := wm.Str(m["pkg_id"])
		fields, _ := m["settings"].([]any)
		if len(fields) == 0 {
			continue
		}
		secrets := map[string]any{}
		for _, x := range fields {
			f, _ := x.(map[string]any)
			if f == nil || wm.Str(f["type"]) != "secret" {
				continue
			}
			key := wm.Str(f["key"])
			if wm.Str(f["where"]) == "server" {
				if s := kept[id+"."+key]; s != nil {
					secrets[key] = map[string]any{"where": "server", "set": true, "hint": wm.Str(s["last4"]), "ms": wm.Num(s["updated_ms"])}
				} else {
					secrets[key] = map[string]any{"where": "server", "set": false}
				}
			} else if held == nil {
				secrets[key] = map[string]any{"where": "device", "set": nil}
			} else {
				has := false
				keys, _ := held[id].([]any)
				for _, k := range keys {
					has = has || wm.Str(k) == key
				}
				secrets[key] = map[string]any{"where": "device", "set": has}
			}
		}
		name := names[id]
		if name == "" {
			name = id
		}
		apps = append(apps, map[string]any{"id": id, "name": name, "version": wm.Str(m["version"]), "code": wm.Int(m["code"]),
			"settings": fields, "secrets": secrets})
	}
	return map[string]any{"status": 200, "body": map[string]any{"apps": apps}}
}

// installed: the device's packages and their names, from its last report ({"installed": [{"id", "name"}]}) and the
// store's wanted list (s3.pkgs: [{"id", "rm"}]); as appInstalled in put_device_settings (scripts/appset/appset.go).
func installed(report any, wanted string) map[string]string {
	out := map[string]string{}
	if r, _ := report.(map[string]any); r != nil {
		list, _ := r["installed"].([]any)
		for _, x := range list {
			if p, _ := x.(map[string]any); p != nil && wm.Str(p["id"]) != "" {
				out[wm.Str(p["id"])] = wm.Str(p["name"])
			}
		}
	}
	var w []any
	if json.Unmarshal([]byte(wanted), &w) == nil {
		for _, x := range w {
			if p, _ := x.(map[string]any); p != nil && wm.Str(p["id"]) != "" && p["rm"] != true {
				if _, has := out[wm.Str(p["id"])]; !has {
					out[wm.Str(p["id"])] = ""
				}
			}
		}
	}
	return out
}
