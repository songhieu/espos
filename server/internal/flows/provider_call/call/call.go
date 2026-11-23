// Workflow provider_call, node call (imported by tools/importer).
package call

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"net/url"
	"regexp"
	"strings"
	"time"
)

// provider_call (2026-11-22, phase 3; s3desk/docs/assistant-providers.md §2.3, §2.8, §2.9): one turn sent to a
// third-party assistant app's provider. In: pkg (the assistant app's package id), req (the v1 turn request that
// assist_reply's dry run built: what the provider may see), user_id, device_id (for its log only, never sent).
//
// The app's row in assist_app_keys gives the endpoint and the signing secret (both set by its developer: the
// developer keys page, `espos ai secret`); the request goes as the exact JSON it was signed over, with
// EspOS-Protocol, EspOS-Request-Id, EspOS-Timestamp and EspOS-Signature (both secrets for 24 hours after a new one).
// https only, a public host (eapEndpointOK); an answer that came through a redirect is refused. 3 failures within 5
// minutes rest the app 2 minutes (calls go straight to the fallback); a 429 rests it for its retry_after_s; a
// 401/403 marks it "unauthorized" (needs attention on its Activity page and for its developer); an answer over 64 KB
// is refused. Every call is logged in ai_calls (kept 7 days) with sizes, the reply's length and a hash: never the
// words. user.ref and device.ref are made this app's own (HMAC with its salt), so two apps cannot match a person;
// no device token, id, e-mail or account id is ever sent (the request carries none; the headers are only these).
//
// Limits of a code node, and what covers them: it cannot resolve a host name, so the address an endpoint resolves
// to (DNS rebinding) is checked by the platform's own egress rules (each hop, redirects included), not here; it
// cannot turn redirects off, so an answer that came through one is refused (the request has gone; the endpoint
// rule and the developer's review are the guard).
//
// Out: {ok, response, error, status, ms}. The response is NOT trusted here: assist_reply's policy checks it with
// eapClean like any answer.

