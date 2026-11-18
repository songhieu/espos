// Workflow device_sync, node pk (imported by tools/importer).
package pk

import "espos/internal/wm"

// The answer's "user_pkgs": the owner's Java games as store catalog entries (s3desk/docs/ecosystem.md §5), with
// signed URLs (valid 3 to 7 days) instead of public ones. A game without a working URL this time is left out.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if !wm.Bool(in["s3"]) {
		return map[string]any{"s3": false, "list": []any{}}
	}
	fresh := map[int]map[string]any{}
	res, _ := in["fresh"].([]any)
	for _, x := range res {
		m, _ := x.(map[string]any)
		if m != nil {
			fresh[wm.Int(m["id"])] = m
		}
	}
	rows, _ := in["rows"].([]any)
	list := []any{}
	for _, x := range rows {
		r, _ := x.(map[string]any)
		if r == nil {
			continue
		}
		url := wm.Str(r["url"])
		icon := wm.Str(r["icon_url"])
		if f, ok := fresh[wm.Int(r["id"])]; ok {
			url = wm.Str(f["url"])
			icon = wm.Str(f["icon_url"])
		}
		if url == "" {
			continue
		}
		meta, _ := r["meta"].(map[string]any)
		minFw := wm.Int(meta["min_fw"])
		if minFw < 400 {
			minFw = 400
		}
		perms, _ := meta["perms"].([]any)
		if perms == nil {
			perms = []any{}
		}
		e := map[string]any{
			"id": wm.Str(r["pkg_id"]), "name": wm.Str(r["name"]), "version": wm.Str(r["version"]), "code": wm.Int(r["code"]),
			"kind": "native", "engine": "j2me", "min_fw": minFw, "category": "game",
			"author": wm.Str(meta["author"]), "desc": wm.Str(meta["desc"]), "icon": meta["icon"], "perms": perms,
			"parts": []any{map[string]any{"id": "core", "req": true, "url": url, "size": wm.Int(r["size"]), "sha256": wm.Str(r["sha256"])}},
			"j2me":  meta["j2me"],
		}
		if wm.Bool(r["has_icon"]) && icon != "" {
			e["icon_url"] = icon
		}
		list = append(list, e)
	}
	return map[string]any{"s3": true, "list": list}
}
