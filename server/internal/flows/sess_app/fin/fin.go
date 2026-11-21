// Workflow sess_app, node fin (imported by tools/importer).
package fin

import "espos/internal/wm"

// desk_queue's answer as the Sessions app reads it (desktop-protocol §9.3): done | sent | confirm_mac | refused |
// failed, with the error code and the status desk_queue chose (409 offline/view-only, 403 off, 429 rate...).
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	q, _ := in["q"].(map[string]any)
	if q["ok"] != true {
		st := wm.Int(q["status"])
		if st == 0 {
			st = 500
		}
		return map[string]any{"status": st, "body": map[string]any{"state": "refused", "err": wm.Str(q["error"])}}
	}
	state := wm.Str(q["state"])
	switch state {
	case "confirm":
		state = "confirm_mac"
	case "queued":
		state = "sent"
	case "done", "sent", "failed":
	case "cancelled", "expired", "denied":
		state = "refused"
	default:
		state = "sent"
	}
	b := map[string]any{"id": wm.Str(q["id"]), "state": state}
	if e := wm.Str(q["err"]); e != "" {
		b["err"] = e
	}
	if t := wm.Str(q["text"]); t != "" {
		b["text"] = t
	}
	return map[string]any{"status": 200, "body": b}
}