const callDeadlineMs = 12000     // §2.8: a JSON answer complete within 12 s
const maxAnswerBytes = 64 * 1024 // an answer bigger than this is refused (too_large)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	pkg := strings.TrimSpace(wm.Str(in["pkg"]))
	req, _ := in["req"].(map[string]any)
	userID := wm.Int(in["user_id"])
	deviceID := wm.Int(in["device_id"])
	t0 := time.Now()
	nowMs := t0.UnixMilli()
	rid := wm.Str(req["request_id"])
	reqBytes := 0
	respBytes := 0
	logCall := func(ok bool, code string, status int, resp map[string]any) {
		if pkg == "" {
			return
		}
		say := wm.Str(eapMap(resp["say"])["text"])
		hash := ""
		if say != "" {
			h := sha256.Sum256([]byte(say))
			hash = hex.EncodeToString(h[:8])
		}
		if _, err := wm.Insert("ai_calls", map[string]any{"pkg_id": pkg, "user_id": userID, "device_id": deviceID, "request_id": rid,
			"ok": ok, "status": status, "error": code, "ms": time.Since(t0).Milliseconds(), "at_ms": nowMs, "req_bytes": reqBytes,
			"resp_bytes": respBytes, "say_len": len([]rune(say)), "say_hash": hash}); err != nil {
			wm.Log("ai_calls: ", err.Error())
		}
	}
	answer := func(ok bool, code string, status int, resp map[string]any) map[string]any {
		logCall(ok, code, status, resp)
		return map[string]any{"ok": ok, "response": resp, "error": code, "status": status, "ms": time.Since(t0).Milliseconds()}
	}
	if pkg == "" || req == nil || rid == "" {
		return answer(false, "bad_request", 0, nil)
	}
	rows, err := wm.QueryOpts("assist_app_keys", map[string]any{"filters": []map[string]any{{"column": "pkg_id", "op": "eq", "value": pkg}}, "limit": 1})
	if err != nil {
		wm.Log("assist_app_keys: ", err.Error())
		return answer(false, "server_error", 0, nil)
	}
	// an app without its own signing secret or ref salt is not set up: never sent (the same refs for two apps would
	// let them match a person)
	if len(rows) != 1 || wm.Str(rows[0]["state"]) != "active" || wm.Str(rows[0]["secret"]) == "" || wm.Str(rows[0]["salt"]) == "" {
		return answer(false, "no_provider", 0, nil)
	}
	app := rows[0]
	if int64(wm.Num(app["rest_until_ms"])) > nowMs {
		return answer(false, "resting", 0, nil)
	}
	endpoint := wm.Str(app["endpoint"])
	if why := eapEndpointOK(endpoint); why != "" {
		return answer(false, why, 0, nil)
	}
	// this app's own refs for the person and the device (its salt): two apps cannot match the same person
	salt := wm.Str(app["salt"])
	if u := eapMap(req["user"]); u != nil {
		u["ref"] = "u_" + saltRef(salt, "u", userID)
	}
	if d := eapMap(req["device"]); d != nil {
		d["ref"] = "d_" + saltRef(salt, "d", deviceID)
	}
	b, jerr := json.Marshal(req)
	if jerr != nil {
		return answer(false, "bad_request", 0, nil)
	}
	body := string(b)
	reqBytes = len(body)
	ts := nowMs / 1000
	sig := fmt.Sprintf("t=%d,v1=%s", ts, eapSign(wm.Str(app["secret"]), ts, rid, body))
	if old := wm.Str(app["secret_old"]); old != "" && int64(wm.Num(app["old_until_ms"])) > nowMs {
		sig += ",v1=" + eapSign(old, ts, rid, body) // rotation: both for 24 hours
	}
	r, ferr := wm.FetchWith("POST", endpoint, body, map[string]any{"Content-Type": "application/json", "Accept": "application/json",
		"EspOS-Protocol": "1", "EspOS-Request-Id": rid, "EspOS-Timestamp": fmt.Sprint(ts), "EspOS-Signature": sig})
	status := wm.Int(r["status"])
	code := ""
	retry := 0
	var resp map[string]any
	switch {
	case ferr != nil:
		code = "unreachable"
	case len(eapList(r["redirects"])) > 0:
		code = "redirected" // §2.9: no redirects followed (the platform follows them; the answer is refused)
	case status == 200:
		resp, _ = r["body"].(map[string]any)
		if resp == nil {
			code = "not_json"
		} else if rb, _ := json.Marshal(resp); len(rb) > maxAnswerBytes {
			respBytes = len(rb)
			code = "too_large"
			resp = nil
		} else {
			respBytes = len(rb)
		}
	case status == 401 || status == 403:
		code = "unauthorized"
	case status == 429:
		code = "rate_limited"
		eb, _ := r["body"].(map[string]any)
		e, _ := eb["error"].(map[string]any)
		retry = wm.Int(e["retry_after_s"])
	case status == 400:
		code = "bad_request"
	default:
		code = "server_error"
	}
	if code == "" && time.Since(t0).Milliseconds() > callDeadlineMs {
		code = "too_slow"
	}
	// the app's health: a success clears its failures; 3 failures within 5 minutes rest it 2 minutes
	upd := map[string]any{"updated_ms": nowMs}
	if code == "" {
		upd["fail_n"] = 0
		upd["last_ok_ms"] = nowMs
		upd["last_status"] = "ok"
	} else if code != "bad_request" {
		n := wm.Int(app["fail_n"]) + 1
		from := int64(wm.Num(app["fail_from_ms"]))
		if nowMs-from > 5*60*1000 {
			n = 1
			from = nowMs
		}
		upd["fail_n"] = n
		upd["fail_from_ms"] = from
		upd["last_status"] = code
		if n >= 3 {
			upd["rest_until_ms"] = nowMs + 2*60*1000
		}
		if code == "rate_limited" && retry > 0 {
			upd["rest_until_ms"] = nowMs + int64(retry)*1000
		}
	}
	if _, uerr := wm.Update("assist_app_keys", wm.Int(app["id"]), upd); uerr != nil {
		wm.Log("assist_app_keys: ", uerr.Error())
	}
	if code != "" {
		return answer(false, code, status, nil)
	}
	return answer(true, "", status, resp)
}

