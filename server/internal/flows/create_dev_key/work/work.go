// Workflow create_dev_key, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"regexp"
	"strings"
	"unicode/utf8"
)

// A new developer key for the espos CLI (espos login): the browser makes "espos_dev_" + 64 random hex digits, shows
// it once and sends only its sha256, its last 4 characters and a name. At most 5 keys per account.
const maxKeys = 5

var hashRe = regexp.MustCompile(`^[0-9a-f]{64}$`)
var hintRe = regexp.MustCompile(`^[0-9a-f]{4}$`)

func no(status int, code, why string) map[string]any {
	return map[string]any{"status": status, "body": map[string]any{"error": why, "code": code}}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return no(401, "not_signed_in", "Cần đăng nhập")
	}
	b, _ := wmIn["body"].(map[string]any)
	h := strings.ToLower(strings.TrimSpace(wm.Str(b["key_hash"])))
	hint := strings.ToLower(strings.TrimSpace(wm.Str(b["hint"])))
	label := strings.Join(strings.Fields(wm.Str(b["label"])), " ")
	if !hashRe.MatchString(h) || !hintRe.MatchString(hint) {
		return no(422, "key_invalid", "Khoá không hợp lệ")
	}
	if label == "" {
		label = "CLI"
	}
	if utf8.RuneCountInString(label) > 40 || strings.ContainsAny(label, "\u2026") || !utf8.ValidString(label) {
		return no(422, "key_label", "Tên khoá tối đa 40 ký tự")
	}
	for _, r := range label {
		if r < 0x20 || r == 0x7f {
			return no(422, "key_label", "Tên khoá tối đa 40 ký tự")
		}
	}
	have, err := wm.QueryOpts("dev_keys", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}}, "limit": maxKeys + 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(have) >= maxKeys {
		return map[string]any{"status": 422, "body": map[string]any{"error": "Tối đa 5 khoá. Thu hồi bớt một khoá cũ.", "code": "key_limit", "params": map[string]any{"max": maxKeys}}}
	}
	row, ierr := wm.Insert("dev_keys", map[string]any{"user_id": uid, "key_hash": h, "label": label, "hint": hint, "last_used_ms": 0})
	if ierr != nil {
		return wm.Fail(ierr)
	}
	return map[string]any{"status": 201, "body": map[string]any{"key": map[string]any{
		"id": wm.Int(row["id"]), "label": label, "hint": hint, "created_at": wm.Str(row["created_at"]), "last_used_ms": 0}}}
}
