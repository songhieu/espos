// Workflow get_markets, node work (imported by tools/importer).
package work

import "espos/internal/wm"

// The cached gold prices, stock indices, stock quotes and exchange rates (market_tick keeps them
// fresh), for the widget previews on the web.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	rows, err := wm.QueryOpts("market_cache", map[string]any{"limit": 10})
	if err != nil {
		return wm.Fail(err)
	}
	out := map[string]any{}
	for _, r := range rows {
		k := wm.Str(r["key"])
		if k == "gold" || k == "index" || k == "fx" || k == "stk" || k == "crypto" {
			out[k] = r["data"]
		}
	}
	return map[string]any{"status": 200, "body": out}
}
