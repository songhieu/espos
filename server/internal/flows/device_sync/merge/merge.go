// Workflow device_sync, node merge (imported by tools/importer).
package merge

import (
	"inkdesk/internal/wm"
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
	// Stamped last, after the dashboard was built: the device sets its clock from it.
	body["now_ms"] = time.Now().UnixMilli()
	return map[string]any{"body": body}
}
