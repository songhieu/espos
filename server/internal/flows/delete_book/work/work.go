// Workflow delete_book, node work (imported by tools/importer).
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
	rows, err := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	// A delivered book has no original left on the server.
	if wm.Str(rows[0]["delivered_at"]) == "" {
		if serr := wm.StorageDelete(wm.Str(rows[0]["file_key"])); serr != nil && wm.Str(rows[0]["status"]) == "ready" {
			return wm.Fail(serr)
		}
	}
	if derr := wm.Delete("books", id); derr != nil {
		return wm.Fail(derr)
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
}
