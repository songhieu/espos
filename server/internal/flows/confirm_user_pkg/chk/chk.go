// Workflow confirm_user_pkg, node chk (imported by tools/importer).
package chk

import (
	"espos/internal/wm"
	"fmt"
	"regexp"
	"strings"
)

// The upload start_user_pkg_upload signed is there: the keys are the caller's own (upkg/u<caller>/<pkg id>/<ms>/),
// the .espk and the icon are within their limits (the same as in start_user_pkg_upload). The next nodes read the
// .espk and check what is inside.
const (
	maxEspk  = 2306867
	maxIcon  = 65536
	maxGames = 30
)

func userPkgID(s string) bool {
	if len(s) < 3 || len(s) > 42 || s[:2] != "u." {
		return false
	}
	t := s[2:]
	for i := 0; i < len(t); i++ {
		c := t[i]
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && i > 0 && i < len(t)-1)) {
			return false
		}
	}
	return true
}

var screenRe = regexp.MustCompile(`^[1-9][0-9]{1,3}x[1-9][0-9]{1,3}$`)

func no(status int, why string) map[string]any {
	return map[string]any{"ok": false, "status": status, "why": why}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return no(401, "Cần đăng nhập")
	}
	b, _ := wmIn["body"].(map[string]any)
	key := wm.Str(b["key"])
	// upkg/u<uid>/<pkg id>/<digits>/core.espk
	parts := strings.Split(key, "/")
	if len(parts) != 5 || parts[0] != "upkg" || parts[1] != fmt.Sprintf("u%d", uid) || !userPkgID(parts[2]) ||
		parts[3] == "" || len(parts[3]) > 16 || strings.Trim(parts[3], "0123456789") != "" || parts[4] != "core.espk" {
		return no(422, "Đường dẫn file không hợp lệ")
	}
	pkgID := parts[2]
	dir := strings.Join(parts[:4], "/") + "/"
	iconKey := wm.Str(b["icon_key"])
	if iconKey != "" && iconKey != dir+"icon.jpg" {
		return no(422, "Đường dẫn biểu tượng không hợp lệ")
	}
	thumb := wm.Str(b["thumb"])
	if thumb != "" && (!strings.HasPrefix(thumb, "data:image/") || len(thumb) > 16000) {
		return no(422, "Ảnh nhỏ của game không hợp lệ")
	}
	m, _ := b["meta"].(map[string]any)
	screen := wm.Str(m["screen"])
	if screen != "" && !screenRe.MatchString(screen) {
		screen = ""
	}

	// The files, as the storage has them (a listing on this route shows only the caller's own).
	listing, lerr := wm.StorageList(dir, "", 10)
	if lerr != nil {
		return wm.Fail(lerr)
	}
	files, _ := listing["files"].([]any)
	size, iconSize := -1, -1
	for _, f := range files {
		fm, _ := f.(map[string]any)
		switch wm.Str(fm["key"]) {
		case key:
			size = wm.Int(fm["size"])
		case dir + "icon.jpg":
			iconSize = wm.Int(fm["size"])
		}
	}
	if size <= 0 {
		return no(409, "Máy chủ chưa nhận được file. Thử tải lại.")
	}
	if size > maxEspk {
		return no(422, "Gói game lớn hơn 2,2 MB")
	}
	if iconKey != "" && (iconSize <= 0 || iconSize > maxIcon) {
		iconKey = "" // no usable icon: the device draws the game's colour tile instead
	}

	have, err := wm.QueryOpts("user_pkgs", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}}, "limit": maxGames + 1})
	if err != nil {
		return wm.Fail(err)
	}
	oldID, oldCode := 0, 0
	for _, r := range have {
		if wm.Str(r["pkg_id"]) == pkgID {
			oldID = wm.Int(r["id"])
			oldCode = wm.Int(r["code"])
		}
	}
	if oldID == 0 && len(have) >= maxGames {
		return no(422, fmt.Sprintf("Tối đa %d game. Xoá bớt game cũ rồi tải lại.", maxGames))
	}
	return map[string]any{
		"ok": true, "status": 200, "why": "", "uid": uid, "key": key, "dir": dir, "icon_key": iconKey, "pkg_id": pkgID,
		"size": size, "thumb": thumb, "screen": screen, "old_id": oldID, "old_code": oldCode,
	}
}
