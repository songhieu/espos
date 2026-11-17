// Workflow device_news_img, node find (imported by tools/importer).
package find

import (
	"fmt"
	"inkdesk/internal/wm"
	"strconv"
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

// Picture n (0 = the first) of a cached story, as the colour desk's baseline JPEG (≤ 320 px wide):
// the copy of the n-th stored picture (the n-th block with an ImgFile).
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	id := storyID(in["id"])
	n, err := strconv.Atoi(strings.TrimSpace(wm.Str(in["n"])))
	if id == "" || err != nil || n < 0 || n > 2 {
		return map[string]any{"ok": false}
	}
	key := fmt.Sprintf("newscache/imgc/%s-%d.jpg", id, n+1)
	have, xerr := exists(key)
	if xerr != nil {
		return wm.Fail(xerr)
	}
	return map[string]any{"ok": have, "key": key}
}
