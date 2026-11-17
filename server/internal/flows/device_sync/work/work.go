// Workflow device_sync, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"fmt"
	"inkdesk/internal/wm"
	"strconv"
	"strings"
	"time"
)

// Keys the device may read and change, with their defaults (docs/inkdesk-api.md).
var deviceDefaults = map[string]string{
	"sys.lang": "vi", "sys.sleep_min": "10",
	"read.font": "LexicaUltralegible", "read.size": "medium", "read.spacing": "1.4",
	"dash.enabled": "1", "dash.layout": "desk", "dash.interval_min": "30", "dash.clock": "1",
	"dash.charging_min": "1", "dash.save_pct": "20", "dash.quiet_from": "23:00", "dash.quiet_to": "06:00",
	"sleep.mode": "dashboard", "sleep.rotate": "sleep", "sleep.order": "seq", "sleep.clock": "1",
	"read.align": "justify", "read.margin": "narrow", "read.indent": "2", "read.para": "1", "read.hyphen": "0",
	"read.aa": "1", "read.refresh": "15", "read.status": "page,percent,battery,chapter", "read.images": "show",
	"sys.pwr_short": "ignore", "sys.side": "prev_next", "sys.front": "bclr", "sys.orient": "portrait", "sys.clock_fmt": "24",
	"read.goal_min": "30",
	"home.w":        "", "sleep.wl": "", "sleep.wp": "",
	// ES3C35P colour desk (kind s3): its widget pages and device settings.
	"s3.p1": "", "s3.p2": "", "s3.l1": "", "s3.l2": "",
	"s3.bright": "70", "s3.volume": "45", "s3.rot": "0", "s3.saver": "120", "s3.click": "1", "s3.dnd": "0",
	"s3.saver_mode": "desk", // clock | desk (StandBy-like, the default since 2026-11-16) | photo
	// The colour desk's app OS (2026-11-16): "" = the device's own default.
	"s3.home": "", "s3.mywidgets": "", "s3.notes": "", "s3.alarms": "", "s3.todo": "", "s3.apps": "",
	"s3.theme": "", "s3.theme_auto": "", "s3.wall": "", "s3.cities": "",
	"s3.photos": "", // the device's own pictures (saver, photo app): photo ids "12,15,9"
	// App store (2026-11-16, s3desk/docs/ecosystem.md §6): the packages the user wants, notification switches.
	"s3.pkgs": "", "s3.notif": "",
	"s3.notif_preview": "1", // 0: banners show only the app's name, not the text
	"s3.saver_pages":   "",  // the custom saver (s3desk/docs/saver.md); "" = the device's defaults
	// The Markets app's codes ("gold:SJC,stocks:FPT,fx:USD,crypto:BTC", at most 20); "" = the built-in mix.
	"s3.markets": "",
	// Lite packages on the X4 (2026-11-17, s3desk/docs/profiles.md §9.1): what the user wants installed, a JSON
	// list [{"id", "code"}, {"id", "rm": true}]. Sent to and taken from an X4 only.
	"x4.pkgs": "",
}

// Settings the web owns that a colour desk may change too (2026-11-16, settings parity), with the web's
// defaults: the same checks as save_device_settings (change both). The city comes with its coordinates.
var s3Shared = map[string]string{"dash.city": "Hà Nội", "dash.lat": "21.0245", "dash.lon": "105.8412", "dash.ics_url": "", "sys.tz_min": "420"}

// Settings kept outside device_settings that a colour desk may change: news_prefs (the user's) and devices.
var s3Elsewhere = map[string]bool{"news.sections": true, "news.per_section": true, "dev.name": true, "dev.fw_auto": true}

// The web's time zones (places.ts TIME_ZONES; save_device_settings allowed["sys.tz_min"]).
var tzMins = map[string]bool{"-600": true, "-480": true, "-420": true, "-360": true, "-300": true, "-240": true, "-180": true, "0": true,
	"60": true, "120": true, "180": true, "240": true, "330": true, "420": true, "480": true, "540": true, "600": true, "720": true}

// The VnExpress sections (save_news): the order the web lists them in.
var newsSlugs = []string{"tin-moi-nhat", "tin-noi-bat", "thoi-su", "the-gioi", "kinh-doanh", "bat-dong-san", "khoa-hoc-cong-nghe", "suc-khoe",
	"giao-duc", "phap-luat", "gia-dinh", "du-lich", "giai-tri", "the-thao", "oto-xe-may", "y-kien"}

const defaultNewsSections = "tin-moi-nhat,thoi-su,the-gioi,kinh-doanh,khoa-hoc-cong-nghe,suc-khoe"

// coordOK: a latitude or longitude in range, as the web stores it (4 decimals).
func coordOK(v string, max float64) (string, bool) {
	f, err := strconv.ParseFloat(strings.TrimSpace(v), 64)
	if err != nil || f < -max || f > max {
		return "", false
	}
	return strconv.FormatFloat(f, 'f', 4, 64), true
}

// checkICS: calendar links, one per line: at most 5 shown and 10 in all ("#" before a link hides it), each
// https:// or webcal:// and at most 600 bytes, 4000 bytes in all.
func checkICS(v string) bool {
	links := strings.Fields(v)
	if len(links) > 10 || len(v) > 4000 {
		return false
	}
	shown := 0
	for _, u := range links {
		if !strings.HasPrefix(u, "#") {
			shown++
		}
		u = strings.TrimPrefix(u, "#")
		if !(strings.HasPrefix(u, "https://") || strings.HasPrefix(u, "webcal://")) || len(u) > 600 {
			return false
		}
	}
	return shown <= 5
}

