// Workflow start_wallpaper_upload, node prep (imported by tools/importer).
package prep

import (
	"fmt"
	"inkdesk/internal/wm"
	"strings"
	"time"
)

const maxWallpapers = 20

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"ok": false, "why": "sign in"}
	}
	b, _ := wmIn["body"].(map[string]any)
	orient := wm.Str(b["orient"])
	size := wm.Int(b["size"])
	thumb := wm.Str(b["thumb"])
	if orient != "p" && orient != "l" {
		return map[string]any{"ok": false, "why": "orient must be p or l"}
	}
	// A 1-bit 480x800 BMP is about 48 KB; a 2-bit one about 96 KB.
	if size <= 0 || size > 300000 {
		return map[string]any{"ok": false, "why": "Ảnh đã chuyển phải nhỏ hơn 300 KB"}
	}
	if !strings.HasPrefix(thumb, "data:image/png;base64,") || len(thumb) > 40000 {
		return map[string]any{"ok": false, "why": "bad thumbnail"}
	}
	have, err := wm.QueryOpts("wallpapers", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}}, "limit": maxWallpapers + 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(have) >= maxWallpapers {
		return map[string]any{"ok": false, "why": fmt.Sprintf("Tối đa %d ảnh", maxWallpapers)}
	}
	now := time.Now()
	key := fmt.Sprintf("wall/u%d/%d.bmp", uid, now.UnixMilli())
	row, err := wm.Insert("wallpapers", map[string]any{
		"user_id": uid, "file_key": key, "orient": orient, "size": size, "status": "uploading", "thumb": thumb, "pos": now.Unix(),
	})
	if err != nil {
		return wm.Fail(err)
	}
	return map[string]any{"ok": true, "id": wm.Int(row["id"]), "key": key, "why": ""}
}
