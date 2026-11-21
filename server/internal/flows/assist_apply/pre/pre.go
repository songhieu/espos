// Workflow assist_apply, node pre (imported by tools/importer).
package pre

import "espos/internal/wm"

// The computer commands of this turn (EspOS Desktop, 2026-11-21): assist_reply puts them in body.desk_todo
// ({acts: [{cmd, args, via, heard, from}], lines}); node each hands every one to desk_queue. None when the turn
// failed or asked for nothing on the computer (the loop then runs zero times).
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	body, _ := in["reply"].(map[string]any)
	todo, _ := body["desk_todo"].(map[string]any)
	acts, _ := todo["acts"].([]any)
	list := []any{}
	if wm.Int(in["status"]) == 200 {
		for _, a := range acts {
			if m, ok := a.(map[string]any); ok && wm.Str(m["cmd"]) != "" && len(list) < 3 {
				list = append(list, m)
			}
		}
	}
	lines, _ := todo["lines"].(map[string]any)
	if lines == nil {
		lines = map[string]any{}
	}
	return map[string]any{"list": list, "lines": lines}
}
