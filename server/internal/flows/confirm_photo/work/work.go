// Workflow confirm_photo, node work (imported by tools/importer).
package work

import "inkdesk/internal/wm"

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
	listing, lerr := wm.StorageList(key, "", 5)
	if lerr != nil {
		return wm.Fail(lerr)
	}
	files, _ := listing["files"].([]any)
	size := -1
	for _, f := range files {
		m, _ := f.(map[string]any)
		if wm.Str(m["key"]) == key {
			size = wm.Int(m["size"])
		}
	}
	if size <= 0 {
		return map[string]any{"status": 409, "body": map[string]any{"error": "file not uploaded yet"}}
	}
	if _, uerr := wm.Update("photos", id, map[string]any{"status": "ready", "size": size}); uerr != nil {
		return wm.Fail(uerr)
	}
	return map[string]any{"status": 200, "body": map[string]any{"id": id, "size": size}}
}