func eapMap(v any) map[string]any {
	m, _ := v.(map[string]any)
	return m
}

// saltRef: hex(HMAC-SHA256(salt, kind|id)), first 8 bytes: a stable ref for one app.
func saltRef(salt, kind string, id int) string {
	m := hmac.New(sha256.New, []byte(salt))
	m.Write([]byte(fmt.Sprintf("%s|%d", kind, id)))
	return hex.EncodeToString(m.Sum(nil)[:8])
}

func eapList(v any) []any {
	l, _ := v.([]any)
	return l
}

// ---- the protocol's shared block (scripts/sync-eap.py copies it from scripts/eap/eap.go) ----

// BEGIN EAP (scripts/eap/eap.go: edit it there)

// eapEndpointOK: "" when a provider's endpoint may be called from the hosted service (§2.9: https only, a public
// host: no localhost, .local/.internal names or private, loopback, link-local addresses), else why not. The
// platform checks every hop of a redirect too; provider_call refuses an answer that came through one.
func eapEndpointOK(raw string) string {
	u, err := url.Parse(strings.TrimSpace(raw))
	if err != nil || u.Scheme != "https" || u.Host == "" || u.User != nil {
		return "not_https"
	}
	h := strings.ToLower(strings.TrimSuffix(u.Hostname(), "."))
	if h == "localhost" || strings.HasSuffix(h, ".localhost") || strings.HasSuffix(h, ".local") || strings.HasSuffix(h, ".internal") || !strings.Contains(h, ".") && !strings.Contains(h, ":") {
		return "private_host"
	}
	if strings.Contains(h, ":") { // an IPv6 literal
		if h == "::1" || h == "::" || strings.HasPrefix(h, "fc") || strings.HasPrefix(h, "fd") || strings.HasPrefix(h, "fe80") || strings.HasPrefix(h, "::ffff:") {
			return "private_host"
		}
		return ""
	}
	var a, b, c, d int
	if n, _ := fmt.Sscanf(h, "%d.%d.%d.%d", &a, &b, &c, &d); n == 4 && fmt.Sprintf("%d.%d.%d.%d", a, b, c, d) == h {
		if a == 10 || a == 127 || a == 0 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254) ||
			(a == 100 && b >= 64 && b <= 127) || a >= 224 {
			return "private_host"
		}
	}
	return ""
}

// eapSign: hex(HMAC-SHA256(secret, timestamp + "." + request_id + "." + body)), the "v1" of EspOS-Signature.
func eapSign(secret string, ts int64, requestID, body string) string {
	m := hmac.New(sha256.New, []byte(secret))
	m.Write([]byte(fmt.Sprintf("%d.%s.%s", ts, requestID, body)))
	return hex.EncodeToString(m.Sum(nil))
}

var eapTopics = map[string]bool{"weather": true, "calendar": true, "alarm": true, "todo": true, "note": true, "news": true,
	"markets": true, "memory": true, "desk": true, "time": true, "reading": true, "chat": true, "refuse": true}
var eapKinds = map[string]bool{"date": true, "pref": true, "family": true, "work": true, "other": true}
var eapHHMM = regexp.MustCompile(`^([01][0-9]|2[0-3]):[0-5][0-9]$`)
var eapDays = regexp.MustCompile(`^[0-6]{0,7}$`)
var eapName = regexp.MustCompile(`^\p{L}[\p{L} '.\-]{0,39}$`)
var eapInt = regexp.MustCompile(`^-?\d{1,6}$`)
var eapCmd = regexp.MustCompile(`^[a-z]+\.[a-z_]+$`)

const eapMaxActions = 30 // a whole list deleted at once (held for a yes) fits
const eapMaxMemory = 20

