// Workflow start_user_pkg_upload, node prep (imported by tools/importer).
package prep

import (
	"fmt"
	"inkdesk/internal/wm"
	"time"
)

// The user's own Java games (the S3's "Game Java" tab): the browser packs the .jar into a .inkp and uploads it, and
// its icon, straight into the user's private storage through the two signed PUT URLs this route answers. Nothing is
// stored here: confirm_user_pkg checks the uploaded file and keeps it. The same limits are in confirm_user_pkg.
const (
	maxInkp  = 2306867 // 2.2 MB: a 2 MB .jar plus its manifest and icon
	maxJar   = 2097152 // 2 MB
	maxIcon  = 65536
	maxGames = 30
)

// userPkgID: "u." then a slug of 1 to 40 lowercase letters, digits and "-" (not first or last).
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

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"ok": false, "status": 401, "why": "Cần đăng nhập"}
	}
	b, _ := wmIn["body"].(map[string]any)
	id := wm.Str(b["pkg_id"])
	size, jarSize, iconSize := wm.Int(b["size"]), wm.Int(b["jar_size"]), wm.Int(b["icon_size"])
	if !userPkgID(id) {
		return map[string]any{"ok": false, "status": 422, "why": "Mã game không hợp lệ"}
	}
	if jarSize <= 0 || jarSize > maxJar {
		return map[string]any{"ok": false, "status": 422, "why": "File .jar lớn hơn 2 MB. Mỗi game tối đa 2 MB."}
	}
	if size < 16 || size > maxInkp {
		return map[string]any{"ok": false, "status": 422, "why": "Gói game lớn hơn 2,2 MB"}
	}
	if iconSize < 0 || iconSize > maxIcon {
		return map[string]any{"ok": false, "status": 422, "why": "Biểu tượng lớn hơn 64 KB"}
	}
	have, err := wm.QueryOpts("user_pkgs", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}}, "limit": maxGames + 1})
	if err != nil {
		return wm.Fail(err)
	}
	replaces := false
	for _, r := range have {
		if wm.Str(r["pkg_id"]) == id {
			replaces = true
		}
	}
	if !replaces && len(have) >= maxGames {
		return map[string]any{"ok": false, "status": 422, "why": fmt.Sprintf("Tối đa %d game. Xoá bớt game cũ rồi tải lại.", maxGames)}
	}
	// One folder per upload: a new version never overwrites the one a device may be downloading.
	dir := fmt.Sprintf("upkg/u%d/%s/%d/", uid, id, time.Now().UnixMilli())
	return map[string]any{"ok": true, "status": 200, "why": "", "key": dir + "core.inkp", "icon_key": dir + "icon.jpg"}
}
