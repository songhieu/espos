// Workflow confirm_user_pkg, node chk (imported by tools/importer).
package chk

import (
	"crypto/sha256"
	"encoding/hex"
	"espos/internal/wm"
	"fmt"
	"strings"
	"time"
)

// The upload start_user_pkg_upload signed is there and is the caller's own: upkg/u<caller>/in/<ms>/core.espk (any
// package), or upkg/u<caller>/<u.id>/<ms>/core.espk (the older web page's Java game). Hands the next nodes the
// caller's packages (to tell a new one from an update), what to install where ("install": "all", a list of the
// caller's device ids, or nothing: only devices that already have it are updated), and the older page's extras.
const (
	maxEspk = 4194304
	maxPkgs = 30
)

func userPkgID(s string) bool {
	if len(s) < 3 || len(s) > 48 || s[:2] != "u." {
		return false
	}
	t := s[2:]
	for i := 0; i < len(t); i++ {
		c := t[i]
		alnum := (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
		if !alnum && !((c == '-' || c == '.' || c == '_') && i > 0 && i < len(t)-1) {
			return false
		}
	}
	return true
}

func no(status int, code, why string) map[string]any {
	return map[string]any{"ok": false, "status": status, "why": why, "code": code, "params": map[string]any{}}
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
	key := wm.Str(b["key"])
	parts := strings.Split(key, "/")
	if len(parts) != 5 || parts[0] != "upkg" || parts[1] != fmt.Sprintf("u%d", uid) || (parts[2] != "in" && !userPkgID(parts[2])) ||
		parts[3] == "" || len(parts[3]) > 16 || strings.Trim(parts[3], "0123456789") != "" || parts[4] != "core.espk" {
		return no(422, "pkg_path", "Đường dẫn file không hợp lệ")
	}
	legacy := ""
	if parts[2] != "in" {
		legacy = parts[2]
	}
	dir := strings.Join(parts[:4], "/") + "/"
	listing, lerr := wm.StorageList(dir, "", 10)
	if lerr != nil {
		return wm.Fail(lerr)
	}
	files, _ := listing["files"].([]any)
	size := -1
	for _, f := range files {
		fm, _ := f.(map[string]any)
		if wm.Str(fm["key"]) == key {
			size = wm.Int(fm["size"])
		}
	}
	if size <= 0 {
		return no(409, "pkg_missing_file", "Máy chủ chưa nhận được file. Thử tải lại.")
	}
	if size > maxEspk {
		return map[string]any{"ok": false, "status": 422, "why": "Gói lớn hơn 4 MB", "code": "pkg_too_big", "params": map[string]any{"max": maxEspk}}
	}
	rows, err := wm.QueryOpts("user_pkgs", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}}, "limit": maxPkgs + 1})
	if err != nil {
		return wm.Fail(err)
	}
	have := []any{}
	for _, r := range rows {
		have = append(have, map[string]any{"id": wm.Int(r["id"]), "pkg_id": wm.Str(r["pkg_id"]), "code": wm.Int(r["code"])})
	}
	var install any = ""
	switch v := b["install"].(type) {
	case string:
		if v == "all" {
			install = "all"
		}
	case []any:
		ids := []any{}
		for _, x := range v {
			if n := wm.Int(x); n > 0 && len(ids) < 50 {
				ids = append(ids, n)
			}
		}
		install = ids
	}
	m, _ := b["meta"].(map[string]any)
	return map[string]any{
		"ok": true, "status": 200, "why": "", "uid": uid, "key": key, "dir": dir, "legacy_id": legacy, "size": size,
		"have": have, "install": install, "thumb": wm.Str(b["thumb"]), "screen": wm.Str(m["screen"]), "ms": time.Now().UnixMilli(),
	}
}
