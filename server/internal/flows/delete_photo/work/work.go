// Workflow delete_photo, node work (imported by tools/importer).
package work

import (
	"inkdesk/internal/wm"
	"strings"
)

// Deletes a picture with every render made from it.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	b, _ := wmIn["body"].(map[string]any)
	id := wm.Int(b["id"])
	rows, err := wm.QueryOpts("photos", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	key := wm.Str(rows[0]["file_key"])
	if slash := strings.LastIndex(key, "/"); slash > 0 {
		listing, lerr := wm.StorageList(key[:slash+1], "", 200)
		if lerr != nil {
			return wm.Fail(lerr)
		}
		files, _ := listing["files"].([]any)
		for _, f := range files {
			m, _ := f.(map[string]any)
			if k := wm.Str(m["key"]); strings.HasPrefix(k, key[:slash+1]) {
				if derr := wm.StorageDelete(k); derr != nil {
					return wm.Fail(derr)
				}
			}
		}
	}
	if derr := wm.Delete("photos", id); derr != nil {
		return wm.Fail(derr)
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
}
