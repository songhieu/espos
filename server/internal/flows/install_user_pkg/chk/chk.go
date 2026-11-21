// Workflow install_user_pkg, node chk (imported by tools/importer).
package chk

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"strings"
)

// {"id": row id} or {"pkg_id": "u.*"}, and "devices": "all" or a list of the caller's device ids (push_user_pkg only
// ever looks at the caller's own devices, so another person's id does nothing). The version asked for is the one kept.
func no(status int, code, why string) map[string]any {
	return map[string]any{"ok": false, "status": status, "why": why, "code_err": code}
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
		return no(401, "not_signed_in", "Cần đăng nhập")
	}
	b, _ := in["body"].(map[string]any)
	filter := map[string]any{"column": "id", "op": "eq", "value": wm.Int(b["id"])}
	if p := wm.Str(b["pkg_id"]); wm.Int(b["id"]) <= 0 {
		if !strings.HasPrefix(p, "u.") || len(p) > 48 {
			return no(404, "pkg_not_found", "Không tìm thấy ứng dụng này")
		}
		filter = map[string]any{"column": "pkg_id", "op": "eq", "value": p}
	}
	rows, err := wm.QueryOpts("user_pkgs", map[string]any{"filters": []map[string]any{
		filter, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return no(404, "pkg_not_found", "Không tìm thấy ứng dụng này")
	}
	var devices any
	switch v := b["devices"].(type) {
	case string:
		if v != "all" {
			return no(422, "devices_invalid", "Chọn máy để cài")
		}
		devices = "all"
	case []any:
		ids := []any{}
		for _, x := range v {
			if n := wm.Int(x); n > 0 && len(ids) < 50 {
				ids = append(ids, n)
			}
		}
		if len(ids) == 0 {
			return no(422, "devices_invalid", "Chọn máy để cài")
		}
		devices = ids
	default:
		return no(422, "devices_invalid", "Chọn máy để cài")
	}
	meta, _ := rows[0]["meta"].(map[string]any)
	if s, isStr := rows[0]["meta"].(string); isStr && s != "" {
		_ = json.Unmarshal([]byte(s), &meta)
	}
	minFw := wm.Int(meta["min_fw"])
	if minFw == 0 {
		minFw = 400 // a Java game kept before meta said its firmware
	}
	return map[string]any{"ok": true, "uid": uid, "pkg_id": wm.Str(rows[0]["pkg_id"]), "code": wm.Int(rows[0]["code"]), "min_fw": minFw, "devices": devices}
}
