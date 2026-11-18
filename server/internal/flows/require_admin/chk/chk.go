// Workflow require_admin, node chk (imported by tools/importer).
package chk

import (
	"espos/internal/wm"
	"strconv"
	"strings"
)

// The signed-in end user is an admin when their id is in the variable admin_users ("468" or "468,512").
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	u, _ := in["user"].(map[string]any)
	uid := wm.Int(u["id"])
	if uid == 0 {
		uid = wm.Int(wmCtx["end_user_id"])
	}
	if uid == 0 {
		return map[string]any{"ok": false, "user_id": 0}
	}
	for _, s := range strings.Split(wm.Str(in["admins"]), ",") {
		if n, err := strconv.Atoi(strings.TrimSpace(s)); err == nil && n == uid {
			return map[string]any{"ok": true, "user_id": uid}
		}
	}
	return map[string]any{"ok": false, "user_id": uid}
}
