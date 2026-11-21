// Workflow sess_app, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"regexp"
	"sort"
	"strings"
	"time"
)

// GET|POST /v1/device/app/dev.espos.sessions/:op — the S3's Sessions app (s3desk/docs/desktop-protocol.md §9):
//   GET  sessions  the account's Macs and Claude sessions (held up to `wait` s until the rev moves)
//   GET  screen    a session's screen since a seq, and a 15 s watch lease (a new watch pokes the Mac)
//   POST send      text / a key / approve / deny → desk_queue (node q), the nonce makes a retry harmless
//   POST suggest   ≤ 3 short replies for the session's prompt (cached per turn) → node ai
//   POST compose   a rough phrase → a clear reply → node ai
// Auth (§9.0): the device credential, which the S3's OS adds only for the calling app's own path (require_device),
// and the guard desk_app_auth: this app is installed on that device. Same account only; desktop_enabled.
// Outputs for the graph: status + body (answer now), or next "send" (q) / "ai" (ai); poke + chan for a new watch.

const (
	pkgID      = "dev.espos.sessions"
	onlineMs   = 180000
	leaseMs    = 15000
	purgeAfter = 10 * 60 * 1000
	fullLines  = 300
	maxBefore  = 200
)

var idRe = regexp.MustCompile(`^[A-Za-z0-9_.:\-]{1,100}$`)
var nonceRe = regexp.MustCompile(`^[0-9a-f]{16,64}$`)

func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body, "next": "", "poke": false}
}

func objOf(v any) map[string]any {
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

func listOf(v any) []any {
	if l, ok := v.([]any); ok {
		return l
	}
	if s, isText := v.(string); isText && s != "" {
		var l []any
		if json.Unmarshal([]byte(s), &l) == nil {
			return l
		}
	}
	return nil
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

// ---------------------------------------------------------------- risky input (s3desk/docs/desktop-risky.json v1)

type risk struct {
	why string
	re  *regexp.Regexp
}

// The same list as s3desk/docs/desktop-risky.json (scripts/test-desktop.py compares them).
var riskyText = []risk{
	{"shell", regexp.MustCompile(`^\s*!`)},
	{"delete", regexp.MustCompile(`\brm\s+-[A-Za-z]*[rRf]`)},
	{"delete", regexp.MustCompile(`\bfind\b.*\s-delete\b`)},
	{"force", regexp.MustCompile(`\bgit\s+push\b.*(\s-f\b|\s--force)`)},
	{"force", regexp.MustCompile(`\bgit\s+reset\s+--hard\b`)},
	{"force", regexp.MustCompile(`\bgit\s+clean\s+-[A-Za-z]*f`)},
	{"sudo", regexp.MustCompile(`\bsudo\b`)},
	{"pipe_shell", regexp.MustCompile(`\b(curl|wget)\b[^|]*\|\s*(sudo\s+)?(sh|bash|zsh)\b`)},
	{"database", regexp.MustCompile(`(?i)\b(drop\s+(table|database|schema)|truncate\s+table)\b`)},
	{"disk", regexp.MustCompile(`\b(mkfs(\.[a-z0-9]+)?|dd\s+if=)`)},
	{"kill", regexp.MustCompile(`\bkill(all)?\s+-9\b`)},
	{"perms", regexp.MustCompile(`\bchmod\s+(-R\s+)?777\b`)},
	{"session", regexp.MustCompile(`^\s*/(clear|exit|quit|logout)\b`)},
}
var riskyKeys = map[string]string{"esc": "interrupt", "ctrl_c": "interrupt"}

// riskyWhy: why the text is risky ("" = it is not); every line is checked.
func riskyWhy(text string) string {
	for _, l := range strings.Split(text, "\n") {
		for _, r := range riskyText {
			if r.re.MatchString(l) {
				return r.why
			}
		}
	}
	return ""
}

// ---------------------------------------------------------------- the Macs

type mac struct {
	id    int
	name  string
	row   map[string]any
	st    map[string]any
	seen  int64
	mode  string
	chan_ string
}

func (m mac) online(now int64) bool { return now-m.seen <= onlineMs && m.mode != "off" }

// streamWhy: "" when the Mac shares terminal text now, else why not (§9.1).
func (m mac) streamWhy(now int64) string {
	switch {
	case !m.online(now):
		return "offline"
	case m.mode == "view":
		return "view"
	case m.st["term_share"] != true:
		return "off_on_mac"
	}
	return ""
}

func sessionOf(st map[string]any, id string) map[string]any {
	for _, x := range listOf(st["sessions"]) {
		if s, _ := x.(map[string]any); s != nil && wm.Str(s["id"]) == id {
			return s
		}
	}
	return nil
}

func macsOf(uid int) ([]mac, int64, error) {
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid},
			{"column": "kind", "op": "eq", "value": "desktop"}, {"column": "revoked", "op": "eq", "value": false}},
			"order_by": "id", "limit": 20, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
			"order_by": "seen_ms", "desc": true, "limit": 20, "with_total": false},
	})
	if err != nil {
		return nil, 0, err
	}
	names := map[int]string{}
	for _, d := range rowsOf(res, 0) {
		names[wm.Int(d["id"])] = wm.Str(d["name"])
	}
	out := []mac{}
	rev := int64(0)
	for _, r := range rowsOf(res, 1) {
		id := wm.Int(r["device_id"])
		name, ok := names[id]
		if !ok {
			continue // unpaired
		}
		st := objOf(r["state"])
		if st == nil {
			st = map[string]any{}
		}
		if n := wm.Str(st["name"]); n != "" {
			name = n
		}
		m := mac{id: id, name: name, row: r, st: st, seen: int64(wm.Num(r["seen_ms"])), mode: wm.Str(r["mode"]),
			chan_: fmt.Sprintf("%d:%s", id, wm.Str(r["chan_key"]))}
		if m.mode == "" {
			m.mode = "on"
		}
		out = append(out, m)
		for _, k := range []string{"state_ms", "seen_ms"} {
			if v := int64(wm.Num(r[k])); v > rev && k == "state_ms" {
				rev = v
			}
		}
		if int64(wm.Num(r["seen_ms"]))/60000 > rev/60000 && m.seen > rev {
			// online/offline flips without a new state: the minute of the last heartbeat moves the rev too
			rev = max64(rev, (m.seen/60000)*60000)
		}
	}
	return out, rev, nil
}

