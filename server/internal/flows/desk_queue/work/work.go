// Workflow desk_queue, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"
	"unicode"
)

// EspOS Desktop (2026-11-21; s3desk/docs/desktop-protocol.md §2.3, §4, §6). One command for a person's Mac, asked by
// the dog (assist_apply, via "voice") or by the web (desktop_act, via "web"). It checks the command against what the
// Mac enabled in its last state (state.commands, state.allow_from, mode) and the shape of the arguments, then inserts
// the desk_cmds row: "queued" (the Mac runs it at its next sync, which the poke asks for at once) or "confirm" (a
// risky command waiting for a person's yes, with a dashboard note for the S3s). The Mac checks everything again.
// The caller pokes rt:desk:<desktop>:<chan_key> (node pub) and node hold waits up to 2 s for a queued one's result.

// The phase-1 commands with their default level (§2.3). The Mac's state.commands decides which are on and at what
// level ("safe" or "confirm"); claude.approve always needs a person's yes. scripts/test-desktop.py checks that
// assist_reply's deskHints names the same commands.
var deskCmds = map[string]string{
	"app.open": "safe", "app.switch": "safe", "app.quit": "confirm",
	"win.hide_all": "safe", "win.zoom": "safe", "win.split": "safe",
	"folder.open": "safe", "url.open": "safe", "web.search": "safe",
	"tab.new": "safe", "tab.next": "safe", "tab.prev": "safe", "page.back": "safe", "page.reload": "safe",
	"video.pause": "safe", "video.play": "safe", "video.full": "safe", "video.seek": "safe",
	"yt.play": "safe", "music.playlist": "safe",
	"media.play": "safe", "media.pause": "safe", "media.toggle": "safe", "media.next": "safe", "media.prev": "safe",
	"vol.up": "safe", "vol.down": "safe", "vol.set": "safe", "vol.mute": "safe",
	"sys.lock": "safe", "sys.display_off": "safe", "sys.bright": "safe", "sys.sleep": "confirm",
	"shortcut.run": "safe",
	"claude.deny":  "safe", "claude.approve": "confirm", "claude.reply": "confirm", "claude.new": "confirm",
	"claude.stop": "confirm", "claude.focus": "safe",
}

const onlineMs = 180000  // a desktop that synced in the last 3 minutes is online (heartbeat every 60 s)
const safeTTL = 30000    // a safe command not delivered in 30 s expires (no late music at 3 a.m.)
const confirmTTL = 60000 // a risky command waits 60 s for a yes (then 30 s more once confirmed)
const perMin = 30        // commands a minute per desktop
const confirmsPerMin = 5 // risky ones a minute per desktop

var idRe = regexp.MustCompile(`^[A-Za-z0-9_.:\-]{1,100}$`)
var folderRe = regexp.MustCompile(`^[A-Za-z0-9_.\-]{1,64}$`)

// line: one line of text, at most n letters, or ok false (missing, not text, too long, or control characters).
func line(v any, n int, required bool) (string, bool) {
	if v == nil {
		return "", !required
	}
	s, isText := v.(string)
	if !isText {
		return "", false
	}
	s = strings.TrimSpace(s)
	for _, r := range s {
		if unicode.IsControl(r) {
			return "", false
		}
	}
	if len([]rune(s)) > n || (required && s == "") {
		return "", false
	}
	return s, true
}

// whole: an integer from a JSON number (or a numeric string), within lo..hi.
func whole(v any, lo, hi int) (int, bool) {
	var f float64
	switch x := v.(type) {
	case float64:
		f = x
	case int:
		f = float64(x)
	case int64:
		f = float64(x)
	case string:
		p, err := strconv.ParseFloat(strings.TrimSpace(x), 64)
		if err != nil {
			return 0, false
		}
		f = p
	default:
		return 0, false
	}
	if f != float64(int(f)) || int(f) < lo || int(f) > hi {
		return 0, false
	}
	return int(f), true
}

