// Workflow delete_user_pkg, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"strings"
	"time"
)

// Deletes one of the caller's Java games: every file of it (all versions), its row, and on each of the caller's S3
// devices it goes into s3.pkgs as {"id", "rm": true} (s3desk/docs/ecosystem.md §6) when that device has it or was
// asked to install it, so the device removes it at its next sync and then drops the entry. Written as the web's
// settings page writes a setting (source web, now), so it wins over an older copy on the device.

// kindOf is the device's board: stored on its row, else told by its firmware name (as device_sync).
func kindOf(row map[string]any) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(wm.Str(row["fw"]), "espos-s3-") {
		return "s3"
	}
	return "x4"
}

func jsonMap(v any) map[string]any {
	m, _ := v.(map[string]any)
	if s, isStr := v.(string); isStr && s != "" {
		_ = json.Unmarshal([]byte(s), &m)
	}
	return m
}

// hasIt: the device's last report lists the package installed, or a job on it.
func hasIt(row map[string]any, id string) bool {
	rep := jsonMap(row["pkgs"])
	inst, _ := rep["installed"].([]any)
	for _, it := range inst {
		m, _ := it.(map[string]any)
		if wm.Str(m["id"]) == id {
			return true
		}
	}
	job, _ := rep["job"].(map[string]any)
	return wm.Str(job["id"]) == id
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "Cần đăng nhập"}}
	}
	b, _ := wmIn["body"].(map[string]any)
	id := wm.Int(b["id"])
	rows, err := wm.QueryOpts("user_pkgs", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if id <= 0 || len(rows) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "Không tìm thấy game này"}}
	}
	pkgID := wm.Str(rows[0]["pkg_id"])

	// ---- the files: every upload of this id, under the caller's own folder ----
	base := fmt.Sprintf("upkg/u%d/%s/", uid, pkgID)
	if !strings.HasPrefix(wm.Str(rows[0]["file_key"]), base) {
		return wm.Fail("user_pkgs " + fmt.Sprint(id) + ": file outside its folder")
	}
	listing, lerr := wm.StorageList(base, "", 200)
	if lerr != nil {
		return wm.Fail(lerr)
	}
	files, _ := listing["files"].([]any)
	for _, f := range files {
		fm, _ := f.(map[string]any)
		if k := wm.Str(fm["key"]); strings.HasPrefix(k, base) {
			if derr := wm.StorageDelete(k); derr != nil {
				return wm.Fail(derr)
			}
		}
	}
	if derr := wm.Delete("user_pkgs", id); derr != nil {
		return wm.Fail(derr)
	}

	// ---- off the caller's S3 devices ----
	devs, qerr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "revoked", "op": "eq", "value": false}}, "limit": 50})
	if qerr != nil {
		return wm.Fail(qerr)
	}
	told := 0
	ms := time.Now().UnixMilli()
	for _, d := range devs {
		if kindOf(d) != "s3" {
			continue
		}
		devID := wm.Int(d["id"])
		cur, serr := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": devID}, {"column": "skey", "op": "eq", "value": "s3.pkgs"}}, "limit": 1})
		if serr != nil {
			return wm.Fail(serr)
		}
		var list []any
		if len(cur) == 1 && wm.Str(cur[0]["svalue"]) != "" {
			if json.Unmarshal([]byte(wm.Str(cur[0]["svalue"])), &list) != nil {
				list = nil // a broken value: replaced (the device sends its own list back)
			}
		}
		wanted := false
		rms := []any{}
		rest := []any{}
		for _, it := range list {
			m, _ := it.(map[string]any)
			if m == nil {
				continue
			}
			if wm.Str(m["id"]) == pkgID {
				wanted = wanted || m["rm"] != true
				continue
			}
			if m["rm"] == true {
				rms = append(rms, m)
			} else {
				rest = append(rest, m)
			}
		}
		had := len(rms)+len(rest) != len(list)
		if !wanted && !hasIt(d, pkgID) && !had {
			continue // never on this device: nothing to tell it
		}
		next := rms
		if wanted || hasIt(d, pkgID) {
			next = append(next, map[string]any{"id": pkgID, "rm": true})
		}
		next = append(next, rest...)
		val := ""
		if len(next) > 0 {
			raw, merr := json.Marshal(next)
			if merr != nil {
				return wm.Fail(merr)
			}
			val = string(raw)
		}
		if len(next) > 64 || len(val) > 6000 {
			wm.Log("s3.pkgs of device ", devID, " is full: the removal is not added")
			continue
		}
		if _, uerr := wm.Upsert("device_settings", []string{"device_id", "skey"}, map[string]any{
			"device_id": devID, "user_id": uid, "skey": "s3.pkgs", "svalue": val, "updated_ms": ms, "source": "web",
		}, nil); uerr != nil {
			return wm.Fail(uerr)
		}
		told++
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true, "devices": told}}
}