func max64(a, b int64) int64 {
	if a > b {
		return a
	}
	return b
}

func listAnswer(macs []mac, rev, now int64) map[string]any {
	ml, sl := []any{}, []any{}
	for _, m := range macs {
		cmds := map[string]any{}
		for k, v := range objOf(m.st["commands"]) {
			if strings.HasPrefix(k, "claude.") {
				cmds[k] = v
			}
		}
		why := m.streamWhy(now)
		e := map[string]any{"id": m.id, "name": m.name, "online": m.online(now), "mode": m.mode, "seen_ms": m.seen,
			"stream": why == "", "confirm_at": wm.Str(m.st["confirm_at"]), "cmds": cmds}
		if why != "" {
			e["stream_why"] = why
		}
		ml = append(ml, e)
		for _, x := range listOf(m.st["sessions"]) {
			s, _ := x.(map[string]any)
			if s == nil {
				continue
			}
			o := map[string]any{"mac": m.id}
			for k, v := range s {
				o[k] = v
			}
			if req, _ := s["req"].(map[string]any); req != nil {
				r2 := map[string]any{}
				for k, v := range req {
					r2[k] = v
				}
				r2["risky"] = riskyWhy(wm.Str(req["cmd"])) != ""
				o["req"] = r2
			}
			sl = append(sl, o)
		}
	}
	return map[string]any{"rev": rev, "now_ms": now, "macs": ml, "sessions": sl}
}

// ---------------------------------------------------------------- screens

type sline struct {
	N   int64
	T   string
	R   any
	Seq int64
}

func linesOf(v any) []sline {
	out := []sline{}
	for _, x := range listOf(v) {
		a, _ := x.([]any)
		if len(a) < 4 {
			continue
		}
		out = append(out, sline{N: int64(wm.Num(a[0])), T: wm.Str(a[1]), R: a[2], Seq: int64(wm.Num(a[3]))})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].N < out[j].N })
	return out
}

