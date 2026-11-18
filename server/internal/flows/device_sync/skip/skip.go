// Workflow device_sync, node skip (imported by tools/importer).
package skip

import "espos/internal/wm"

// A game whose file could not be signed this time: left out of this answer, tried again at the next sync.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	it, _ := wmIn["item"].(map[string]any)
	return map[string]any{"id": wm.Int(it["id"]), "url": "", "icon_url": ""}
}
