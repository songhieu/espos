// Workflow get_news, node work (imported by tools/importer).
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
	prefs := map[string]any{
		"enabled": false, "sections": "tin-moi-nhat,thoi-su,the-gioi,kinh-doanh,khoa-hoc-cong-nghe,suc-khoe",
		"hour": 6, "per_section": 5, "images": true, "keep": 7, "last_date": "", "last_note": "", "saved": false,
	}
	rows, err := wm.QueryOpts("news_prefs", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) == 1 {
		r := rows[0]
		prefs = map[string]any{
			"enabled": r["enabled"] == true, "sections": wm.Str(r["sections"]), "hour": wm.Int(r["hour"]),
			"per_section": wm.Int(r["per_section"]), "images": r["images"] == true, "keep": wm.Int(r["keep"]),
			"last_date": wm.Str(r["last_date"]), "last_note": wm.Str(r["last_note"]), "saved": true,
		}
	}
	books, berr := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "kind", "op": "eq", "value": "news"}},
		"order_by": "id", "desc": true, "limit": 30})
	if berr != nil {
		return wm.Fail(berr)
	}
	issues := []any{}
	for _, b := range books {
		issues = append(issues, map[string]any{"id": wm.Int(b["id"]), "title": wm.Str(b["title"]), "size": wm.Int(b["size"]),
			"progress": b["progress"], "created_at": wm.Str(b["created_at"]), "on_device": b["on_device"] == true})
	}
	return map[string]any{"status": 200, "body": map[string]any{"prefs": prefs, "issues": issues}}
}