func lineOut(l sline) map[string]any {
	o := map[string]any{"n": l.N, "t": l.T}
	if l.R != nil {
		if rs, ok := l.R.([]any); ok && len(rs) > 0 {
			o["r"] = rs
		}
	}
	return o
}

// frame: the lines changed after `since` (server seq), or the last fullLines when since is 0, from before this epoch
// or the client is ahead.
func frame(row map[string]any, since int64) map[string]any {
	ls := linesOf(row["lines"])
	seq := int64(wm.Num(row["seq"]))
	epochSeq := int64(wm.Num(row["epoch_seq"]))
	full := since <= 0 || since > seq || since < epochSeq
	if !full && len(ls) > 0 {
		oldest := ls[0].Seq
		for _, l := range ls {
			if l.Seq < oldest {
				oldest = l.Seq
			}
		}
		_ = oldest
	}
	out := []any{}
	if full {
		from := 0
		if len(ls) > fullLines {
			from = len(ls) - fullLines
		}
		for _, l := range ls[from:] {
			out = append(out, lineOut(l))
		}
	} else {
		for _, l := range ls {
			if l.Seq > since {
				out = append(out, lineOut(l))
			}
		}
	}
	return map[string]any{"epoch": wm.Str(row["epoch"]), "seq": seq, "full": full, "cols": wm.Int(row["cols"]),
		"rows": wm.Int(row["rows"]), "top": int64(wm.Num(row["top"])), "cur": listOf(row["cur"]), "lines": out}
}

// ---------------------------------------------------------------- Run

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if wm.Str(in["on"]) != "1" {
		return answer(503, map[string]any{"error": "desktop_disabled"})
	}
	now := time.Now().UnixMilli()
	// the guards: require_device (the device credential) and desk_app_auth (this app is installed on it)
	tr, _ := in["auth"].(map[string]any)
	devID, uid := wm.Int(tr["device_id"]), wm.Int(tr["user_id"])
	if devID == 0 || uid == 0 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	if wm.Str(tr["pkg_id"]) != pkgID {
		return answer(403, map[string]any{"error": "app_not_installed"})
	}
	params, _ := in["params"].(map[string]any)
	query, _ := in["query"].(map[string]any)
	body, _ := in["body"].(map[string]any)
	if body == nil {
		body = map[string]any{}
	}
	op := wm.Str(params["op"])
	method := strings.ToUpper(wm.Str(in["method"]))
	devName := wm.Str(tr["dev_name"])
	switch {
	case method == "GET" && op == "sessions":
		return sessions(uid, query, now)
	case method == "GET" && op == "screen":
		return screen(uid, devID, query, now)
	case method == "POST" && op == "send":
		return send(uid, devID, devName, body, now)
	case method == "POST" && (op == "suggest" || op == "compose"):
		return aiPrep(op, uid, devID, body, now)
	}
	return answer(404, map[string]any{"error": "not_found"})
}

func clampWait(v any, max int) int {
	w := wm.Int(v)
	if w < 0 {
		return 0
	}
	if w > max {
		return max
	}
	return w
}

func sessions(uid int, q map[string]any, now int64) map[string]any {
	macs, rev, err := macsOf(uid)
	if err != nil {
		return wm.Fail(err)
	}
	if len(macs) == 0 {
		return answer(404, map[string]any{"error": "no_desktop"})
	}
	if wait := clampWait(q["wait"], 4); wait > 0 && q["rev"] != nil && int64(wm.Num(q["rev"])) == rev {
		end := time.Now().Add(time.Duration(wait) * time.Second)
		for time.Now().Before(end) {
			time.Sleep(500 * time.Millisecond)
			m2, r2, e2 := macsOf(uid)
			if e2 != nil {
				break
			}
			if r2 != rev {
				macs = m2
				rev = r2
				break
			}
		}
		now = time.Now().UnixMilli()
	}
	return answer(200, listAnswer(macs, rev, now))
}

func findMac(uid int, id int) (*mac, error) {
	macs, _, err := macsOf(uid)
	if err != nil {
		return nil, err
	}
	for i := range macs {
		if macs[i].id == id {
			return &macs[i], nil
		}
	}
	return nil, nil
}

