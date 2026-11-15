// Workflow start_book_upload, node prep (imported by tools/importer).
package prep

import (
	"fmt"
	"inkdesk/internal/wm"
	"strings"
	"time"
)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"ok": false, "why": "sign in"}
	}
	b, _ := wmIn["body"].(map[string]any)
	name := strings.TrimSpace(wm.Str(b["file_name"]))
	title := strings.TrimSpace(wm.Str(b["title"]))
	author := strings.TrimSpace(wm.Str(b["author"]))
	size := wm.Int(b["size"])
	low := strings.ToLower(name)
	if !(strings.HasSuffix(low, ".epub") || strings.HasSuffix(low, ".txt")) {
		return map[string]any{"ok": false, "why": "Chỉ nhận file .epub hoặc .txt"}
	}
	if size <= 0 || size > 100*1024*1024 {
		return map[string]any{"ok": false, "why": "File tối đa 100 MB"}
	}
	if title == "" {
		title = strings.TrimSuffix(strings.TrimSuffix(name, ".epub"), ".txt")
	}
	if len([]rune(title)) > 200 {
		title = string([]rune(title)[:200])
	}
	if len([]rune(author)) > 120 {
		author = string([]rune(author)[:120])
	}
	safe := wm.SafeKey(name)
	if len(safe) > 120 {
		safe = safe[len(safe)-120:]
	}
	key := fmt.Sprintf("books/u%d/%d-%s", uid, time.Now().UnixMilli(), safe)
	row, err := wm.Insert("books", map[string]any{
		"user_id": uid, "title": title, "author": author, "file_key": key, "file_name": safe, "size": size, "status": "uploading",
	})
	if err != nil {
		return wm.Fail(err)
	}
	return map[string]any{"ok": true, "id": wm.Int(row["id"]), "key": key, "why": ""}
}
