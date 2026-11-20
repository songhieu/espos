// Workflow admin_sign, node check (imported by tools/importer).
package check

import (
	"espos/internal/wm"
	"strings"
)

// The store's files only (the route's guards: require_admin, admin_require_full):
//   - apps-s3/<name>: public; .espk (packages, Java games wrapped by the page), .jpg (icons, screenshots) or .json
//     (the catalog, its backups, the admin's drafts);
//   - paid-s3/<name>.espk: PRIVATE, the copies of paid packages that GET /v1/device/pkg-link signs for entitled
//     devices only.
//
// A plain lowercase name in both.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	b, _ := wmIn["body"].(map[string]any)
	key := wm.Str(b["key"])
	dir, name, ok := strings.Cut(key, "/")
	if !ok || (dir != "apps-s3" && dir != "paid-s3") || len(name) < 6 || len(name) > 120 {
		return map[string]any{"ok": false, "why": "key: apps-s3/<name> or paid-s3/<name>"}
	}
	for i := 0; i < len(name); i++ {
		c := name[i]
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '.' || c == '_' || c == '-'))) {
			return map[string]any{"ok": false, "why": "key: lowercase letters, digits, . _ - only"}
		}
	}
	if strings.Contains(name, "..") {
		return map[string]any{"ok": false, "why": "key: no .."}
	}
	if dir == "paid-s3" {
		if !strings.HasSuffix(name, ".espk") {
			return map[string]any{"ok": false, "why": "key: paid-s3/ holds .espk files only"}
		}
		return map[string]any{"ok": true, "key": key, "private": true, "why": ""}
	}
	if !(strings.HasSuffix(name, ".espk") || strings.HasSuffix(name, ".jpg") || strings.HasSuffix(name, ".json")) {
		return map[string]any{"ok": false, "why": "key: a .espk, .jpg or .json file"}
	}
	return map[string]any{"ok": true, "key": key, "private": false, "why": ""}
}
