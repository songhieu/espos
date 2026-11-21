// Workflow desktop_sync, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"regexp"
	"strings"
	"time"
)

// POST /v1/desktop/sync (2026-11-21; s3desk/docs/desktop-protocol.md §2): the one authenticated exchange of the
// EspOS Desktop Mac app. Up: its state (stored in desk_state), the results of commands it ran, its own decisions on
// risky ones, alerts and notices (they become dashboard notes). Down: the commands to run or to ask about, the ids
// to drop, the heartbeat. With "wait_s" (its channel is down) the answer waits up to 25 s for a command.
//
// desk_cmds states: confirm -> (queued | denied | expired | cancelled) -> sent (in an answer) -> done | failed | expired.
// A sent command not reported yet goes down again until it expires (the Mac keeps seen nonces until expiry); one the
// Mac reported never does.

const maxState = 16384         // bytes of the state JSON
const maxWait = 25             // seconds a long-poll may wait (the platform's runner allows 50 s a run)
const noticeCap = 10           // alerts + notify turned into notes a minute per desktop
const dedupeMs = 30 * 60000    // an alert key shown in the last 30 minutes is not shown again
const cancelMs = 120000        // a delivered command that ended is named in "cancel" for 2 minutes
const keepMs = 30 * 86400000   // desk_cmds are kept 30 days
const purgeEvery = 6 * 3600000 // and swept at most every 6 hours per desktop
const confirmedTTL = 30000     // a command confirmed on the Mac may run for 30 s more

var appRe = regexp.MustCompile(`^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}([.+\-][0-9A-Za-z.\-]{1,20})?$`)

func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body}
}

