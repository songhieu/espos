// Workflow save_notify_key, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"regexp"
	"strings"
)

var hashRe = regexp.MustCompile(`^[0-9a-f]{64}$`)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	b, _ := wmIn["body"].(map[string]any)
	h := strings.ToLower(strings.TrimSpace(wm.Str(b["key_hash"])))
	if !hashRe.MatchString(h) {
		return map[string]any{"status": 422, "body": map[string]any{"error": "key_hash"}}
	}
	if _, err := wm.Upsert("user_profiles", []string{"user_id"}, map[string]any{"user_id": uid, "notify_hash": h}, nil); err != nil {
		return wm.Fail(err)
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true}}
}
