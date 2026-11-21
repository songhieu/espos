// Workflow delete_dev_key, node work (imported by tools/importer).
package work

import "espos/internal/wm"

// Revokes one of the caller's developer keys: its row goes, so the next CLI call with it gets 401 bad_key.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "Cần đăng nhập", "code": "not_signed_in"}}
	}
	b, _ := wmIn["body"].(map[string]any)
	id := wm.Int(b["id"])
	rows, err := wm.QueryOpts("dev_keys", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if id <= 0 || len(rows) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "Không tìm thấy khoá này", "code": "key_not_found"}}
	}
	if derr := wm.Delete("dev_keys", id); derr != nil {
		return wm.Fail(derr)
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
}
