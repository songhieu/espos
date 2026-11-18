// Workflow get_profile, node work (imported by tools/importer).
package work

import "espos/internal/wm"

// GET /v1/profile: {call_name}: how the dogs (the AI assistant, assist_reply) call the person; "" = not set.

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmIn["uid"])
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	rows, err := wm.QueryOpts("user_profiles", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return map[string]any{"status": 500, "body": map[string]any{"error": "server_error"}}
	}
	name := ""
	if len(rows) == 1 {
		name = wm.Str(rows[0]["call_name"])
	}
	return map[string]any{"status": 200, "body": map[string]any{"call_name": name}}
}
