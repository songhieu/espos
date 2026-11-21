// Workflow confirm_user_pkg, node keep (imported by tools/importer).
package keep

import (
	"espos/internal/wm"
	"fmt"
	"strings"
	"time"
)

// The package checked by parse is now stored at its own folder (upkg/u<uid>/<u.id>/<ms>/): its row is written (a new
// version replaces the old one, its signed URLs signed again at the next device sync), then the files of earlier
// uploads of this id and the upload itself go. any_owner: the files may come from the web (the person's own) or the
// CLI (written on a developer-key route, so the website's); every key here is built from the caller and the
// package's checked id, never taken from the request.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	pkgID := wm.Str(in["pkg_id"])
	finalKey := wm.Str(in["final_key"])
	base := fmt.Sprintf("upkg/u%d/%s/", uid, pkgID)
	if uid <= 0 || !strings.HasPrefix(pkgID, "u.") || !strings.HasPrefix(finalKey, base) {
		return wm.Fail("confirm_user_pkg: missing input")
	}
	dir := finalKey[:strings.LastIndex(finalKey, "/")+1]
	iconKey := ""
	if wm.Bool(in["has_icon"]) {
		iconKey = wm.Str(in["icon_key"])
	}
	meta, _ := in["meta"].(map[string]any)
	row, uerr := wm.Upsert("user_pkgs", []string{"user_id", "pkg_id"}, map[string]any{
		"user_id": uid, "pkg_id": pkgID, "name": wm.Str(in["name"]), "version": wm.Str(in["version"]), "code": wm.Int(in["code"]),
		"engine": wm.Str(in["engine"]), "size": wm.Int(in["size"]), "sha256": wm.Str(in["sha256"]), "file_key": finalKey,
		"icon_key": iconKey, "meta": meta, "thumb": wm.Str(in["thumb"]), "url": "", "icon_url": "", "url_exp_ms": 0,
	}, nil)
	if uerr != nil {
		return wm.Fail(uerr)
	}
	// Earlier uploads of this id, and the upload itself (when it was not already this folder).
	gone := 0
	for _, prefix := range []string{base, wm.Str(in["upload_dir"])} {
		if !strings.HasPrefix(prefix, fmt.Sprintf("upkg/u%d/", uid)) || strings.Count(prefix, "/") < 3 {
			continue
		}
		listing, lerr := wm.StorageList(prefix, "", 200)
		if lerr != nil {
			wm.Log("old files not listed: ", lerr.Error())
			continue
		}
		files, _ := listing["files"].([]any)
		for _, f := range files {
			fm, _ := f.(map[string]any)
			k := wm.Str(fm["key"])
			if strings.HasPrefix(k, prefix) && !strings.HasPrefix(k, dir) {
				if serr := wm.StorageDelete(k); serr != nil {
					wm.Log("old file stays: ", serr.Error())
				} else {
					gone++
				}
			}
		}
	}
	id := wm.Int(row["id"]) // wm.Upsert answers {"row", "created"} (older engines: the row itself)
	if r, isMap := row["row"].(map[string]any); isMap {
		id = wm.Int(r["id"])
	}
	if id == 0 {
		id = wm.Int(in["old_id"])
	}
	j, _ := meta["j2me"].(map[string]any)
	icon, _ := meta["icon"].(map[string]any)
	pkg := map[string]any{
		"id": id, "pkg_id": pkgID, "src_id": wm.Str(in["src_id"]), "name": wm.Str(in["name"]), "version": wm.Str(in["version"]),
		"code": wm.Int(in["code"]), "size": wm.Int(in["size"]), "sha256": wm.Str(in["sha256"]), "renamed": wm.Bool(in["renamed"]),
		"kind": wm.Str(meta["kind"]), "engine": wm.Str(in["engine"]), "category": wm.Str(meta["category"]), "desc": wm.Str(meta["desc"]),
		"perms": meta["perms"], "min_fw": wm.Int(meta["min_fw"]), "jar_size": wm.Int(meta["jar_size"]), "screen": wm.Str(meta["screen"]),
		"profile": wm.Str(j["profile"]), "icon_bg": icon["bg"], "thumb": wm.Str(in["thumb"]), "created_at": time.Now().UTC().Format(time.RFC3339),
	}
	wm.Log("kept ", pkgID, " code ", wm.Int(in["code"]), "; old files removed: ", gone)
	return map[string]any{"pkg": pkg, "replaced": wm.Int(in["old_id"]) > 0, "old_code": wm.Int(in["old_code"])}
}
