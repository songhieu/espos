// Workflow device_wallpaper, node find (imported by tools/importer).
package find

import "inkdesk/internal/wm"

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	id := wm.Int(in["id"])
	uid := wm.Int(in["user_id"])
	if id <= 0 || uid <= 0 {
		return map[string]any{"ok": false}
	}
	rows, err := wm.QueryOpts("wallpapers", map[string]any{
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
	return map[string]any{"ok": true, "key": wm.Str(rows[0]["file_key"])}
}
