// Workflow dev_ai_set, node work (imported by tools/importer).
package work

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

// POST /v1/dev/ai {pkg, endpoint?, secret?, state?} (developer key: espos ai secret / endpoint / pause, the developer
// page): sets up the provider of one of the developer's own assistant apps (a private "u." package whose manifest
// provides "assistant"; store apps are set up by review, hosted).
//   endpoint  https to a public host (eapEndpointOK, the same rule provider_call applies)
//   secret    "esps_" + 32 to 80 letters, digits, - or _: made by the CLI or the browser (code nodes have no random
//             source) and shown to the developer once; a new one keeps the previous one valid 24 hours (rotation)
//   state     active or disabled
// The first secret also fixes the app's ref salt (from that secret, the package and the time; never changes after,
// so a provider's refs stay the same across rotations). A new app needs both an endpoint and a secret.
// Answer: {pkg, endpoint, secret_last4, state, rotated}. The secret is never returned or logged.

var secretRe = regexp.MustCompile(`^esps_[A-Za-z0-9_-]{32,80}$`)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	d, _ := in["dev"].(map[string]any)
	uid := wm.Int(d["user_id"])
	if uid <= 0 {
		return fail(401, "bad_key", "bad developer key")
	}
	body, _ := in["body"].(map[string]any)
	pkg := strings.TrimSpace(wm.Str(body["pkg"]))
	if !strings.HasPrefix(pkg, "u.") || len(pkg) > 60 {
		return fail(422, "not_yours", "only your own private assistant apps (u.…); store apps are set up by review")
	}
	res, err := wm.QueryMany([]map[string]any{
		{"table": "user_pkgs", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}, {"column": "pkg_id", "op": "eq", "value": pkg}}, "limit": 1, "with_total": false},
		{"table": "app_manifests", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}, {"column": "pkg_id", "op": "eq", "value": pkg}}, "limit": 1, "with_total": false},
		{"table": "assist_app_keys", "filters": []map[string]any{{"column": "pkg_id", "op": "eq", "value": pkg}}, "limit": 1, "with_total": false},
	})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rowsAt(res, 0)) != 1 {
		return fail(404, "not_yours", "no such package of yours (espos upload it first)")
	}
	if m := rowsAt(res, 1); len(m) != 1 || !providesSlot(m[0]["provides"], "assistant") {
		return fail(422, "not_assistant", `its manifest does not provide "assistant"`)
	}
	var row map[string]any
	if k := rowsAt(res, 2); len(k) == 1 {
		row = k[0]
		if wm.Int(row["user_id"]) != uid {
			return fail(404, "not_yours", "no such package of yours")
		}
	}
	nowMs := time.Now().UnixMilli()
	upd := map[string]any{"updated_ms": nowMs}
	if v, has := body["endpoint"]; has {
		ep := strings.TrimSpace(wm.Str(v))
		if why := eapEndpointOK(ep); why != "" {
			return fail(422, "bad_endpoint", "the endpoint must be https on a public host ("+why+")")
		}
		upd["endpoint"] = ep
	}
	rotated := false
	if v, has := body["secret"]; has {
		sec := strings.TrimSpace(wm.Str(v))
		if !secretRe.MatchString(sec) {
			return fail(422, "bad_secret", `a secret is "esps_" and 32 to 80 letters, digits, - or _ (espos ai secret makes one)`)
		}
		upd["secret"] = sec
		if row != nil && wm.Str(row["secret"]) != "" && wm.Str(row["secret"]) != sec {
			upd["secret_old"] = wm.Str(row["secret"])
			upd["old_until_ms"] = nowMs + 24*3600*1000
			rotated = true
		}
		if row == nil || wm.Str(row["salt"]) == "" {
			m := hmac.New(sha256.New, []byte(sec))
			m.Write([]byte(fmt.Sprintf("salt|%s|%d", pkg, nowMs)))
			upd["salt"] = hex.EncodeToString(m.Sum(nil)[:16])
		}
	}
	if v, has := body["state"]; has {
		s := wm.Str(v)
		if s != "active" && s != "disabled" {
			return fail(422, "bad_state", "state is active or disabled")
		}
		upd["state"] = s
	}
	if len(upd) == 1 {
		return fail(422, "nothing", "give endpoint, secret or state")
	}
	var saved map[string]any
	if row == nil {
		if upd["endpoint"] == nil || upd["secret"] == nil {
			return fail(422, "need_endpoint_and_secret", "a new assistant app needs its endpoint and a secret")
		}
		upd["pkg_id"] = pkg
		upd["user_id"] = uid
		if upd["state"] == nil {
			upd["state"] = "active"
		}
		r, ierr := wm.Insert("assist_app_keys", upd)
		if ierr != nil {
			return wm.Fail(ierr)
		}
		saved = r
	} else {
		r, uerr := wm.Update("assist_app_keys", wm.Int(row["id"]), upd)
		if uerr != nil {
			return wm.Fail(uerr)
		}
		saved = r
	}
	if saved == nil {
		saved = map[string]any{}
	}
	pick := func(k string) any {
		if v, has := upd[k]; has {
			return v
		}
		if row != nil {
			return row[k]
		}
		return saved[k]
	}
	sec := wm.Str(pick("secret"))
	last4 := ""
	if len(sec) >= 4 {
		last4 = sec[len(sec)-4:]
	}
	return map[string]any{"status": 200, "body": map[string]any{"pkg": pkg, "endpoint": wm.Str(pick("endpoint")), "secret_last4": last4,
		"state": wm.Str(pick("state")), "rotated": rotated}}
}

func fail(status int, code, msg string) map[string]any {
	return map[string]any{"status": status, "body": map[string]any{"error": msg, "code": code}}
}

func rowsAt(res []map[string]any, i int) []map[string]any {
	if i >= len(res) {
		return nil
	}
	if r, ok := res[i]["rows"].([]map[string]any); ok {
		return r
	}
	out := []map[string]any{}
	l, _ := res[i]["rows"].([]any)
	for _, x := range l {
		if m, ok := x.(map[string]any); ok {
			out = append(out, m)
		}
	}
	return out
}

func providesSlot(v any, slot string) bool {
	l, _ := v.([]any)
	for _, x := range l {
		if wm.Str(x) == slot {
			return true
		}
		if m, ok := x.(map[string]any); ok && wm.Str(m["slot"]) == slot {
			return true
		}
	}
	return false
}

var _ = json.Marshal

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
