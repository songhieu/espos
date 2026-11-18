// Workflow save_profile, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"regexp"
	"strings"
)

// POST /v1/profile {"call_name": "Lan"}: how the dogs call the person (Tài khoản › "Tên của bạn"). "" clears it.
// A dog told "tên mình là Lan" saves it the same way (assist_reply's cleanName: change both).

var nameRe = regexp.MustCompile(`^\p{L}[\p{L} '.\-]{0,39}$`)

func cleanName(s string) string {
	s = strings.Join(strings.Fields(s), " ")
	if len([]rune(s)) > 40 || !nameRe.MatchString(s) {
		return ""
	}
	return s
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	body, _ := in["body"].(map[string]any)
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	raw := strings.TrimSpace(wm.Str(body["call_name"]))
	name := cleanName(raw)
	if raw != "" && name == "" {
		return map[string]any{"status": 422, "body": map[string]any{"error": "Tên chỉ gồm chữ, tối đa 40 ký tự"}}
	}
	if _, err := wm.Upsert("user_profiles", []string{"user_id"}, map[string]any{"user_id": uid, "call_name": name}, nil); err != nil {
		wm.Log("save: ", err.Error())
		return map[string]any{"status": 500, "body": map[string]any{"error": "server_error"}}
	}
	return map[string]any{"status": 200, "body": map[string]any{"call_name": name}}
}
