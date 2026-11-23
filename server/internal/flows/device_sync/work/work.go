// Workflow device_sync, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"strconv"
	"strings"
	"time"
)

// Keys the device may read and change, with their defaults (docs/device-api.md).
var deviceDefaults = map[string]string{
	"sys.lang": "en", "sys.sleep_min": "10",
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
	"s3.pkg_auto":      "1", // 0: installed apps no longer take a newer store version by themselves (s3desk/docs/ecosystem.md §6)
	"s3.saver_pages":   "",  // the custom saver (s3desk/docs/saver.md); "" = the device's defaults
	// The Markets app's codes ("gold:SJC,stocks:FPT,fx:USD,crypto:BTC", at most 20); "" = the built-in mix.
	"s3.markets": "",
	// Landscape Home pages (2026-11-18, s3desk/docs/widgets.md §4b): {"v":1,"pages":[...],"bg":[...]}; "" = the device
	// lays them out from the portrait ones (s3.home).
	"s3.home_l": "",
	// PawBoop (2026-11-18, s3desk/docs/pawboop.md): the chosen dog (a key of petDogs; "" = the first installed) and the
	// dog assistant's switches (checkAssist). The AI assistant (assist_reply) talks as s3.pet_dog when not told a dog.
	"s3.pet_dog": "", "s3.assist": "",
	// The assistant slot (2026-11-22, s3desk/docs/app-slots.md §7, §16): the package id of the device's assistant app,
	// or "none" (none installed: assist_reply answers 409 no_assistant); "" = the device's default (PawBoop when
	// installed). Colour desks only (checkSlot); the device writes it when its assistant app goes or comes.
	"slot.assistant": "",
	// The saver slot (§7): the saver app's package id, or "builtin" (the saver pages); "" = the device's default.
	"slot.saver": "",
	// The first-boot setup (2026-11-21, s3desk/docs/onboarding-plan.md): what the device sends when it is done ("again"
	// from the web runs it again; checkOnboard), and the name it asked for (checkCallName; copied into the account).
	"s3.onboard": "", "s3.call_name": "",
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
var s3OSJSON = map[string]int{"s3.home": 6000, "s3.home_l": 6000, "s3.mywidgets": 8000, "s3.notes": 16000, "s3.alarms": 2000, "s3.todo": 4000, "s3.apps": 4000}

// s3OSCheck: handled is true for the colour desk's OS keys, good when v may be stored as it is
// (valid JSON within its size, or a known value; "" always). A bad value is skipped, never cut.
func s3OSCheck(k, v string) (handled bool, good bool) {
	if k == "s3.home_l" {
		return true, checkHomeL(v) == "" // the landscape pages' shape, as save_device_settings checks it
	}
	if max, ok := s3OSJSON[k]; ok {
		return true, v == "" || (len(v) <= max && json.Valid([]byte(v)))
	}
	switch k {
	case "s3.pkgs":
		return true, checkPkgsSetting(v) == ""
	case "s3.notif":
		return true, checkNotifSetting(v) == ""
	case "s3.notif_preview", "s3.pkg_auto":
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
	case "s3.pet_dog":
		return true, petDogs[v]
	case "s3.assist":
		return true, checkAssist(v) == ""
	case "s3.onboard":
		return true, checkOnboard(v) == ""
	case "s3.call_name":
		return true, checkCallName(v) == ""
	case "slot.assistant", "slot.saver":
		return true, checkSlot(k, v)
	}
	return false, false
}

// checkSlot: a slot's value: "" (the device's default), "none" (the assistant) or "builtin" (the saver), or a package
// id. The same check is in put_device_settings (which also wants the package on the device): change both.
func checkSlot(k, v string) bool {
	return v == "" || (k == "slot.assistant" && v == "none") || (k == "slot.saver" && v == "builtin") || pkgID(v)
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

// checkHomeL: s3.home_l, the colour desk's landscape Home pages (s3desk/docs/widgets.md §4b, home.cpp readPages):
// "" or {"v":1,"pages":[[entries]],"bg":[...]} within 6000 bytes, at most 8 pages. Entries as in s3.home: an app
// ["a", id, x, y] (1 x 1) or a widget ["w", kind, x, y, size, style, opt, open?, skin?] (size by its first letter as
// the device's sizeOf: m 4 x 2, l 4 x 4, anything else S 2 x 2), each inside the 5 x 4 grid. Overlaps are left to the
// device, which drops them. "bg": no longer than "pages", each null or an object. "" when good, else the reason.
// The same function is in device_sync and save_device_settings: keep the two copies identical.
func checkHomeL(v string) string {
	if v == "" {
		return ""
	}
	if len(v) > 6000 {
		return "quá dài (tối đa 6000 byte)"
	}
	if !json.Valid([]byte(v)) {
		return "không phải JSON"
	}
	var d map[string]any
	if json.Unmarshal([]byte(v), &d) != nil || d == nil {
		return "cần một đối tượng JSON"
	}
	if d["v"] != float64(1) {
		return "cần \"v\": 1"
	}
	pages, ok := d["pages"].([]any)
	if !ok {
		return "cần danh sách trang \"pages\""
	}
	if len(pages) > 8 {
		return "tối đa 8 trang"
	}
	for i, pg := range pages {
		where := "trang " + strconv.Itoa(i+1)
		items, ok := pg.([]any)
		if !ok {
			return where + " không hợp lệ"
		}
		for _, it := range items {
			e, ok := it.([]any)
			if !ok || len(e) < 2 || (e[0] != "a" && e[0] != "w") {
				return where + ": có mục không hợp lệ"
			}
			id, _ := e[1].(string)
			if id == "" {
				return where + ": thiếu mã ứng dụng hoặc widget"
			}
			name := "ứng dụng " + id
			cols, rows := 1.0, 1.0
			if e[0] == "w" {
				name = "widget " + id
				cols, rows = 2, 2
				size := ""
				if len(e) > 4 {
					size, _ = e[4].(string)
				}
				if strings.HasPrefix(size, "m") {
					cols, rows = 4, 2
				} else if strings.HasPrefix(size, "l") {
					cols, rows = 4, 4
				}
			}
			var x, y float64
			xok, yok := false, false
			if len(e) >= 4 {
				x, xok = e[2].(float64)
				y, yok = e[3].(float64)
			}
			if !xok || !yok || x != float64(int64(x)) || y != float64(int64(y)) {
				return where + ": vị trí của " + name + " phải là số nguyên"
			}
			if x < 0 || y < 0 || x+cols > 5 || y+rows > 4 {
				return where + ": " + name + " nằm ngoài lưới 5 x 4"
			}
		}
	}
	if b, has := d["bg"]; has {
		list, ok := b.([]any)
		if !ok {
			return "\"bg\" cần là một danh sách"
		}
		if len(list) > len(pages) {
			return "nền nhiều hơn số trang"
		}
		for i, x := range list {
			if _, obj := x.(map[string]any); x != nil && !obj {
				return "nền trang " + strconv.Itoa(i+1) + " không hợp lệ"
			}
		}
	}
	return ""
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

// ---- custom saver: s3.saver_pages (s3desk/docs/saver.md §2; bands, shadows and slideshows since 2026-11-17) ----
// The same check is in device_sync and save_device_settings: change both. Light: the shape and the ranges the
// device relies on; unknown keys are kept (a newer editor's). "" when good, else the reason (Vietnamese).
var saverBg = map[string]bool{"black": true, "color": true, "grad": true, "wall": true, "photo": true, "tank": true}
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
	slides := 0
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
		// text over pictures: a band behind the text and a soft shadow (saver.md §2)
		if v, has := e["bg"]; has && !hex6(v) {
			return where + ": màu nền chữ của phần tử " + t + " không hợp lệ"
		}
		if !inRange(e, "bo", 10, 100) || !inRange(e, "sh", 0, 1) {
			return where + ": nền hoặc bóng chữ của phần tử " + t + " không hợp lệ"
		}
		// a photo's slideshow: the device's pictures (all) or up to 12 ids, seconds per picture, order, transition, offset
		if t == "photo" {
			if why := checkSlideshow(e); why != "" {
				return where + ": " + why
			}
			if e["all"] == float64(1) || len(asList(e["ids"])) > 1 {
				slides++
				if slides > 3 {
					return where + ": tối đa 3 khung ảnh tự đổi mỗi trang"
				}
			}
		}
	}
	return ""
}

func asList(v any) []any {
	l, _ := v.([]any)
	return l
}

func checkSlideshow(e map[string]any) string {
	if !inRange(e, "all", 0, 1) || !inRange(e, "of", 0, 11) {
		return "ảnh tự đổi không hợp lệ"
	}
	if v, has := e["ids"]; has {
		l, ok := v.([]any)
		if !ok || len(l) > 12 {
			return "tối đa 12 ảnh trong một khung"
		}
		for _, x := range l {
			n, ok := x.(float64)
			if !ok || n < 1 || n > 2e9 || n != float64(int64(n)) {
				return "mã ảnh không hợp lệ"
			}
		}
	}
	if v, has := e["iv"]; has && v != float64(10) && v != float64(30) && v != float64(60) && v != float64(300) && v != float64(900) {
		return "đổi ảnh mỗi 10, 30, 60, 300 hoặc 900 giây"
	}
	if v, has := e["ord"]; has && v != "s" && v != "r" {
		return "thứ tự ảnh phải là s hoặc r"
	}
	if v, has := e["tr"]; has && v != "c" && v != "f" {
		return "chuyển ảnh phải là c hoặc f"
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
	if strings.HasPrefix(fw, "espos-s3-") {
		return "s3"
	}
	if strings.HasPrefix(fw, "espos-bot-") {
		return "bot"
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

// "espos-x4-1.2.3" -> 10203 (0 when unreadable), as the device computes it.
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
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 500, // package settings (app.*) add rows: device_ping reads as many
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
	if boardKind == "s3" || boardKind == "bot" {
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
	// Package settings the device changed (app.<package id>.<key>, docs/manifest-driven.md §1, s3desk): checked as
	// the web's saves are (put_device_settings), against app_manifests and the device's packages (this sync's report,
	// else the last one, plus the store's wanted list).
	appSchemas := map[string]any{}
	appInst := map[string]bool{}
	appIDs := []any{}
	for _, it := range list(body["settings"]) {
		m, _ := it.(map[string]any)
		if pid, _, ok := appKey(wm.Str(m["k"])); ok && (boardKind == "s3" || boardKind == "bot") {
			appIDs = append(appIDs, pid)
		}
	}
	if len(appIDs) > 0 {
		mans, merr := wm.QueryOpts("app_manifests", map[string]any{"filters": []map[string]any{
			{"column": "pkg_id", "op": "in", "value": appIDs}, {"column": "user_id", "op": "in", "value": []any{0, userID}}}, "limit": 80})
		if merr != nil {
			return wm.Fail(merr)
		}
		for _, own := range []bool{false, true} { // the store's row, then the person's own upload of that id wins
			for _, r := range mans {
				if (wm.Int(r["user_id"]) != 0) == own {
					appSchemas[wm.Str(r["pkg_id"])] = r["settings"]
				}
			}
		}
		report := body["pkgs"]
		if report == nil && len(kindRows) == 1 {
			report = kindRows[0]["pkgs"]
		}
		appInst = appInstalled(report, cur["s3.pkgs"].v)
	}
	for _, it := range list(body["settings"]) {
		m, _ := it.(map[string]any)
		k := wm.Str(m["k"])
		appOwn := false
		if strings.HasPrefix(k, "app.") {
			if (boardKind != "s3" && boardKind != "bot") || appCheck(k, wm.Str(m["v"]), appSchemas, appInst) != "" {
				continue
			}
			appOwn = true
		}
		if s3Elsewhere[k] && boardKind == "s3" {
			elsewhere = append(elsewhere, m)
			continue
		}
		_, shared := s3Shared[k]
		botOwn := false
		if appOwn {
			// checked above
		} else if boardKind == "bot" {
			// The EspOS Bot: its own keys (checked whole: JSON lists are longer than 60) and city / time zone.
			_, own := botDefaults[k]
			if !own && !(shared && k != "dash.ics_url") {
				continue
			}
			if own && checkBot(k, wm.Str(m["v"])) != "" {
				continue
			}
			botOwn = own
		} else if strings.HasPrefix(k, "bot.") {
			continue
		} else if _, allowed := deviceDefaults[k]; !allowed && !(shared && boardKind == "s3") {
			continue
		}
		if h, _ := s3OSCheck(k, ""); h && boardKind != "s3" {
			continue // the app-OS keys belong to the colour desk only
		}
		if strings.HasPrefix(k, "x4.") && boardKind == "s3" {
			continue // the X4's packages list belongs to the X4 only
		}
		v := cut(wm.Str(m["v"]), 60)
		if botOwn || appOwn {
			v = wm.Str(m["v"])
		} else if shared {
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
		if k == "s3.call_name" && strings.TrimSpace(v) != "" {
			if perr := setupName(userID, strings.TrimSpace(v)); perr != nil {
				return wm.Fail(perr)
			}
		}
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
	// The EspOS Bot picks its channel (bot.ota): "stable" = releases of kind "bot", "beta" = "bot-beta" or "bot"
	// (the newer), "off" = no update offered.
	wantFw := map[string]bool{kind: true}
	if kind == "bot" {
		ch := botDefaults["bot.ota"]
		if c, ok := cur["bot.ota"]; ok && c.v != "" {
			ch = c.v
		}
		switch ch {
		case "off":
			wantFw = map[string]bool{}
		case "beta":
			wantFw["bot-beta"] = true
		}
	}
	for _, f := range fwRows {
		fk := wm.Str(f["kind"])
		if fk == "" {
			fk = "x4"
		}
		if wantFw[fk] {
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
	if diag, ok := body["diag"].(map[string]any); ok && (kind == "s3" || kind == "bot") {
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
	// Packages gone from the device (s3desk docs/app-slots.md §15.2): their settings app.<id>.* and their server-kept
	// secrets go, at the first sync whose (valid) report and the store's wanted list no longer have them.
	if _, fresh := upd["pkgs"]; fresh && (kind == "s3" || kind == "bot") {
		have := appInstalled(body["pkgs"], cur["s3.pkgs"].v)
		for _, r := range rows {
			k := wm.Str(r["skey"])
			if pid, _, ok := appKey(k); ok && !have[pid] {
				if derr := wm.Delete("device_settings", wm.Int(r["id"])); derr != nil {
					return wm.Fail(derr)
				}
				delete(cur, k)
			}
		}
		gone, gerr := wm.QueryOpts("app_secrets", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 100})
		if gerr != nil {
			return wm.Fail(gerr)
		}
		for _, r := range gone {
			if !have[wm.Str(r["pkg_id"])] {
				if derr := wm.Delete("app_secrets", wm.Int(r["id"])); derr != nil {
					return wm.Fail(derr)
				}
			}
		}
		if !have[sessPkg] { // the Sessions app went: its token stops working (§9.0)
			if rerr := revokeSessTokens(deviceID); rerr != nil {
				return wm.Fail(rerr)
			}
		}
	}
	// Which package secrets the device's own store holds (keys only, never values), for the web's "Set on the device".
	if sv, has := body["app_secrets"]; has && (kind == "s3" || kind == "bot") {
		if held := appHeld(sv); held != nil {
			upd["app_secrets"] = held
		}
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
	if kind == "bot" {
		for k, def := range botDefaults {
			settings[k] = def
			if c, ok := cur[k]; ok {
				settings[k] = c.v
				if c.ms > rev {
					rev = c.ms
				}
			}
		}
		for _, k := range botShared {
			settings[k] = s3Shared[k]
			if c, ok := cur[k]; ok {
				settings[k] = c.v
				if c.ms > rev {
					rev = c.ms
				}
			}
		}
	}
	// PawBoop's chosen dog goes only to a colour desk that has PawBoop (s3desk docs/app-slots.md §16): its packages
	// report (this sync's, else the last one) or the store's wanted list. No report yet: sent as before. Its time still
	// counts in the rev, as device_ping counts every key.
	petHave := true
	if kind == "s3" {
		rep := body["pkgs"]
		if rep == nil && len(kindRows) == 1 {
			rep = kindRows[0]["pkgs"]
		}
		if r, _ := rep.(map[string]any); r != nil {
			petHave = appInstalled(rep, cur["s3.pkgs"].v)["vn.pawboop.pet"]
		}
	}
	for k, def := range deviceDefaults {
		if kind == "bot" {
			break // the EspOS Bot has none of these (reading, sleep screens, X4 dashboard keys)
		}
		if (strings.HasPrefix(k, "s3.") || strings.HasPrefix(k, "slot.")) && kind != "s3" {
			continue // the colour desk's keys (and its app slots) go to the colour desk only
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
		if k == "s3.pet_dog" && !petHave {
			delete(settings, k)
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
		// Notes raise the rev (2026-11-21, EspOS Desktop protocol §5): the person's newest notifications row, as
		// device_ping's rev, so a new dashboard note reaches the colour desk within a minute.
		// scripts/check-ping-keys.py checks both do.
		nr, nerr := wm.QueryOpts("notifications", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}},
			"order_by": "created_at", "desc": true, "limit": 1})
		if nerr != nil {
			return wm.Fail(nerr)
		}
		if len(nr) == 1 {
			if t, perr := time.Parse(time.RFC3339Nano, strings.TrimSpace(wm.Str(nr[0]["created_at"]))); perr == nil && t.UnixMilli() > rev {
				rev = t.UnixMilli()
			}
		}
		// The Claude widget (2026-11-21): the person's desktop's usage_ms, as device_ping's rev (check-ping-keys.py).
		ur, uerr := wm.QueryOpts("desk_state", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}},
			"order_by": "usage_ms", "desc": true, "limit": 1})
		if uerr != nil {
			return wm.Fail(uerr)
		}
		if len(ur) == 1 {
			if v := int64(wm.Num(ur[0]["usage_ms"])); v > rev {
				rev = v
			}
		}
	}
	if kind == "s3" || kind == "bot" {
		// Package settings (app.<package id>.<key>): every one stored for this device; it keeps those of the packages
		// it has. device_ping folds them into its rev the same way (scripts/check-ping-keys.py).
		for k, c := range cur {
			if strings.HasPrefix(k, "app.") {
				settings[k] = c.v
				if c.ms > rev {
					rev = c.ms
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
		if webSet[k] && kind != "bot" {
			webKeys = append(webKeys, k)
		}
	}
	if kind == "bot" {
		for k := range botDefaults {
			if webSet[k] {
				webKeys = append(webKeys, k)
			}
		}
	}
	// The S3 Sessions app's own token (s3desk docs/desktop-protocol.md §9.0): JS apps never get the device token, so
	// the server mints one per (device, dev.espos.sessions) and hands it over below as the package secret espos_token
	// (only its hash stays once the device acked it). A device whose held list lacks it gets a new one.
	if mintAppTokens && kind == "s3" && wm.Str(in["desk_on"]) == "1" {
		var rep any = body["pkgs"]
		if rep == nil && len(devRows) == 1 {
			rep = devRows[0]["pkgs"]
		}
		if appInstalled(rep, cur["s3.pkgs"].v)[sessPkg] {
			if merr := mintSessToken(deviceID, userID, nowMs, body["app_secrets"]); merr != nil {
				return wm.Fail(merr)
			}
		}
	}
	// Server-kept package secrets the device uses through an "auth" binding (s3desk docs/app-slots.md §15.2): sent
	// until acked ("v": "" = forget it); an ack whose ms is the row's updated_ms marks it delivered (an empty row then
	// goes).
	secretsOut := []any{}
	if kind == "s3" || kind == "bot" {
		srows, serr := wm.QueryOpts("app_secrets", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 100})
		if serr != nil {
			return wm.Fail(serr)
		}
		acked := map[string]int64{}
		for _, a := range list(body["secret_acks"]) {
			m, _ := a.(map[string]any)
			acked[wm.Str(m["app"])+"\x00"+wm.Str(m["key"])] = int64(wm.Num(m["ms"]))
		}
		pids := []any{}
		for _, r := range srows {
			pids = append(pids, wm.Str(r["pkg_id"]))
		}
		bound := map[string]map[string]bool{}
		if len(pids) > 0 {
			mans, merr := wm.QueryOpts("app_manifests", map[string]any{"filters": []map[string]any{
				{"column": "pkg_id", "op": "in", "value": pids}, {"column": "user_id", "op": "in", "value": []any{0, userID}}}, "limit": 100})
			if merr != nil {
				return wm.Fail(merr)
			}
			for _, own := range []bool{false, true} {
				for _, m := range mans {
					if (wm.Int(m["user_id"]) != 0) == own {
						bound[wm.Str(m["pkg_id"])] = appBound(m["auth"])
					}
				}
			}
		}
		for _, r := range srows {
			pid, sk := wm.Str(r["pkg_id"]), wm.Str(r["skey"])
			if !bound[pid][sk] {
				continue // used by the server only (fetch rules)
			}
			up := int64(wm.Num(r["updated_ms"]))
			if ms, ok := acked[pid+"\x00"+sk]; ok && ms == up {
				if pid == sessPkg && sk == sessKey && wm.Str(r["value"]) != "" {
					// the Sessions token reached the device: keep only its hash (desk_app_tokens)
					if _, uerr := wm.Update("app_secrets", wm.Int(r["id"]), map[string]any{"value": "", "delivered_ms": up}); uerr != nil {
						return wm.Fail(uerr)
					}
					continue
				}
				if wm.Str(r["value"]) == "" {
					if derr := wm.Delete("app_secrets", wm.Int(r["id"])); derr != nil {
						return wm.Fail(derr)
					}
				} else if _, uerr := wm.Update("app_secrets", wm.Int(r["id"]), map[string]any{"delivered_ms": up}); uerr != nil {
					return wm.Fail(uerr)
				}
				continue
			}
			if int64(wm.Num(r["delivered_ms"])) != up {
				secretsOut = append(secretsOut, map[string]any{"app": pid, "key": sk, "v": wm.Str(r["value"]), "ms": up})
			}
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
	if len(secretsOut) > 0 {
		out["secrets"] = secretsOut
	}
	if kind == "bot" {
		out["kind"] = "bot" // merge adds the bot's small weather from the dashboard
	}
	// A product preset's newer version (s3desk docs/app-slots.md §17.3): only to a unit Kho máy prepared with that
	// preset (its admin_stock row), only when the device reports an older version; the device merges it key by key,
	// keeping what the person changed. Devices without a preset report none and cost nothing here.
	if rp, ok := body["preset"].(map[string]any); ok && kind == "s3" {
		pid := wm.Str(rp["id"])
		pv := wm.Int(rp["v"])
		if pid != "" && len(pid) <= 41 {
			stk, serr := wm.QueryOpts("admin_stock", map[string]any{"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1})
			if serr != nil {
				return wm.Fail(serr)
			}
			if len(stk) == 1 && wm.Str(stk[0]["preset"]) == pid {
				prs, prerr := wm.QueryOpts("admin_presets", map[string]any{"filters": []map[string]any{{"column": "code", "op": "eq", "value": pid},
					{"column": "version", "op": "gt", "value": pv}, {"column": "active", "op": "eq", "value": true}}, "order_by": "version", "desc": true, "limit": 1})
				if prerr != nil {
					return wm.Fail(prerr)
				}
				if len(prs) == 1 {
					if devs, ok := prs[0]["device"].(map[string]any); ok {
						if f, ok := devs[wm.Str(stk[0]["board"])].(map[string]any); ok {
							out["preset"] = f
						}
					}
				}
			}
		}
	}
	return map[string]any{"status": 200, "body": out}
}

// petDogs: the PawBoop dogs a colour desk may choose (s3.pet_dog; s3desk/src/pet/dogs.cpp ALL, dogs.json order).
// "" = the device takes the first dog installed.
var petDogs = map[string]bool{"": true, "bao": true, "momo": true, "boop": true, "leaf": true, "volt": true, "byte": true,
	"shiba": true, "corgi": true, "husky": true, "golden": true}

// checkOnboard: s3.onboard, the first-boot setup (s3desk/src/onboard.cpp, docs/onboarding-plan.md): "" (never ran),
// "again" (the web asks the device to run it again), or what the device sends when it is done:
// {"v":1,"done":<unix s>,"dog":"<key>","skip":["wifi","pair",...]} within 300 bytes. "" when good, else the reason.
// The same function is in device_sync and put_device_settings: keep the two copies identical.
func checkOnboard(v string) string {
	if v == "" || v == "again" {
		return ""
	}
	if len(v) > 300 {
		return "quá dài (tối đa 300 byte)"
	}
	var d struct {
		V    int      `json:"v"`
		Done int64    `json:"done"`
		Dog  string   `json:"dog"`
		Skip []string `json:"skip"`
	}
	if json.Unmarshal([]byte(v), &d) != nil || d.V < 1 || d.V > 99 || d.Done < 0 {
		return "cần \"again\" hoặc {\"v\",\"done\",\"dog\",\"skip\"}"
	}
	if d.Dog != "" && !onboardDogs[d.Dog] {
		return "dog: không có chó " + d.Dog
	}
	steps := map[string]bool{"lang": true, "wifi": true, "dog": true, "name": true, "call": true, "city": true, "pair": true, "tips": true}
	for _, s := range d.Skip {
		if !steps[s] {
			return "skip: không có bước " + s
		}
	}
	return ""
}

// onboardDogs: the PawBoop dogs (s3desk/src/pet/dogs.cpp ALL).
var onboardDogs = map[string]bool{"bao": true, "momo": true, "boop": true, "leaf": true, "volt": true, "byte": true, "shiba": true,
	"corgi": true, "husky": true, "golden": true}

// checkCallName: s3.call_name, the name the first-boot setup asked for (how the dogs call the person; the first sync
// after it copies it into user_profiles.call_name when the account has none). At most 40 letters, one line.
// The same function is in device_sync and put_device_settings: keep the two copies identical.
func checkCallName(v string) string {
	if len([]rune(v)) > 40 || strings.ContainsAny(v, "\r\n\t") {
		return "tối đa 40 ký tự, một dòng"
	}
	return ""
}

// setupName: the name typed in the device's first-boot setup becomes the account's (user_profiles.call_name, the
// web's Tài khoản › "Tên của bạn") when the account has none yet; a name set on the web or told to a dog stays.
func setupName(userID int, name string) error {
	rows, err := wm.QueryOpts("user_profiles", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}}, "limit": 1})
	if err != nil {
		return err
	}
	if len(rows) == 0 {
		_, err = wm.Insert("user_profiles", map[string]any{"user_id": userID, "call_name": name})
		return err
	}
	if strings.TrimSpace(wm.Str(rows[0]["call_name"])) != "" {
		return nil
	}
	_, err = wm.Update("user_profiles", wm.Int(rows[0]["id"]), map[string]any{"call_name": name})
	return err
}

// checkAssist: s3.assist, the dog assistant's switches (s3desk/src/pet/assist.cpp prefs/setPrefs): "" or a JSON
// object of at most these keys: on, greet, weather, alarm, meal (0 or 1), water, move, eyes (minutes between
// habit reminders, 0 to 600; 0 = off), the wake word's wake (0-2), wake_word (0-1), wake_sens (1-3), calls (1-31) and the RGB LED's
// led (0-2), led_bright (1-3), follow (0-1: the dog listens on after an answer) and ack (0-2: what it does when it
// hears its name: nothing, a beep, a word in its voice). "" when good, else the reason.
// The same function is in device_sync and put_device_settings: keep the two copies identical.
func checkAssist(v string) string {
	if v == "" {
		return ""
	}
	if len(v) > 200 {
		return "quá dài (tối đa 200 byte)"
	}
	var d map[string]any
	if json.Unmarshal([]byte(v), &d) != nil || d == nil {
		return "cần một đối tượng JSON"
	}
	for k, x := range d {
		n, ok := x.(float64)
		if !ok || n != float64(int(n)) {
			return k + ": cần một số nguyên"
		}
		switch k {
		case "on", "greet", "weather", "alarm", "meal":
			if n != 0 && n != 1 {
				return k + ": 0 hoặc 1"
			}
		case "water", "move", "eyes":
			if n < 0 || n > 600 {
				return k + ": 0 đến 600 phút"
			}
		case "wake": // the "Gâu" wake word (s3desk/src/wake): 0 off, 1 only on the charger, 2 always
			if n < 0 || n > 2 {
				return k + ": 0, 1 hoặc 2"
			}
		case "wake_word": // 0 "gâu gâu", 1 "gâu" alone too
			if n != 0 && n != 1 {
				return k + ": 0 hoặc 1"
			}
		case "wake_sens": // 1 low, 2 normal, 3 high
			if n < 1 || n > 3 {
				return k + ": 1 đến 3"
			}
		case "calls": // which calls wake the dog, bits: 1 "gâu gâu", 2 "ki ki", 4 "lu lu", 8 "cún ơi", 16 "milu"
			if n < 1 || n > 31 {
				return k + ": 1 đến 31"
			}
		case "led": // the board's RGB LED (s3desk/src/led.h): 0 off, 1 only with the dog, 2 the dog and arrivals
			if n < 0 || n > 2 {
				return k + ": 0, 1 hoặc 2"
			}
		case "led_bright": // 1 low, 2 normal, 3 high
			if n < 1 || n > 3 {
				return k + ": 1 đến 3"
			}
		case "follow": // after a spoken answer the dog listens on for a close question (default 1)
			if n != 0 && n != 1 {
				return k + ": 0 hoặc 1"
			}
		case "ack": // when "gâu gâu" is heard: 0 nothing, 1 a beep, 2 a word in the dog's voice (default 2)
			if n < 0 || n > 2 {
				return k + ": 0, 1 hoặc 2"
			}
		default:
			return k + ": không có công tắc này"
		}
	}
	return ""
}

// ---- EspOS Bot keys (kind "bot", board "bot128"; contract with wakeword 2026-11-20). The SAME block is in
// device_sync, put_device_settings and get_device_settings: scripts/bot-keys.py writes it, check-bot-keys.py checks.
// All values are strings. A bot gets only these, plus botShared (city, time zone) with s3Shared's defaults.
var botDefaults = map[string]string{
	"bot.style": "eyes", "bot.screens": `[{"id":"clock_bold","s":20},{"id":"date_page","s":8},{"id":"weather","s":8},{"id":"eyes","s":20},{"id":"aquarium","s":15}]`,
	"bot.bright": "60", "bot.night": "22:00-07:00", "bot.dog": "", "bot.wake": "2", "bot.follow": "1", "bot.voice": "1", "bot.volume": "60",
	"bot.alarms": "", "bot.ota": "stable", "sys.lang": "vi",
}
var botShared = []string{"dash.city", "dash.lat", "dash.lon", "sys.tz_min"}

// Style and screen ids are open (new looks and standby screens come with new firmware art): a lowercase id; the device
// shows its default for one it does not know.
func botID(v string, max int) bool {
	if v == "" || len(v) > max || v[0] < 'a' || v[0] > 'z' {
		return false
	}
	for _, c := range v {
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
			return false
		}
	}
	return true
}

func botHHMM(s string) bool {
	if len(s) != 5 || s[2] != ':' {
		return false
	}
	h := (int(s[0])-'0')*10 + int(s[1]) - '0'
	m := (int(s[3])-'0')*10 + int(s[4]) - '0'
	return s[0] >= '0' && s[0] <= '2' && s[1] >= '0' && s[1] <= '9' && s[3] >= '0' && s[3] <= '5' && s[4] >= '0' && s[4] <= '9' && h < 24 && m < 60
}

func botInt(v string, lo, hi int) bool {
	if v == "" || len(v) > 4 {
		return false
	}
	n := 0
	for _, c := range v {
		if c < '0' || c > '9' {
			return false
		}
		n = n*10 + int(c-'0')
	}
	return n >= lo && n <= hi
}

// checkBot: "" when v is a good value for the bot key k, else the reason (Vietnamese, for the web).
func checkBot(k, v string) string {
	switch k {
	case "bot.style":
		if botID(v, 16) {
			return ""
		}
		return "Kiểu màn: mã không hợp lệ"
	case "bot.bright":
		if botInt(v, 5, 100) {
			return ""
		}
		return "Độ sáng: 5 đến 100"
	case "bot.wake":
		if botInt(v, 0, 3) {
			return ""
		}
		return "Độ nhạy: 0 đến 3"
	case "bot.volume":
		if botInt(v, 0, 100) {
			return ""
		}
		return "Âm lượng: 0 đến 100"
	case "bot.follow":
		if v == "0" || v == "1" {
			return ""
		}
		return "Nghe tiếp sau khi trả lời: 0 hoặc 1"
	case "bot.voice":
		if v == "0" || v == "1" {
			return ""
		}
		return "Giọng nói: 1 bật, 0 chỉ trả lời bằng chữ"
	case "bot.ota":
		if v == "stable" || v == "beta" || v == "off" {
			return ""
		}
		return "Cập nhật: stable, beta hoặc off"
	case "sys.lang":
		if v == "vi" || v == "en" {
			return ""
		}
		return "Ngôn ngữ: vi hoặc en"
	case "bot.dog":
		if v == "" {
			return ""
		}
		if len(v) > 20 || v[0] < 'a' || v[0] > 'z' {
			return "Chó: mã không hợp lệ"
		}
		for _, c := range v {
			if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
				return "Chó: mã không hợp lệ"
			}
		}
		return ""
	case "bot.night":
		if v == "" || (len(v) == 11 && v[5] == '-' && botHHMM(v[:5]) && botHHMM(v[6:]) && v[:5] != v[6:]) {
			return ""
		}
		return "Giờ đêm: HH:MM-HH:MM hoặc để trống"
	case "bot.screens":
		var list []map[string]any
		if len(v) > 1000 || json.Unmarshal([]byte(v), &list) != nil || len(list) < 1 || len(list) > 16 {
			return "Màn chờ: từ 1 đến 16 màn"
		}
		for _, it := range list {
			s := wm.Num(it["s"])
			if !botID(wm.Str(it["id"]), 24) || s < 3 || s > 600 || s != float64(int(s)) {
				return "Màn chờ: mã màn không hợp lệ, hoặc ngoài 3 đến 600 giây"
			}
		}
		return ""
	case "bot.alarms":
		if v == "" {
			return ""
		}
		var list []map[string]any
		if len(v) > 2000 || json.Unmarshal([]byte(v), &list) != nil || len(list) > 20 {
			return "Báo thức: tối đa 20"
		}
		for _, a := range list {
			h := wm.Num(a["h"])
			m := wm.Num(a["m"])
			if h < 0 || h > 23 || m < 0 || m > 59 || h != float64(int(h)) || m != float64(int(m)) || len([]rune(wm.Str(a["label"]))) > 40 {
				return "Báo thức: giờ hoặc tên không hợp lệ"
			}
			days := wm.Str(a["days"])
			if len(days) > 7 || strings.Trim(days, "0123456") != "" {
				return "Báo thức: ngày trong tuần 0 đến 6"
			}
			if d := wm.Str(a["date"]); d != "" {
				if _, err := time.Parse("2006-01-02", d); err != nil {
					return "Lời nhắc: ngày dạng YYYY-MM-DD"
				}
			}
		}
		return ""
	}
	return "không phải khoá của bot"
}

// ---- end of the EspOS Bot keys block

// BEGIN APPSET (scripts/appset/appset.go, copied by scripts/sync-appset.py: edit it there)

// Package settings (docs/manifest-driven.md §1): "app.<package id>.<key>" values, checked against the package's
// schema as the SDK's cli/src/settings.ts checkValue does (the same cases: sdk/schemas/settings-vectors.json).
const appMaxValue = 2000 // characters of one value

// appKey splits "app.<package id>.<key>": the key is after the last dot, the id between "app." and it.
func appKey(k string) (string, string, bool) {
	if !strings.HasPrefix(k, "app.") {
		return "", "", false
	}
	rest := k[4:]
	i := strings.LastIndex(rest, ".")
	if i <= 0 || i == len(rest)-1 {
		return "", "", false
	}
	id, key := rest[:i], rest[i+1:]
	return id, key, appIDOK(id) && appFieldKeyOK(key)
}

// appIDOK: a package id (a-z 0-9 . _ -, 1-48 characters, starting with a letter or digit).
func appIDOK(id string) bool {
	if len(id) < 1 || len(id) > 48 {
		return false
	}
	for i, c := range id {
		ok := (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '.' || c == '_' || c == '-'))
		if !ok {
			return false
		}
	}
	return true
}

// appFieldKeyOK: a field key (a-z 0-9 _, 1-24 characters, starting with a letter).
func appFieldKeyOK(k string) bool {
	if len(k) < 1 || len(k) > 24 || k[0] < 'a' || k[0] > 'z' {
		return false
	}
	for _, c := range k {
		if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
			return false
		}
	}
	return true
}

// appFields: a schema's fields by key (sections skipped).
func appFields(settings any) map[string]map[string]any {
	out := map[string]map[string]any{}
	list, _ := settings.([]any)
	for _, x := range list {
		f, _ := x.(map[string]any)
		k, _ := f["key"].(string)
		if f != nil && k != "" && f["type"] != "section" {
			out[k] = f
		}
	}
	return out
}

func appNum(v any) (float64, bool) {
	switch x := v.(type) {
	case float64:
		return x, true
	case int:
		return float64(x), true
	case int64:
		return float64(x), true
	case json.Number:
		f, err := x.Float64()
		return f, err == nil
	}
	return 0, false
}

func appRunes(s string) int { return len([]rune(s)) }

// appDecimal: -?(digits[.digits] | .digits), as the SDK's number rule.
func appDecimal(s string) bool {
	if strings.HasPrefix(s, "-") {
		s = s[1:]
	}
	dot := strings.Index(s, ".")
	whole := s
	frac := ""
	if dot >= 0 {
		whole = s[:dot]
		frac = s[dot+1:]
		if frac == "" {
			return false
		}
	}
	if whole == "" && frac == "" {
		return false
	}
	for _, c := range whole + frac {
		if c < '0' || c > '9' {
			return false
		}
	}
	return true
}

// appCheckValue: "" when raw is a valid value of field f, else why not (the SDK's checkValue, rule for rule).
func appCheckValue(f map[string]any, raw string) string {
	if appRunes(raw) > appMaxValue {
		return "at most " + strconv.Itoa(appMaxValue) + " characters"
	}
	typ, _ := f["type"].(string)
	for _, c := range raw {
		if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0x7f {
			return "no control characters"
		}
		if (c == '\n' || c == '\r') && typ != "text" {
			return "no control characters"
		}
	}
	switch typ {
	case "string", "text":
		max := 80
		if typ == "text" {
			max = 500
		}
		if m, ok := appNum(f["max"]); ok {
			max = int(m)
		}
		if appRunes(raw) > max {
			return "at most " + strconv.Itoa(max) + " characters"
		}
		return ""
	case "number":
		if !appDecimal(raw) {
			return "a number like 4 or 2.5"
		}
		v, err := strconv.ParseFloat(raw, 64)
		if err != nil {
			return "a number like 4 or 2.5"
		}
		min, hasMin := appNum(f["min"])
		if hasMin && v < min {
			return "at least " + strconv.FormatFloat(min, 'f', -1, 64)
		}
		if max, ok := appNum(f["max"]); ok && v > max {
			return "at most " + strconv.FormatFloat(max, 'f', -1, 64)
		}
		if b, _ := f["int"].(bool); b && v != float64(int64(v)) {
			return "a whole number"
		}
		if step, ok := appNum(f["step"]); ok && step > 0 {
			n := (v - min) / step // min is 0 when the field has none
			fr := n - float64(int64(n))
			if fr < 0 {
				fr = -fr
			}
			if fr > 1e-9 && 1-fr > 1e-9 {
				return "in steps of " + strconv.FormatFloat(step, 'f', -1, 64)
			}
		}
		return ""
	case "bool":
		if raw == "1" || raw == "0" {
			return ""
		}
		return "\"1\" or \"0\""
	case "enum":
		opts, _ := f["options"].([]any)
		for _, o := range opts {
			if om, _ := o.(map[string]any); om != nil && om["v"] == raw {
				return ""
			}
		}
		return "one of the options"
	case "list":
		var a []any
		if json.Unmarshal([]byte(raw), &a) != nil || a == nil && raw != "[]" {
			return "a JSON list"
		}
		max := 50
		if m, ok := appNum(f["max_items"]); ok {
			max = int(m)
		}
		if len(a) > max {
			return "at most " + strconv.Itoa(max) + " items"
		}
		of, _ := f["of"].(map[string]any)
		if of == nil {
			of = map[string]any{"type": "string"}
		}
		for _, x := range a {
			s, ok := x.(string)
			if !ok {
				return "each item is text"
			}
			if e := appCheckValue(of, s); e != "" {
				return "an item: " + e
			}
		}
		return ""
	case "time":
		if len(raw) == 5 && raw[2] == ':' {
			h, e1 := strconv.Atoi(raw[:2])
			m, e2 := strconv.Atoi(raw[3:])
			if e1 == nil && e2 == nil && raw[0] != '-' && raw[3] != '-' && h < 24 && m < 60 {
				return ""
			}
		}
		return "HH:MM (00:00 to 23:59)"
	case "date":
		if len(raw) != 10 {
			return "YYYY-MM-DD"
		}
		if _, err := time.Parse("2006-01-02", raw); err != nil {
			return "a real date"
		}
		return ""
	case "color":
		if len(raw) == 7 && raw[0] == '#' {
			for _, c := range raw[1:] {
				if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
					return "#rrggbb"
				}
			}
			return ""
		}
		return "#rrggbb"
	case "url":
		bad := "an https:// address of at most 300 characters"
		if appRunes(raw) > 300 || !strings.HasPrefix(raw, "https://") || strings.ContainsAny(raw, " \t") {
			return bad
		}
		host := raw[8:]
		if i := strings.IndexAny(host, "/?#"); i >= 0 {
			host = host[:i]
		}
		dot := strings.Index(host, ".")
		if host == "" || dot <= 0 || dot == len(host)-1 {
			return bad
		}
		return ""
	case "city":
		var c map[string]any
		if json.Unmarshal([]byte(raw), &c) != nil || c == nil {
			return "JSON {\"name\", \"lat\", \"lon\"}"
		}
		name, _ := c["name"].(string)
		if strings.TrimSpace(name) == "" || appRunes(name) > 80 {
			return "JSON {\"name\": 1-80 characters, \"lat\", \"lon\"}"
		}
		lat, ok1 := appNum(c["lat"])
		lon, ok2 := appNum(c["lon"])
		if !ok1 || !ok2 || lat < -90 || lat > 90 || lon < -180 || lon > 180 {
			return "lat -90..90 and lon -180..180"
		}
		return ""
	case "photo":
		if len(raw) < 1 || len(raw) > 40 {
			return "a photo id"
		}
		for _, c := range raw {
			if !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
				return "a photo id"
			}
		}
		return ""
	case "secret":
		return "a secret is never a setting value (the OS keeps it apart)"
	case "section":
		return "a section has no value"
	}
	return "unknown type"
}

// appInstalled: the packages of a device, from its last report (devices.pkgs: {"installed": [{"id"}]}) and the
// store's wanted list (the setting s3.pkgs: [{"id", "rm"}]), so a value can be set while a package installs.
func appInstalled(report any, wanted string) map[string]bool {
	out := map[string]bool{}
	if r, _ := report.(map[string]any); r != nil {
		list, _ := r["installed"].([]any)
		for _, x := range list {
			if p, _ := x.(map[string]any); p != nil {
				if id, _ := p["id"].(string); id != "" {
					out[id] = true
				}
			}
		}
	}
	var w []any
	if json.Unmarshal([]byte(wanted), &w) == nil {
		for _, x := range w {
			if p, _ := x.(map[string]any); p != nil {
				if id, _ := p["id"].(string); id != "" && p["rm"] != true {
					out[id] = true
				}
			}
		}
	}
	return out
}

// appCheckCode: ("", "") when the change k = v may be stored for this device, else a code for the web
// (unknown_app_key, app_not_installed, invalid_setting) and why not. schemas holds each package's "settings" (from
// app_manifests) by id; installed what the device has.
func appCheckCode(k, v string, schemas map[string]any, installed map[string]bool) (string, string) {
	id, key, ok := appKey(k)
	if !ok {
		return "unknown_app_key", k + ": not a package setting (app.<package id>.<key>)"
	}
	if !installed[id] {
		return "app_not_installed", k + ": " + id + " is not installed on this device"
	}
	f := appFields(schemas[id])[key]
	if f == nil {
		return "unknown_app_key", k + ": " + id + " has no setting " + key
	}
	if v == "" && f["type"] != "secret" {
		return "", "" // not set: the device uses the field's default (kept as "", so the device hears of the reset)
	}
	if e := appCheckValue(f, v); e != "" {
		return "invalid_setting", k + ": " + e
	}
	return "", ""
}

// appCheck: "" when the change may be stored, else why not (appCheckCode without the code).
func appCheck(k, v string, schemas map[string]any, installed map[string]bool) string {
	_, why := appCheckCode(k, v, schemas, installed)
	return why
}

// appBound: the secret keys a package's "auth" bindings use (docs/app-slots.md §15.1); a server-kept one of them is
// delivered to the device, the others stay on the server.
func appBound(auth any) map[string]bool {
	out := map[string]bool{}
	a, _ := auth.(map[string]any)
	for _, x := range a {
		b, _ := x.(map[string]any)
		if b == nil {
			continue
		}
		if l, ok := b["basic"].([]any); ok {
			for _, k := range l {
				if s, _ := k.(string); s != "" {
					out[s] = true
				}
			}
		}
		if s, _ := b["bearer"].(string); s != "" {
			out[s] = true
		}
		for _, part := range []string{"header", "query"} {
			if m, ok := b[part].(map[string]any); ok {
				for _, k := range m {
					if s, _ := k.(string); s != "" {
						out[s] = true
					}
				}
			}
		}
	}
	return out
}

// appRepointed: the secret keys of the bindings whose send_to starts with {field}: they must be entered again when
// that field changes, so keys never follow a re-pointed address.
func appRepointed(auth any, field string) []string {
	out := []string{}
	a, _ := auth.(map[string]any)
	for _, x := range a {
		b, _ := x.(map[string]any)
		to, _ := b["send_to"].([]any)
		hit := false
		for _, t := range to {
			if s, _ := t.(string); strings.HasPrefix(s, "{"+field+"}") {
				hit = true
			}
		}
		if hit {
			for k := range appBound(map[string]any{"b": b}) {
				out = append(out, k)
			}
		}
	}
	return out
}

// END APPSET

// ---- the S3 Sessions app's token (desktop-protocol §9.0) ----

// mintAppTokens: off (lead's final call 2026-11-21): the app calls with the device credential the OS adds for its own
// path (net:espos), checked by require_device + desk_app_auth. The app-scoped token is a later upgrade, once package
// secrets reach devices (phase 1c); this code is kept for it (scripts/test-desktop.py checks it stays off).
const mintAppTokens = false

const sessPkg = "dev.espos.sessions"
const sessKey = "espos_token"

func revokeSessTokens(deviceID int) error {
	toks, err := wm.QueryOpts("desk_app_tokens", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": deviceID}, {"column": "revoked", "op": "eq", "value": false}}, "limit": 20})
	if err != nil {
		return err
	}
	for _, t := range toks {
		if _, uerr := wm.Update("desk_app_tokens", wm.Int(t["id"]), map[string]any{"revoked": true}); uerr != nil {
			return uerr
		}
	}
	return nil
}

// mintSessToken: a token for (device, dev.espos.sessions) unless a live one exists that the device still holds (its
// sync's app_secrets list names the key, or the value is still on its way).
func mintSessToken(deviceID, userID int, now int64, held any) error {
	res, err := wm.QueryMany([]map[string]any{
		{"table": "desk_app_tokens", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID},
			{"column": "pkg_id", "op": "eq", "value": sessPkg}, {"column": "revoked", "op": "eq", "value": false}}, "limit": 5, "with_total": false},
		{"table": "app_secrets", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID},
			{"column": "pkg_id", "op": "eq", "value": sessPkg}, {"column": "skey", "op": "eq", "value": sessKey}}, "limit": 1, "with_total": false},
	})
	if err != nil {
		return err
	}
	rowsAt := func(i int) []map[string]any {
		out := []map[string]any{}
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
	toks, secs := rowsAt(0), rowsAt(1)
	if len(toks) > 0 {
		if len(secs) == 1 && wm.Str(secs[0]["value"]) != "" {
			return nil // minted, not acked yet
		}
		h := appHeld(held)
		if h == nil {
			return nil // the device did not say what it holds (older firmware): nothing to judge
		}
		keys, _ := h[sessPkg].([]any) // the app missing from the list = it holds nothing (app-slots §15.2)
		for _, k := range keys {
			if k == sessKey {
				return nil
			}
		}
		if rerr := revokeSessTokens(deviceID); rerr != nil { // it lost the token (reset, reinstall): a new one
			return rerr
		}
	}
	// no crypto/rand here: the run's own ids (never shown to the device), the clock and the device, hashed twice
	ctx := wm.Context()
	seed := sha256.Sum256([]byte(fmt.Sprint("sess-token|", deviceID, "|", userID, "|", time.Now().UnixNano(), "|", ctx["run_id"], "|", ctx["node_id"])))
	sum2 := sha256.Sum256([]byte(fmt.Sprint(hex.EncodeToString(seed[:]), "|", time.Now().UnixNano(), "|", ctx["run_id"])))
	tok := hex.EncodeToString(sum2[:])
	sum := sha256.Sum256([]byte(tok))
	if _, ierr := wm.Insert("desk_app_tokens", map[string]any{"device_id": deviceID, "user_id": userID, "pkg_id": sessPkg,
		"token_hash": hex.EncodeToString(sum[:]), "created_ms": now, "used_ms": 0, "revoked": false}); ierr != nil {
		return ierr
	}
	_, uerr := wm.Upsert("app_secrets", []string{"device_id", "pkg_id", "skey"}, map[string]any{"device_id": deviceID,
		"user_id": userID, "pkg_id": sessPkg, "skey": sessKey, "value": tok, "last4": tok[60:], "updated_ms": now, "delivered_ms": 0}, nil)
	return uerr
}

// appHeld: the sync body's "app_secrets" ({"<pkg id>": ["key", ...]}, s3desk docs/app-slots.md §15.2) cleaned: at most
// 50 packages of 40 keys, valid ids and keys only; nil when it is not that shape.
func appHeld(v any) map[string]any {
	m, ok := v.(map[string]any)
	if !ok || len(m) > 50 {
		return nil
	}
	out := map[string]any{}
	for pid, x := range m {
		keys, ok := x.([]any)
		if !appIDOK(pid) || !ok || len(keys) > 40 {
			continue
		}
		clean := []any{}
		for _, k := range keys {
			if s, ok := k.(string); ok && appFieldKeyOK(s) {
				clean = append(clean, s)
			}
		}
		out[pid] = clean
	}
	return out
}