// eapArgs: each action type's argument names.
var eapArgs = map[string][]string{
	"alarm.add": {"time", "days", "label"}, "alarm.update": {"ref", "time", "days", "label", "on"}, "alarm.delete": {"ref"},
	"todo.add": {"text"}, "todo.update": {"ref", "text"}, "todo.done": {"ref", "done"}, "todo.delete": {"ref"},
	"note.add": {"title", "body"}, "note.update": {"ref", "title", "body", "append"}, "note.delete": {"ref"},
	"setting.set": {"key", "value"}, "profile.name": {"name"}, "desk.run": {"cmd", "args"},
}
var eapListOf = map[string]string{"alarm": "alarms", "todo": "todo", "note": "notes"}

func eapObj(v any) (map[string]any, bool) {
	m, ok := v.(map[string]any)
	return m, ok && m != nil
}

func eapRunes(s string) int { return len([]rune(s)) }

func eapCut(s string, n int) string {
	r := []rune(s)
	if len(r) > n {
		return string(r[:n])
	}
	return s
}

func eapLine(s string) string { return strings.Join(strings.Fields(s), " ") }

// eapStr: v is a string of at most max characters.
func eapStr(v any, max int) bool {
	s, ok := v.(string)
	return ok && eapRunes(s) <= max
}

func eapStrings(v any) []string {
	out := []string{}
	if l, ok := v.([]any); ok {
		for _, x := range l {
			if s, isStr := x.(string); isStr {
				out = append(out, s)
			}
		}
	}
	return out
}

func eapHas(l []string, s string) bool {
	for _, x := range l {
		if x == s {
			return true
		}
	}
	return false
}

// eapRefs: the refs of one context list of the request (alarms, todo, notes, memories).
func eapRefs(req map[string]any, block string) map[string]bool {
	out := map[string]bool{}
	ctx, _ := eapObj(req["context"])
	b, _ := eapObj(ctx[block])
	items, _ := b["items"].([]any)
	for _, x := range items {
		if m, ok := eapObj(x); ok {
			if r, isStr := m["ref"].(string); isStr {
				out[r] = true
			}
		}
	}
	return out
}

func eapNum(v any) (float64, bool) {
	switch x := v.(type) {
	case float64:
		return x, true
	case int:
		return float64(x), true
	case int64:
		return float64(x), true
	}
	return 0, false
}