func screenRow(deskID int, s string) (map[string]any, error) {
	rows, err := wm.QueryOpts("desk_screens", map[string]any{"filters": []map[string]any{
		{"column": "desktop_id", "op": "eq", "value": deskID}, {"column": "session", "op": "eq", "value": s}}, "limit": 1})
	if err != nil || len(rows) == 0 {
		return nil, err
	}
	return rows[0], nil
}

func screen(uid, devID int, q map[string]any, now int64) map[string]any {
	deskID := wm.Int(q["mac"])
	s := wm.Str(q["s"])
	if deskID <= 0 || !idRe.MatchString(s) {
		return answer(422, map[string]any{"error": "bad_args"})
	}
	m, err := findMac(uid, deskID)
	if err != nil {
		return wm.Fail(err)
	}
	if m == nil {
		return answer(404, map[string]any{"error": "no_desktop"})
	}
	if why := m.streamWhy(now); why != "" {
		return answer(409, map[string]any{"error": "not_streaming", "why": why})
	}
	sess := sessionOf(m.st, s)
	if sess == nil {
		return answer(409, map[string]any{"error": "not_streaming", "why": "ended"})
	}
	if wm.Str(sess["term"]) != "iterm" {
		return answer(409, map[string]any{"error": "not_streaming", "why": "not_iterm"})
	}
	row, err := screenRow(deskID, s)
	if err != nil {
		return wm.Fail(err)
	}
	// the watch lease (§9.2): a new or lapsed watch pokes the Mac so it starts at once
	poke := row == nil || int64(wm.Num(row["watch_ms"])) < now
	lease := map[string]any{"watch_ms": now + leaseMs, "watch_dev": devID, "purge_ms": now + leaseMs + purgeAfter}
	if row == nil {
		vals := map[string]any{"user_id": uid, "desktop_id": deskID, "session": s, "epoch": "", "mac_seq": 0, "seq": 0,
			"epoch_seq": 0, "lines": []any{}, "top": 0, "cur": []any{}, "cols": 0, "rows": 0, "up_ms": 0, "up_n": 0}
		for k, v := range lease {
			vals[k] = v
		}
		if _, ierr := wm.Insert("desk_screens", vals); ierr != nil {
			return wm.Fail(ierr)
		}
		row = vals
	} else if now+leaseMs-int64(wm.Num(row["watch_ms"])) > 3000 { // renew at most every 3 s
		if _, uerr := wm.Update("desk_screens", wm.Int(row["id"]), lease); uerr != nil {
			return wm.Fail(uerr)
		}
	}
	if b := q["before"]; b != nil && wm.Str(b) != "" {
		before := int64(wm.Num(b))
		count := wm.Int(q["count"])
		if count <= 0 || count > maxBefore {
			count = maxBefore
		}
		ls := linesOf(row["lines"])
		out := []any{}
		for _, l := range ls {
			if l.N < before {
				out = append(out, lineOut(l))
			}
		}
		if len(out) > count {
			out = out[len(out)-count:]
		}
		return answer(200, map[string]any{"epoch": wm.Str(row["epoch"]), "lines": out})
	}
	since := int64(wm.Num(q["since"]))
	wait := clampWait(q["wait"], 3)
	if wait > 0 && (wm.Str(row["epoch"]) == "" || (since > 0 && since >= int64(wm.Num(row["seq"])))) {
		end := time.Now().Add(time.Duration(wait) * time.Second)
		for time.Now().Before(end) {
			time.Sleep(400 * time.Millisecond)
			r2, e2 := screenRow(deskID, s)
			if e2 != nil || r2 == nil {
				break
			}
			if int64(wm.Num(r2["seq"])) != int64(wm.Num(row["seq"])) {
				row = r2
				break
			}
		}
	}
	out := answer(200, nil)
	if wm.Str(row["epoch"]) == "" {
		out["body"] = map[string]any{"epoch": "", "seq": 0, "full": true, "lines": []any{}, "starting": true}
	} else {
		out["body"] = frame(row, since)
	}
	if poke {
		out["poke"] = true
		out["chan"] = m.chan_
	}
	return out
}

// ---------------------------------------------------------------- send

