// Workflow desk_sweep, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"time"
)

// Every 5 minutes: terminal screen copies whose watch ended 10 minutes ago (desk_screens.purge_ms), and Claude notes
// (kind claude_perm / claude_wait) past their time. desktop_sync also clears a desktop's own when it syncs; this
// catches a Mac that went away.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	now := time.Now().UnixMilli()
	res, err := wm.QueryMany([]map[string]any{
		{"table": "desk_screens", "filters": []map[string]any{{"column": "purge_ms", "op": "lt", "value": now}},
			"order_by": "purge_ms", "limit": 200, "with_total": false},
		{"table": "notifications", "filters": []map[string]any{{"column": "kind", "op": "in", "value": []any{"claude_perm", "claude_wait"}},
			{"column": "exp_ms", "op": "lt", "value": now}}, "limit": 200, "with_total": false},
	})
	if err != nil {
		return wm.Fail(err)
	}
	ops := []map[string]any{}
	n := map[string]int{}
	for i, t := range []string{"desk_screens", "notifications"} {
		var rows []map[string]any
		switch r := res[i]["rows"].(type) {
		case []map[string]any:
			rows = r
		case []any:
			for _, x := range r {
				if m, ok := x.(map[string]any); ok {
					rows = append(rows, m)
				}
			}
		}
		for _, r := range rows {
			ops = append(ops, map[string]any{"op": "delete", "table": t, "id": wm.Int(r["id"])})
			n[t]++
		}
	}
	if len(ops) > 0 {
		if _, berr := wm.Batch(ops); berr != nil {
			return wm.Fail(berr)
		}
	}
	return map[string]any{"screens": n["desk_screens"], "notes": n["notifications"]}
}
