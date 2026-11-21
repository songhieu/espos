// Workflow desktop_view, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"strconv"
	"time"
)

// GET /v1/devices/:id/desktop (s3desk/docs/desktop-protocol.md §6): the desktop's page on the web, for its owner.
// enabled false = the feature is paused (variable desktop_enabled): the page says so and offers nothing to press.
// pending: risky commands waiting for a yes; log: the last 50 commands. Read only: a command past its expiry shows
// as expired here even before the next sync writes it.

const onlineMs = 180000

func objOf(v any) any {
	if m, ok := v.(map[string]any); ok {
		return m
	}
	if s, isText := v.(string); isText && s != "" {
		var m map[string]any
		if json.Unmarshal([]byte(s), &m) == nil {
			return m
		}
	}
	return nil
}

func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(in["uid"])
	}
	if uid == 0 {
		return answer(401, map[string]any{"error": "sign in"})
	}
	params, _ := in["params"].(map[string]any)
	id, perr := strconv.Atoi(wm.Str(params["id"]))
	if perr != nil || id <= 0 {
		return answer(404, map[string]any{"error": "not_found"})
	}
	now := time.Now().UnixMilli()
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": id}}, "limit": 1, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": id}}, "limit": 1, "with_total": false},
		{"table": "desk_cmds", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": id}},
			"order_by": "created_ms", "desc": true, "limit": 50, "with_total": false},
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
	if len(devs) != 1 || wm.Int(devs[0]["user_id"]) != uid || wm.Bool(devs[0]["revoked"]) || wm.Str(devs[0]["kind"]) != "desktop" {
		return answer(404, map[string]any{"error": "not_found"})
	}
	var st map[string]any
	if s := rows(1); len(s) == 1 {
		st = s[0]
	}
	seen := int64(wm.Num(st["seen_ms"]))
	mode := wm.Str(st["mode"])
	if mode == "" {
		mode = "on"
	}
	pending := []any{}
	log := []any{}
	for _, c := range rows(2) {
		state := wm.Str(c["state"])
		live := int64(wm.Num(c["exp_ms"])) > now
		if (state == "confirm" || state == "queued" || state == "sent") && !live {
			state = "expired"
		}
		args := objOf(c["args"])
		if args == nil {
			args = map[string]any{}
		}
		from := map[string]any{"device_id": wm.Int(c["from_device_id"]), "name": wm.Str(c["from_name"]), "via": wm.Str(c["via"])}
		if state == "confirm" {
			pending = append(pending, map[string]any{"id": wm.Str(c["cmd_id"]), "cmd": wm.Str(c["cmd"]), "args": args,
				"risk": wm.Str(c["risk"]), "state": state, "exp_ms": int64(wm.Num(c["exp_ms"])), "from": from,
				"heard": wm.Str(c["heard"]), "created_ms": int64(wm.Num(c["created_ms"]))})
		}
		log = append(log, map[string]any{"id": wm.Str(c["cmd_id"]), "cmd": wm.Str(c["cmd"]), "args": args, "risk": wm.Str(c["risk"]),
			"state": state, "from": from, "heard": wm.Str(c["heard"]), "confirmed": objOf(c["confirmed"]), "result": objOf(c["result"]),
			"created_ms": int64(wm.Num(c["created_ms"])), "done_ms": int64(wm.Num(c["done_ms"]))})
	}
	state := objOf(st["state"])
	if state == nil {
		state = map[string]any{}
	}
	return answer(200, map[string]any{
		"enabled": wm.Str(in["on"]) == "1", "online": seen > 0 && now-seen <= onlineMs && mode != "off", "seen_ms": seen,
		"mode": mode, "app": wm.Str(st["app"]), "os": wm.Str(st["os"]), "state": state, "state_ms": int64(wm.Num(st["state_ms"])),
		"pending": pending, "log": log, "now_ms": now,
	})
}
