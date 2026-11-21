// Workflow list_user_pkgs, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"strings"
)

// The caller's own packages for the web's list (Ứng dụng của tôi) and `espos apps`: what they show, never the storage
// keys or the signed URLs. With ?devices=1 also each S3 device of the caller: what it reported installed (id -> code),
// what s3.pkgs asks of it (id -> {code, rm}) and its job, for the user's packages only.
func jsonMap(v any) map[string]any {
	m, _ := v.(map[string]any)
	if s, isStr := v.(string); isStr && s != "" {
		_ = json.Unmarshal([]byte(s), &m)
	}
	return m
}

func kindOf(row map[string]any) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(wm.Str(row["fw"]), "espos-s3-") {
		return "s3"
	}
	return "x4"
}

// devUser: the account of a developer key (espos CLI) in this route's own Authorization header: the /v1/dev/* routes
// are public, with the guards require_dev_key (which refuses a wrong key first) and admin_dev_active. On the web's
// end_user routes the platform keeps that header to itself, so this finds nothing there and the signed-in person
// counts. Only the key's sha256 is stored (dev_keys.key_hash).
func devUser(auth string) (int, error) {
	const prefix = "Bearer espos_dev_"
	auth = strings.TrimSpace(auth)
	if len(auth) != len(prefix)+64 || !strings.HasPrefix(auth, prefix) || strings.ToLower(auth[7:]) != auth[7:] {
		return 0, nil
	}
	if _, err := hex.DecodeString(auth[len(prefix):]); err != nil {
		return 0, nil
	}
	sum := sha256.Sum256([]byte(auth[7:]))
	rows, err := wm.QueryOpts("dev_keys", map[string]any{
		"filters": []map[string]any{{"column": "key_hash", "op": "eq", "value": hex.EncodeToString(sum[:])}}, "limit": 1})
	if err != nil || len(rows) != 1 {
		return 0, err
	}
	return wm.Int(rows[0]["user_id"]), nil
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(in["uid"])
	}
	if uid == 0 {
		dk, kerr := devUser(wm.Str(wmIn["auth"]))
		if kerr != nil {
			return wm.Fail(kerr)
		}
		uid = dk // a developer key's account (the CLI's /v1/dev/* routes)
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "Cần đăng nhập", "code": "not_signed_in"}}
	}
	rows, err := wm.QueryOpts("user_pkgs", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
		"order_by": "id", "desc": true, "limit": 40,
	})
	if err != nil {
		return wm.Fail(err)
	}
	out := []any{}
	mine := map[string]bool{}
	for _, r := range rows {
		meta := jsonMap(r["meta"])
		j, _ := meta["j2me"].(map[string]any)
		icon, _ := meta["icon"].(map[string]any)
		kind, engine := wm.Str(meta["kind"]), wm.Str(r["engine"])
		if kind == "" || engine == "" {
			kind, engine = "native", "j2me" // kept before packages other than Java games were taken
		}
		perms, _ := meta["perms"].([]any)
		if perms == nil {
			perms = []any{}
		}
		minFw := wm.Int(meta["min_fw"])
		if minFw == 0 && engine == "j2me" {
			minFw = 400
		}
		mine[wm.Str(r["pkg_id"])] = true
		out = append(out, map[string]any{
			"id": wm.Int(r["id"]), "pkg_id": wm.Str(r["pkg_id"]), "src_id": wm.Str(meta["src_id"]), "name": wm.Str(r["name"]),
			"version": wm.Str(r["version"]), "code": wm.Int(r["code"]), "size": wm.Int(r["size"]), "sha256": wm.Str(r["sha256"]),
			"kind": kind, "engine": engine, "category": wm.Str(meta["category"]), "desc": wm.Str(meta["desc"]), "perms": perms,
			"min_fw": minFw, "jar_size": wm.Int(meta["jar_size"]), "screen": wm.Str(meta["screen"]), "profile": wm.Str(j["profile"]),
			"icon_bg": icon["bg"], "thumb": wm.Str(r["thumb"]), "created_at": wm.Str(r["created_at"]), "updated_at": wm.Str(r["updated_at"]),
		})
	}
	body := map[string]any{"pkgs": out, "max": 30, "max_bytes": 4194304}
	q, _ := in["query"].(map[string]any)
	if d := wm.Str(q["devices"]); d == "1" || d == "true" {
		devs, derr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
			{"column": "user_id", "op": "eq", "value": uid}, {"column": "revoked", "op": "eq", "value": false}}, "order_by": "id", "limit": 50})
		if derr != nil {
			return wm.Fail(derr)
		}
		list := []any{}
		for _, dv := range devs {
			if kindOf(dv) != "s3" {
				continue
			}
			devID := wm.Int(dv["id"])
			rep := jsonMap(dv["pkgs"])
			inst := map[string]any{}
			insts, _ := rep["installed"].([]any)
			for _, it := range insts {
				m, _ := it.(map[string]any)
				if id := wm.Str(m["id"]); mine[id] {
					inst[id] = wm.Int(m["code"])
				}
			}
			wants := map[string]any{}
			cur, serr := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
				{"column": "device_id", "op": "eq", "value": devID}, {"column": "skey", "op": "eq", "value": "s3.pkgs"}}, "limit": 1})
			if serr != nil {
				return wm.Fail(serr)
			}
			if len(cur) == 1 {
				var l []any
				_ = json.Unmarshal([]byte(wm.Str(cur[0]["svalue"])), &l)
				for _, it := range l {
					m, _ := it.(map[string]any)
					if id := wm.Str(m["id"]); mine[id] {
						wants[id] = map[string]any{"code": wm.Int(m["code"]), "rm": m["rm"] == true}
					}
				}
			}
			job, _ := rep["job"].(map[string]any)
			if !mine[wm.Str(job["id"])] {
				job = nil
			}
			list = append(list, map[string]any{
				"id": devID, "name": wm.Str(dv["name"]), "fw": wm.Str(dv["fw"]), "last_sync": wm.Str(dv["last_sync"]),
				"installed": inst, "wants": wants, "job": job,
			})
		}
		body["devices"] = list
	}
	return map[string]any{"status": 200, "body": body}
}