func send(uid, devID int, devName string, b map[string]any, now int64) map[string]any {
	deskID := wm.Int(b["mac"])
	s := wm.Str(b["s"])
	nonce := strings.ToLower(wm.Str(b["nonce"]))
	kind := wm.Str(b["kind"])
	if deskID <= 0 || !idRe.MatchString(s) || !nonceRe.MatchString(nonce) {
		return answer(422, map[string]any{"error": "bad_args"})
	}
	ack := b["ack_risky"] == true
	var cmd string
	args := map[string]any{"session": s}
	switch kind {
	case "text":
		t, _ := b["text"].(string)
		if strings.TrimSpace(t) == "" || len([]rune(t)) > 2000 {
			return answer(422, map[string]any{"error": "bad_args"})
		}
		if why := riskyWhy(t); why != "" && !ack {
			return answer(428, map[string]any{"error": "risky", "why": why})
		}
		cmd = "claude.reply"
		args["text"] = t
	case "key":
		k := strings.ToLower(wm.Str(b["key"]))
		if why, ok := riskyKeys[k]; ok && !ack {
			return answer(428, map[string]any{"error": "risky", "why": why})
		}
		cmd = "claude.key"
		args["key"] = k
	case "approve", "deny":
		req := wm.Str(b["req"])
		if !idRe.MatchString(req) {
			return answer(422, map[string]any{"error": "bad_args"})
		}
		cmd = "claude." + kind
		args["req"] = req
	default:
		return answer(422, map[string]any{"error": "bad_args"})
	}
	out := answer(0, nil)
	out["next"] = "send"
	out["q"] = map[string]any{"uid": uid, "cmd": cmd, "args": args, "desktop_id": deskID, "from_device_id": devID,
		"from_name": devName, "via": "app", "confirm": true, "key": nonce, "heard": ""}
	return out
}

// ---------------------------------------------------------------- AI (prepared here, node ai calls the model)

// tail: the last n lines of a session's screen copy as plain text (the newest last).
func tail(row map[string]any, n int) string {
	if row == nil {
		return ""
	}
	ls := linesOf(row["lines"])
	if len(ls) > n {
		ls = ls[len(ls)-n:]
	}
	var b strings.Builder
	for _, l := range ls {
		b.WriteString(l.T)
		b.WriteByte('\n')
	}
	return strings.TrimRight(b.String(), "\n")
}

