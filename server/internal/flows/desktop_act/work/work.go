// Workflow desktop_act, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"espos/internal/wm"
	"fmt"
	"strconv"
	"strings"
	"time"
)

// POST /v1/devices/:id/desktop/act (s3desk/docs/desktop-protocol.md §6), the desktop's owner on the web:
//   {"op":"confirm","id"}  a command waiting for a yes is queued (confirmed by the web; 30 s more; a new nonce, so the
//                          Mac that showed the question as "confirm" runs it once)
//   {"op":"cancel","id"}   a waiting or queued command is cancelled (the Mac drops its prompt)
//   {"op":"run","cmd","args","confirm"}  a new command (node q calls desk_queue, via "web"); a risky one runs at once
//                          only with "confirm": true (the page asked the person first)
// Confirm and cancel poke the Mac's channel (node pub).

const confirmedTTL = 30000

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	no := func(status int, code string, extra ...any) map[string]any {
		b := map[string]any{"error": code}
		for i := 0; i+1 < len(extra); i += 2 {
			b[wm.Str(extra[i])] = extra[i+1]
		}
		return map[string]any{"status": status, "body": b, "run": false, "poke": false}
	}
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(in["uid"])
	}
	if uid == 0 {
		return no(401, "sign in")
	}
	if wm.Str(in["on"]) != "1" {
		return no(503, "desktop_disabled")
	}
	params, _ := in["params"].(map[string]any)
	id, perr := strconv.Atoi(wm.Str(params["id"]))
	if perr != nil || id <= 0 {
		return no(404, "not_found")
	}
	body, _ := in["body"].(map[string]any)
	op := wm.Str(body["op"])
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": id}}, "limit": 1, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": id}}, "limit": 1, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return no(500, "server_error")
	}
	first := func(i int) map[string]any {
		if i >= len(res) {
			return nil
		}
		switch r := res[i]["rows"].(type) {
		case []map[string]any:
			if len(r) > 0 {
				return r[0]
			}
		case []any:
			if len(r) > 0 {
				m, _ := r[0].(map[string]any)
				return m
			}
		}
		return nil
	}
	d := first(0)
	if d == nil || wm.Int(d["user_id"]) != uid || wm.Bool(d["revoked"]) || wm.Str(d["kind"]) != "desktop" {
		return no(404, "not_found")
	}
	if op == "run" {
		cmd := strings.TrimSpace(wm.Str(body["cmd"]))
		if cmd == "" || len(cmd) > 40 {
			return no(422, "unknown_command")
		}
		return map[string]any{"run": true, "poke": false, "cmd": cmd, "args": body["args"], "confirm": wm.Bool(body["confirm"]),
			"desktop_id": id, "status": 200, "body": map[string]any{}}
	}
	if op != "confirm" && op != "cancel" {
		return no(422, "bad_op")
	}
	cid := strings.TrimSpace(wm.Str(body["id"]))
	if cid == "" || len(cid) > 64 {
		return no(404, "not_found")
	}
	rows, err := wm.QueryOpts("desk_cmds", map[string]any{"filters": []map[string]any{
		{"column": "cmd_id", "op": "eq", "value": cid}, {"column": "desktop_id", "op": "eq", "value": id}}, "limit": 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return no(500, "server_error")
	}
	if len(rows) != 1 {
		return no(404, "not_found")
	}
	c := rows[0]
	now := time.Now().UnixMilli()
	state := wm.Str(c["state"])
	live := int64(wm.Num(c["exp_ms"])) > now
	if !live && (state == "confirm" || state == "queued" || state == "sent") {
		state = "expired"
	}
	vals := map[string]any{}
	if op == "confirm" {
		if state != "confirm" {
			return no(409, "not_pending", "state", state)
		}
		st, _ := first(1)["state"].(map[string]any)
		if wm.Str(st["confirm_at"]) == "mac" {
			return no(409, "confirm_on_mac", "state", state) // the Mac's settings: risky commands are confirmed there only
		}
		sum := sha256.Sum256([]byte(fmt.Sprint(cid, "confirm", wm.Str(d["token_hash"]), time.Now().UnixNano(), wmCtx["run_id"])))
		vals = map[string]any{"state": "queued", "exp_ms": now + confirmedTTL, "nonce": hex.EncodeToString(sum[:16]),
			"confirmed": map[string]any{"by": "web", "where": "web", "at_ms": now}}
		state = "queued"
	} else {
		if state == "sent" {
			return no(409, "already_sent", "state", state)
		}
		if state != "confirm" && state != "queued" {
			return no(409, "not_pending", "state", state)
		}
		vals = map[string]any{"state": "cancelled", "done_ms": now}
		state = "cancelled"
	}
	if _, uerr := wm.Update("desk_cmds", wm.Int(c["id"]), vals); uerr != nil {
		wm.Log("save: ", uerr.Error())
		return no(500, "server_error")
	}
	key := wm.Str(first(1)["chan_key"])
	return map[string]any{"run": false, "poke": key != "", "chan": fmt.Sprintf("%d:%s", id, key), "status": 200,
		"body": map[string]any{"ok": true, "id": cid, "state": state}}
}