// checkMarkets: "" or up to 20 "kind:CODE" tokens (gold, stocks, fx, crypto), as a markets widget's option.
func checkMarkets(v string) bool {
	if v == "" {
		return true
	}
	toks := strings.Split(v, ",")
	if len(toks) > 20 || len(v) > 400 {
		return false
	}
	for _, t := range toks {
		k, c, ok := strings.Cut(strings.TrimSpace(t), ":")
		if !ok || (k != "gold" && k != "stocks" && k != "fx" && k != "crypto") || c == "" || len(c) > 12 || strings.Trim(c, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != "" {
			return false
		}
	}
	return true
}

// cleanSections: the known slugs of a comma list, in the web's order; "" when none.
func cleanSections(v string) string {
	want := map[string]bool{}
	for _, s := range strings.Split(v, ",") {
		want[strings.TrimSpace(s)] = true
	}
	out := []string{}
	for _, s := range newsSlugs {
		if want[s] {
			out = append(out, s)
		}
	}
	return strings.Join(out, ",")
}

// s3OSJSON: the colour desk's JSON settings and their size limits in bytes.
var s3OSJSON = map[string]int{"s3.home": 6000, "s3.mywidgets": 8000, "s3.notes": 16000, "s3.alarms": 2000, "s3.todo": 4000, "s3.apps": 4000}

// s3OSCheck: handled is true for the colour desk's OS keys, good when v may be stored as it is
// (valid JSON within its size, or a known value; "" always). A bad value is skipped, never cut.
func s3OSCheck(k, v string) (handled bool, good bool) {
	if max, ok := s3OSJSON[k]; ok {
		return true, v == "" || (len(v) <= max && json.Valid([]byte(v)))
	}
	switch k {
	case "s3.pkgs":
		return true, checkPkgsSetting(v) == ""
	case "s3.notif":
		return true, checkNotifSetting(v) == ""
	case "s3.notif_preview":
		return true, v == "" || v == "0" || v == "1"
	case "s3.saver_pages":
		return true, checkSaverPages(v) == ""
	case "s3.theme":
		return true, v == "" || v == "color" || v == "light" || v == "dark" || v == "photo"
	case "s3.theme_auto":
		return true, v == "" || v == "0" || v == "1"
	case "s3.wall":
		return true, len([]rune(v)) <= 20 && !strings.ContainsAny(v, "\r\n\t")
	case "s3.cities":
		return true, len([]rune(v)) <= 200 && len(v) <= 800
	case "s3.photos":
		return true, photoIDs(v)
	case "s3.markets":
		return true, checkMarkets(v)
	}
	return false, false
}

// photoIDs: "" or up to 12 photo ids, digits separated by commas ("12,15,9"), at most 120 characters.
func photoIDs(v string) bool {
	if v == "" {
		return true
	}
	parts := strings.Split(v, ",")
	if len(v) > 120 || len(parts) > 12 {
		return false
	}
	for _, p := range parts {
		if p == "" || len(p) > 9 || p[0] == '0' || strings.Trim(p, "0123456789") != "" {
			return false
		}
	}
	return true
}

// ---- app store: package ids, s3.pkgs, s3.notif and the device's packages report (docs/ecosystem.md §6) ----
// The same checks are in save_device_settings: change both.

// pkgID: "vn.pawboop.run", "news": a lowercase letter or digit, then letters, digits, ". _ -"; at most 64.
func pkgID(s string) bool {
	if len(s) == 0 || len(s) > 64 {
		return false
	}
	for i := 0; i < len(s); i++ {
		c := s[i]
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '.' || c == '_' || c == '-'))) {
			return false
		}
	}
	return true
}

// partName: "core", "roads10": 1 to 24 of a-z 0-9 _ -.
func partName(s string) bool {
	if len(s) == 0 || len(s) > 24 {
		return false
	}
	for i := 0; i < len(s); i++ {
		c := s[i]
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
			return false
		}
	}
	return true
}

// whole: a JSON number that is a whole number in [0, max].
func whole(v any, max float64) (int64, bool) {
	var f float64
	switch n := v.(type) {
	case float64:
		f = n
	case int:
		f = float64(n)
	case int64:
		f = float64(n)
	case json.Number:
		x, err := n.Float64()
		if err != nil {
			return 0, false
		}
		f = x
	default:
		return 0, false
	}
	if f < 0 || f > max || f != float64(int64(f)) {
		return 0, false
	}
	return int64(f), true
}

// plain: a string of at most n characters without control characters.
func plain(v any, n int) (string, bool) {
	s, ok := v.(string)
	if !ok || len([]rune(s)) > n {
		return "", false
	}
	for _, r := range s {
		if r < 0x20 || r == 0x7f {
			return "", false
		}
	}
	return s, true
}

// partList: an optional list of at most 8 part names.
func partList(v any) ([]any, bool) {
	arr, ok := v.([]any)
	if !ok || len(arr) > 8 {
		return nil, false
	}
	out := []any{}
	for _, x := range arr {
		s, ok := x.(string)
		if !ok || !partName(s) {
			return nil, false
		}
		out = append(out, s)
	}
	return out, true
}

// checkPkgsSetting: s3.pkgs is "" or a JSON list of at most 64 packages, 6000 bytes:
// [{"id", "parts": [...], "vol": "flash"|"sd", "rm": bool, "code": int}]; id required and unique,
// other keys optional; unknown keys are kept as they are. "" when good, else the reason.
func checkPkgsSetting(v string) string {
	if v == "" {
		return ""
	}
	if len(v) > 6000 {
		return "quá dài (tối đa 6000 byte)"
	}
	var list []any
	if json.Unmarshal([]byte(v), &list) != nil || list == nil {
		return "cần một danh sách JSON"
	}
	if len(list) > 64 {
		return "tối đa 64 gói"
	}
	seen := map[string]bool{}
	for _, it := range list {
		m, ok := it.(map[string]any)
		if !ok {
			return "mỗi gói cần là một đối tượng JSON"
		}
		id, _ := m["id"].(string)
		if !pkgID(id) {
			return "mã gói không hợp lệ"
		}
		if seen[id] {
			return "gói " + id + " có hai lần"
		}
		seen[id] = true
		if p, has := m["parts"]; has {
			if _, ok := partList(p); !ok {
				return id + ": danh sách phần không hợp lệ"
			}
		}
		if vol, has := m["vol"]; has && vol != "flash" && vol != "sd" {
			return id + ": nơi cài phải là flash hoặc sd"
		}
		if rm, has := m["rm"]; has {
			if _, ok := rm.(bool); !ok {
				return id + ": rm phải là true hoặc false"
			}
		}
		if c, has := m["code"]; has {
			if _, ok := whole(c, 2e9); !ok {
				return id + ": code phải là số nguyên"
			}
		}
	}
	return ""
}

// ---- Lite packages on the X4 (s3desk/docs/profiles.md §9): x4.pkgs, faces in dash.layout, capabilities ----
// The same checks are in save_device_settings: change both.

// checkX4Pkgs: x4.pkgs is "" or a JSON list of at most 24 packages, 2,000 bytes: [{"id", "code"?, "rm"?}], ids
// unique, no other keys (the X4 keeps every setting in RAM). "" when good, else the reason.
func checkX4Pkgs(v string) string {
	if v == "" {
		return ""
	}
	if len(v) > 2000 {
		return "quá dài (tối đa 2000 byte)"
	}
	var list []any
	if json.Unmarshal([]byte(v), &list) != nil || list == nil {
		return "cần một danh sách JSON"
	}
	if len(list) > 24 {
		return "tối đa 24 gói"
	}
	seen := map[string]bool{}
	for _, it := range list {
		m, ok := it.(map[string]any)
		if !ok {
			return "mỗi gói cần là một đối tượng JSON"
		}
		id, _ := m["id"].(string)
		if !pkgID(id) || seen[id] {
			return "mã gói không hợp lệ hoặc có hai lần"
		}
		seen[id] = true
		for k, x := range m {
			switch k {
			case "id":
			case "code":
				if _, ok := whole(x, 2e9); !ok {
					return id + ": code phải là số nguyên"
				}
			case "rm":
				if _, ok := x.(bool); !ok {
					return id + ": rm phải là true hoặc false"
				}
			default:
				return id + ": chỉ có id, code, rm"
			}
		}
	}
	return ""
}

