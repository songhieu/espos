// Workflow start_photo_upload, node prep (imported by tools/importer).
package prep

import (
	"fmt"
	"inkdesk/internal/wm"
	"strings"
	"time"
)

const maxPhotos = 60

// A picture for photo widgets: the original (shrunk by the browser to at most 1600 px, JPEG) and a small
// PNG thumbnail for the editor.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"ok": false, "why": "sign in"}
	}
	b, _ := wmIn["body"].(map[string]any)
	size, w, h := wm.Int(b["size"]), wm.Int(b["w"]), wm.Int(b["h"])
	thumb := wm.Str(b["thumb"])
	if size <= 0 || size > 3000000 {
		return map[string]any{"ok": false, "why": "Ảnh phải nhỏ hơn 3 MB"}
	}
	if w < 16 || h < 16 || w > 4000 || h > 4000 {
		return map[string]any{"ok": false, "why": "Kích thước ảnh không hợp lệ"}
	}
	if !strings.HasPrefix(thumb, "data:image/") || len(thumb) > 40000 {
		return map[string]any{"ok": false, "why": "bad thumbnail"}
	}
	have, err := wm.QueryOpts("photos", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}}, "limit": maxPhotos + 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(have) >= maxPhotos {
		return map[string]any{"ok": false, "why": fmt.Sprintf("Tối đa %d ảnh", maxPhotos)}
	}
	now := time.Now()
	key := fmt.Sprintf("photo/u%d/%d/o.jpg", uid, now.UnixMilli())
	row, err := wm.Insert("photos", map[string]any{
		"user_id": uid, "file_key": key, "size": size, "status": "uploading", "thumb": thumb, "pos": now.Unix(), "w": w, "h": h,
	})
	if err != nil {
		return wm.Fail(err)
	}
	return map[string]any{"ok": true, "id": wm.Int(row["id"]), "key": key, "why": ""}
}
