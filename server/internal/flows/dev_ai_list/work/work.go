// Workflow dev_ai_list, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"sort"
	"time"
)

// GET /v1/dev/ai (developer key: espos ai status, the developer page): the developer's assistant apps (their private
// packages whose manifest provides "assistant"), each with its provider setup (endpoint, the secret's last 4, state)
// and its health: the last status, resting until, and the last 24 hours of calls (count, failures by code, median
// time). Never a secret, never what was said (ai_calls holds none).

func Run(wmIn, wmCtx map[string]any) map[string]any {
	d, _ := wmIn["dev"].(map[string]any)
	uid := wm.Int(d["user_id"])
	if uid <= 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "bad developer key", "code": "bad_key"}}
	}
	nowMs := time.Now().UnixMilli()
	res, err := wm.QueryMany([]map[string]any{
		{"table": "app_manifests", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 50, "with_total": false},
		{"table": "assist_app_keys", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 50, "with_total": false},
		{"table": "user_pkgs", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 50, "with_total": false},
	})
	if err != nil {
		return wm.Fail(err)
	}
	names := map[string]string{}
	for _, r := range rows(res, 2) {
		names[wm.Str(r["pkg_id"])] = wm.Str(r["name"])
	}
	keys := map[string]map[string]any{}
	for _, r := range rows(res, 1) {
		keys[wm.Str(r["pkg_id"])] = r
	}
	pkgs := []string{}
	for _, m := range rows(res, 0) {
		if provides(m["provides"], "assistant") && names[wm.Str(m["pkg_id"])] != "" {
			pkgs = append(pkgs, wm.Str(m["pkg_id"]))
		}
	}
	sort.Strings(pkgs)
	pkgQ := []any{}
	for _, p := range pkgs {
		pkgQ = append(pkgQ, p)
	}
	calls := []map[string]any{}
	if len(pkgQ) > 0 {
		c, cerr := wm.QueryOpts("ai_calls", map[string]any{"filters": []map[string]any{{"column": "pkg_id", "op": "in", "value": pkgQ},
			{"column": "at_ms", "op": "gte", "value": nowMs - 24*3600*1000}}, "order_by": "at_ms", "desc": true, "limit": 500})
		if cerr != nil {
			return wm.Fail(cerr)
		}
		calls = c
	}
	out := []any{}
	for _, p := range pkgs {
		k := keys[p]
		o := map[string]any{"pkg": p, "name": names[p], "set_up": k != nil}
		if k != nil {
			sec := wm.Str(k["secret"])
			last4 := ""
			if len(sec) >= 4 {
				last4 = sec[len(sec)-4:]
			}
			o["endpoint"] = wm.Str(k["endpoint"])
			o["secret_last4"] = last4
			o["rotating_until_ms"] = int64(wm.Num(k["old_until_ms"]))
			o["state"] = wm.Str(k["state"])
			o["last_status"] = wm.Str(k["last_status"])
			o["last_ok_ms"] = int64(wm.Num(k["last_ok_ms"]))
			o["resting_until_ms"] = int64(wm.Num(k["rest_until_ms"]))
		}
		n := 0
		failed := map[string]any{}
		ms := []int{}
		for _, c := range calls {
			if wm.Str(c["pkg_id"]) != p {
				continue
			}
			n++
			if wm.Bool(c["ok"]) {
				ms = append(ms, wm.Int(c["ms"]))
			} else {
				failed[wm.Str(c["error"])] = wm.Int(failed[wm.Str(c["error"])]) + 1
			}
		}
		sort.Ints(ms)
		med := 0
		if len(ms) > 0 {
			med = ms[len(ms)/2]
		}
		o["calls_24h"] = map[string]any{"n": n, "failed": failed, "median_ms": med}
		out = append(out, o)
	}
	return map[string]any{"status": 200, "body": map[string]any{"apps": out}}
}

func rows(res []map[string]any, i int) []map[string]any {
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

// provides: the manifest's "provides" names the slot (a list of names, or of {slot: …} objects).
func provides(v any, slot string) bool {
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
