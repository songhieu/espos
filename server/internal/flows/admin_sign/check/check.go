// Workflow admin_sign, node check (imported by tools/importer).
package check

import (
	"inkdesk/internal/wm"
	"strings"
)

// The store's files only (the route's guard require_admin lets admins in): apps-s3/<name>, a plain lowercase name
// ending in .inkp (packages, Java games wrapped by the page), .jpg (icons, screenshots) or .json (the catalog, its
// backups, the admin's drafts).
func Run(wmIn, wmCtx map[string]any) map[string]any {
	b, _ := wmIn["body"].(map[string]any)
	key := wm.Str(b["key"])
	dir, name, ok := strings.Cut(key, "/")
	if !ok || dir != "apps-s3" || len(name) < 6 || len(name) > 120 {
		return map[string]any{"ok": false, "why": "key: apps-s3/<name>"}
	}
	for i := 0; i < len(name); i++ {
		c := name[i]
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '.' || c == '_' || c == '-'))) {
			return map[string]any{"ok": false, "why": "key: lowercase letters, digits, . _ - only"}
		}
	}
	if strings.Contains(name, "..") || !(strings.HasSuffix(name, ".inkp") || strings.HasSuffix(name, ".jpg") || strings.HasSuffix(name, ".json")) {
		return map[string]any{"ok": false, "why": "key: a .inkp, .jpg or .json file"}
	}
	return map[string]any{"ok": true, "key": key, "why": ""}
}
