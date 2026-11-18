// Workflow list_user_pkgs, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
)

// The caller's games for the web's list (the fields it shows; never the storage keys or the signed URLs).
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "Cần đăng nhập"}}
	}
	rows, err := wm.QueryOpts("user_pkgs", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
		"order_by": "id", "desc": true, "limit": 40,
	})
	if err != nil {
		return wm.Fail(err)
	}
	out := []any{}
	for _, r := range rows {
		meta, _ := r["meta"].(map[string]any)
		if s, isStr := r["meta"].(string); isStr && s != "" {
			_ = json.Unmarshal([]byte(s), &meta)
		}
		j, _ := meta["j2me"].(map[string]any)
		icon, _ := meta["icon"].(map[string]any)
		out = append(out, map[string]any{
			"id": wm.Int(r["id"]), "pkg_id": wm.Str(r["pkg_id"]), "name": wm.Str(r["name"]), "version": wm.Str(r["version"]),
			"code": wm.Int(r["code"]), "size": wm.Int(r["size"]), "jar_size": wm.Int(meta["jar_size"]), "screen": wm.Str(meta["screen"]),
			"profile": wm.Str(j["profile"]), "icon_bg": icon["bg"], "thumb": wm.Str(r["thumb"]), "created_at": wm.Str(r["created_at"]),
		})
	}
	return map[string]any{"status": 200, "body": map[string]any{"pkgs": out}}
}
