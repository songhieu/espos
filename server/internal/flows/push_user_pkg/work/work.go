// Workflow push_user_pkg, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"strconv"
	"strings"
	"time"
)

// One of the user's own packages (user_pkgs, id "u.*") asked for on their S3 devices through s3.pkgs
// (s3desk/docs/ecosystem.md §6), written as the web's settings page writes a setting (source web, now):
//   - a device that has it, or lists it, gets "code" raised to the new one: the device updates it at its next sync
//     (store.cpp reconcile: the entry's code above the installed one and the user_pkgs entry as new);
//   - a device in "devices" ("all", or a list of the user's device ids) that does not have it gets an install entry
//     {id, parts ["core"], vol, code}: on the memory card when the device reported one, else on the device;
//   - every other device is left alone (a person who removed it on the device keeps it removed).
// A pending removal ({"id","rm":true}) of the same id is replaced only on a device asked for by name or "all".
// The list stays within the server's limits (64 entries, 6000 bytes); a device whose list is full is skipped.
// Answers, per device touched: {id, name, action: install | update | wait_fw | full, fw}.

func kindOf(row map[string]any) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(wm.Str(row["fw"]), "espos-s3-") {
		return "s3"
	}
	return "x4"
}

func jsonMap(v any) map[string]any {
	m, _ := v.(map[string]any)
	if s, isStr := v.(string); isStr && s != "" {
		_ = json.Unmarshal([]byte(s), &m)
	}
	return m
}

// fwCode: "espos-s3-0.5.7" -> 507 (0 when unknown).
func fwCode(v string) int {
	if i := strings.LastIndex(v, "-"); i >= 0 {
		v = v[i+1:]
	}
	p := strings.Split(v, ".")
	if len(p) < 2 {
		return 0
	}
	n := 0
	for i := 0; i < 3; i++ {
		x := 0
		if i < len(p) {
			x, _ = strconv.Atoi(p[i])
		}
		n = n*100 + x
	}
	return n
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	pkgID := wm.Str(in["pkg_id"])
	code := wm.Int(in["code"])
	minFw := wm.Int(in["min_fw"])
	if uid <= 0 || !strings.HasPrefix(pkgID, "u.") || code <= 0 {
		return map[string]any{"ok": false, "devices": []any{}, "why": "push_user_pkg: missing input"}
	}
	all := false
	want := map[int]bool{}
	switch d := in["devices"].(type) {
	case string:
		all = d == "all"
	case []any:
		for _, x := range d {
			if n := wm.Int(x); n > 0 {
				want[n] = true
			}
		}
	}
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "revoked", "op": "eq", "value": false}}, "limit": 50})
	if err != nil {
		return wm.Fail(err)
	}
	ms := time.Now().UnixMilli()
	out := []any{}
	for _, d := range devs {
		if kindOf(d) != "s3" {
			continue
		}
		devID := wm.Int(d["id"])
		asked := all || want[devID]
		rep := jsonMap(d["pkgs"])
		var inst map[string]any
		insts, _ := rep["installed"].([]any)
		for _, it := range insts {
			m, _ := it.(map[string]any)
			if wm.Str(m["id"]) == pkgID {
				inst = m
			}
		}
		cur, serr := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": devID}, {"column": "skey", "op": "eq", "value": "s3.pkgs"}}, "limit": 1})
		if serr != nil {
			return wm.Fail(serr)
		}
		var list []any
		if len(cur) == 1 && wm.Str(cur[0]["svalue"]) != "" {
			if json.Unmarshal([]byte(wm.Str(cur[0]["svalue"])), &list) != nil {
				list = nil
			}
		}
		at := -1
		for i, it := range list {
			m, _ := it.(map[string]any)
			if m != nil && wm.Str(m["id"]) == pkgID {
				at = i
			}
		}
		action := ""
		vol := "flash"
		if sd, _ := rep["vols"].(map[string]any); sd != nil {
			if c, _ := sd["sd"].(map[string]any); c != nil && wm.Bool(c["present"]) {
				vol = "sd"
			}
		}
		if inst != nil && wm.Str(inst["vol"]) != "" {
			vol = wm.Str(inst["vol"])
		}
		switch {
		case at >= 0:
			m, _ := list[at].(map[string]any)
			if m["rm"] == true {
				if !asked {
					continue // being removed and not asked for again
				}
				list[at] = map[string]any{"id": pkgID, "parts": []any{"core"}, "vol": vol, "code": code}
				action = "install"
			} else {
				if wm.Int(m["code"]) >= code {
					continue // already asked for this version or a newer one
				}
				m["code"] = code
				action = "update"
				if inst == nil {
					action = "install"
				}
			}
		case inst != nil:
			if wm.Int(inst["code"]) >= code {
				continue
			}
			list = append(list, map[string]any{"id": pkgID, "vol": wm.Str(inst["vol"]), "code": code})
			action = "update"
		case asked:
			list = append(list, map[string]any{"id": pkgID, "parts": []any{"core"}, "vol": vol, "code": code})
			action = "install"
		default:
			continue
		}
		fw := wm.Str(d["fw"])
		row := map[string]any{"id": devID, "name": wm.Str(d["name"]), "action": action, "fw": fw}
		raw, merr := json.Marshal(list)
		if merr != nil {
			return wm.Fail(merr)
		}
		if len(list) > 64 || len(raw) > 6000 {
			row["action"] = "full"
			out = append(out, row)
			continue
		}
		if _, uerr := wm.Upsert("device_settings", []string{"device_id", "skey"}, map[string]any{
			"device_id": devID, "user_id": uid, "skey": "s3.pkgs", "svalue": string(raw), "updated_ms": ms, "source": "web",
		}, nil); uerr != nil {
			return wm.Fail(uerr)
		}
		if c := fwCode(fw); c > 0 && c < minFw {
			row["action"] = "wait_fw" // asked; it installs once the device runs new enough firmware
		}
		out = append(out, row)
	}
	return map[string]any{"ok": true, "devices": out, "why": ""}
}