// faceLayout: a dash.layout that names a face package, "pkg:<id>" or "pkg:<id>@<w>x<h>" (three digits each).
func faceLayout(v string) bool {
	if !strings.HasPrefix(v, "pkg:") {
		return false
	}
	id, size, sized := strings.Cut(v[4:], "@")
	if !pkgID(id) {
		return false
	}
	if !sized {
		return true
	}
	w, h, ok := strings.Cut(size, "x")
	return ok && len(w) == 3 && len(h) == 3 && strings.Trim(w, "0123456789") == "" && strings.Trim(h, "0123456789") == ""
}

// capName: a capability flag ("screen.eink") or a runtime name ("decl", "engine.pet"): lowercase words with dots.
func capName(s string) bool {
	if len(s) == 0 || len(s) > 24 || s[0] < 'a' || s[0] > 'z' || s[len(s)-1] == '.' {
		return false
	}
	for i := 0; i < len(s); i++ {
		c := s[i]
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '.' && s[i-1] != '.')) {
			return false
		}
	}
	return true
}

// capsReport: what the device can run, from the sync body ("caps", "rt", "screen"; profiles.md §9.3), as stored in
// devices.caps: {"caps": [...], "rt": {"decl": 1}, "screen": "800x480"}. nil when the body has none or a bad one.
func capsReport(body map[string]any) map[string]any {
	cv, hasCaps := body["caps"]
	rv, hasRt := body["rt"]
	sv, hasScreen := body["screen"]
	if !hasCaps && !hasRt && !hasScreen {
		return nil
	}
	caps := []any{}
	if hasCaps {
		arr, ok := cv.([]any)
		if !ok || len(arr) > 32 {
			return nil
		}
		for _, x := range arr {
			c, _ := x.(string)
			if !capName(c) {
				return nil
			}
			caps = append(caps, c)
		}
	}
	rt := map[string]any{}
	if hasRt {
		obj, ok := rv.(map[string]any)
		if !ok || len(obj) > 16 {
			return nil
		}
		for k, x := range obj {
			n, ok := whole(x, 1000)
			if !capName(k) || !ok {
				return nil
			}
			rt[k] = n
		}
	}
	out := map[string]any{"caps": caps, "rt": rt, "screen": ""}
	if hasScreen {
		sc, _ := sv.(string)
		w, h, ok := strings.Cut(sc, "x")
		if !ok || len(w) < 2 || len(w) > 4 || len(h) < 2 || len(h) > 4 || strings.Trim(w, "0123456789") != "" || strings.Trim(h, "0123456789") != "" {
			return nil
		}
		out["screen"] = sc
	}
	if r, has := body["rotate"]; has {
		if b, ok := r.(bool); ok {
			out["rotate"] = b
		}
	}
	return out
}

// checkNotifSetting: s3.notif is "" or a JSON object of at most 64 app ids -> true/false, 2000 bytes.
func checkNotifSetting(v string) string {
	if v == "" {
		return ""
	}
	if len(v) > 2000 {
		return "quá dài (tối đa 2000 byte)"
	}
	var m map[string]any
	if json.Unmarshal([]byte(v), &m) != nil || m == nil {
		return "cần một đối tượng JSON"
	}
	if len(m) > 64 {
		return "tối đa 64 ứng dụng"
	}
	for k, x := range m {
		if !pkgID(k) {
			return "mã ứng dụng không hợp lệ"
		}
		if _, ok := x.(bool); !ok {
			return k + ": cần true hoặc false"
		}
	}
	return ""
}

var jobStates = map[string]bool{"download": true, "write": true, "move": true, "remove": true, "done": true, "error": true}

// kbPair reads a volume's {"total": KB, "free": KB} (free <= total).
func kbPair(m map[string]any) (int64, int64, bool) {
	t, ok1 := whole(m["total"], 1<<31)
	f, ok2 := whole(m["free"], 1<<31)
	return t, f, ok1 && ok2 && f <= t
}

// pkgsReport checks what the device has (sync body "pkgs") and returns a clean copy with only the known
// fields, or why it is refused (then the last report stays). An installed entry whose name (over 40 characters),
// type or src is not one the web can show is left out on its own, its id in `dropped`, and the rest is kept (a
// package copied onto a card by hand may say anything; the device never cuts names):
// {"vols": {"flash": {"total", "free"}, "sd": {"present", "total", "free"}},
//
//	"installed": [{"id", "name"?, "addon_of"?, "type"?, "src"?, "version", "code", "vol", "parts", "kb", "ok"}],
//	"job": {"id", "state", "pct", "err"} | null}
//
// lite: an X4 (profiles.md §9.3): packages live only on the card, so "vols.flash" may be missing (and is then left
// out of the copy), and an "sd" volume may say only "present".
func pkgsReport(v any, lite bool) (map[string]any, string, []string) {
	src, ok := v.(map[string]any)
	if !ok {
		return nil, "pkgs: not an object", nil
	}
	if raw, err := json.Marshal(src); err != nil || len(raw) > 8192 {
		return nil, "pkgs: larger than 8 KB", nil
	}
	vols, _ := src["vols"].(map[string]any)
	fl, _ := vols["flash"].(map[string]any)
	if fl == nil && !lite {
		return nil, "pkgs.vols.flash: missing", nil
	}
	var flOut map[string]any
	if fl != nil {
		ft, ff, good := kbPair(fl)
		if !good {
			return nil, "pkgs.vols.flash: total/free must be whole KB, free <= total", nil
		}
		flOut = map[string]any{"total": ft, "free": ff}
	}
	sdOut := map[string]any{"present": false}
	if sv, has := vols["sd"]; has && sv != nil {
		sd, ok := sv.(map[string]any)
		if !ok {
			return nil, "pkgs.vols.sd: not an object", nil
		}
		if p, has := sd["present"]; has {
			if _, ok := p.(bool); !ok {
				return nil, "pkgs.vols.sd.present: not a bool", nil
			}
		}
		_, sdTotal := sd["total"]
		if sd["present"] == true && lite && !sdTotal {
			sdOut = map[string]any{"present": true}
		} else if sd["present"] == true {
			st, sf, good := kbPair(sd)
			if !good {
				return nil, "pkgs.vols.sd: total/free must be whole KB, free <= total", nil
			}
			sdOut = map[string]any{"present": true, "total": st, "free": sf}
		}
	}
	installed := []any{}
	dropped := []string{}
	if iv, has := src["installed"]; has && iv != nil {
		arr, ok := iv.([]any)
		if !ok || len(arr) > 64 {
			return nil, "pkgs.installed: a list of at most 64", nil
		}
		seen := map[string]bool{}
		for _, it := range arr {
			m, ok := it.(map[string]any)
			if !ok {
				return nil, "pkgs.installed: entries must be objects", nil
			}
			id, _ := m["id"].(string)
			if !pkgID(id) || seen[id] {
				return nil, "pkgs.installed: bad or repeated id", nil
			}
			seen[id] = true
			e := map[string]any{"id": id, "ok": m["ok"] != false}
			if vol := m["vol"]; vol == "flash" || vol == "sd" {
				e["vol"] = vol
			} else {
				return nil, "pkgs.installed[" + id + "].vol: flash or sd", nil
			}
			ver, okv := plain(m["version"], 20)
			if m["version"] != nil && !okv {
				return nil, "pkgs.installed[" + id + "].version: text of at most 20", nil
			}
			e["version"] = ver
			code, okc := whole(m["code"], 2e9)
			if m["code"] != nil && !okc {
				return nil, "pkgs.installed[" + id + "].code: a whole number", nil
			}
			e["code"] = code
			kb, okk := whole(m["kb"], 1<<31)
			if m["kb"] != nil && !okk {
				return nil, "pkgs.installed[" + id + "].kb: whole KB", nil
			}
			e["kb"] = kb
			parts := []any{}
			if m["parts"] != nil {
				p, okp := partList(m["parts"])
				if !okp {
					return nil, "pkgs.installed[" + id + "].parts: at most 8 part names", nil
				}
				parts = p
			}
			e["parts"] = parts
			if m["ok"] != nil {
				if _, okb := m["ok"].(bool); !okb {
					return nil, "pkgs.installed[" + id + "].ok: not a bool", nil
				}
			}
			if m["name"] != nil {
				name, okn := plain(m["name"], 40)
				if !okn {
					dropped = append(dropped, id)
					continue
				}
				e["name"] = name
			}
			if m["addon_of"] != nil {
				// an add-on (e.g. PawBoop's "Chạy lấy xu") names the app it belongs to
				a, _ := m["addon_of"].(string)
				if !pkgID(a) || a == id {
					return nil, "pkgs.installed[" + id + "].addon_of: another package's id", nil
				}
				e["addon_of"] = a
			}
			if m["type"] != nil {
				// what the package is (face, widget, theme; app, pack): the web lists them by type
				t, _ := m["type"].(string)
				if len(t) < 2 || len(t) > 12 || strings.Trim(t, "abcdefghijklmnopqrstuvwxyz") != "" {
					dropped = append(dropped, id)
					continue
				}
				e["type"] = t
			}
			if m["src"] != nil {
				if m["src"] != "card" {
					dropped = append(dropped, id)
					continue
				}
				e["src"] = "card" // copied onto the card by hand (sideloaded), not from the store
			}
			installed = append(installed, e)
		}
	}
	var job any
	if jv, has := src["job"]; has && jv != nil {
		m, ok := jv.(map[string]any)
		if !ok {
			return nil, "pkgs.job: not an object", nil
		}
		id, _ := m["id"].(string)
		state, _ := m["state"].(string)
		if !pkgID(id) || !jobStates[state] {
			return nil, "pkgs.job: bad id or state", nil
		}
		pct, okp := whole(m["pct"], 100)
		if m["pct"] != nil && !okp {
			return nil, "pkgs.job.pct: 0 to 100", nil
		}
		errText, oke := plain(m["err"], 80)
		if m["err"] != nil && !oke {
			return nil, "pkgs.job.err: text of at most 80", nil
		}
		job = map[string]any{"id": id, "state": state, "pct": pct, "err": errText}
	}
	volsOut := map[string]any{"sd": sdOut}
	if flOut != nil {
		volsOut["flash"] = flOut
	}
	return map[string]any{"vols": volsOut, "installed": installed, "job": job}, "", dropped
}

