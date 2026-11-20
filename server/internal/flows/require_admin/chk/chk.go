// Workflow require_admin, node chk (imported by tools/importer).
package chk

import (
	"espos/internal/wm"
	"strconv"
	"strings"
)

// The signed-in end user is an admin when:
//   - their id is in the variable admin_users ("510" or "510,512"): role "owner" (never removable from the page), or
//   - the table admin_roles has a row for them: role "full" or "readonly".
//
// Write routes add the guard admin_require_full, which refuses "readonly".
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	u, _ := in["user"].(map[string]any)
	uid := wm.Int(u["id"])
	if uid == 0 {
		uid = wm.Int(wmCtx["end_user_id"])
	}
	if uid == 0 {
		return map[string]any{"ok": false, "user_id": 0, "role": ""}
	}
	for _, s := range strings.Split(wm.Str(in["admins"]), ",") {
		if n, err := strconv.Atoi(strings.TrimSpace(s)); err == nil && n == uid {
			return map[string]any{"ok": true, "user_id": uid, "role": "owner"}
		}
	}
	rows, err := wm.QueryOpts("admin_roles", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) == 1 {
		if r := wm.Str(rows[0]["role"]); r == "full" || r == "readonly" {
			return map[string]any{"ok": true, "user_id": uid, "role": r}
		}
	}
	return map[string]any{"ok": false, "user_id": uid, "role": ""}
}