// eapActionWhy: why one action's arguments do not fit ("" when they do): "args" or "ref".
func eapActionWhy(typ string, a, req map[string]any) string {
	for k := range a {
		if !eapHas(eapArgs[typ], k) {
			return "args"
		}
	}
	if list := eapListOf[strings.Split(typ, ".")[0]]; list != "" && eapHas(eapArgs[typ], "ref") {
		r, ok := a["ref"].(string)
		if !ok {
			return "args"
		}
		if !eapRefs(req, list)[r] {
			return "ref"
		}
	}
	timeOK := func(v any) bool { s, ok := v.(string); return ok && eapHHMM.MatchString(s) }
	daysOK := func(v any) bool { s, ok := v.(string); return ok && eapDays.MatchString(s) }
	textOK := func(v any, max int) bool {
		s, ok := v.(string)
		return ok && strings.TrimSpace(s) != "" && eapRunes(s) <= max
	}
	switch typ {
	case "alarm.add":
		if !timeOK(a["time"]) || (a["days"] != nil && !daysOK(a["days"])) || (a["label"] != nil && !eapStr(a["label"], 40)) {
			return "args"
		}
	case "alarm.update":
		if a["time"] == nil && a["days"] == nil && a["label"] == nil && a["on"] == nil {
			return "args"
		}
		if (a["time"] != nil && !timeOK(a["time"])) || (a["days"] != nil && !daysOK(a["days"])) || (a["label"] != nil && !eapStr(a["label"], 40)) {
			return "args"
		}
		if _, isBool := a["on"].(bool); a["on"] != nil && !isBool {
			return "args"
		}
	case "todo.add", "todo.update":
		if !textOK(a["text"], 120) {
			return "args"
		}
	case "todo.done":
		if _, isBool := a["done"].(bool); !isBool {
			return "args"
		}
	case "note.add":
		t, b := "", ""
		if a["title"] != nil {
			if !eapStr(a["title"], 60) {
				return "args"
			}
			t = a["title"].(string)
		}
		if a["body"] != nil {
			if !eapStr(a["body"], 2000) {
				return "args"
			}
			b = a["body"].(string)
		}
		if strings.TrimSpace(t) == "" && strings.TrimSpace(b) == "" {
			return "args"
		}
	case "note.update":
		if a["title"] == nil && a["body"] == nil && a["append"] == nil {
			return "args"
		}
		if (a["title"] != nil && !eapStr(a["title"], 60)) || (a["body"] != nil && !eapStr(a["body"], 2000)) || (a["append"] != nil && !eapStr(a["append"], 2000)) {
			return "args"
		}
	case "setting.set":
		caps, _ := eapObj(req["capabilities"])
		settings, _ := caps["settings"].([]any)
		var cp map[string]any
		for _, x := range settings {
			if m, ok := eapObj(x); ok && m["key"] == a["key"] {
				cp = m
			}
		}
		v, isStr := a["value"].(string)
		if cp == nil || !isStr {
			return "args"
		}
		ctype, _ := cp["type"].(string) // a switch on an interface value panics in the code nodes' interpreter
		switch ctype {
		case "int":
			if !eapInt.MatchString(v) {
				return "args"
			}
			n := 0.0
			fmt.Sscan(v, &n)
			if lo, ok := eapNum(cp["min"]); ok && n < lo {
				return "args"
			}
			if hi, ok := eapNum(cp["max"]); ok && n > hi {
				return "args"
			}
		case "enum":
			if !eapHas(eapStrings(cp["values"]), v) {
				return "args"
			}
		case "time":
			if !eapHHMM.MatchString(v) {
				return "args"
			}
		default:
			if eapRunes(v) < 1 || eapRunes(v) > 60 {
				return "args"
			}
		}
	case "profile.name":
		n, ok := a["name"].(string)
		if !ok || !eapName.MatchString(eapLine(n)) {
			return "args"
		}
	case "desk.run":
		// any command by its name: the computer's own queue decides (off, offline, not allowed) and says why
		if c, isStr := a["cmd"].(string); !isStr || !eapCmd.MatchString(c) {
			return "args"
		}
		if _, ok := eapObj(a["args"]); a["args"] != nil && !ok {
			return "args"
		}
	}
	return ""
}

