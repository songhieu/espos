// Workflow post_notification, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"espos/internal/wm"
	"strings"
)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	headers, _ := in["headers"].(map[string]any)
	body, _ := in["body"].(map[string]any)
	auth := strings.TrimSpace(wm.Str(headers["authorization"]))
	key := strings.TrimSpace(strings.TrimPrefix(auth, "Bearer "))
	if !strings.HasPrefix(auth, "Bearer ") || len(key) < 32 || len(key) > 128 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	sum := sha256.Sum256([]byte(key))
	rows, err := wm.QueryOpts("user_profiles", map[string]any{
		"filters": []map[string]any{{"column": "notify_hash", "op": "eq", "value": hex.EncodeToString(sum[:])}},
		"limit":   1,
	})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	// Phone automations forward whole notifications: long text is shortened, never refused.
	clip := func(v string, n int) string {
		r := []rune(strings.Join(strings.Fields(v), " "))
		if len(r) <= n {
			return string(r)
		}
		return string(r[:n-1]) + "…"
	}
	title := clip(wm.Str(body["title"]), 120)
	source := clip(wm.Str(body["source"]), 40)
	if title == "" {
		return map[string]any{"status": 422, "body": map[string]any{"error": "title required"}}
	}
	if source != "" {
		title = source + " · " + title
	}
	if _, ierr := wm.Insert("notifications", map[string]any{"user_id": wm.Int(rows[0]["user_id"]), "title": title, "source": source}); ierr != nil {
		return wm.Fail(ierr)
	}
	return map[string]any{"status": 201, "body": map[string]any{"ok": true}}
}
