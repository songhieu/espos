// Workflow list_books, node work (imported by tools/importer).
package work

import "espos/internal/wm"

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	rows, err := wm.QueryOpts("books", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
		"order_by": "id", "desc": true, "limit": 500,
	})
	if err != nil {
		return wm.Fail(err)
	}
	out := []any{}
	var total int64
	for _, r := range rows {
		if wm.Str(r["kind"]) == "news" {
			continue
		}
		delivered := wm.Str(r["delivered_at"])
		if delivered == "" {
			// Only originals still waiting for the device take space on the server.
			total += int64(wm.Num(r["size"]))
		}
		out = append(out, map[string]any{
			"id": wm.Int(r["id"]), "title": wm.Str(r["title"]), "author": wm.Str(r["author"]),
			"file_name": wm.Str(r["file_name"]), "size": wm.Int(r["size"]), "status": wm.Str(r["status"]),
			"created_at": wm.Str(r["created_at"]), "on_device": r["on_device"] == true, "delivered_at": delivered,
		})
	}
	return map[string]any{"status": 200, "body": map[string]any{"books": out, "bytes": total}}
}