// eapClean: the server's check of a provider's answer to req (§2.6, §2.8, §2.11). A wrong version, another
// request's id or no words to say fail the turn (error); anything else that does not fit is dropped and named, the
// rest is kept, as the CLI's cleanResponse does (assistant-v1-vectors.json).
func eapClean(resp any, req map[string]any) (map[string]any, []string, string) {
	dropped := []string{}
	r, ok := eapObj(resp)
	if !ok {
		return nil, dropped, "not_object"
	}
	if r["v"] != "1" {
		return nil, dropped, "bad_version"
	}
	if r["request_id"] != req["request_id"] {
		return nil, dropped, "wrong_request"
	}
	say, ok := eapObj(r["say"])
	text, isStr := say["text"].(string)
	if !ok || !isStr || eapLine(text) == "" {
		return nil, dropped, "no_say"
	}
	out := map[string]any{"v": "1", "request_id": r["request_id"]}
	long := say["long"] == true
	dev, _ := eapObj(req["device"])
	max := 500
	if long {
		max = 950
	}
	key := "say_max_chars"
	if long {
		key = "say_max_chars_long"
	}
	if n, ok := eapNum(dev[key]); ok && n > 0 {
		max = int(n)
	}
	text = eapLine(text)
	if eapRunes(text) > max {
		text = eapCut(text, max)
		if end := strings.LastIndexAny(text, ".!?"); end >= 0 && len([]rune(text[:end])) > max*2/5 {
			text = text[:end+1]
		}
		dropped = append(dropped, "say_cut")
	}
	o := map[string]any{"text": text, "long": long}
	if say["lang"] != nil {
		if eapStr(say["lang"], 16) {
			o["lang"] = say["lang"]
		} else {
			dropped = append(dropped, "say_lang")
		}
	}
	if say["if_not_done"] != nil {
		if s, ok := say["if_not_done"].(string); ok && eapRunes(s) <= 300 && eapLine(s) != "" {
			o["if_not_done"] = eapLine(s)
		} else {
			dropped = append(dropped, "if_not_done")
		}
	}
	out["say"] = o
	if r["heard"] != nil {
		if h, ok := r["heard"].(string); ok {
			out["heard"] = eapCut(eapLine(h), 500)
		} else {
			dropped = append(dropped, "heard")
		}
	}
	caps, _ := eapObj(req["capabilities"])
	out["mood"] = "happy"
	if r["mood"] != nil {
		if m, ok := r["mood"].(string); ok && eapHas(eapStrings(caps["moods"]), m) {
			out["mood"] = m
		} else {
			dropped = append(dropped, "mood")
		}
	}
	out["topic"] = "chat"
	if r["topic"] != nil {
		if t, ok := r["topic"].(string); ok && eapTopics[t] {
			out["topic"] = t
		} else {
			dropped = append(dropped, "topic")
		}
	}
	allowed := eapStrings(caps["actions"])
	acts := []any{}
	if r["actions"] != nil {
		if l, ok := r["actions"].([]any); !ok {
			dropped = append(dropped, "actions")
		} else {
			ids := map[string]bool{}
			for i, x := range l {
				if i >= eapMaxActions {
					if i == eapMaxActions {
						dropped = append(dropped, "actions_over")
					}
					continue
				}
				a, isObj := eapObj(x)
				id, _ := a["id"].(string)
				name := id
				if name == "" {
					name = fmt.Sprint(i)
				}
				typ, isType := a["type"].(string)
				args, hasArgs := eapObj(a["args"])
				if !isObj || id == "" || eapRunes(id) > 32 || !isType || !hasArgs {
					dropped = append(dropped, "action:"+name+":shape")
					continue
				}
				if _, known := eapArgs[typ]; !known || !eapHas(allowed, typ) {
					dropped = append(dropped, "action:"+name+":type")
					continue
				}
				if ids[id] {
					dropped = append(dropped, "action:"+name+":dup_id")
					continue
				}
				if why := eapActionWhy(typ, args, req); why != "" {
					dropped = append(dropped, "action:"+name+":"+why)
					continue
				}
				ids[id] = true
				acts = append(acts, map[string]any{"id": id, "type": typ, "args": args})
			}
		}
	}
	out["actions"] = acts
	mems := []any{}
	if r["memory"] != nil {
		l, ok := r["memory"].([]any)
		if !ok || !eapHas(allowed, "memory") {
			dropped = append(dropped, "memory")
		} else {
			refs := eapRefs(req, "memories")
			for i, x := range l {
				if i >= eapMaxMemory {
					if i == eapMaxMemory {
						dropped = append(dropped, "memory_over")
					}
					continue
				}
				m, isObj := eapObj(x)
				if !isObj {
					dropped = append(dropped, fmt.Sprintf("memory:%d:shape", i))
					continue
				}
				fact := ""
				if f, isStr := m["fact"].(string); isStr {
					fact = eapLine(f)
				}
				ref, _ := m["ref"].(string)
				op, _ := m["op"].(string)
				switch op {
				case "remember":
					if fact == "" || eapRunes(fact) > 160 {
						dropped = append(dropped, fmt.Sprintf("memory:%d:fact", i))
						continue
					}
					kind, _ := m["kind"].(string)
					if !eapKinds[kind] {
						kind = "other"
					}
					mems = append(mems, map[string]any{"op": "remember", "fact": fact, "kind": kind})
				case "update":
					if !refs[ref] {
						dropped = append(dropped, fmt.Sprintf("memory:%d:ref", i))
						continue
					}
					if fact == "" || eapRunes(fact) > 160 {
						dropped = append(dropped, fmt.Sprintf("memory:%d:fact", i))
						continue
					}
					mems = append(mems, map[string]any{"op": "update", "ref": ref, "fact": fact})
				case "forget":
					// by ref, or by words ("match": the server finds at most 3 memories that hold them)
					if match, isStr := m["match"].(string); isStr && ref == "" {
						if match = eapLine(match); match == "" || eapRunes(match) > 80 {
							dropped = append(dropped, fmt.Sprintf("memory:%d:match", i))
							continue
						}
						mems = append(mems, map[string]any{"op": "forget", "match": match})
						continue
					}
					if !refs[ref] {
						dropped = append(dropped, fmt.Sprintf("memory:%d:ref", i))
						continue
					}
					mems = append(mems, map[string]any{"op": "forget", "ref": ref})
				default:
					dropped = append(dropped, fmt.Sprintf("memory:%d:op", i))
				}
			}
		}
	}
	out["memory"] = mems
	// the person's answer to what the server holds (request "held"), and a wish to hold this turn's actions
	out["ask_first"] = r["ask_first"] == true
	if r["held"] != nil {
		h, ok := eapObj(r["held"])
		rh, _ := eapObj(req["held"])
		kept := map[string]any{}
		bad := !ok
		for k, v := range h {
			s, isStr := v.(string)
			okv := isStr && (s == "yes" || s == "no" || (k == "desk" && (s == "unclear" || s == "other")))
			if (k != "actions" && k != "desk") || !okv || rh[k] == nil {
				bad = true
				continue
			}
			kept[k] = s
		}
		if bad {
			dropped = append(dropped, "held")
		}
		if len(kept) > 0 {
			out["held"] = kept
		}
	}
	fu, _ := eapObj(r["follow_up"])
	out["follow_up"] = map[string]any{"listen": fu["listen"] == true}
	ses, _ := eapObj(r["session"])
	note := ""
	if s, ok := ses["note"].(string); ok {
		note = strings.TrimSpace(eapCut(eapLine(s), 100))
	}
	var summary any
	if s, ok := ses["summary"].(string); ok && eapLine(s) != "" {
		summary = eapCut(eapLine(s), 1000)
	}
	out["session"] = map[string]any{"note": note, "summary": summary, "end": ses["end"] == true}
	if r["audio"] != nil {
		a, ok := eapObj(r["audio"])
		ao, _ := eapObj(caps["audio_out"])
		rate, isNum := eapNum(a["rate"])
		rates, _ := ao["rates"].([]any)
		rateOK := len(rates) == 0
		for _, x := range rates {
			if n, isN := eapNum(x); isN && n == rate {
				rateOK = true
			}
		}
		data, _ := a["data"].(string)
		if ok && a["format"] == "pcm16" && isNum && rate == float64(int(rate)) && rateOK && data != "" {
			out["audio"] = map[string]any{"format": "pcm16", "rate": rate, "data": data}
		} else {
			dropped = append(dropped, "audio")
		}
	}
	return out, dropped, ""
}

