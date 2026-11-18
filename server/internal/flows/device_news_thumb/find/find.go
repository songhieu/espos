// Workflow device_news_thumb, node find (imported by tools/importer).
package find

import (
	"espos/internal/wm"
	"strings"
)

// storyID: a news cache id is 16 lowercase hex digits (news worker: fnv64a of the link).
func storyID(v any) string {
	id := strings.ToLower(strings.TrimSpace(wm.Str(v)))
	if len(id) != 16 {
		return ""
	}
	for _, c := range id {
		if !(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') {
			return ""
		}
	}
	return id
}

func exists(key string) (bool, error) {
	listing, err := wm.StorageList(key, "", 2)
	if err != nil {
		return false, err
	}
	files, _ := listing["files"].([]any)
	for _, f := range files {
		m, _ := f.(map[string]any)
		if wm.Str(m["key"]) == key && wm.Int(m["size"]) > 0 {
			return true, nil
		}
	}
	return false, nil
}

// The square thumbnail the news worker cut from a story's first picture.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	id := storyID(wmIn["id"])
	if id == "" {
		return map[string]any{"ok": false}
	}
	key := "newscache/thumb/" + id + ".jpg"
	have, xerr := exists(key)
	if xerr != nil {
		return wm.Fail(xerr)
	}
	return map[string]any{"ok": have, "key": key}
}