func rowsOf(res []map[string]any, i int) []map[string]any {
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

// objOf: a json column or a request field as a map (text JSON from older rows is parsed).
func objOf(v any) map[string]any {
	if m, ok := v.(map[string]any); ok {
		return m
	}
	if s, isText := v.(string); isText && s != "" {
		m := map[string]any{}
		if json.Unmarshal([]byte(s), &m) == nil {
			return m
		}
	}
	return nil
}

// fit: one line of at most n letters, cut on a letter boundary, without an ellipsis (owner rule: no "…").
func fit(s string, n int) string {
	r := []rune(strings.Join(strings.Fields(s), " "))
	if len(r) <= n {
		return string(r)
	}
	return strings.TrimRight(string(r[:n]), " ·,;:-")
}

// noticeTitle: a notes row's title (§2): "title · body", at most 120 letters.
func noticeTitle(title, body string) string {
	t := strings.Join(strings.Fields(title), " ")
	b := strings.Join(strings.Fields(body), " ")
	s := t
	if t == "" {
		s = b
	} else if b != "" {
		s = t + " · " + b
	}
	return fit(s, 120)
}

// cmdOut: a desk_cmds row as the Mac reads it (§2.2).
func cmdOut(c map[string]any, state string) map[string]any {
	args := objOf(c["args"])
	if args == nil {
		args = map[string]any{}
	}
	var confirmed any
	if m := objOf(c["confirmed"]); m != nil {
		confirmed = m
	}
	return map[string]any{
		"id": wm.Str(c["cmd_id"]), "cmd": wm.Str(c["cmd"]), "args": args, "risk": wm.Str(c["risk"]), "state": state,
		"exp_ms": int64(wm.Num(c["exp_ms"])), "nonce": wm.Str(c["nonce"]),
		"from":  map[string]any{"device_id": wm.Int(c["from_device_id"]), "name": wm.Str(c["from_name"]), "via": wm.Str(c["via"])},
		"heard": wm.Str(c["heard"]), "confirmed": confirmed,
	}
}

// watchList: the sessions a device watches now (§9.5), none in view or off mode.
func watchList(screens []map[string]any, now int64, mode string) []any {
	out := []any{}
	if mode != "on" {
		return out
	}
	for _, sc := range screens {
		if until := int64(wm.Num(sc["watch_ms"])); until >= now {
			out = append(out, map[string]any{"s": wm.Str(sc["session"]), "until_ms": until})
		}
	}
	return out
}

// claudeNotes: one note per pending permission request (kind claude_perm, ref = the req id) and per session waiting
// for the person (claude_wait), for the S3 pop-up (§9.6); a note whose request or wait is gone is deleted, so it is
// cleared everywhere at the next sync. Only this desktop's notes (arg "<desktop>:...") are touched.
func claudeNotes(deskID, userID int, st map[string]any, have []map[string]any, now int64) []map[string]any {
	prefix := fmt.Sprintf("%d:", deskID)
	want := map[string]map[string]any{}
	if ss, ok := st["sessions"].([]any); ok {
		for _, x := range ss {
			m, _ := x.(map[string]any)
			if m == nil {
				continue
			}
			sid := wm.Str(m["id"])
			name := fit(wm.Str(m["name"]), 30)
			switch wm.Str(m["state"]) {
			case "perm":
				req, _ := m["req"].(map[string]any)
				rid := wm.Str(req["id"])
				if rid == "" || sid == "" {
					continue
				}
				exp := int64(wm.Num(req["deadline_ms"]))
				if exp <= now {
					exp = now + 10*60000
				}
				what := strings.TrimSpace(wm.Str(req["tool"]) + " " + wm.Str(req["cmd"]))
				want["p|"+rid] = map[string]any{"user_id": userID, "kind": "claude_perm", "app": "dev.espos.sessions",
					"arg": prefix + sid + ":" + rid, "ref": rid, "exp_ms": exp, "source": "desktop",
					"title": fit("Claude ("+name+"): "+what, 120),
					"data": map[string]any{"head": name, "tool": fit(wm.Str(req["tool"]), 40), "mono": fit(wm.Str(req["cmd"]), 300),
						"text": fit(wm.Str(req["desc"]), 200), "mac": deskID, "s": sid, "req": rid}}
			case "input":
				if sid == "" {
					continue
				}
				want["w|"+sid] = map[string]any{"user_id": userID, "kind": "claude_wait", "app": "dev.espos.sessions",
					"arg": prefix + sid, "ref": "w:" + sid, "exp_ms": now + 30*60000, "source": "desktop",
					"title": fit("Claude ("+name+") đang chờ bạn", 120),
					"data":  map[string]any{"head": name, "text": fit(wm.Str(m["last"]), 200), "mac": deskID, "s": sid}}
			}
		}
	}
	ops := []map[string]any{}
	for _, r := range have {
		if !strings.HasPrefix(wm.Str(r["arg"]), prefix) {
			continue // another desktop's
		}
		k := "w|" + strings.TrimPrefix(wm.Str(r["ref"]), "w:")
		if wm.Str(r["kind"]) == "claude_perm" {
			k = "p|" + wm.Str(r["ref"])
		}
		if _, still := want[k]; still {
			delete(want, k) // already there
			continue
		}
		ops = append(ops, map[string]any{"op": "delete", "table": "notifications", "id": wm.Int(r["id"])})
	}
	for _, v := range want {
		ops = append(ops, map[string]any{"op": "insert", "table": "notifications", "values": v})
	}
	return ops
}

// usageSig: what the S3's Claude widget shows, coarsely (see the stamp in Run).
func usageSig(st map[string]any, mode string) string {
	bucket := func(k string) int {
		u, _ := st["usage"].(map[string]any)
		w, _ := u[k].(map[string]any)
		if w == nil || w["pct"] == nil {
			return -1
		}
		return int(wm.Num(w["pct"])) / 5
	}
	open, waiting := 0, 0
	if ss, ok := st["sessions"].([]any); ok {
		for _, x := range ss {
			if m, isMap := x.(map[string]any); isMap {
				open++
				if s := wm.Str(m["state"]); s == "perm" || s == "input" {
					waiting++
				}
			}
		}
	}
	return fmt.Sprintf("%d|%d|%d|%d|%s", bucket("five_hour"), bucket("seven_day"), open, waiting, mode)
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if wm.Str(in["on"]) != "1" {
		return answer(503, map[string]any{"error": "desktop_disabled"})
	}
	dev, _ := in["device"].(map[string]any)
	deviceID := wm.Int(dev["device_id"])
	userID := wm.Int(dev["user_id"])
	if deviceID == 0 || userID == 0 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	body, _ := in["body"].(map[string]any)
	if body == nil {
		body = map[string]any{}
	}
	t0 := time.Now()
	now := t0.UnixMilli()

	// ---- the request ----
	mode := wm.Str(body["mode"])
	if mode != "view" && mode != "off" {
		mode = "on" // "off": the Mac's emergency stop, sent once as it disconnects (§9.1)
	}
	wait := wm.Int(body["wait_s"])
	if wait < 0 {
		wait = 0
	}
	if wait > maxWait {
		wait = maxWait
	}
	var newState map[string]any
	stateJSON := ""
	if body["state"] != nil {
		newState, _ = body["state"].(map[string]any)
		if newState == nil {
			return answer(422, map[string]any{"error": "bad_state"})
		}
		b, _ := json.Marshal(newState)
		if len(b) > maxState {
			return answer(413, map[string]any{"error": "state_too_big", "max_bytes": maxState})
		}
		stateJSON = string(b)
	}
	results, _ := body["results"].([]any)
	decisions, _ := body["mac_decisions"].([]any)
	if len(results) > 50 || len(decisions) > 50 {
		return answer(422, map[string]any{"error": "too_many"})
	}
	ids := []any{}
	for _, l := range [][]any{results, decisions} {
		for _, x := range l {
			if m, ok := x.(map[string]any); ok && wm.Str(m["id"]) != "" {
				ids = append(ids, wm.Str(m["id"]))
			}
		}
	}

	// ---- one round trip: the device, its state, its open commands, the ones that ended lately, the reported ones ----
	q := []map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1, "with_total": false},
		{"table": "desk_cmds", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID},
			{"column": "state", "op": "in", "value": []any{"confirm", "queued", "sent"}}}, "order_by": "created_ms", "limit": 100, "with_total": false},
		{"table": "desk_cmds", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID},
			{"column": "done_ms", "op": "gte", "value": now - cancelMs}}, "order_by": "created_ms", "limit": 100, "with_total": false},
	}
	if len(ids) > 0 {
		q = append(q, map[string]any{"table": "desk_cmds", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID},
			{"column": "cmd_id", "op": "in", "value": ids}}, "limit": 100, "with_total": false})
	}
	res, err := wm.QueryMany(q)
	if err != nil {
		wm.Log("read: ", err.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	devs := rowsOf(res, 0)
	if len(devs) != 1 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	if wm.Str(devs[0]["kind"]) != "desktop" {
		return answer(403, map[string]any{"error": "not_desktop"})
	}
	var row map[string]any
	if r := rowsOf(res, 1); len(r) == 1 {
		row = r[0]
	}
	st := newState
	if st == nil {
		st = objOf(row["state"])
	}
	if st == nil {
		st = map[string]any{}
	}
	confirmAt := wm.Str(st["confirm_at"]) // "s3" (the account side), "mac" or "either"
	byID := map[string]map[string]any{}
	order := []string{}
	for i := 2; i < len(res); i++ {
		for _, c := range rowsOf(res, i) {
			id := wm.Str(c["cmd_id"])
			if byID[id] == nil {
				byID[id] = c
				order = append(order, id)
			}
		}
	}
	// The S3 Sessions app (§9): the sessions a device watches now, and this desktop's Claude notes.
	r9, err := wm.QueryMany([]map[string]any{
		{"table": "desk_screens", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID}},
			"limit": 50, "with_total": false},
		{"table": "notifications", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID},
			{"column": "kind", "op": "in", "value": []any{"claude_perm", "claude_wait"}}}, "limit": 100, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	screens := rowsOf(r9, 0)
	ops := []map[string]any{}
	if stateJSON != "" {
		ops = append(ops, claudeNotes(deviceID, userID, newState, rowsOf(r9, 1), now)...)
	}
	set := func(c map[string]any, vals map[string]any) {
		for k, v := range vals {
			c[k] = v
		}
		ops = append(ops, map[string]any{"op": "update", "table": "desk_cmds", "id": wm.Int(c["id"]), "values": vals})
	}

	// ---- 1. risky commands the person answered on the Mac ----
	for _, x := range decisions {
		d, _ := x.(map[string]any)
		c := byID[wm.Str(d["id"])]
		if c == nil || wm.Str(c["state"]) != "confirm" || int64(wm.Num(c["exp_ms"])) <= now || confirmAt == "s3" {
			continue
		}
		if wm.Bool(d["allow"]) {
			vals := map[string]any{"state": "sent", "exp_ms": now + confirmedTTL,
				"confirmed": map[string]any{"by": "mac", "where": "mac", "at_ms": now}}
			if wm.Num(c["sent_ms"]) == 0 {
				vals["sent_ms"] = now
			}
			set(c, vals)
		} else {
			set(c, map[string]any{"state": "denied", "done_ms": now})
		}
	}

	// ---- 2. results: done or failed, once (a command the Mac reported never goes down again) ----
	for _, x := range results {
		r, _ := x.(map[string]any)
		c := byID[wm.Str(r["id"])]
		if c == nil || wm.Str(c["state"]) == "done" || wm.Str(c["state"]) == "failed" {
			continue
		}
		ok := wm.Bool(r["ok"])
		out := map[string]any{"ok": ok, "text": fit(wm.Str(r["text"]), 200), "err": fit(wm.Str(r["err"]), 200),
			"ms": wm.Int(r["ms"]), "confirmed_here": wm.Bool(r["confirmed_here"])}
		vals := map[string]any{"state": "failed", "result": out, "done_ms": now}
		if ok {
			vals["state"] = "done"
		}
		if wm.Bool(r["confirmed_here"]) && objOf(c["confirmed"]) == nil {
			vals["confirmed"] = map[string]any{"by": "mac", "where": "mac", "at_ms": now}
		}
		set(c, vals)
	}

	// ---- 3. expiry: nothing runs late ----
	for _, id := range order {
		c := byID[id]
		switch wm.Str(c["state"]) {
		case "confirm", "queued", "sent":
			if int64(wm.Num(c["exp_ms"])) <= now {
				set(c, map[string]any{"state": "expired", "done_ms": now})
			}
		}
	}

	// ---- 4. alerts and notices become dashboard notes (the S3s show them as "EspOS" notes) ----
	minute := now - now%60000
	nMin := int64(wm.Num(row["notice_min"]))
	n := wm.Int(row["notice_n"])
	if nMin != minute {
		nMin = minute
		n = 0
	}
	keys := map[string]any{}
	for k, v := range objOf(row["notice_keys"]) {
		if int64(wm.Num(v)) > now-dedupeMs {
			keys[k] = v
		}
	}
	dropped := 0
	for _, kind := range []string{"alerts", "notify"} {
		l, _ := body[kind].([]any)
		for i, x := range l {
			m, _ := x.(map[string]any)
			if m == nil || i >= 20 {
				dropped++
				continue
			}
			title := noticeTitle(wm.Str(m["title"]), wm.Str(m["body"]))
			if title == "" {
				continue
			}
			key := ""
			if kind == "alerts" {
				key = fit(wm.Str(m["key"]), 200)
				if key != "" && keys[key] != nil {
					continue // the same alert within 30 minutes: shown once
				}
			}
			if n >= noticeCap {
				dropped++
				continue
			}
			ops = append(ops, map[string]any{"op": "insert", "table": "notifications", "values": map[string]any{
				"user_id": userID, "title": title, "source": "desktop"}})
			n++
			if key != "" {
				keys[key] = now
			}
		}
	}

	// ---- 5. the state row, the device row, old commands ----
	chanKey := wm.Str(row["chan_key"])
	if chanKey == "" {
		sum := sha256.Sum256([]byte(fmt.Sprint(deviceID, wm.Str(devs[0]["token_hash"]), time.Now().UnixNano(), wmCtx["run_id"])))
		chanKey = hex.EncodeToString(sum[:16])
	}
	vals := map[string]any{"device_id": deviceID, "user_id": userID, "seen_ms": now, "mode": mode, "chan_key": chanKey,
		"notice_min": nMin, "notice_n": n, "notice_keys": keys}
	if a := fit(wm.Str(body["app"]), 40); a != "" {
		vals["app"] = a
	}
	if o := fit(wm.Str(body["os"]), 60); o != "" {
		vals["os"] = o
	}
	if stateJSON != "" {
		vals["state"] = newState
		vals["state_ms"] = now
	}
	// The S3's Claude widget (2026-11-21): a coarse signature of what it shows (5-hour and weekly % in steps of 5,
	// limits known, sessions open and waiting, mode). When it changes, usage_ms moves; device_ping and device_sync fold
	// the person's newest usage_ms into the S3's rev, so the widget updates within a minute without a sync per token.
	if sig := usageSig(st, mode); sig != wm.Str(row["usage_sig"]) {
		vals["usage_sig"] = sig
		vals["usage_ms"] = now
	}
	if now-int64(wm.Num(row["purge_ms"])) > purgeEvery {
		old, qerr := wm.QueryOpts("desk_cmds", map[string]any{"filters": []map[string]any{
			{"column": "desktop_id", "op": "eq", "value": deviceID}, {"column": "created_ms", "op": "lt", "value": now - keepMs}},
			"order_by": "created_ms", "limit": 50})
		if qerr != nil {
			wm.Log("purge: ", qerr.Error())
		} else {
			for _, o := range old {
				ops = append(ops, map[string]any{"op": "delete", "table": "desk_cmds", "id": wm.Int(o["id"])})
			}
			for _, sc := range screens { // terminal copies 10 min after their last watch (§9.2)
				if int64(wm.Num(sc["purge_ms"])) < now {
					ops = append(ops, map[string]any{"op": "delete", "table": "desk_screens", "id": wm.Int(sc["id"])})
				}
			}
			vals["purge_ms"] = now
		}
	}
	devVals := map[string]any{"last_sync": t0.UTC().Format(time.RFC3339)}
	if app := strings.TrimSpace(wm.Str(body["app"])); appRe.MatchString(app) {
		devVals["fw"] = "espos-desktop-" + app
	}
	ops = append(ops, map[string]any{"op": "update", "table": "devices", "id": deviceID, "values": devVals})
	if _, uerr := wm.Upsert("desk_state", []string{"device_id"}, vals, nil); uerr != nil {
		wm.Log("state: ", uerr.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	if _, berr := wm.Batch(ops); berr != nil {
		wm.Log("save: ", berr.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}

	// ---- 6. the answer: commands to run or to ask about, ids to drop ----
	collect := func(rows []map[string]any, at int64) ([]any, []any, []map[string]any) {
		cmds, cancel, sent := []any{}, []any{}, []map[string]any{}
		for _, c := range rows {
			stt := wm.Str(c["state"])
			live := int64(wm.Num(c["exp_ms"])) > at
			switch {
			case (stt == "queued" || stt == "sent") && live && mode == "on":
				cmds = append(cmds, cmdOut(c, "queued"))
				if stt == "queued" {
					sent = append(sent, map[string]any{"op": "update", "table": "desk_cmds", "id": wm.Int(c["id"]),
						"values": map[string]any{"state": "sent", "sent_ms": at}})
				}
			case stt == "confirm" && live && mode == "on" && confirmAt != "s3":
				cmds = append(cmds, cmdOut(c, "confirm"))
				if wm.Num(c["sent_ms"]) == 0 {
					sent = append(sent, map[string]any{"op": "update", "table": "desk_cmds", "id": wm.Int(c["id"]),
						"values": map[string]any{"sent_ms": at}})
				}
			case (stt == "cancelled" || stt == "expired" || stt == "denied") && wm.Num(c["sent_ms"]) > 0 &&
				int64(wm.Num(c["done_ms"])) >= at-cancelMs:
				cancel = append(cancel, wm.Str(c["cmd_id"]))
			}
		}
		return cmds, cancel, sent
	}
	rows := []map[string]any{}
	for _, id := range order {
		rows = append(rows, byID[id])
	}
	cmds, cancel, sent := collect(rows, now)

	watch := watchList(screens, now, mode)

	// Long-poll (the Mac's channel is down): wait for a new command, a new question, a new cancel or a new watch,
	// re-reading once a second, up to wait_s.
	if wait > 0 && len(cmds) == 0 && len(cancel) == 0 {
		end := t0.Add(time.Duration(wait) * time.Second)
		for time.Now().Before(end) {
			time.Sleep(time.Second)
			at := time.Now().UnixMilli()
			r2, qerr := wm.QueryMany([]map[string]any{
				{"table": "desk_cmds", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID},
					{"column": "state", "op": "in", "value": []any{"confirm", "queued", "sent"}}}, "order_by": "created_ms", "limit": 100, "with_total": false},
				{"table": "desk_cmds", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID},
					{"column": "done_ms", "op": "gte", "value": now}}, "order_by": "created_ms", "limit": 100, "with_total": false},
				{"table": "desk_screens", "filters": []map[string]any{{"column": "desktop_id", "op": "eq", "value": deviceID},
					{"column": "watch_ms", "op": "gte", "value": at}}, "limit": 50, "with_total": false},
			})
			if qerr != nil {
				wm.Log("wait: ", qerr.Error())
				break
			}
			fresh := append(rowsOf(r2, 0), rowsOf(r2, 1)...)
			news := false
			for _, c := range fresh {
				stt := wm.Str(c["state"])
				if stt == "queued" || (stt == "confirm" && wm.Num(c["sent_ms"]) == 0 && confirmAt != "s3" && mode == "on") ||
					((stt == "cancelled" || stt == "expired" || stt == "denied") && wm.Num(c["sent_ms"]) > 0) {
					news = true
				}
			}
			if w2 := watchList(rowsOf(r2, 2), at, mode); len(w2) > len(watch) {
				watch = w2 // a device started watching a session: answer now so the Mac starts uploading
				news = true
			}
			if news {
				c2, k2, s2 := collect(fresh, at)
				cmds = c2
				cancel = k2
				sent = s2
				break
			}
		}
	}
	if len(sent) > 0 {
		if _, berr := wm.Batch(sent); berr != nil {
			wm.Log("sent: ", berr.Error())
			return answer(500, map[string]any{"error": "server_error"})
		}
	}
	return answer(200, map[string]any{"now_ms": time.Now().UnixMilli(), "commands": cmds, "cancel": cancel,
		"heartbeat_s": 60, "poll_s": 0, "dropped_notices": dropped, "watch": watch})
}