// ---- scopes from the app's permissions (§2.10; the SDK's assistant-v1-scopes.json, compared by eap_test.go) ----

var eapAlways = []string{"turn.text", "locale"}

// eapPermScopes: the request scopes each permission (declared in the manifest, accepted at install) grants.
var eapPermScopes = map[string][]string{
	"assist.audio":      {"turn.audio"},
	"assist.name":       {"profile.name"},
	"location:city":     {"place"},
	"data:weather":      {"weather"},
	"data:calendar":     {"calendar"},
	"data:markets":      {"markets"},
	"data:news":         {"news"},
	"data:reading":      {"reading"},
	"assist.alarms":     {"alarms"},
	"assist.todo":       {"todo"},
	"assist.notes":      {"notes"},
	"assist.notes.body": {"notes", "notes.body"},
	"assist.memory":     {"memories"},
	"assist.history":    {"history"},
	"assist.desk":       {"desk"},
	"alarms.write":      {"act.alarms"},
	"todo.write":        {"act.todo"},
	"notes.write":       {"act.notes"},
	"settings.write":    {"act.settings"},
	"memory.write":      {"act.memory"},
	"desk.control":      {"act.desk"},
}

// eapScopeActions: the action types each act.* scope allows.
var eapScopeActions = map[string][]string{
	"act.alarms":   {"alarm.add", "alarm.update", "alarm.delete"},
	"act.todo":     {"todo.add", "todo.update", "todo.done", "todo.delete"},
	"act.notes":    {"note.add", "note.update", "note.delete"},
	"act.settings": {"setting.set"},
	"act.memory":   {"memory", "profile.name"},
	"act.desk":     {"desk.run"},
}

