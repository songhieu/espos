// Workflow start_user_pkg_upload, node prep (imported by tools/importer).
package prep

import (
	"crypto/sha256"
	"encoding/hex"
	"espos/internal/wm"
	"fmt"
	"strconv"
	"strings"
	"time"
)

// The user's own packages (web: device page › Ứng dụng của tôi; CLI: espos upload with a developer key): the client
// uploads the .espk it built straight into storage through the signed PUT URL this route answers. Nothing is kept
// here: confirm_user_pkg reads the uploaded file, checks it with the validator's rules and keeps it under its id.
//   - {size}: any package. The file goes to upkg/u<uid>/in/<ms>/core.espk; the id is read from it at confirm.
//   - {pkg_id, size, jar_size, icon_size}: the older web page's Java game (id "u.<slug>" known up front), uploaded
//     to upkg/u<uid>/<pkg_id>/<ms>/, with its icon next to it. Kept working while that page is still open somewhere.
//
// The same limits are in confirm_user_pkg.
const (
	maxEspk = 4194304 // 4 MB a package (the .espk as uploaded)
	maxJar  = 2097152 // 2 MB a Java game
	maxIcon = 65536
	maxPkgs = 30
	maxJ2me = 2306867 // the older page's Java package: 2.2 MB
)

// userPkgID: "u." then 1 to 46 characters of a-z 0-9 . _ - (a letter or digit first and last).
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

// cleanStaging: uploads under upkg/u<uid>/in/ older than an hour were never confirmed (refused, or the client
// stopped): they go. The node has any_owner (web uploads are the person's, CLI uploads the website's); the folder is
// built from the caller, never from the request.
func cleanStaging(uid int) {
	pre := fmt.Sprintf("upkg/u%d/in/", uid)
	listing, err := wm.StorageList(pre, "", 100)
	if err != nil {
		wm.Log("staging not listed: ", err.Error())
		return
	}
	old := time.Now().UnixMilli() - 3600*1000
	files, _ := listing["files"].([]any)
	for _, f := range files {
		fm, _ := f.(map[string]any)
		k := wm.Str(fm["key"])
		rest := strings.TrimPrefix(k, pre)
		if rest == k || !strings.Contains(rest, "/") {
			continue
		}
		ms, perr := strconv.ParseInt(rest[:strings.Index(rest, "/")], 10, 64)
		if perr != nil || ms > old {
			continue
		}
		if derr := wm.StorageDelete(k); derr != nil {
			wm.Log("staging file stays: ", derr.Error())
		}
	}
}

func no(status int, code, why string, params map[string]any) map[string]any {
	if params == nil {
		params = map[string]any{}
	}
	return map[string]any{"ok": false, "status": status, "why": why, "code": code, "params": params}
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
		return no(401, "not_signed_in", "Cần đăng nhập", nil)
	}
	cleanStaging(uid)
	b, _ := in["body"].(map[string]any)
	id := wm.Str(b["pkg_id"])
	size := wm.Int(b["size"])
	ms := time.Now().UnixMilli()
	if id == "" {
		if size < 16 || size > maxEspk {
			return no(422, "pkg_too_big", "Gói lớn hơn 4 MB", map[string]any{"max": maxEspk})
		}
		dir := fmt.Sprintf("upkg/u%d/in/%d/", uid, ms)
		return map[string]any{"ok": true, "status": 200, "why": "", "key": dir + "core.espk", "icon_key": dir + "icon.jpg", "max": maxEspk}
	}
	// ---- the older web page's Java game ----
	jarSize, iconSize := wm.Int(b["jar_size"]), wm.Int(b["icon_size"])
	if !userPkgID(id) {
		return no(422, "pkg_id", "Mã game không hợp lệ", nil)
	}
	if jarSize <= 0 || jarSize > maxJar {
		return no(422, "jar_too_big", "File .jar lớn hơn 2 MB. Mỗi game tối đa 2 MB.", nil)
	}
	if size < 16 || size > maxJ2me {
		return no(422, "pkg_too_big", "Gói game lớn hơn 2,2 MB", map[string]any{"max": maxJ2me})
	}
	if iconSize < 0 || iconSize > maxIcon {
		return no(422, "icon_too_big", "Biểu tượng lớn hơn 64 KB", nil)
	}
	have, err := wm.QueryOpts("user_pkgs", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}}, "limit": maxPkgs + 1})
	if err != nil {
		return wm.Fail(err)
	}
	replaces := false
	for _, r := range have {
		if wm.Str(r["pkg_id"]) == id {
			replaces = true
		}
	}
	if !replaces && len(have) >= maxPkgs {
		return no(422, "pkg_limit", fmt.Sprintf("Tối đa %d game. Xoá bớt game cũ rồi tải lại.", maxPkgs), map[string]any{"max": maxPkgs})
	}
	// One folder per upload: a new version never overwrites the one a device may be downloading.
	dir := fmt.Sprintf("upkg/u%d/%s/%d/", uid, id, ms)
	return map[string]any{"ok": true, "status": 200, "why": "", "key": dir + "core.espk", "icon_key": dir + "icon.jpg", "max": maxJ2me}
}