// checkArgs: the arguments of cmd, cleaned (only the keys the command takes), or "" and false when their shape is
// wrong (§2.3). Existence (app installed, folder allowed, session waiting, req id) is the Mac's to check.
func checkArgs(cmd string, raw any) (map[string]any, bool) {
	a, _ := raw.(map[string]any)
	if raw != nil && a == nil {
		if s, isText := raw.(string); !isText || strings.TrimSpace(s) != "" {
			return nil, false
		}
	}
	if a == nil {
		a = map[string]any{}
	}
	out := map[string]any{}
	str := func(k string, n int, required bool) bool {
		s, ok := line(a[k], n, required)
		if ok && s != "" {
			out[k] = s
		}
		return ok
	}
	ids := func(k string, re *regexp.Regexp) bool {
		s, ok := line(a[k], 100, true)
		if !ok || !re.MatchString(s) {
			return false
		}
		out[k] = s
		return true
	}
	switch cmd {
	case "app.open", "app.switch", "app.quit", "music.playlist", "shortcut.run":
		return out, str("name", 100, true)
	case "win.split":
		return out, str("a", 100, true) && str("b", 100, true)
	case "folder.open":
		return out, ids("id", folderRe)
	case "url.open":
		s, ok := line(a["url"], 500, true)
		if !ok {
			return nil, false
		}
		u, err := url.Parse(s)
		if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" {
			return nil, false
		}
		out["url"] = s
		return out, true
	case "web.search":
		e := strings.ToLower(strings.TrimSpace(wm.Str(a["engine"])))
		if e == "" {
			e = "google"
		}
		if e != "google" && e != "youtube" {
			return nil, false
		}
		out["engine"] = e
		return out, str("q", 100, true)
	case "video.seek":
		s, ok := whole(a["s"], -600, 600)
		if !ok || s == 0 {
			return nil, false
		}
		out["s"] = s
		return out, true
	case "yt.play":
		return out, str("q", 100, false) // no q: YouTube's home in Chrome (owner, 2026-11-21: a bare "mở nhạc")
	case "vol.up", "vol.down":
		by := 10
		if a["by"] != nil {
			b, ok := whole(a["by"], 1, 30)
			if !ok {
				return nil, false
			}
			by = b
		}
		out["by"] = by
		return out, true
	case "vol.set":
		v, ok := whole(a["v"], 0, 100)
		if !ok {
			return nil, false
		}
		out["v"] = v
		return out, true
	case "vol.mute":
		on := 1
		switch x := a["on"].(type) {
		case nil:
		case bool:
			if !x {
				on = 0
			}
		default:
			v, ok := whole(x, 0, 1)
			if !ok {
				return nil, false
			}
			on = v
		}
		out["on"] = on
		return out, true
	case "sys.bright":
		by, ok := whole(a["by"], -50, 50)
		if !ok || by == 0 {
			return nil, false
		}
		out["by"] = by
		return out, true
	case "claude.deny", "claude.approve":
		return out, ids("session", idRe) && ids("req", idRe)
	case "claude.stop", "claude.focus":
		return out, ids("session", idRe)
	case "claude.reply":
		return out, ids("session", idRe) && str("text", 200, true)
	case "claude.new":
		t := strings.Join(strings.Fields(wm.Str(a["task"])), " ")
		if t == "" || len([]rune(t)) > 300 {
			return nil, false
		}
		out["task"] = t
		return out, ids("folder", folderRe)
	}
	return out, true // the commands without arguments
}

// sessionName: a Claude session's name in the Mac's state, else "".
func sessionName(st map[string]any, id string) (string, map[string]any) {
	l, _ := st["sessions"].([]any)
	for _, x := range l {
		s, _ := x.(map[string]any)
		if s != nil && wm.Str(s["id"]) == id {
			req, _ := s["req"].(map[string]any)
			return wm.Str(s["name"]), req
		}
	}
	return "", nil
}

