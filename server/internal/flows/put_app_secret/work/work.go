// Workflow put_app_secret, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"strings"
	"time"
)

// POST /v1/devices/:id/secrets (end_user): a package's secret that the server keeps (settings field of type
// "secret" with "where": "server", s3desk docs/manifest-driven.md §1.4). Body:
//   {"pkg": "<package id>", "key": "<field key>", "value": "<the secret>"}   set or replace it
//   {"pkg": "<package id>", "key": "<field key>", "remove": true}           remove it
// DELETE /v1/devices/:id/secrets?pkg=<package id>&key=<field key> removes it too (the web's removeAppSecret).
// Answers {"ok": true, "set": bool, "hint": "4F2A" (the last 4 characters), "ms"}. The value is never returned by this or any other route; the device gets it
// only when the field has "send_to" (device_sync, once), and the server's fetch rules use it.
// Checks: the device is the caller's and not revoked; the package is installed on it (its last report or the store's
// wanted list); the package's schema (app_manifests) has that key as a server secret; the value is 1-500 characters
// without spaces at either end or control characters.

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	p, _ := in["params"].(map[string]any)
	b, _ := in["body"].(map[string]any)
	q, _ := in["query"].(map[string]any)
	if strings.EqualFold(wm.Str(in["method"]), "DELETE") {
		b = map[string]any{"pkg": q["pkg"], "key": q["key"], "remove": true}
	}
	deviceID := wm.Int(p["id"])
	pkg := strings.TrimSpace(wm.Str(b["pkg"]))
	key := strings.TrimSpace(wm.Str(b["key"]))
	remove := wm.Bool(b["remove"])
	value := wm.Str(b["value"])
	if pkg == "" || key == "" || (!remove && value == "") {
		return bad("pkg, key and value (or remove) are needed", "secret_invalid")
	}
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": deviceID}, {"column": "user_id", "op": "eq", "value": uid},
		{"column": "revoked", "op": "eq", "value": false}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(devs) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	rows, err := wm.QueryOpts("app_manifests", map[string]any{"filters": []map[string]any{
		{"column": "pkg_id", "op": "eq", "value": pkg}, {"column": "user_id", "op": "in", "value": []any{0, uid}}}, "limit": 2})
	if err != nil {
		return wm.Fail(err)
	}
	mans := []map[string]any{}
	for _, own := range []bool{true, false} { // the person's own upload of the id first, else the store's
		for _, r := range rows {
			if (wm.Int(r["user_id"]) != 0) == own && len(mans) == 0 {
				mans = append(mans, r)
			}
		}
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
	if !installed(devs[0]["pkgs"], w)[pkg] {
		return bad(pkg+" is not installed on this device", "app_not_installed")
	}
	var field map[string]any
	if len(mans) == 1 {
		list, _ := mans[0]["settings"].([]any)
		for _, x := range list {
			if f, _ := x.(map[string]any); f != nil && wm.Str(f["key"]) == key {
				field = f
			}
		}
	}
	if field == nil || wm.Str(field["type"]) != "secret" {
		return bad(pkg+" has no secret "+key, "unknown_app_key")
	}
	if wm.Str(field["where"]) != "server" {
		return bad(key+" is kept on the device only: set it there", "secret_device_only")
	}
	bound := appBoundKeys(mans[0]["auth"])[key] // used by an auth binding: the device gets it (device_sync)
	rows, err = wm.QueryOpts("app_secrets", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": deviceID}, {"column": "pkg_id", "op": "eq", "value": pkg},
		{"column": "skey", "op": "eq", "value": key}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if remove {
		if len(rows) == 1 && bound && wm.Num(rows[0]["delivered_ms"]) > 0 {
			// the device holds it: an empty row tells it to forget the secret, deleted once it acks (device_sync)
			if _, uerr := wm.Update("app_secrets", wm.Int(rows[0]["id"]), map[string]any{"value": "", "last4": "",
				"updated_ms": time.Now().UnixMilli(), "delivered_ms": 0}); uerr != nil {
				return wm.Fail(uerr)
			}
		} else if len(rows) == 1 {
			if derr := wm.Delete("app_secrets", wm.Int(rows[0]["id"])); derr != nil {
				return wm.Fail(derr)
			}
		}
		return map[string]any{"status": 200, "body": map[string]any{"ok": true, "set": false, "hint": "", "ms": time.Now().UnixMilli()}}
	}
	if why := secretOK(value); why != "" {
		return bad(why, "secret_invalid")
	}
	r := []rune(value)
	last4 := string(r[len(r)-min4(len(r)):])
	ms := time.Now().UnixMilli()
	vals := map[string]any{"device_id": deviceID, "user_id": uid, "pkg_id": pkg, "skey": key, "value": value,
		"last4": last4, "updated_ms": ms, "delivered_ms": 0}
	if _, uerr := wm.Upsert("app_secrets", []string{"device_id", "pkg_id", "skey"}, vals, nil); uerr != nil {
		return wm.Fail(uerr)
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true, "set": true, "hint": last4, "ms": ms}}
}

func bad(why, code string) map[string]any {
	return map[string]any{"status": 422, "body": map[string]any{"error": why, "code": code}}
}

func min4(n int) int {
	if n < 4 {
		return n
	}
	return 4
}

// secretOK: 1-500 characters, no spaces at either end, no control characters.
func secretOK(v string) string {
	if n := len([]rune(v)); n < 1 || n > 2000 {
		return "a secret is 1 to 2000 characters"
	}
	if strings.TrimSpace(v) != v {
		return "no spaces at the start or the end"
	}
	for _, c := range v {
		if c < 0x20 || c == 0x7f {
			return "no control characters"
		}
	}
	return ""
}

// installed: the device's packages from its last report and the store's wanted list (as appInstalled in
// put_device_settings, scripts/appset/appset.go).
func installed(report any, wanted string) map[string]bool {
	out := map[string]bool{}
	if r, _ := report.(map[string]any); r != nil {
		list, _ := r["installed"].([]any)
		for _, x := range list {
			if p, _ := x.(map[string]any); p != nil && wm.Str(p["id"]) != "" {
				out[wm.Str(p["id"])] = true
			}
		}
	}
	for _, x := range jsonList(wanted) {
		if p, _ := x.(map[string]any); p != nil && wm.Str(p["id"]) != "" && p["rm"] != true {
			out[wm.Str(p["id"])] = true
		}
	}
	return out
}

func jsonList(s string) []any {
	var out []any
	if json.Unmarshal([]byte(s), &out) != nil {
		return nil
	}
	return out
}

// appBoundKeys: the secret keys a package's "auth" bindings use (as appBound in scripts/appset/appset.go).
func appBoundKeys(auth any) map[string]bool {
	out := map[string]bool{}
	a, _ := auth.(map[string]any)
	for _, x := range a {
		b, _ := x.(map[string]any)
		if b == nil {
			continue
		}
		if l, ok := b["basic"].([]any); ok {
			for _, k := range l {
				out[wm.Str(k)] = true
			}
		}
		if s := wm.Str(b["bearer"]); s != "" {
			out[s] = true
		}
		for _, part := range []string{"header", "query"} {
			if m, ok := b[part].(map[string]any); ok {
				for _, k := range m {
					out[wm.Str(k)] = true
				}
			}
		}
	}
	return out
}
