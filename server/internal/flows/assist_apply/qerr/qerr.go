// Workflow assist_apply, node qerr (imported by tools/importer).
package qerr

import "espos/internal/wm"

// desk_queue itself failed (not a refusal: those come back as data): this command counts as not done.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	wm.Log("desk_queue failed: ", wm.Str(wmIn["error"]))
	return map[string]any{"ok": false, "state": "refused", "error": "server_error"}
}