// cmdText: the command in a few Vietnamese words, for the S3's dashboard note and the web ("thoát app Safari").
func cmdText(cmd string, a map[string]any, st map[string]any) string {
	s := func(k string) string { return wm.Str(a[k]) }
	switch cmd {
	case "app.open":
		return "mở app " + s("name")
	case "app.switch":
		return "chuyển sang " + s("name")
	case "app.quit":
		return "thoát app " + s("name")
	case "win.hide_all":
		return "ẩn hết cửa sổ"
	case "win.zoom":
		return "phóng to cửa sổ"
	case "win.split":
		return "chia đôi màn hình " + s("a") + " và " + s("b")
	case "folder.open":
		name := s("id")
		l, _ := st["folders"].([]any)
		for _, x := range l {
			f, _ := x.(map[string]any)
			if f != nil && wm.Str(f["id"]) == s("id") && wm.Str(f["name"]) != "" {
				name = wm.Str(f["name"])
			}
		}
		return "mở thư mục " + name
	case "url.open":
		if u, err := url.Parse(s("url")); err == nil && u.Host != "" {
			return "mở trang " + u.Host
		}
		return "mở một trang web"
	case "web.search":
		if s("engine") == "youtube" {
			return "tìm trên YouTube: " + s("q")
		}
		return "tìm trên Google: " + s("q")
	case "tab.new":
		return "mở thẻ mới"
	case "tab.next":
		return "sang thẻ sau"
	case "tab.prev":
		return "về thẻ trước"
	case "page.back":
		return "quay lại trang trước"
	case "page.reload":
		return "tải lại trang"
	case "video.pause":
		return "dừng video"
	case "video.play":
		return "phát video"
	case "video.full":
		return "xem video toàn màn hình"
	case "video.seek":
		if n := wm.Int(a["s"]); n < 0 {
			return fmt.Sprintf("tua lại %d giây", -n)
		}
		return fmt.Sprintf("tua tới %d giây", wm.Int(a["s"]))
	case "yt.play":
		if s("q") != "" {
			return "mở YouTube: " + s("q")
		}
		return "mở YouTube"
	case "music.playlist":
		return "phát danh sách nhạc " + s("name")
	case "media.play":
		return "phát nhạc"
	case "media.pause":
		return "dừng nhạc"
	case "media.toggle":
		return "phát hay dừng nhạc"
	case "media.next":
		return "chuyển bài tiếp theo"
	case "media.prev":
		return "quay lại bài trước"
	case "vol.up":
		return fmt.Sprintf("tăng âm lượng %d", wm.Int(a["by"]))
	case "vol.down":
		return fmt.Sprintf("giảm âm lượng %d", wm.Int(a["by"]))
	case "vol.set":
		return fmt.Sprintf("đặt âm lượng %d%%", wm.Int(a["v"]))
	case "vol.mute":
		if wm.Int(a["on"]) == 0 {
			return "bật tiếng"
		}
		return "tắt tiếng"
	case "sys.lock":
		return "khóa màn hình"
	case "sys.display_off":
		return "tắt màn hình"
	case "sys.bright":
		if wm.Int(a["by"]) < 0 {
			return "giảm độ sáng màn hình"
		}
		return "tăng độ sáng màn hình"
	case "sys.sleep":
		return "cho máy ngủ"
	case "shortcut.run":
		return "chạy phím tắt " + s("name")
	case "claude.deny", "claude.approve", "claude.reply", "claude.stop", "claude.focus":
		name, req := sessionName(st, s("session"))
		where := ""
		if name != "" {
			where = " ở phiên " + name
		}
		switch cmd {
		case "claude.approve":
			if c := wm.Str(req["cmd"]); c != "" && wm.Str(req["id"]) == s("req") {
				return "cho Claude chạy " + c + where
			}
			return "duyệt yêu cầu của Claude" + where
		case "claude.deny":
			return "từ chối yêu cầu của Claude" + where
		case "claude.reply":
			return "trả lời Claude" + where + ": " + s("text")
		case "claude.stop":
			return "dừng Claude" + where
		}
		return "mở phiên Claude" + where
	case "claude.new":
		return "giao việc mới cho Claude: " + s("task")
	}
	return cmd
}

