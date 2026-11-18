// Workflow start_pair, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"regexp"
	"strings"
	"time"
)

var codeRe = regexp.MustCompile(`^[0-9]{6}$`)
var hashRe = regexp.MustCompile(`^[0-9a-f]{64}$`)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	body, _ := in["body"].(map[string]any)
	code := strings.TrimSpace(wm.Str(body["code"]))
	hash := strings.ToLower(strings.TrimSpace(wm.Str(body["token_hash"])))
	name := strings.TrimSpace(wm.Str(body["name"]))
	if !codeRe.MatchString(code) || !hashRe.MatchString(hash) {
		return map[string]any{"status": 422, "body": map[string]any{"error": "invalid"}}
	}
	if name == "" || len([]rune(name)) > 40 {
		name = "Xteink X4"
	}
	kind := wm.Str(body["kind"])
	if kind != "s3" {
		kind = "x4"
	}
	now := time.Now().UnixMilli()
	// Free a code (or token) left by an expired attempt.
	for _, f := range []map[string]any{{"column": "code", "op": "eq", "value": code}, {"column": "token_hash", "op": "eq", "value": hash}} {
		old, err := wm.QueryOpts("pairings", map[string]any{"filters": []map[string]any{f}, "limit": 1})
		if err != nil {
			return wm.Fail(err)
		}
		if len(old) == 1 {
			if wm.Str(old[0]["status"]) == "pending" && int64(wm.Num(old[0]["expires_ms"])) > now && wm.Str(old[0]["token_hash"]) != hash {
				return map[string]any{"status": 409, "body": map[string]any{"error": "code_taken"}}
			}
			if derr := wm.Delete("pairings", old[0]["id"]); derr != nil {
				return wm.Fail(derr)
			}
		}
	}
	if _, err := wm.Insert("pairings", map[string]any{
		"code": code, "token_hash": hash, "name": name, "status": "pending", "expires_ms": now + 600000, "kind": kind,
	}); err != nil {
		return wm.Fail(err)
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true, "expires_s": 600}}
}
