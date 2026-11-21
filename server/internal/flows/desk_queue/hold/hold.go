// Workflow desk_queue, node hold (imported by tools/importer).
package hold

import (
	"espos/internal/wm"
	"time"
)

// After the poke: a queued (safe) command is usually fetched, run and reported by the Mac within a second or two.
// Re-read the row every 150 ms for up to 2 s, so the dog can say the real result (protocol §4):
// done (text from the Mac), failed (err), or still queued / sent. A command waiting for a yes is answered at once.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	state := wm.Str(in["state"])
	if !wm.Bool(in["wait"]) || wm.Str(in["id"]) == "" {
		return map[string]any{"state": state, "text": "", "err": ""}
	}
	w := wm.Int(in["wait_s"]) // 2, or 9 for yt.play (it checks the music really plays)
	if w < 1 || w > 9 {
		w = 2
	}
	end := time.Now().Add(time.Duration(w) * time.Second)
	for time.Now().Before(end) {
		time.Sleep(150 * time.Millisecond)
		rows, err := wm.QueryOpts("desk_cmds", map[string]any{"filters": []map[string]any{
			{"column": "cmd_id", "op": "eq", "value": wm.Str(in["id"])}}, "limit": 1})
		if err != nil {
			wm.Log("read: ", err.Error())
			break
		}
		if len(rows) != 1 {
			break
		}
		state = wm.Str(rows[0]["state"])
		if state == "done" || state == "failed" || state == "expired" || state == "cancelled" {
			r, _ := rows[0]["result"].(map[string]any)
			return map[string]any{"state": state, "text": wm.Str(r["text"]), "err": wm.Str(r["err"])}
		}
	}
	return map[string]any{"state": state, "text": "", "err": ""}
}
