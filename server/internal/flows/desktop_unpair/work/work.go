// Workflow desktop_unpair, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"time"
)

// POST /v1/desktop/unpair (s3desk/docs/desktop-protocol.md §1): the Mac leaves the account. Its device row is
// revoked (the token stops working, as the web's "Gỡ máy"), the commands still waiting for it are cancelled and its
// state row says "off", so the dog stops offering it.
func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if wm.Str(in["on"]) != "1" {
		return answer(503, map[string]any{"error": "desktop_disabled"})
	}
	dev, _ := in["device"].(map[string]any)
	deviceID := wm.Int(dev["device_id"])
	if deviceID == 0 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	now := time.Now().UnixMilli()
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1, "with_total": false},
		{"table": "desk_cmds", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID},
			{"column": "state", "op": "in", "value": []any{"confirm", "queued", "sent"}}}, "limit": 100, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	rows := func(i int) []map[string]any {
		out := []map[string]any{}
		if i >= len(res) {
			return out
		}
		switch r := res[i]["rows"].(type) {
		case []map[string]any:
			return r
		case []any:
			for _, x := range r {
				if m, ok := x.(map[string]any); ok {
					out = append(out, m)
				}
			}
		}
		return out
	}
	devs := rows(0)
	if len(devs) != 1 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	if wm.Str(devs[0]["kind"]) != "desktop" {
		return answer(403, map[string]any{"error": "not_desktop"})
	}
	ops := []map[string]any{{"op": "update", "table": "devices", "id": deviceID, "values": map[string]any{"revoked": true}}}
	for _, c := range rows(1) {
		ops = append(ops, map[string]any{"op": "update", "table": "desk_cmds", "id": wm.Int(c["id"]),
			"values": map[string]any{"state": "cancelled", "done_ms": now}})
	}
	if s := rows(2); len(s) == 1 {
		ops = append(ops, map[string]any{"op": "update", "table": "desk_state", "id": wm.Int(s[0]["id"]), "values": map[string]any{"mode": "off"}})
	}
	if _, berr := wm.Batch(ops); berr != nil {
		wm.Log("save: ", berr.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	return answer(200, map[string]any{"ok": true})
}
