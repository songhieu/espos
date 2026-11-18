// Workflow device_sync, node keep (imported by tools/importer).
package keep

import (
	"espos/internal/wm"
	"time"
)

// One of the owner's games was signed again (7 days): kept on its row, so the next syncs hand out the same URLs
// until they come within 3 days of their end. A failed write only means it is signed again next time.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	it, _ := in["item"].(map[string]any)
	id := wm.Int(it["id"])
	url := wm.Str(in["url"])
	icon := ""
	if wm.Bool(it["has_icon"]) {
		icon = wm.Str(in["icon"])
	}
	exp := time.Now().UnixMilli() + 7*24*3600*1000 - 60000
	if id > 0 && url != "" {
		if _, err := wm.Update("user_pkgs", id, map[string]any{"url": url, "icon_url": icon, "url_exp_ms": exp}); err != nil {
			wm.Log("user_pkgs ", id, ": ", err.Error())
		}
	}
	return map[string]any{"id": id, "url": url, "icon_url": icon}
}
