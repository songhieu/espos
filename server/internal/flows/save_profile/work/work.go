// Workflow save_profile, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"regexp"
	"strings"
)

// POST /v1/profile {"call_name": "Lan"} and/or {"web_lang": "en"}: only the fields sent change.
//   call_name: how the dogs call the person (Tài khoản › "Tên của bạn"). "" clears it. A dog told "tên mình là Lan"
//              saves it the same way (assist_reply's cleanName: change both).
//   web_lang:  the web app's language, "vi" or "en" (the account emails, auth_mail_*, use it too).
// A refusal carries a stable "code" next to the Vietnamese "error", for the web to say it in English.

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
	vals := map[string]any{"user_id": uid}
	if v, ok := body["call_name"]; ok {
		raw := strings.TrimSpace(wm.Str(v))
		name := cleanName(raw)
		if raw != "" && name == "" {
			return map[string]any{"status": 422, "body": map[string]any{"error": "Tên chỉ gồm chữ, tối đa 40 ký tự", "code": "name_invalid"}}
		}
		vals["call_name"] = name
	}
	if v, ok := body["web_lang"]; ok {
		lang := wm.Str(v)
		if lang != "vi" && lang != "en" {
			return map[string]any{"status": 422, "body": map[string]any{"error": "web_lang: vi hoặc en", "code": "web_lang_invalid"}}
		}
		vals["web_lang"] = lang
	}
	if len(vals) == 1 {
		return map[string]any{"status": 422, "body": map[string]any{"error": "Không có gì để lưu", "code": "nothing_to_save"}}
	}
	if _, err := wm.Upsert("user_profiles", []string{"user_id"}, vals, nil); err != nil {
		wm.Log("save: ", err.Error())
		return map[string]any{"status": 500, "body": map[string]any{"error": "server_error"}}
	}
	rows, err := wm.QueryOpts("user_profiles", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil || len(rows) != 1 {
		// saved; answer what was sent
		return map[string]any{"status": 200, "body": map[string]any{"call_name": wm.Str(vals["call_name"]), "web_lang": wm.Str(vals["web_lang"])}}
	}
	return map[string]any{"status": 200, "body": map[string]any{"call_name": wm.Str(rows[0]["call_name"]), "web_lang": wm.Str(rows[0]["web_lang"])}}
}
