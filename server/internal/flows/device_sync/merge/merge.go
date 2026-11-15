// Workflow device_sync, node merge (imported by tools/importer).
package merge

import (
	"time"
)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	body, _ := in["sync"].(map[string]any)
	if body == nil {
		body = map[string]any{}
	}
	body["dashboard"] = in["dashboard"]
	// Stamped last, after the dashboard was built: the device sets its clock from it.
	body["now_ms"] = time.Now().UnixMilli()
	return map[string]any{"body": body}
}