// ---- custom saver: s3.saver_pages (s3desk/docs/saver.md §2) ----
// The same check is in device_sync and save_device_settings: change both. Light: the shape and the ranges the
// device relies on; unknown keys are kept (a newer editor's). "" when good, else the reason (Vietnamese).
var saverBg = map[string]bool{"black": true, "color": true, "grad": true, "wall": true, "photo": true}
var saverEl = map[string]bool{"time": true, "date": true, "lunar": true, "text": true, "next": true, "alarm": true,
	"temp": true, "box": true, "photo": true, "w": true}

func hex6(v any) bool {
	s, ok := v.(string)
	if !ok || len(s) != 6 {
		return false
	}
	for i := 0; i < 6; i++ {
		c := s[i]
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
			return false
		}
	}
	return true
}

// inRange: absent, or a whole number in [lo, hi].
func inRange(m map[string]any, k string, lo, hi float64) bool {
	v, has := m[k]
	if !has {
		return true
	}
	n, ok := v.(float64)
	return ok && n >= lo && n <= hi && n == float64(int64(n))
}

// wallID: a built-in wallpaper's id in a background ("w": "tim", "bien"...), or none.
func wallID(v any) bool {
	if v == nil {
		return true
	}
	s, ok := v.(string)
	if !ok || len(s) > 20 {
		return false
	}
	return strings.Trim(s, "abcdefghijklmnopqrstuvwxyz0123456789-") == ""
}

func checkSaverPage(p any, where string) string {
	m, ok := p.(map[string]any)
	if !ok {
		return where + " không hợp lệ"
	}
	bg, ok := m["bg"].(map[string]any)
	if !ok || !saverBg[wm.Str(bg["t"])] {
		return where + ": nền không hợp lệ"
	}
	for _, k := range []string{"c", "c2"} {
		if v, has := bg[k]; has && !hex6(v) {
			return where + ": màu nền không hợp lệ"
		}
	}
	if !inRange(bg, "dim", 0, 90) || !inRange(bg, "id", 0, 2e9) || !wallID(bg["w"]) {
		return where + ": nền không hợp lệ"
	}
	els, has := m["e"]
	if !has {
		return ""
	}
	list, ok := els.([]any)
	if !ok {
		return where + ": danh sách phần tử không hợp lệ"
	}
	if len(list) > 24 {
		return where + ": tối đa 24 phần tử"
	}
	for _, it := range list {
		e, ok := it.(map[string]any)
		t := wm.Str(e["t"])
		if !ok || !saverEl[t] {
			return where + ": có phần tử không hợp lệ"
		}
		if !inRange(e, "x", -480, 480) || !inRange(e, "y", -480, 480) || !inRange(e, "w", 1, 480) || !inRange(e, "h", 1, 480) {
			return where + ": vị trí hoặc cỡ của phần tử " + t + " ngoài màn"
		}
		if v, has := e["c"]; has && !hex6(v) {
			return where + ": màu của phần tử " + t + " không hợp lệ"
		}
		if !inRange(e, "o", 10, 100) || !inRange(e, "b", 0, 1) || !inRange(e, "px", 0, 96) || !inRange(e, "r", 0, 240) || !inRange(e, "id", 0, 2e9) {
			return where + ": tuỳ chọn của phần tử " + t + " không hợp lệ"
		}
		if a, has := e["a"]; has && a != "l" && a != "c" && a != "r" {
			return where + ": canh lề phải là l, c hoặc r"
		}
		if t == "text" {
			if s, has := e["s"]; has {
				str, ok := s.(string)
				if !ok || len([]rune(str)) > 80 {
					return where + ": chữ tự soạn tối đa 80 ký tự"
				}
			}
		}
		if t == "w" {
			if !pkgID(wm.Str(e["k"])) {
				return where + ": widget không hợp lệ"
			}
			if s := wm.Str(e["s"]); s != "S" && s != "M" && s != "L" {
				return where + ": cỡ widget phải là S, M hoặc L"
			}
		}
		for _, k := range []string{"f", "st", "op"} {
			if v, has := e[k]; has {
				str, ok := v.(string)
				if !ok || len(str) > 160 {
					return where + ": tuỳ chọn của phần tử " + t + " không hợp lệ"
				}
			}
		}
	}
	return ""
}

