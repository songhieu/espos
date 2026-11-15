// Workflow start_photo_render, node prep (imported by tools/importer).
package prep

import (
	"fmt"
	"inkdesk/internal/wm"
	"strconv"
	"strings"
)

// renderKey is where the render of a picture for a w x h box with params p ("ca55") lives, next to the original;
// "" when the size or params are not valid.
func renderKey(orig, dim, p string) string {
	w, h, ok := strings.Cut(dim, "x")
	wi, e1 := strconv.Atoi(w)
	hi, e2 := strconv.Atoi(h)
	if !ok || e1 != nil || e2 != nil || wi < 16 || hi < 16 || wi > 800 || hi > 800 {
		return ""
	}
	if len(p) != 4 || (p[0] != 'c' && p[0] != 'n') || (p[1] != 'a' && p[1] != 't') || p[2] < '0' || p[2] > '9' || p[3] < '0' || p[3] > '9' {
		return ""
	}
	slash := strings.LastIndex(orig, "/")
	if slash < 0 {
		return ""
	}
	return fmt.Sprintf("%s/%dx%d-%s.bmp", orig[:slash], wi, hi, p)
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

// The browser renders a picture for a widget box (1-bit BMP, at most 800x800 ≈ 80 KB) and uploads it,
// unless that render is already there.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"ok": false, "why": "sign in"}
	}
	b, _ := wmIn["body"].(map[string]any)
	id := wm.Int(b["id"])
	size := wm.Int(b["size"])
	rows, err := wm.QueryOpts("photos", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return map[string]any{"ok": false, "why": "Không có ảnh này"}
	}
	key := renderKey(wm.Str(rows[0]["file_key"]), wm.Str(b["dim"]), wm.Str(b["p"]))
	if key == "" {
		return map[string]any{"ok": false, "why": "Cỡ hoặc tuỳ chọn ảnh không hợp lệ"}
	}
	have, xerr := exists(key)
	if xerr != nil {
		return wm.Fail(xerr)
	}
	if !have && (size <= 0 || size > 100000) {
		return map[string]any{"ok": false, "why": "Ảnh đã chuyển quá lớn"}
	}
	return map[string]any{"ok": true, "exists": have, "key": key, "why": ""}
}
