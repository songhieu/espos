// Workflow device_sync, node x4p (imported by tools/importer).
package x4p

import (
	"encoding/json"
	"inkdesk/internal/wm"
	"strings"
)

// The answer's "pkgs" for an X4 (s3desk/docs/profiles.md §9.2): the packages x4.pkgs lists without "rm", resolved
// from the v2 store catalog (public storage apps/catalog.json, scripts/publish_pkg.py), only those that fit the
// device (every "requires" flag in its caps, its rt[runtime] at api.min or higher), each as its core part:
// {id, name, version, code, type, url, size, sha256}. The X4 never reads a catalog itself.
// ok false (the key is left out: "unknown this time", the device changes nothing) when the catalog cannot be read.
// Any other device: nothing (x4 false), and no catalog read.
var catalogURL = wm.PkgCatalogURL()

// What an X4 that has not said yet can run: the Lite profile (profiles.md §1.3).
var liteCaps = []any{"screen.eink", "input.buttons", "store.sd", "net.wifi", "net.ble", "power.battery"}
var liteRt = map[string]any{"decl": float64(1)}

func kindOf(row map[string]any, fw string) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(fw, "inkdesk-s3-") {
		return "s3"
	}
	return "x4"
}

func asMap(v any) map[string]any {
	m, _ := v.(map[string]any)
	if s, isStr := v.(string); isStr && s != "" {
		_ = json.Unmarshal([]byte(s), &m)
	}
	return m
}

// fits: every flag the package requires is in caps, and the device has its runtime at api.min or higher.
func fits(e map[string]any, caps []any, rt map[string]any) bool {
	have := map[string]bool{}
	for _, c := range caps {
		have[wm.Str(c)] = true
	}
	req, _ := e["requires"].([]any)
	for _, r := range req {
		if !have[wm.Str(r)] {
			return false
		}
	}
	runtime := wm.Str(e["runtime"])
	lvl, has := rt[runtime]
	if runtime == "" || !has {
		return false
	}
	min := 1.0
	if api, ok := e["api"].(map[string]any); ok {
		if n, isNum := api["min"].(float64); isNum {
			min = n
		}
	}
	return wm.Num(lvl) >= min
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	no := map[string]any{"x4": false, "ok": false, "pkgs": []any{}}
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": wm.Int(in["device_id"])}}, "limit": 1})
	if err != nil {
		wm.Log("devices: ", err.Error())
		return no
	}
	dev := map[string]any{}
	if len(devs) == 1 {
		dev = devs[0]
	}
	if kindOf(dev, wm.Str(in["fw"])) != "x4" {
		return no
	}
	// What the user wants (the merged x4.pkgs this answer carries).
	settings, _ := in["settings"].(map[string]any)
	var wish []any
	if s := wm.Str(settings["x4.pkgs"]); s != "" {
		_ = json.Unmarshal([]byte(s), &wish)
	}
	want := []string{}
	for _, it := range wish {
		m, _ := it.(map[string]any)
		if id := wm.Str(m["id"]); id != "" && m["rm"] != true {
			want = append(want, id)
		}
	}
	if len(want) == 0 {
		return map[string]any{"x4": true, "ok": true, "pkgs": []any{}}
	}
	// What the device can run: this sync's body, else its last report (devices.caps), else the Lite profile.
	caps, _ := in["caps"].([]any)
	rt, _ := in["rt"].(map[string]any)
	stored := asMap(dev["caps"])
	if caps == nil {
		caps, _ = stored["caps"].([]any)
	}
	if rt == nil {
		rt, _ = stored["rt"].(map[string]any)
	}
	if caps == nil {
		caps = liteCaps
	}
	if rt == nil {
		rt = liteRt
	}
	res, ferr := wm.FetchWith("GET", catalogURL, nil, map[string]any{"Cache-Control": "no-cache"})
	if ferr != nil {
		wm.Log("catalog: ", ferr.Error())
		return map[string]any{"x4": true, "ok": false, "pkgs": []any{}}
	}
	body := asMap(res["body"])
	if wm.Int(res["status"]) != 200 || body == nil {
		wm.Log("catalog: HTTP ", wm.Int(res["status"]))
		return map[string]any{"x4": true, "ok": false, "pkgs": []any{}}
	}
	list, _ := body["packages"].([]any)
	byID := map[string]map[string]any{}
	for _, x := range list {
		if e, ok := x.(map[string]any); ok {
			byID[wm.Str(e["id"])] = e
		}
	}
	out := []any{}
	for _, id := range want {
		e := byID[id]
		if e == nil || !fits(e, caps, rt) {
			continue // not in the store (any more), or it does not run here: the web shows why
		}
		parts, _ := e["parts"].([]any)
		var core map[string]any
		for _, p := range parts {
			if pm, ok := p.(map[string]any); ok && wm.Str(pm["id"]) == "core" {
				core = pm
			}
		}
		if core == nil || wm.Str(core["url"]) == "" {
			continue
		}
		out = append(out, map[string]any{
			"id": id, "name": wm.Str(e["name"]), "version": wm.Str(e["version"]), "code": wm.Int(e["code"]),
			"type": wm.Str(e["type"]), "url": wm.Str(core["url"]), "size": wm.Int(core["size"]), "sha256": wm.Str(core["sha256"]),
		})
	}
	return map[string]any{"x4": true, "ok": true, "pkgs": out}
}