func checkSaverPages(v string) string {
	if v == "" {
		return ""
	}
	if len(v) > 8000 {
		return "quá dài (tối đa 8000 byte)"
	}
	var d map[string]any
	if json.Unmarshal([]byte(v), &d) != nil || d == nil {
		return "không phải JSON"
	}
	if d["v"] != float64(1) {
		return "cần \"v\": 1"
	}
	if c, has := d["cycle"]; has && c != float64(0) && c != float64(60) && c != float64(300) && c != float64(900) {
		return "đổi trang mỗi 0, 60, 300 hoặc 900 giây"
	}
	if !inRange(d, "light", 5, 100) {
		return "độ sáng từ 5 đến 100"
	}
	if n, has := d["night"]; has && n != "page" && n != "off" && n != "day" {
		return "ban đêm phải là page, off hoặc day"
	}
	for _, k := range []string{"p", "l"} {
		pv, has := d[k]
		if !has {
			continue
		}
		pages, ok := pv.([]any)
		if !ok || len(pages) < 1 || len(pages) > 3 {
			return "mỗi chiều có từ 1 đến 3 trang"
		}
		for i, pg := range pages {
			where := "trang dọc " + strconv.Itoa(i+1)
			if k == "l" {
				where = "trang ngang " + strconv.Itoa(i+1)
			}
			if why := checkSaverPage(pg, where); why != "" {
				return why
			}
		}
	}
	for _, k := range []string{"np", "nl"} {
		if pg, has := d[k]; has {
			if why := checkSaverPage(pg, "trang đêm"); why != "" {
				return why
			}
		}
	}
	return ""
}

func isLayoutKey(k string) bool {
	switch k {
	case "home.w", "sleep.wl", "sleep.wp", "s3.p1", "s3.p2", "s3.l1", "s3.l2":
		return true
	}
	return false
}

// kindOf is the device's board: stored on its row, else told by its firmware name.
func kindOf(row map[string]any, fw string) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(fw, "inkdesk-s3-") {
		return "s3"
	}
	return "x4"
}

func list(v any) []any {
	a, _ := v.([]any)
	if len(a) > 50 {
		return a[:50]
	}
	return a
}