// fit: at most n letters, cut on a letter boundary, without an ellipsis (owner rule: no "…" on screens).
func fit(s string, n int) string {
	r := []rune(strings.Join(strings.Fields(s), " "))
	if len(r) <= n {
		return string(r)
	}
	return strings.TrimRight(string(r[:n]), " ·,;:-")
}

// stateOf: the Mac's stored state (a json column comes back as a map, or as text from older rows).
func stateOf(v any) map[string]any {
	if m, ok := v.(map[string]any); ok {
		return m
	}
	m := map[string]any{}
	if s, isText := v.(string); isText && s != "" {
		_ = json.Unmarshal([]byte(s), &m)
	}
	return m
}

// newID: 32 hex from the time, the run and the desktop's token hash (a value only the server holds), so a nonce or a
// command id cannot be guessed; the platform offers no random source to code nodes.
func newID(parts ...any) string {
	ctx := wm.Context()
	sum := sha256.Sum256([]byte(fmt.Sprint(append(parts, time.Now().UnixNano(), ctx["run_id"], ctx["node_id"])...)))
	return hex.EncodeToString(sum[:16])
}

func uuidOf(h string) string {
	b := []byte(h)
	b[12] = '4'
	b[16] = "89ab"[strings.IndexByte("0123456789abcdef", b[16])%4]
	return string(b[0:8]) + "-" + string(b[8:12]) + "-" + string(b[12:16]) + "-" + string(b[16:20]) + "-" + string(b[20:32])
}

// confirmWhere: where a risky command may get its yes (state.confirm_at): "either" (the Mac or the web), "mac", "web".
func confirmWhere(at string) string {
	switch at {
	case "mac":
		return "mac"
	case "s3":
		return "web" // the account side: the web (and, once its app ships, an S3)
	}
	return "either"
}

