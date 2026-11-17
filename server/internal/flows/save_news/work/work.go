// Workflow save_news, node work (imported by tools/importer).
package work

import (
	"inkdesk/internal/wm"
	"strings"
	"time"
)

var slugs = map[string]bool{}

func init() {
	for _, s := range []string{"tin-moi-nhat", "tin-noi-bat", "thoi-su", "the-gioi", "kinh-doanh", "bat-dong-san", "khoa-hoc-cong-nghe", "suc-khoe", "giao-duc", "phap-luat", "gia-dinh", "du-lich", "giai-tri", "the-thao", "oto-xe-may", "y-kien"} {
		slugs[s] = true
	}
}

func oneOf(v int, opts ...int) bool {
	for _, o := range opts {
		if v == o {
			return true
		}
	}
	return false
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	b, _ := wmIn["body"].(map[string]any)
	if b == nil {
		return map[string]any{"status": 422, "body": map[string]any{"error": "missing body"}}
	}
	var picked []string
	for _, s := range strings.Split(wm.Str(b["sections"]), ",") {
		s = strings.TrimSpace(s)
		if slugs[s] {
			picked = append(picked, s)
		}
	}
	if len(picked) == 0 {
		return map[string]any{"status": 422, "body": map[string]any{"error": "Chọn ít nhất một chuyên mục"}}
	}
	hour := wm.Int(b["hour"])
	per := wm.Int(b["per_section"])
	keep := wm.Int(b["keep"])
	if !oneOf(hour, 5, 6, 7, 12) || !oneOf(per, 3, 5, 10) || !oneOf(keep, 3, 7, 30) {
		return map[string]any{"status": 422, "body": map[string]any{"error": "bad value"}}
	}
	if _, err := wm.Upsert("news_prefs", []string{"user_id"}, map[string]any{
		"user_id": uid, "enabled": b["enabled"] == true, "sections": strings.Join(picked, ","),
		"hour": hour, "per_section": per, "images": b["images"] == true, "keep": keep,
		"updated_ms": time.Now().UnixMilli(), // a colour desk's own change wins only when newer (device_sync)
	}, nil); err != nil {
		return wm.Fail(err)
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
}
