// Workflow sign_pkg_upload, node check (imported by tools/importer).
package check

import (
	"inkdesk/internal/wm"
	"strings"
)

// The store's files only: apps-s3/<name> (or apps-s3-test/ for checking the web page) and apps/<name> (the v2
// catalog), a plain lowercase name ending in .inkp, .jpg, .png (the X4's black-and-white previews) or .json. A
// public /files upload must be JSON (8 MiB), so a package part bigger than ~6 MB goes up through the signed PUT URL
// this route answers (api_key route: callers hold a bbk_ key).
func Run(wmIn, wmCtx map[string]any) map[string]any {
	b, _ := wmIn["body"].(map[string]any)
	key := wm.Str(b["key"])
	dir, name, ok := strings.Cut(key, "/")
	// apps/: the v2 catalog (faces, widgets, themes; s3desk/docs/profiles.md §10) and its files.
	if !ok || (dir != "apps-s3" && dir != "apps-s3-test" && dir != "apps") || len(name) < 6 || len(name) > 120 {
		return map[string]any{"ok": false, "why": "key: apps-s3/<name> or apps/<name>"}
	}
	for i := 0; i < len(name); i++ {
		c := name[i]
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '.' || c == '_' || c == '-'))) {
			return map[string]any{"ok": false, "why": "key: lowercase letters, digits, . _ - only"}
		}
	}
	if strings.Contains(name, "..") || !(strings.HasSuffix(name, ".inkp") || strings.HasSuffix(name, ".jpg") || strings.HasSuffix(name, ".json") || strings.HasSuffix(name, ".png")) {
		return map[string]any{"ok": false, "why": "key: a .inkp, .jpg, .png or .json file"}
	}
	return map[string]any{"ok": true, "key": key, "why": ""}
}
