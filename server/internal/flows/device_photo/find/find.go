// Workflow device_photo, node find (imported by tools/importer).
package find

import (
	"espos/internal/wm"
	"fmt"
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
	if len(p) != 4 || (p[0] != 'c' && p[0] != 'n') || (p[1] != 'a' && p[1] != 't' && p[1] != 'j') || p[2] < '0' || p[2] > '9' || p[3] < '0' || p[3] > '9' {
		return ""
	}
	slash := strings.LastIndex(orig, "/")
	if slash < 0 {
		return ""
	}
	ext := "bmp" // 1-bit for e-ink; "j" renders are colour JPEGs for the S3
	if p[1] == 'j' {
		ext = "jpg"
	}
	return fmt.Sprintf("%s/%dx%d-%s.%s", orig[:slash], wi, hi, p, ext)
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

// A device downloads the render of one picture for one of its widget boxes.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	id := wm.Int(in["id"])
	uid := wm.Int(in["user_id"])
	if id <= 0 || uid <= 0 {
		return map[string]any{"ok": false}
	}
	rows, err := wm.QueryOpts("photos", map[string]any{
		"filters": []map[string]any{
			{"column": "id", "op": "eq", "value": id},
			{"column": "user_id", "op": "eq", "value": uid},
			{"column": "status", "op": "eq", "value": "ready"},
		},
		"limit": 1,
	})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return map[string]any{"ok": false}
	}
	key := renderKey(wm.Str(rows[0]["file_key"]), wm.Str(in["dim"]), wm.Str(in["p"]))
	if key == "" {
		return map[string]any{"ok": false}
	}
	have, xerr := exists(key)
	if xerr != nil {
		return wm.Fail(xerr)
	}
	return map[string]any{"ok": have, "key": key}
}