func refuse(status int, code, what string) map[string]any {
	return map[string]any{"ok": false, "status": status, "error": code, "what": what}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if wm.Str(in["on"]) != "1" {
		return refuse(503, "desktop_disabled", "")
	}
	uid := wm.Int(in["uid"])
	cmd := strings.TrimSpace(wm.Str(in["cmd"]))
	if uid <= 0 {
		return refuse(401, "unauthorized", "")
	}
	if _, known := deskCmds[cmd]; !known {
		return refuse(422, "unknown_command", "")
	}
	via := wm.Str(in["via"])
	if via != "voice" && via != "app" && via != "web" && via != "cli" {
		via = "voice"
	}
	fromID := wm.Int(in["from_device_id"])
	now := time.Now().UnixMilli()

	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid},
			{"column": "revoked", "op": "eq", "value": false}}, "order_by": "id", "limit": 50, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
			"order_by": "seen_ms", "desc": true, "limit": 10, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return refuse(500, "server_error", "")
	}
	rowsOf := func(i int) []map[string]any {
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
	desks := map[int]map[string]any{}
	for _, d := range rowsOf(0) {
		if wm.Str(d["kind"]) == "desktop" {
			desks[wm.Int(d["id"])] = d
		}
	}
	want := wm.Int(in["desktop_id"])
	var dev, row map[string]any
	for _, s := range rowsOf(1) { // newest first: without a desktop named, the one seen last
		id := wm.Int(s["device_id"])
		if desks[id] == nil || (want > 0 && id != want) {
			continue
		}
		dev = desks[id]
		row = s
		break
	}
	if dev == nil && want > 0 {
		dev = desks[want]
	}
	if dev == nil && want == 0 {
		for _, d := range desks {
			dev = d
		}
	}
	if dev == nil {
		return refuse(404, "no_desktop", "")
	}
	deskID := wm.Int(dev["id"])
	if row == nil || now-int64(wm.Num(row["seen_ms"])) > onlineMs {
		return refuse(409, "desktop_offline", "")
	}
	st := stateOf(row["state"])
	args, okArgs := checkArgs(cmd, in["args"])
	what := ""
	if okArgs {
		what = cmdText(cmd, args, st)
	}
	if wm.Str(row["mode"]) != "on" && wm.Str(row["mode"]) != "" {
		return refuse(409, "desktop_view_only", what)
	}
	cmds, _ := st["commands"].(map[string]any)
	risk := wm.Str(cmds[cmd])
	if risk != "safe" && risk != "confirm" {
		return refuse(403, "command_off", what)
	}
	if allow, isList := st["allow_from"].([]any); isList && via != "web" {
		ok := false
		for _, x := range allow {
			if wm.Int(x) == fromID && fromID > 0 {
				ok = true
			}
		}
		if !ok {
			return refuse(403, "sender_not_allowed", what)
		}
	}
	if !okArgs {
		return refuse(422, "bad_args", "")
	}
	if cmd == "claude.approve" {
		risk = "confirm" // always a person's yes (§2.1)
	}
	if cmd == "shortcut.run" {
		l, _ := st["shortcuts"].([]any)
		for _, x := range l {
			sc, _ := x.(map[string]any)
			if sc != nil && wm.Str(sc["name"]) == wm.Str(args["name"]) && wm.Bool(sc["risky"]) {
				risk = "confirm"
			}
		}
	}
	recent, err := wm.QueryOpts("desk_cmds", map[string]any{"filters": []map[string]any{
		{"column": "desktop_id", "op": "eq", "value": deskID}, {"column": "created_ms", "op": "gte", "value": now - 60000}},
		"order_by": "created_ms", "desc": true, "limit": perMin + 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return refuse(500, "server_error", what)
	}
	risky := 0
	for _, r := range recent {
		if wm.Str(r["risk"]) == "confirm" {
			risky++
		}
	}
	if len(recent) >= perMin || (risk == "confirm" && risky >= confirmsPerMin) {
		return refuse(429, "rate_limited", what)
	}

	confirmAt := wm.Str(st["confirm_at"]) // "s3" (the account side: an S3 or the web), "mac" or "either"
	state := "queued"
	exp := now + safeTTL
	var confirmed any
	if risk == "confirm" {
		if wm.Bool(in["confirm"]) && via == "web" && confirmAt != "mac" {
			confirmed = map[string]any{"by": "web", "where": "web", "at_ms": now} // the page asked the person first
		} else {
			state = "confirm"
			exp = now + confirmTTL
		}
	}
	fromName := fit(wm.Str(in["from_name"]), 40)
	if fromName == "" && via == "web" {
		fromName = "web"
	}
	id := uuidOf(newID(deskID, cmd, uid, wm.Str(dev["token_hash"])))
	vals := map[string]any{
		"cmd_id": id, "user_id": uid, "desktop_id": deskID, "from_device_id": fromID, "from_name": fromName, "via": via,
		"heard": fit(wm.Str(in["heard"]), 300), "cmd": cmd, "args": args, "risk": risk, "state": state,
		"nonce": newID(id, "nonce", wm.Str(dev["token_hash"])), "exp_ms": exp, "created_ms": now, "sent_ms": 0, "done_ms": 0,
	}
	if confirmed != nil {
		vals["confirmed"] = confirmed
	}
	ops := []map[string]any{{"op": "insert", "table": "desk_cmds", "values": vals}}
	if state == "confirm" {
		// The S3s show it as an "EspOS" note within a minute (the note raises the ping's rev).
		ops = append(ops, map[string]any{"op": "insert", "table": "notifications", "values": map[string]any{
			"user_id": uid, "title": fit("Bấm để xác nhận: "+what, 120), "source": "desktop"}})
	}
	if _, berr := wm.Batch(ops); berr != nil {
		wm.Log("save: ", berr.Error())
		return refuse(500, "server_error", what)
	}
	// The poke channel is rt:desk:<desktop>:<chan_key> (node pub writes the "rt:desk:" part: the platform checks a
	// channel's kind in the drawing).
	return map[string]any{"ok": true, "status": 200, "id": id, "state": state, "risk": risk, "what": what,
		"exp_ms": exp, "desktop_id": deskID, "chan": fmt.Sprintf("%d:%s", deskID, wm.Str(row["chan_key"])), "wait": state == "queued",
		"where": confirmWhere(confirmAt)}
}