func aiPrep(op string, uid, devID int, b map[string]any, now int64) map[string]any {
	deskID := wm.Int(b["mac"])
	s := wm.Str(b["s"])
	if deskID <= 0 || !idRe.MatchString(s) {
		return answer(422, map[string]any{"error": "bad_args"})
	}
	// 20 a minute per device for each of suggest / compose (§9.4), counted on a counters row of desk_app_tokens (one
	// per device: token_hash "device:<id>", revoked, so it never opens anything; a new token keeps the count)
	tr, terr := appRow(devID, uid)
	if terr != nil {
		return wm.Fail(terr)
	}
	min := now / 60000
	rl := objOf(tr["rl"])
	if rl == nil || int64(wm.Num(tr["rl_min"])) != min {
		rl = map[string]any{}
	}
	if wm.Int(rl[op]) >= 20 {
		return answer(429, map[string]any{"error": "rate", "retry_s": 60 - (now/1000)%60})
	}
	m, err := findMac(uid, deskID)
	if err != nil {
		return wm.Fail(err)
	}
	if m == nil {
		return answer(404, map[string]any{"error": "no_desktop"})
	}
	sess := sessionOf(m.st, s)
	if sess == nil {
		return answer(409, map[string]any{"error": "not_streaming", "why": "ended"})
	}
	row, err := screenRow(deskID, s)
	if err != nil {
		return wm.Fail(err)
	}
	screenText := ""
	if m.streamWhy(now) == "" {
		screenText = tail(row, 40)
	}
	ctx := sessContext(sess, screenText)
	lang := wm.Str(b["lang"])
	if lang == "" {
		lang = "vi"
	}
	var key string
	msgs := []any{}
	switch op {
	case "suggest":
		// the turn: the session's state, its pending request and the screen's last lines; asking again is free
		h := sha256.Sum256([]byte(fmt.Sprint(wm.Str(sess["state"]), "|", wm.Str(objOf(sess["req"])["id"]), "|", wm.Str(sess["last"]), "|", tail(row, 12), "|", lang)))
		key = hex.EncodeToString(h[:12])
		if row != nil && wm.Str(row["sugg_key"]) == key {
			chips := listOf(objOf(row["sugg"])["chips"])
			if len(chips) == 0 {
				return answer(204, map[string]any{})
			}
			return answer(200, map[string]any{"chips": chips, "for_seq": wm.Num(objOf(row["sugg"])["for_seq"]), "cached": true})
		}
		msgs = append(msgs,
			map[string]any{"role": "system", "content": "You help someone answer a Claude Code session from a small screen. " +
				"Give at most 3 short replies the person is likely to send next (each at most 40 characters), in the language " +
				lang + ", as JSON {\"chips\":[...]}. Prefer: approve/continue, ask to explain, a concrete fix request. " +
				"Never suggest destructive shell commands. If nothing fits, {\"chips\":[]}."},
			map[string]any{"role": "user", "content": ctx})
	case "compose":
		rough := strings.TrimSpace(wm.Str(b["rough"]))
		if rough == "" || len([]rune(rough)) > 600 {
			return answer(422, map[string]any{"error": "bad_args"})
		}
		msgs = append(msgs,
			map[string]any{"role": "system", "content": "Rewrite the person's rough words as one clear message to send to a " +
				"Claude Code session, keeping their intent and language, no more than 3 sentences, no preamble. Use the " +
				"screen only to resolve what they refer to. Answer JSON {\"text\":\"...\"}."},
			map[string]any{"role": "user", "content": ctx + "\n\nRough words: " + rough})
	}
	rl[op] = wm.Int(rl[op]) + 1
	if id := wm.Int(tr["id"]); id > 0 {
		if _, uerr := wm.Update("desk_app_tokens", id, map[string]any{"rl_min": min, "rl": rl}); uerr != nil {
			wm.Log("rl: ", uerr.Error())
		}
	}
	rowID := 0
	if row != nil {
		rowID = wm.Int(row["id"])
	}
	out := answer(0, nil)
	out["next"] = "ai"
	out["ai"] = map[string]any{"op": op, "messages": msgs, "row_id": rowID, "key": key, "for_seq": wm.Num(b["seq"]),
		"device_id": devID, "user_id": uid, "desktop_id": deskID, "session": s}
	out["device_id"] = devID
	out["user_id"] = uid
	return out
}

// appRow: the device's counters row for this app (made on first use; it can never authenticate: revoked, no hash).
func appRow(devID, uid int) (map[string]any, error) {
	key := fmt.Sprintf("device:%d", devID)
	rows, err := wm.QueryOpts("desk_app_tokens", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": devID}, {"column": "token_hash", "op": "eq", "value": key}}, "limit": 1})
	if err != nil {
		return nil, err
	}
	if len(rows) == 1 {
		return rows[0], nil
	}
	vals := map[string]any{"device_id": devID, "user_id": uid, "pkg_id": pkgID, "token_hash": key, "created_ms": time.Now().UnixMilli(),
		"used_ms": 0, "revoked": true, "rl_min": 0, "rl": map[string]any{}}
	if _, ierr := wm.Insert("desk_app_tokens", vals); ierr != nil {
		return nil, ierr
	}
	again, qerr := wm.QueryOpts("desk_app_tokens", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": devID}, {"column": "token_hash", "op": "eq", "value": key}}, "limit": 1})
	if qerr != nil {
		return nil, qerr
	}
	if len(again) == 1 {
		return again[0], nil
	}
	return vals, nil
}

func sessContext(sess map[string]any, screenText string) string {
	var b strings.Builder
	fmt.Fprintf(&b, "Session %q, state %s", wm.Str(sess["name"]), wm.Str(sess["state"]))
	if t := wm.Str(sess["tool"]); t != "" {
		fmt.Fprintf(&b, ", tool %s", t)
	}
	if req := objOf(sess["req"]); req != nil {
		fmt.Fprintf(&b, "\nIt asks permission: %s %s (%s)", wm.Str(req["tool"]), wm.Str(req["cmd"]), wm.Str(req["desc"]))
	}
	if l := wm.Str(sess["last"]); l != "" {
		fmt.Fprintf(&b, "\nLast message: %s", l)
	}
	if screenText != "" {
		b.WriteString("\nScreen (last lines):\n")
		b.WriteString(screenText)
	}
	return b.String()
}