// eapScopesFor: the scopes an assistant app gets for its granted permissions, in a stable order.
func eapScopesFor(perms []string) []string {
	have := map[string]bool{}
	out := []string{}
	add := func(s string) {
		if !have[s] {
			have[s] = true
			out = append(out, s)
		}
	}
	for _, s := range eapAlways {
		add(s)
	}
	for _, p := range perms {
		for _, s := range eapPermScopes[p] {
			add(s)
		}
	}
	return out
}

// eapBlockScope: the scope that lets a context block through.
var eapBlockScope = map[string]string{"place": "place", "weather": "weather", "calendar": "calendar", "markets": "markets", "news": "news",
	"reading": "reading", "alarms": "alarms", "todo": "todo", "notes": "notes", "memories": "memories", "history": "history", "desk": "desk"}

// eapFilter: the request as an app with these scopes may see it (a copy; req is not changed): the context blocks,
// the person's name, the recording, the notes' bodies, the actions and the computer's commands it was not granted
// are left out, and "scopes" says what is in.
func eapFilter(req map[string]any, scopes []string) map[string]any {
	ok := map[string]bool{}
	sc := make([]any, len(scopes))
	for i, s := range scopes {
		ok[s] = true
		sc[i] = s
	}
	out := map[string]any{}
	for k, v := range req {
		out[k] = v
	}
	out["scopes"] = sc
	ctx, _ := eapObj(req["context"])
	nctx := map[string]any{}
	for k, v := range ctx {
		if ok[eapBlockScope[k]] {
			nctx[k] = v
		}
	}
	if notes, has := eapObj(nctx["notes"]); has && !ok["notes.body"] {
		items, _ := notes["items"].([]any)
		bare := make([]any, len(items))
		for i, x := range items {
			n, _ := eapObj(x)
			bare[i] = map[string]any{"ref": n["ref"], "title": n["title"]}
		}
		nn := map[string]any{}
		for k, v := range notes {
			nn[k] = v
		}
		nn["items"] = bare
		nctx["notes"] = nn
	}
	out["context"] = nctx
	if u, has := eapObj(req["user"]); has {
		nu := map[string]any{}
		for k, v := range u {
			if k != "name" || ok["profile.name"] {
				nu[k] = v
			}
		}
		out["user"] = nu
	}
	if t, has := eapObj(req["turn"]); has {
		nt := map[string]any{}
		for k, v := range t {
			nt[k] = v
		}
		if in, hasIn := eapObj(t["input"]); hasIn && !ok["turn.audio"] {
			ni := map[string]any{}
			for k, v := range in {
				if k != "audio" {
					ni[k] = v
				}
			}
			nt["input"] = ni
		}
		out["turn"] = nt
	}
	if caps, has := eapObj(req["capabilities"]); has {
		nc := map[string]any{}
		for k, v := range caps {
			nc[k] = v
		}
		allowed := map[string]bool{}
		for s, types := range eapScopeActions {
			if ok[s] {
				for _, t := range types {
					allowed[t] = true
				}
			}
		}
		acts := []any{}
		for _, a := range eapStrings(caps["actions"]) {
			if allowed[a] {
				acts = append(acts, a)
			}
		}
		nc["actions"] = acts
		if !ok["act.desk"] {
			delete(nc, "desk")
		}
		if !ok["act.settings"] {
			delete(nc, "settings")
		}
		out["capabilities"] = nc
	}
	if h, has := eapObj(req["held"]); has && !ok["act.desk"] && h["desk"] != nil {
		nh := map[string]any{}
		for k, v := range h {
			if k != "desk" {
				nh[k] = v
			}
		}
		out["held"] = nh
	}
	return out
}

// END EAP