// "inkdesk-1.2.3" -> 10203 (0 when unreadable), as the device computes it.
func versionCode(v string) int {
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

func cut(s string, n int) string {
	r := []rune(strings.TrimSpace(s))
	if len(r) <= n {
		return string(r)
	}
	return string(r[:n])
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	body, _ := in["body"].(map[string]any)
	dev, _ := in["device"].(map[string]any)
	if body == nil {
		body = map[string]any{}
	}
	deviceID := wm.Int(dev["device_id"])
	userID := wm.Int(dev["user_id"])
	if deviceID == 0 || userID == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	now := time.Now()
	nowMs := now.UnixMilli()

	// ---- settings: device changes win only if they are newer ----
	rows, err := wm.QueryOpts("device_settings", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 200,
	})
	if err != nil {
		return wm.Fail(err)
	}
	type cell struct {
		v  string
		ms int64
	}
	cur := map[string]cell{}
	webSet := map[string]bool{} // keys last changed on the web: they win on a device that never had them
	for _, r := range rows {
		cur[wm.Str(r["skey"])] = cell{wm.Str(r["svalue"]), int64(wm.Num(r["updated_ms"]))}
		webSet[wm.Str(r["skey"])] = wm.Str(r["source"]) == "web"
	}
	// The board, for the colour desk's own keys (read again with the firmware state below).
	kindRows, krerr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1})
	if krerr != nil {
		return wm.Fail(krerr)
	}
	boardKind := kindOf(map[string]any{}, wm.Str(body["fw"]))
	if len(kindRows) == 1 {
		boardKind = kindOf(kindRows[0], wm.Str(body["fw"]))
	}
	// A colour desk sends its city with the coordinates it picked: the three are taken together or not at all.
	place := map[string]string{}
	if boardKind == "s3" {
		for _, it := range list(body["settings"]) {
			m, _ := it.(map[string]any)
			if k := wm.Str(m["k"]); k == "dash.city" || k == "dash.lat" || k == "dash.lon" {
				place[k] = strings.TrimSpace(wm.Str(m["v"]))
			}
		}
		city := place["dash.city"]
		lat, latOK := coordOK(place["dash.lat"], 90)
		lon, lonOK := coordOK(place["dash.lon"], 180)
		if n := len([]rune(city)); n < 1 || n > 60 || strings.ContainsAny(city, "\r\n\t") || !latOK || !lonOK {
			place = map[string]string{}
		} else {
			place["dash.lat"] = lat
			place["dash.lon"] = lon
		}
	}
	icsChanged := false
	elsewhere := []map[string]any{}
	for _, it := range list(body["settings"]) {
		m, _ := it.(map[string]any)
		k := wm.Str(m["k"])
		if s3Elsewhere[k] && boardKind == "s3" {
			elsewhere = append(elsewhere, m)
			continue
		}
		_, shared := s3Shared[k]
		if _, allowed := deviceDefaults[k]; !allowed && !(shared && boardKind == "s3") {
			continue
		}
		if h, _ := s3OSCheck(k, ""); h && boardKind != "s3" {
			continue // the app-OS keys belong to the colour desk only
		}
		if strings.HasPrefix(k, "x4.") && boardKind == "s3" {
			continue // the X4's packages list belongs to the X4 only
		}
		v := cut(wm.Str(m["v"]), 60)
		if shared {
			// The web's own keys, with the web's checks; a bad value is skipped and the stored one goes back.
			raw := strings.TrimSpace(wm.Str(m["v"]))
			switch k {
			case "dash.city", "dash.lat", "dash.lon":
				pv, ok := place[k]
				if !ok {
					continue
				}
				v = pv
			case "dash.ics_url":
				if !checkICS(raw) {
					continue
				}
				v = raw
			case "sys.tz_min":
				if !tzMins[raw] {
					continue
				}
				v = raw
			}
		} else if isLayoutKey(k) {
			// Widget layouts are JSON up to 4000 bytes; a cut one would break the screen, so skip it instead.
			v = wm.Str(m["v"])
			var probe map[string]any
			if len(v) > 4000 || (v != "" && json.Unmarshal([]byte(v), &probe) != nil) {
				continue
			}
		} else if handled, good := s3OSCheck(k, wm.Str(m["v"])); handled {
			if !good {
				continue
			}
			v = wm.Str(m["v"])
		} else if k == "x4.pkgs" {
			// Kept whole (never cut): a cut list would lose packages. A bad one is skipped.
			v = strings.TrimSpace(wm.Str(m["v"]))
			if checkX4Pkgs(v) != "" {
				continue
			}
		} else if k == "dash.layout" && strings.HasPrefix(strings.TrimSpace(wm.Str(m["v"])), "pkg:") {
			// A face package as the sleep screen ("pkg:<id>@480x800" can be longer than 60 characters).
			v = strings.TrimSpace(wm.Str(m["v"]))
			if boardKind == "s3" || !faceLayout(v) {
				continue
			}
		}
		age := int64(wm.Num(m["age_s"]))
		if age < 0 {
			age = 0
		}
		ts := nowMs - age*1000
		if old, ok := cur[k]; ok && old.ms > ts {
			continue
		}
		if k == "dash.ics_url" && cur[k].v != v {
			icsChanged = true
		}
		if _, uerr := wm.Upsert("device_settings", []string{"device_id", "skey"}, map[string]any{
			"device_id": deviceID, "user_id": userID, "skey": k, "svalue": v, "updated_ms": ts, "source": "device",
		}, nil); uerr != nil {
			return wm.Fail(uerr)
		}
		cur[k] = cell{v, ts}
		webSet[k] = false
	}
	// New calendar links from the device: today's cached events go, so the next dashboard reads them.
	if icsChanged {
		old, qerr := wm.QueryOpts("ics_cache", map[string]any{"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 5})
		if qerr != nil {
			return wm.Fail(qerr)
		}
		for _, r := range old {
			if derr := wm.Delete("ics_cache", wm.Int(r["id"])); derr != nil {
				return wm.Fail(derr)
			}
		}
	}

	// ---- settings kept elsewhere, from a colour desk: the device's name and fw_auto (devices), the news
	// sections and stories per section (news_prefs, the user's). Newest write wins, against their *_ms. ----
	devName := wm.Str(dev["name"])
	if len(kindRows) == 1 && wm.Str(kindRows[0]["name"]) != "" {
		devName = wm.Str(kindRows[0]["name"])
	}
	var newsRow map[string]any
	if boardKind == "s3" {
		np, nerr := wm.QueryOpts("news_prefs", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}}, "limit": 1})
		if nerr != nil {
			return wm.Fail(nerr)
		}
		if len(np) == 1 {
			newsRow = np[0]
		}
		devUpd := map[string]any{}
		newsUpd := map[string]any{}
		newsMs := int64(0)
		if newsRow != nil {
			newsMs = int64(wm.Num(newsRow["updated_ms"]))
		}
		newsAt := newsMs
		for _, m := range elsewhere {
			k := wm.Str(m["k"])
			v := strings.TrimSpace(wm.Str(m["v"]))
			age := int64(wm.Num(m["age_s"]))
			if age < 0 {
				age = 0
			}
			ts := nowMs - age*1000
			switch k {
			case "dev.name":
				if n := len([]rune(v)); n < 1 || n > 40 || len(kindRows) != 1 || int64(wm.Num(kindRows[0]["name_ms"])) > ts {
					continue
				}
				devUpd["name"] = v
				devUpd["name_ms"] = ts
				devName = v
			case "dev.fw_auto":
				if (v != "0" && v != "1") || len(kindRows) != 1 || int64(wm.Num(kindRows[0]["fw_auto_ms"])) > ts {
					continue
				}
				devUpd["fw_auto"] = v == "1"
				devUpd["fw_auto_ms"] = ts
			case "news.sections":
				c := cleanSections(v) // unknown slugs dropped, as save_news does
				if c == "" || newsMs > ts {
					continue
				}
				newsUpd["sections"] = c
			case "news.per_section":
				if (v != "3" && v != "5" && v != "10") || newsMs > ts {
					continue
				}
				n, _ := strconv.Atoi(v)
				newsUpd["per_section"] = n
			}
			if strings.HasPrefix(k, "news.") && ts > newsAt {
				newsAt = ts
			}
		}
		if len(devUpd) > 0 {
			if _, uerr := wm.Update("devices", deviceID, devUpd); uerr != nil {
				return wm.Fail(uerr)
			}
		}
		if len(newsUpd) > 0 {
			newsUpd["updated_ms"] = newsAt
			if newsRow == nil {
				// The user's first news settings: the web's defaults for the rest (morning paper off).
				newsUpd["enabled"] = false
				newsUpd["hour"] = 6
				newsUpd["images"] = true
				newsUpd["keep"] = 7
				if _, ok := newsUpd["sections"]; !ok {
					newsUpd["sections"] = defaultNewsSections
				}
				if _, ok := newsUpd["per_section"]; !ok {
					newsUpd["per_section"] = 5
				}
			}
			newsUpd["user_id"] = userID
			if _, uerr := wm.Upsert("news_prefs", []string{"user_id"}, newsUpd, nil); uerr != nil {
				return wm.Fail(uerr)
			}
			if newsRow == nil {
				newsRow = map[string]any{}
			}
			for k, v := range newsUpd {
				newsRow[k] = v
			}
		}
	}

	// ---- reading progress ----
	for _, it := range list(body["progress"]) {
		m, _ := it.(map[string]any)
		id := wm.Int(m["book_id"])
		p := wm.Int(m["percent"])
		if id <= 0 || p < 0 || p > 100 {
			continue
		}
		b, qerr := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
			{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": userID}}, "limit": 1})
		if qerr != nil {
			return wm.Fail(qerr)
		}
		if len(b) == 1 {
			if _, uerr := wm.Update("books", id, map[string]any{"progress": p}); uerr != nil {
				return wm.Fail(uerr)
			}
		}
	}

	// ---- reading statistics: absolute per-day, per-book totals from the device ----
	for _, it := range list(body["reading"]) {
		m, _ := it.(map[string]any)
		day := wm.Str(m["d"])
		if len(day) != 10 || day[4] != '-' || day[7] != '-' {
			continue
		}
		key := cut(wm.Str(m["k"]), 160)
		sec, pages := wm.Int(m["s"]), wm.Int(m["p"])
		if sec <= 0 || sec > 86400 || pages < 0 || pages > 20000 {
			continue
		}
		ex, qerr := wm.QueryOpts("reading_days", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": deviceID}, {"column": "day", "op": "eq", "value": day},
			{"column": "book_key", "op": "eq", "value": key}}, "limit": 1})
		if qerr != nil {
			return wm.Fail(qerr)
		}
		title := cut(wm.Str(m["t"]), 120)
		if len(ex) == 1 {
			// Totals only grow: a reader that lost its log must not erase what is here.
			oldSec, oldPages := wm.Int(ex[0]["seconds"]), wm.Int(ex[0]["pages"])
			if sec > oldSec || pages > oldPages {
				if sec < oldSec {
					sec = oldSec
				}
				if pages < oldPages {
					pages = oldPages
				}
				if _, uerr := wm.Update("reading_days", wm.Int(ex[0]["id"]), map[string]any{"seconds": sec, "pages": pages, "title": title}); uerr != nil {
					return wm.Fail(uerr)
				}
			}
			continue
		}
		if _, ierr := wm.Insert("reading_days", map[string]any{
			"user_id": userID, "device_id": deviceID, "day": day, "book_key": key, "book_id": wm.Int(m["id"]),
			"title": title, "seconds": sec, "pages": pages,
		}); ierr != nil {
			return wm.Fail(ierr)
		}
	}

	// ---- device heartbeat ----
	upd := map[string]any{"last_sync": now.UTC().Format(time.RFC3339)}

	// ---- firmware: the device's install reports, and the update it should get ----
	devRows, drerr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1})
	if drerr != nil {
		return wm.Fail(drerr)
	}
	fwReq, fwAuto := 0, false
	kind := kindOf(map[string]any{}, wm.Str(body["fw"]))
	if len(devRows) == 1 {
		fwReq = wm.Int(devRows[0]["fw_req"])
		fwAuto = devRows[0]["fw_auto"] == true
		kind = kindOf(devRows[0], wm.Str(body["fw"]))
		if wm.Str(devRows[0]["kind"]) == "" {
			upd["kind"] = kind
		}
	}
	runningCode := versionCode(wm.Str(body["fw"]))
	reported := false
	for _, it := range list(body["ota"]) {
		m, _ := it.(map[string]any)
		ok := m["ok"] == true
		state := "failed"
		if ok {
			state = "ok"
		}
		at := now.Add(-time.Duration(wm.Int(m["age_s"])) * time.Second).UTC().Format(time.RFC3339)
		upd["ota_state"] = map[string]any{"state": state, "version": cut(wm.Str(m["version"]), 40), "why": cut(wm.Str(m["why"]), 60), "at": at}
		// Success or failure, this request is over: the web shows the result (and offers a retry).
		upd["fw_req"] = 0
		fwReq = 0
		reported = true
	}
	if fwReq > 0 && runningCode >= fwReq {
		// Already running the requested version (its report got lost).
		upd["fw_req"] = 0
		fwReq = 0
		if !reported {
			upd["ota_state"] = map[string]any{"state": "ok", "version": cut(wm.Str(body["fw"]), 40), "at": now.UTC().Format(time.RFC3339)}
		}
	}
	// An update record for a version the device already runs or has passed (an old failure, say, before a
	// newer build went on over USB) is history: cleared, so no page reports or offers it.
	if _, set := upd["ota_state"]; !set && !reported && runningCode > 0 && len(devRows) == 1 {
		st, _ := devRows[0]["ota_state"].(map[string]any)
		if s, isStr := devRows[0]["ota_state"].(string); isStr && s != "" {
			_ = json.Unmarshal([]byte(s), &st)
		}
		if st != nil && wm.Str(st["state"]) != "" {
			if v := versionCode(wm.Str(st["version"])); v > 0 && v <= runningCode {
				upd["ota_state"] = map[string]any{"state": ""}
			}
		}
	}
	var fwUpdate map[string]any
	// The newest release for this board only (an X4 image must never reach an S3, and back).
	fwRows, lferr := wm.QueryOpts("firmware", map[string]any{"order_by": "code", "desc": true, "limit": 50})
	if lferr != nil {
		return wm.Fail(lferr)
	}
	latestFw := []map[string]any{}
	for _, f := range fwRows {
		fk := wm.Str(f["kind"])
		if fk == "" {
			fk = "x4"
		}
		if fk == kind {
			latestFw = append(latestFw, f)
			break
		}
	}
	if len(latestFw) == 1 && runningCode > 0 && wm.Int(latestFw[0]["code"]) > runningCode {
		f := latestFw[0]
		code := wm.Int(f["code"])
		fwUpdate = map[string]any{
			"version": wm.Str(f["version"]), "url": wm.AbsURL(wm.Str(in["host"]), wm.Str(f["url"])), "sha256": wm.Str(f["sha256"]), "size": wm.Int(f["size"]),
			"notes": wm.Str(f["notes"]), "date": wm.Str(f["released"]),
			// Install now: asked on the web, or auto-update while the reader is charging.
			"install": (fwReq > 0 && fwReq >= code) || (fwAuto && body["usb"] == true),
		}
	}
	if b := wm.Int(body["battery"]); b > 0 && b <= 100 {
		upd["battery"] = b
	}
	if fw := cut(wm.Str(body["fw"]), 40); fw != "" {
		upd["fw"] = fw
	}
	// The colour desk's health report (fps, heap, resets…), kept as sent when it is a small object.
	if diag, ok := body["diag"].(map[string]any); ok && kind == "s3" {
		if raw, merr := json.Marshal(diag); merr == nil && len(raw) <= 2048 {
			upd["diag"] = diag
			upd["diag_at"] = nowMs
		}
	}
	// The packages report of a colour desk or an X4: volumes, installed packages, the job running
	// (docs/ecosystem.md §6; the X4's: profiles.md §9.3).
	// A malformed report is ignored (the last one stays) and the answer says why.
	pkgsErr := ""
	if pv, has := body["pkgs"]; has && pv != nil && (kind == "s3" || kind == "x4") {
		if rep, why, dropped := pkgsReport(pv, kind == "x4"); why == "" {
			upd["pkgs"] = rep
			upd["pkgs_at"] = nowMs
			if len(dropped) > 0 {
				pkgsErr = "pkgs.installed: left out " + strings.Join(dropped, ", ") + " (name over 40 characters, or an unknown type or src)"
			}
		} else {
			pkgsErr = why
		}
	}
	// What the device can run (capabilities, runtime levels, screen; profiles.md §9.3), for the web's store.
	if cr := capsReport(body); cr != nil {
		upd["caps"] = cr
	}
	// Reading fonts on the SD card (names only), so the web can show what is installed.
	if fl, ok := body["fonts"].([]any); ok {
		names := []any{}
		for _, f := range fl {
			if n := cut(wm.Str(f), 40); n != "" && len(names) < 40 {
				names = append(names, n)
			}
		}
		upd["fonts"] = names
	}
	// WiFi networks the device has saved, with its last result for each (names only).
	if wf, ok := body["wifi"].(map[string]any); ok {
		saved := []any{}
		for _, it := range list(wf["saved"]) {
			m, _ := it.(map[string]any)
			ssid := cut(wm.Str(m["ssid"]), 32)
			if ssid == "" || len(saved) == 10 {
				continue
			}
			saved = append(saved, map[string]any{"ssid": ssid, "ok": m["ok"] == true, "code": wm.Int(m["code"]), "age_s": wm.Int(m["age_s"])})
		}
		upd["wifi_state"] = saved
		// Acknowledged changes: the password leaves the server here.
		for _, it := range list(wf["acks"]) {
			m, _ := it.(map[string]any)
			id := wm.Int(m["id"])
			ops, qerr := wm.QueryOpts("wifi_ops", map[string]any{"filters": []map[string]any{
				{"column": "id", "op": "eq", "value": id}, {"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1})
			if qerr != nil {
				return wm.Fail(qerr)
			}
			if len(ops) != 1 {
				continue
			}
			status := "done"
			result := ""
			if m["ok"] != true {
				status = "failed"
				result = cut(wm.Str(m["why"]), 80)
			}
			if _, uerr := wm.Update("wifi_ops", id, map[string]any{"status": status, "result": result, "pass": ""}); uerr != nil {
				return wm.Fail(uerr)
			}
		}
	}
	if _, uerr := wm.Update("devices", deviceID, upd); uerr != nil {
		return wm.Fail(uerr)
	}

	// Battery history: voltage tells real drain apart from the percentage settling off the charger.
	if mv := wm.Int(body["battery_mv"]); mv > 2500 && mv < 4500 {
		if _, ierr := wm.Insert("battery_log", map[string]any{
			"device_id": deviceID, "user_id": userID, "mv": mv, "pct": wm.Int(body["battery"]), "usb": body["usb"] == true, "at_ms": nowMs,
		}); ierr != nil {
			return wm.Fail(ierr)
		}
	}

	// ---- answer: merged settings ----
	settings := map[string]any{}
	var rev int64
	for k, def := range deviceDefaults {
		if strings.HasPrefix(k, "s3.") != (kind == "s3") && strings.HasPrefix(k, "s3.") {
			continue // the colour desk's keys go to the colour desk only
		}
		if strings.HasPrefix(k, "x4.") && kind == "s3" {
			continue // and the X4's to the X4 only
		}
		settings[k] = def
		if c, ok := cur[k]; ok {
			settings[k] = c.v
			if c.ms > rev {
				rev = c.ms
			}
		}
	}
	if kind == "s3" {
		// The web's keys the colour desk shows and changes too, and those kept elsewhere.
		for k, def := range s3Shared {
			settings[k] = def
			if c, ok := cur[k]; ok {
				settings[k] = c.v
				if c.ms > rev {
					rev = c.ms
				}
			}
		}
		settings["news.sections"] = defaultNewsSections
		settings["news.per_section"] = "5"
		if newsRow != nil {
			if c := cleanSections(wm.Str(newsRow["sections"])); c != "" {
				settings["news.sections"] = c
			}
			if p := wm.Int(newsRow["per_section"]); p == 3 || p == 5 || p == 10 {
				settings["news.per_section"] = strconv.Itoa(p)
			}
			if ms := int64(wm.Num(newsRow["updated_ms"])); ms > rev {
				rev = ms
			}
		}
		settings["dev.name"] = devName
		settings["dev.fw_auto"] = "0"
		if fwAuto {
			settings["dev.fw_auto"] = "1"
		}
		if len(devRows) == 1 {
			for _, c := range []string{"name_ms", "fw_auto_ms"} {
				if ms := int64(wm.Num(devRows[0][c])); ms > rev {
					rev = ms
				}
			}
		}
	}

	// ---- answer: books ----
	have := map[int]bool{}
	for _, h := range list(body["have_books"]) {
		have[wm.Int(h)] = true
	}
	// Byte size of each book file on the card (firmware 0.3.2 and later). A matching size is the
	// proof of a full copy: only then is the stored original deleted, as Send to Kindle does.
	haveSize := map[int]int{}
	for _, h := range list(body["have_sizes"]) {
		m, _ := h.(map[string]any)
		if id := wm.Int(m["id"]); id > 0 {
			haveSize[id] = wm.Int(m["size"])
		}
	}
	host := wm.PublicBase(wm.Str(in["host"]))
	if host == "" {
		host = wm.PublicBase("")
	}
	brows, berr := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": userID}, {"column": "status", "op": "eq", "value": "ready"}},
		"order_by": "id", "limit": 500})
	if berr != nil {
		return wm.Fail(berr)
	}
	booksOut := []any{}
	for _, r := range brows {
		id := wm.Int(r["id"])
		patch := map[string]any{}
		if (r["on_device"] == true) != have[id] {
			patch["on_device"] = have[id]
		}
		delivered := wm.Str(r["delivered_at"]) != ""
		if have[id] && !delivered && wm.Int(r["size"]) > 0 && haveSize[id] == wm.Int(r["size"]) {
			// A failed delete keeps the book undelivered, so the next sync tries again.
			if serr := wm.StorageDelete(wm.Str(r["file_key"])); serr == nil {
				patch["delivered_at"] = time.Now().UTC().Format(time.RFC3339)
				delivered = true
			}
		}
		if len(patch) > 0 {
			if _, uerr := wm.Update("books", id, patch); uerr != nil {
				return wm.Fail(uerr)
			}
		}
		var u any
		if !have[id] && !delivered {
			u = fmt.Sprintf("%s/v1/device/book/%d", host, id)
		}
		booksOut = append(booksOut, map[string]any{
			"id": id, "title": cut(wm.Str(r["title"]), 80), "file": wm.Str(r["file_name"]), "size": wm.Int(r["size"]), "url": u,
		})
	}

	// ---- answer: sleep-screen pictures (a URL only for the ones the device lacks) ----
	haveWall := map[int]bool{}
	for _, h := range list(body["have_walls"]) {
		haveWall[wm.Int(h)] = true
	}
	wrows, werr := wm.QueryOpts("wallpapers", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": userID}, {"column": "status", "op": "eq", "value": "ready"}},
		"order_by": "pos", "limit": 50})
	if werr != nil {
		return wm.Fail(werr)
	}
	wallsOut := []any{}
	for _, r := range wrows {
		id := wm.Int(r["id"])
		var u any
		if !haveWall[id] {
			u = fmt.Sprintf("%s/v1/device/wallpaper/%d", host, id)
		}
		wallsOut = append(wallsOut, map[string]any{"id": id, "orient": wm.Str(r["orient"]), "url": u})
	}

	// ---- answer: WiFi changes queued on the web ----
	opRows, oerr := wm.QueryOpts("wifi_ops", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": deviceID}, {"column": "status", "op": "eq", "value": "pending"}},
		"order_by": "id", "limit": 10})
	if oerr != nil {
		return wm.Fail(oerr)
	}
	wifiOut := []any{}
	for _, r := range opRows {
		wifiOut = append(wifiOut, map[string]any{"id": wm.Int(r["id"]), "op": wm.Str(r["op"]), "ssid": wm.Str(r["ssid"]), "pass": wm.Str(r["pass"])})
	}

	tzMin := 420
	if c, ok := cur["sys.tz_min"]; ok {
		if v := wm.Int(c.v); v >= -720 && v <= 840 && (v != 0 || c.v == "0") {
			tzMin = v
		}
	}
	webKeys := []any{}
	for k := range deviceDefaults {
		if webSet[k] {
			webKeys = append(webKeys, k)
		}
	}
	out := map[string]any{
		"now_ms": nowMs, "tz_min": tzMin,
		"device":       map[string]any{"id": deviceID, "name": devName, "user": wm.Str(dev["email"])},
		"settings_rev": rev, "settings": settings, "books": booksOut,
		"wallpapers": wallsOut, "wifi_ops": wifiOut, "web_keys": webKeys, "fw_update": fwUpdate,
	}
	if pkgsErr != "" {
		out["pkgs_err"] = pkgsErr
	}
	return map[string]any{"status": 200, "body": out}
}
