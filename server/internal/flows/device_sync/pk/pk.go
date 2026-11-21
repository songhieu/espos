// Workflow device_sync, node pk (imported by tools/importer).
package pk

import "espos/internal/wm"

// The answer's "user_pkgs": the owner's own packages as store catalog entries (s3desk/docs/ecosystem.md §5), with
// signed URLs (valid 3 to 7 days) instead of public ones. A package without a working URL this time is left out.
// The entry comes from what confirm_user_pkg checked and kept (meta): a JavaScript app (kind js, engine js) or a Java
// game (kind native, engine j2me; rows from before meta had a kind are Java games). min_fw is at least what the
// engine needs (300 for packages, 400 for Java), so an older device says "needs a software update" instead of failing.
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
		kind, engine := wm.Str(meta["kind"]), wm.Str(r["engine"])
		if kind == "" || engine == "" {
			kind, engine = "native", "j2me"
		}
		js := kind == "js" && engine == "js"
		if !js && !(kind == "native" && engine == "j2me") {
			continue // nothing else is ever kept; never hand a device an engine it was not checked for
		}
		minFw := wm.Int(meta["min_fw"])
		if js && minFw < 300 {
			minFw = 300
		}
		if !js && minFw < 400 {
			minFw = 400
		}
		category := wm.Str(meta["category"])
		if category == "" {
			category = "game"
		}
		perms, _ := meta["perms"].([]any)
		if perms == nil {
			perms = []any{}
		}
		e := map[string]any{
			"id": wm.Str(r["pkg_id"]), "name": wm.Str(r["name"]), "version": wm.Str(r["version"]), "code": wm.Int(r["code"]),
			"kind": kind, "engine": engine, "min_fw": minFw, "category": category,
			"author": wm.Str(meta["author"]), "desc": wm.Str(meta["desc"]), "icon": meta["icon"], "perms": perms,
			"parts": []any{map[string]any{"id": "core", "req": true, "url": url, "size": wm.Int(r["size"]), "sha256": wm.Str(r["sha256"])}},
		}
		if !js {
			e["j2me"] = meta["j2me"]
		}
		if tr, ok := meta["i18n"].(map[string]any); ok && len(tr) > 0 {
			e["i18n"] = tr
		}
		if wm.Bool(r["has_icon"]) && icon != "" {
			e["icon_url"] = icon
		}
		list = append(list, e)
	}
	return map[string]any{"s3": true, "list": list}
}
