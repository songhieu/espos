// Workflow device_sync, node merge (imported by tools/importer).
package merge

import (
	"espos/internal/wm"
	"time"
)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	body, _ := in["sync"].(map[string]any)
	if body == nil {
		body = map[string]any{}
	}
	body["dashboard"] = in["dashboard"]
	// A colour desk gets its owner's own Java games (an X4 answer stays as it was).
	if wm.Bool(in["s3"]) {
		list, _ := in["user_pkgs"].([]any)
		if list == nil {
			list = []any{}
		}
		body["user_pkgs"] = list
	}
	// An X4 gets the packages x4.pkgs asks for, resolved from the store (profiles.md §9.2); left out when the store
	// could not be read this time (x4p failed or ok false), so the device changes nothing.
	if wm.Bool(in["x4"]) && wm.Bool(in["x4_ok"]) {
		list, _ := in["x4_pkgs"].([]any)
		if list == nil {
			list = []any{}
		}
		body["pkgs"] = list
	}
	// The EspOS Bot: a small weather object for its standby screen, from the dashboard built above.
	if wm.Str(body["kind"]) == "bot" {
		if d, ok := in["dashboard"].(map[string]any); ok {
			if w, isMap := d["weather"].(map[string]any); isMap {
				body["weather"] = map[string]any{"city": w["city"], "t": w["temp"], "icon": w["icon"], "desc": w["desc"],
					"hi": w["max"], "lo": w["min"], "rain": w["rain"], "hum": w["hum"], "at": d["now_ms"]}
			}
		}
	}
	// The licence (hosted espos.dev only: the route's guard admin_lic, which the self-hosted server does not have).
	// No "lic" key = no licensing: the device unlocks everything. Its changes raise settings_rev, as device_ping's rev.
	if g, ok := in["guards"].(map[string]any); ok {
		if l, isMap := g["admin_lic"].(map[string]any); isMap {
			if lic, has := l["lic"].(map[string]any); has && lic != nil {
				body["lic"] = lic
				if ms := int64(wm.Num(l["lic_ms"])); ms > int64(wm.Num(body["settings_rev"])) {
					body["settings_rev"] = ms
				}
			}
		}
	}
	// Stamped last, after the dashboard was built: the device sets its clock from it.
	body["now_ms"] = time.Now().UnixMilli()
	return map[string]any{"body": body}
}
