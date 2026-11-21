// Workflow app_manifests_tick, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"strings"
	"time"
)

// Every 15 minutes (2026-11-21, s3desk docs/manifest-driven.md §1.3): the store's two catalogs (apps-s3/catalog.json for
// S3 apps, apps/catalog.json for v2 packages; written by scripts/publish_pkg.py, which copies each manifest's
// "settings", "provides", "server", "assistant" and "unlocks" into its entry) are read, and every package that has a
// settings schema or fills a slot gets a row in app_manifests, which put_device_settings and device_sync check
// app.<id>.<key> values against. A row changes only when the catalog has a newer code; private uploads (u.*) and
// developer uploads write their own rows (source user / dev) and are left alone here.

var catalogs = wm.PkgCatalogURLs()

func Run(wmIn, wmCtx map[string]any) map[string]any {
	have := map[string]map[string]any{}
	rows, err := wm.QueryOpts("app_manifests", map[string]any{"limit": 1000})
	if err != nil {
		wm.Log("read: ", err.Error())
		return map[string]any{"error": "read"}
	}
	for _, r := range rows {
		if wm.Int(r["user_id"]) == 0 {
			have[wm.Str(r["pkg_id"])] = r // the store's rows; people's own uploads (user_id) are theirs
		}
	}
	now := time.Now().UnixMilli()
	changed, seen := 0, 0
	for _, u := range catalogs {
		r, ferr := wm.Fetch("GET", u, nil)
		if ferr != nil || wm.Int(r["status"]) != 200 {
			wm.Log("catalog ", u, ": not read")
			continue
		}
		body, _ := r["body"].(map[string]any)
		pkgs, _ := body["packages"].([]any)
		for _, x := range pkgs {
			e, _ := x.(map[string]any)
			id := wm.Str(e["id"])
			if e == nil || id == "" || (e["settings"] == nil && e["provides"] == nil && e["server"] == nil) {
				continue
			}
			seen++
			old := have[id]
			if strings.HasPrefix(id, "u.") || (old != nil && wm.Int(old["code"]) >= wm.Int(e["code"])) {
				continue // private ids are people's own uploads; an older or equal code changes nothing
			}
			vals := map[string]any{"pkg_id": id, "user_id": 0, "code": wm.Int(e["code"]), "version": wm.Str(e["version"]),
				"settings": e["settings"], "provides": e["provides"], "server": e["server"], "auth": e["auth"], "source": "store", "updated_ms": now}
			if _, uerr := wm.Upsert("app_manifests", []string{"pkg_id", "user_id"}, vals, nil); uerr != nil {
				wm.Log("save ", id, ": ", uerr.Error())
				continue
			}
			changed++
		}
	}
	return map[string]any{"packages": seen, "changed": changed}
}
