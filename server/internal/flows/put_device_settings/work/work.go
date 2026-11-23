// Workflow put_device_settings, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"
)

// Device-visible keys and their defaults (docs/device-api.md); web-only keys never reach the device.
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
	// or "none"; "" = the device's default. Colour desks only; a package id must be on the device (Run).
	"slot.assistant": "",
	"slot.saver":     "", // the saver app's package id, or "builtin"
	// The first-boot setup (2026-11-21, s3desk/docs/onboarding-plan.md): what the device sends when it is done ("again"
	// from the web runs it again; checkOnboard), and the name it asked for (checkCallName; copied into the account).
	"s3.onboard": "", "s3.call_name": "",
	// Lite packages on the X4 (s3desk/docs/profiles.md §9.1): [{"id", "code"}, {"id", "rm": true}]; X4 only.
	"x4.pkgs": "",
}

// s3OSJSON: the colour desk's JSON settings and their size limits in bytes.
var s3OSJSON = map[string]int{"s3.home": 6000, "s3.home_l": 6000, "s3.mywidgets": 8000, "s3.notes": 16000, "s3.alarms": 2000, "s3.todo": 4000, "s3.apps": 4000}

// isS3OSKey: the app-OS keys, accepted for colour desks (kind s3) only.
func isS3OSKey(k string) bool {
	if _, ok := s3OSJSON[k]; ok {
		return true
	}
	return k == "s3.theme" || k == "s3.theme_auto" || k == "s3.wall" || k == "s3.cities" || k == "s3.photos" || k == "s3.pkgs" || k == "s3.notif" || k == "s3.notif_preview" || k == "s3.pkg_auto" || k == "s3.saver_pages" || k == "s3.markets" || k == "s3.pet_dog" || k == "s3.assist" || k == "s3.onboard" || k == "s3.call_name" || k == "slot.assistant" || k == "slot.saver"
}

// checkMarkets: "" or up to 20 "kind:CODE" tokens (gold, stocks, fx, crypto), as a markets widget's option.
// The same check is in device_sync: change both.
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

// ---- app store: package ids, s3.pkgs and s3.notif (s3desk/docs/ecosystem.md §6) ----
// The same checks are in device_sync: change both.

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

// ---- Lite packages on the X4 (s3desk/docs/profiles.md §9): x4.pkgs, faces in dash.layout ----
// The same checks are in device_sync: change both.

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

var webDefaults = map[string]string{
	"dash.city": "Hà Nội", "dash.lat": "21.0245", "dash.lon": "105.8412", "dash.ics_url": "", "sys.tz_min": "420",
	"dash.blocks": "weather,lunar,calendar,notes",
}
var allowed = map[string][]string{
	// sys.lang: every language either device carries (the X4 reader's lib/I18n, s3desk/i18n/languages.json); a
	// device ignores a language it does not have.
	"sys.lang": {"en", "vi", "ar", "az", "be", "bg", "bs", "ca", "ca-valencia", "cs", "da", "de", "es", "fa", "fi", "fr", "he", "hu", "id", "it", "ja", "kk", "ko", "lt", "nb", "nl", "ook", "pl", "pt-BR", "pt-PT", "ro", "ru", "sk", "sl", "sv", "tr", "uk", "zh-Hans"}, "sys.sleep_min": {"5", "10", "15", "30"},
	"read.font": {"notosans", "Literata", "Merriweather", "Bitter", "Lora", "SourceSerif4", "Vollkorn", "NotoSerif", "NotoSerifExtended", "GentiumBookPlus", "IBMPlexSerif", "Alegreya", "Libron", "Inter", "SourceSans3", "IBMPlexSans", "NotoSansExtended", "LexicaUltralegible", "OpenDyslexic", "IBMPlexMono", "SourceCodePro"}, "read.size": {"small", "medium", "large", "xlarge"},
	"read.spacing": {"1.0", "1.1", "1.2", "1.3", "1.4", "1.6", "1.75"}, "dash.enabled": {"0", "1"}, "dash.layout": {"desk", "calendar", "portrait", "landscape", "wl", "wp"},
	"dash.interval_min": {"5", "15", "30", "60"}, "dash.clock": {"0", "1"}, "dash.charging_min": {"0", "1"}, "dash.save_pct": {"0", "10", "20", "30"},
	"sleep.mode": {"dashboard", "images", "cover", "auto"}, "sleep.rotate": {"sleep", "30", "60", "1440"},
	"sleep.order": {"seq", "random"}, "sleep.clock": {"0", "1"},
	"read.align": {"justify", "left"}, "read.margin": {"narrow", "medium", "wide"}, "read.indent": {"0", "1", "2"},
	"read.para": {"0", "1"}, "read.hyphen": {"0", "1"}, "read.aa": {"0", "1"}, "read.refresh": {"1", "5", "10", "15", "30"},
	"read.images": {"show", "bw", "hide"}, "sys.pwr_short": {"ignore", "sleep", "page", "refresh"},
	"sys.side": {"prev_next", "next_prev"}, "sys.front": {"bclr", "lrbc", "lbcr", "bcrl"},
	"sys.orient": {"portrait", "landscape", "inverted"}, "sys.clock_fmt": {"24", "12"},
	"read.goal_min": {"10", "15", "20", "30", "45", "60"},
	"s3.rot":        {"0", "1", "2", "3"}, "s3.saver": {"0", "60", "120", "300", "600"},
	"s3.click": {"0", "1"}, "s3.dnd": {"0", "1"}, "s3.saver_mode": {"clock", "desk", "photo"},
	"s3.theme": {"", "color", "light", "dark", "photo"}, "s3.theme_auto": {"", "0", "1"},
	"s3.notif_preview": {"", "0", "1"}, "s3.pkg_auto": {"", "0", "1"},
	"s3.pet_dog": {"", "bao", "momo", "boop", "leaf", "volt", "byte", "shiba", "corgi", "husky", "golden"},
	"sys.tz_min": {"-600", "-480", "-420", "-360", "-300", "-240", "-180", "0", "60", "120", "180", "240", "330", "420", "480", "540", "600", "720"},
}

var widgetKinds = map[string]bool{"clock": true, "date": true, "month": true, "events": true, "countdown": true, "weather": true,
	"reading": true, "stats": true, "news": true, "notify": true, "battery": true, "app": true, "gold": true,
	"stocks": true, "fx": true, "crypto": true, "books": true, "photo": true, "overview": true,
	"pkg":  true, // a widget package (s3desk/docs/profiles.md §5): option = its id; X4 layouts only
	"word": true, // removed widget: older layouts keep validating, the device skips it
}

// Frame styles of a layout ("k"): rounded cards, ruled paper, bare, bold squares, cards with a shadow.
var layoutSkins = map[string]bool{"": true, "card": true, "line": true, "bare": true, "bold": true, "shadow": true}

// photoOpt checks a photo widget's option: "<mode><every><fit><dither><brightness>|id,id,…"
// mode r/s/f (random, in turn, fixed), every v/h/d (each draw, hour, day), fit c/n (cover, contain),
// dither a/t (fine dots, bold), brightness 00–99; up to 12 picture ids.
func photoOpt(opt string) bool {
	head, ids, ok := strings.Cut(opt, "|")
	if !ok || len(head) != 6 || !strings.ContainsRune("rsf", rune(head[0])) || !strings.ContainsRune("vhd", rune(head[1])) ||
		!strings.ContainsRune("cn", rune(head[2])) || !strings.ContainsRune("atj", rune(head[3])) {
		return false
	}
	if head[4] < '0' || head[4] > '9' || head[5] < '0' || head[5] > '9' {
		return false
	}
	if ids == "" {
		return true
	}
	list := strings.Split(ids, ",")
	if len(list) > 12 {
		return false
	}
	for _, id := range list {
		if n, err := strconv.Atoi(id); err != nil || n <= 0 {
			return false
		}
	}
	return true
}

// checkLayout validates a widget layout (home.w, sleep.wl, sleep.wp):
// {"v":1,"f":1,"k":"card","i":[[kind,x,y,w,h,style,opt,flags],…]} (flags "i" = drawn inverted).
func checkLayout(k, v string) string {
	if v == "" {
		return ""
	}
	if len(v) > 4000 {
		return k + ": bố cục quá dài"
	}
	var doc struct {
		K string  `json:"k"`
		I [][]any `json:"i"`
	}
	if json.Unmarshal([]byte(v), &doc) != nil {
		return k + ": bố cục không đọc được"
	}
	if !layoutSkins[doc.K] {
		return k + ": kiểu khung không hợp lệ"
	}
	cols, rows := 4, 7
	if k == "sleep.wl" || k == "s3.l1" || k == "s3.l2" {
		cols, rows = 6, 4
	}
	s3 := strings.HasPrefix(k, "s3.")
	if len(doc.I) == 0 || len(doc.I) > 20 {
		return k + ": cần từ 1 đến 20 widget"
	}
	used := map[int]bool{}
	hasApps := false
	for _, it := range doc.I {
		if len(it) < 5 {
			return k + ": widget thiếu thông tin"
		}
		kind := wm.Str(it[0])
		if !widgetKinds[kind] {
			return k + ": không có widget " + kind
		}
		// The colour desk has no books: no reading widgets there.
		if s3 && (kind == "reading" || kind == "stats" || kind == "books" || kind == "word" || kind == "overview") {
			return k + ": máy S3 không có widget đọc sách"
		}
		// Widget packages: drawn by the X4 only (the S3 has no declarative renderer yet).
		if kind == "pkg" && s3 {
			return k + ": máy S3 chưa vẽ được tiện ích từ gói"
		}
		// The sleep screens are only looked at: no shortcut buttons there.
		if kind == "app" && k != "home.w" {
			return k + ": màn chờ không đặt được lối tắt ứng dụng"
		}
		x, y, w, h := wm.Int(it[1]), wm.Int(it[2]), wm.Int(it[3]), wm.Int(it[4])
		if x < 0 || y < 0 || w < 1 || h < 1 || x+w > cols || y+h > rows {
			return k + ": widget nằm ngoài lưới"
		}
		for yy := y; yy < y+h; yy++ {
			for xx := x; xx < x+w; xx++ {
				if used[yy*cols+xx] {
					return k + ": hai widget chồng lên nhau"
				}
				used[yy*cols+xx] = true
			}
		}
		style, opt, flags := "", "", ""
		if len(it) > 5 {
			style = wm.Str(it[5])
		}
		if len(it) > 6 {
			opt = wm.Str(it[6])
		}
		if len(it) > 7 {
			flags = wm.Str(it[7])
		}
		if len(style) > 16 || len(opt) > 160 || (flags != "" && flags != "i") {
			return k + ": tuỳ chọn widget quá dài"
		}
		if kind == "pkg" && (!pkgID(opt) || style != "") {
			return k + ": tiện ích từ gói cần mã gói"
		}
		if kind == "photo" && !photoOpt(opt) {
			return k + ": tuỳ chọn ảnh không hợp lệ"
		}
		if kind == "app" && style == "apps" {
			hasApps = true
		}
	}
	if k == "home.w" && !hasApps {
		return "Màn chính cần có ô Ứng dụng"
	}
	return ""
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

var tzNameRe = regexp.MustCompile(`^[A-Za-z][A-Za-z_]*(/[A-Za-z0-9_+\-]+){0,2}$`)

// checkValue answers "" when (k, v) is acceptable, else the reason.
func checkValue(k, v string) string {
	if k == "dash.layout" && strings.HasPrefix(v, "pkg:") {
		if faceLayout(v) {
			return ""
		}
		return "dash.layout: mặt màn chờ từ gói không hợp lệ"
	}
	if k == "x4.pkgs" {
		if why := checkX4Pkgs(v); why != "" {
			return k + ": " + why
		}
		return ""
	}
	if opts, ok := allowed[k]; ok {
		for _, o := range opts {
			if o == v {
				return ""
			}
		}
		return k + ": giá trị không hợp lệ"
	}
	switch k {
	case "slot.assistant", "slot.saver":
		// "" (the device's default), "none" (assistant) / "builtin" (saver), or a package id (device_sync's checkSlot;
		// Run wants it on the device)
		if v == "" || (k == "slot.assistant" && v == "none") || (k == "slot.saver" && v == "builtin") || pkgID(v) {
			return ""
		}
		return k + ": mã ứng dụng không hợp lệ"
	case "dash.quiet_from", "dash.quiet_to":
		if len(v) == 5 && v[2] == 0x3a && v[0] >= 0x30 && v[0] <= 0x32 && v[1] >= 0x30 && v[1] <= 0x39 && v[3] >= 0x30 && v[3] <= 0x35 && v[4] >= 0x30 && v[4] <= 0x39 {
			return ""
		}
		return k + ": HH:MM"
	case "read.status":
		// Status bar items, comma-separated; empty hides the bar's text.
		for _, t := range strings.Split(v, ",") {
			switch t {
			case "page", "percent", "battery", "clock", "chapter", "":
			default:
				return "read.status"
			}
		}
		return ""
	case "home.w", "sleep.wl", "sleep.wp", "s3.p1", "s3.p2", "s3.l1", "s3.l2":
		return checkLayout(k, v)
	case "s3.home", "s3.mywidgets", "s3.notes", "s3.alarms", "s3.todo", "s3.apps":
		// Only checked for being JSON within its size: the device owns the format.
		if v == "" {
			return ""
		}
		if len(v) > s3OSJSON[k] {
			return k + ": quá dài (tối đa " + strconv.Itoa(s3OSJSON[k]) + " byte)"
		}
		if !json.Valid([]byte(v)) {
			return k + ": không phải JSON"
		}
		return ""
	case "s3.home_l":
		if why := checkHomeL(v); why != "" {
			return k + ": " + why
		}
		return ""
	case "s3.wall":
		if len([]rune(v)) <= 20 && !strings.ContainsAny(v, "\r\n\t") {
			return ""
		}
		return k + ": tối đa 20 ký tự"
	case "s3.cities":
		if len([]rune(v)) <= 200 && len(v) <= 800 {
			return ""
		}
		return k + ": tối đa 200 ký tự"
	case "s3.photos":
		if photoIDs(v) {
			return ""
		}
		return k + ": tối đa 12 mã ảnh, cách nhau bằng dấu phẩy"
	case "s3.pkgs":
		if why := checkPkgsSetting(v); why != "" {
			return k + ": " + why
		}
		return ""
	case "s3.notif":
		if why := checkNotifSetting(v); why != "" {
			return k + ": " + why
		}
		return ""
	case "s3.saver_pages":
		if why := checkSaverPages(v); why != "" {
			return k + ": " + why
		}
		return ""
	case "s3.bright", "s3.volume":
		n, err := strconv.Atoi(v)
		lo := 0
		if k == "s3.bright" {
			lo = 5
		}
		if err != nil || n < lo || n > 100 {
			return k + ": " + strconv.Itoa(lo) + " đến 100"
		}
		return ""
	case "dash.city":
		if n := len([]rune(v)); n >= 1 && n <= 60 {
			return ""
		}
		return "dash.city"
	case "dash.ics_url":
		// Up to 5 iCal links shown (Google, iCloud, Outlook…), one per line; "#" before a link hides it
		// (the colour desk's calendar switch), 10 links in all. The same check is in device_sync: change both.
		links := strings.Fields(v)
		shown := 0
		for _, u := range links {
			if !strings.HasPrefix(u, "#") {
				shown++
			}
		}
		if shown > 5 || len(links) > 10 || len(v) > 4000 {
			return "Tối đa 5 lịch"
		}
		for _, u := range links {
			u = strings.TrimPrefix(u, "#")
			if !(strings.HasPrefix(u, "https://") || strings.HasPrefix(u, "webcal://")) || len(u) > 600 {
				return "Mỗi lịch cần một địa chỉ bắt đầu bằng https:// hoặc webcal://"
			}
		}
		return ""
	case "s3.assist":
		if why := checkAssist(v); why != "" {
			return k + ": " + why
		}
		return ""
	case "s3.onboard":
		if why := checkOnboard(v); why != "" {
			return k + ": " + why
		}
		return ""
	case "s3.call_name":
		if why := checkCallName(v); why != "" {
			return k + ": " + why
		}
		return ""
	case "s3.markets":
		if checkMarkets(v) {
			return ""
		}
		return k + ": tối đa 20 mã dạng gold:SJC, stocks:FPT, fx:USD, crypto:BTC"
	case "sys.tz":
		// The device's IANA zone ("Europe/Berlin", 2026-11-21): the dog's clock right across daylight saving
		// (assist_reply placeOf; it falls back to sys.tz_min when the name is unknown there). "" clears it.
		if v == "" || (len(v) <= 40 && tzNameRe.MatchString(v)) {
			return ""
		}
		return k + ": tên múi giờ IANA, ví dụ Asia/Ho_Chi_Minh"
	case "dash.blocks":
		for _, b := range strings.Split(v, ",") {
			switch b {
			case "weather", "lunar", "calendar", "notes", "":
			case "word": // removed with the vocabulary; old values stay valid and are ignored
			case "tasks": // the removed task board; old values stay valid and are ignored
			default:
				return "dash.blocks"
			}
		}
		return ""
	}
	return k + ": không được phép"
}

// geocode finds a city with Open-Meteo (no key). ok=false when nothing matched.
func geocode(city string) (string, string, string, bool, error) {
	res, err := wm.Fetch("GET", "https://geocoding-api.open-meteo.com/v1/search?count=1&language=vi&format=json&name="+url.QueryEscape(city), nil)
	if err != nil {
		return "", "", "", false, err
	}
	body, _ := res["body"].(map[string]any)
	results, _ := body["results"].([]any)
	if len(results) == 0 {
		return "", "", "", false, nil
	}
	r, _ := results[0].(map[string]any)
	return fmt.Sprintf("%.4f", wm.Num(r["latitude"])), fmt.Sprintf("%.4f", wm.Num(r["longitude"])), wm.Str(r["name"]), true, nil
}

func putSetting(deviceID, userID int, k, v string, ms int64) error {
	_, err := wm.Upsert("device_settings", []string{"device_id", "skey"}, map[string]any{
		"device_id": deviceID, "user_id": userID, "skey": k, "svalue": v, "updated_ms": ms, "source": "web",
	}, nil)
	return err
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	in := wmIn
	p, _ := in["params"].(map[string]any)
	b, _ := in["body"].(map[string]any)
	id := wm.Int(p["id"])
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": uid},
		{"column": "revoked", "op": "eq", "value": false}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(devs) != 1 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not found"}}
	}
	changes, _ := b["changes"].(map[string]any)
	if len(changes) == 0 || len(changes) > 30 {
		return map[string]any{"status": 422, "body": map[string]any{"error": "changes"}}
	}
	vals := map[string]string{}
	// Package settings (app.<package id>.<key>, docs/manifest-driven.md §1, s3desk): checked against the package's
	// schema (app_manifests) and the device's packages (its last report and the store's wanted list s3.pkgs).
	appSchemas := map[string]any{}
	appAuth := map[string]any{}
	appInst := map[string]bool{}
	appIDs := []any{}
	for k := range changes {
		if pid, _, ok := appKey(k); ok {
			appIDs = append(appIDs, pid)
		}
	}
	// The slots name packages the device must have ("none" and "builtin" name none).
	slotPkg := map[string]string{}
	for _, k := range []string{"slot.assistant", "slot.saver"} {
		if v := strings.TrimSpace(wm.Str(changes[k])); v != "" && v != "none" && v != "builtin" {
			slotPkg[k] = v
		}
	}
	if len(appIDs) > 0 || len(slotPkg) > 0 {
		if len(appIDs) > 0 {
			mans, qerr := wm.QueryOpts("app_manifests", map[string]any{"filters": []map[string]any{
				{"column": "pkg_id", "op": "in", "value": appIDs}, {"column": "user_id", "op": "in", "value": []any{0, uid}}}, "limit": 80})
			if qerr != nil {
				return wm.Fail(qerr)
			}
			for _, own := range []bool{false, true} { // the store's row, then the person's own upload of that id wins
				for _, r := range mans {
					if (wm.Int(r["user_id"]) != 0) == own {
						appSchemas[wm.Str(r["pkg_id"])] = r["settings"]
						appAuth[wm.Str(r["pkg_id"])] = r["auth"]
					}
				}
			}
		}
		wanted, werr := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": id}, {"column": "skey", "op": "eq", "value": "s3.pkgs"}}, "limit": 1})
		if werr != nil {
			return wm.Fail(werr)
		}
		w := ""
		if len(wanted) == 1 {
			w = wm.Str(wanted[0]["svalue"])
		}
		appInst = appInstalled(devs[0]["pkgs"], w)
	}
	// A city picked from the web's list comes with its coordinates: no lookup needed.
	lat, latOK := coord(wm.Str(changes["dash.lat"]), 90)
	lon, lonOK := coord(wm.Str(changes["dash.lon"]), 180)
	for k, v := range changes {
		s := strings.TrimSpace(wm.Str(v))
		if k == "dash.lat" || k == "dash.lon" {
			continue
		}
		if strings.HasPrefix(k, "app.") {
			// Kept as typed (a text's spaces and lines matter); colour desks and bots only.
			if kind := wm.Str(devs[0]["kind"]); kind != "s3" && kind != "bot" {
				return map[string]any{"status": 422, "body": map[string]any{"error": k + ": máy này không có ứng dụng", "code": "no_packages", "key": k}}
			}
			if code, why := appCheckCode(k, wm.Str(v), appSchemas, appInst); why != "" {
				return map[string]any{"status": 422, "body": map[string]any{"error": why, "code": code, "key": k, "why": why}}
			}
			vals[k] = wm.Str(v)
			continue
		}
		// The EspOS Bot (kind "bot"): its own keys (checkBot) and city / time zone; nothing else, and its keys
		// nowhere else.
		if wm.Str(devs[0]["kind"]) == "bot" {
			if _, own := botDefaults[k]; own {
				if why := checkBot(k, s); why != "" {
					return map[string]any{"status": 422, "body": map[string]any{"error": why, "code": "invalid_setting", "key": k}}
				}
				vals[k] = s
				continue
			}
			if k != "dash.city" && k != "sys.tz_min" && k != "sys.tz" {
				return map[string]any{"status": 422, "body": map[string]any{"error": k + ": không dành cho EspOS Bot", "code": "not_for_bot", "key": k}}
			}
		} else if strings.HasPrefix(k, "bot.") {
			return map[string]any{"status": 422, "body": map[string]any{"error": k + ": chỉ dành cho EspOS Bot", "code": "bot_only", "key": k}}
		}
		if why := checkValue(k, s); why != "" {
			// "code" + "key": for the web to say it in its own language (the Vietnamese reason stays in "error")
			return map[string]any{"status": 422, "body": map[string]any{"error": why, "code": "invalid_setting", "key": k}}
		}
		if isS3OSKey(k) && wm.Str(devs[0]["kind"]) != "s3" {
			return map[string]any{"status": 422, "body": map[string]any{"error": k + ": chỉ dành cho máy S3", "code": "s3_only", "key": k}}
		}
		// The assistant slot names an app the device has (its last report, or the store's wanted list).
		if sp := slotPkg[k]; sp != "" && !appInst[sp] {
			why := k + ": " + sp + " is not installed on this device"
			return map[string]any{"status": 422, "body": map[string]any{"error": why, "code": "app_not_installed", "key": k, "why": why}}
		}
		// The X4's packages (and faces from them) belong to the X4 (a row without a kind is an X4 from before the column).
		if (k == "x4.pkgs" || (k == "dash.layout" && strings.HasPrefix(s, "pkg:"))) && wm.Str(devs[0]["kind"]) == "s3" {
			return map[string]any{"status": 422, "body": map[string]any{"error": k + ": chỉ dành cho máy X4", "code": "x4_only", "key": k}}
		}
		vals[k] = s
	}
	if _, ok := vals["dash.city"]; ok && latOK && lonOK {
		vals["dash.lat"] = lat
		vals["dash.lon"] = lon
	} else if city, ok := vals["dash.city"]; ok {
		lat, lon, found, hit, gerr := geocode(city)
		if gerr != nil {
			return wm.Fail(gerr)
		}
		if !hit {
			return map[string]any{"status": 422, "body": map[string]any{"error": "Không tìm thấy thành phố", "code": "city_not_found"}}
		}
		vals["dash.city"] = found
		vals["dash.lat"] = lat
		vals["dash.lon"] = lon
	}
	ms := time.Now().UnixMilli()
	for k, v := range vals {
		if perr := putSetting(id, uid, k, v, ms); perr != nil {
			return wm.Fail(perr)
		}
	}
	// A package's address changed (a url or text field an "auth" binding sends to, s3desk docs/app-slots.md §15.1):
	// its server-kept secrets are cleared, so keys never follow a re-pointed address. A removal the device must hear
	// of stays as an empty row until the device acks it (device_sync).
	for k := range vals {
		pid, field, ok := appKey(k)
		if !ok {
			continue
		}
		for _, sk := range appRepointed(appAuth[pid], field) {
			old, qerr := wm.QueryOpts("app_secrets", map[string]any{"filters": []map[string]any{
				{"column": "device_id", "op": "eq", "value": id}, {"column": "pkg_id", "op": "eq", "value": pid},
				{"column": "skey", "op": "eq", "value": sk}}, "limit": 1})
			if qerr != nil {
				return wm.Fail(qerr)
			}
			if len(old) == 1 && wm.Str(old[0]["value"]) != "" {
				if _, uerr := wm.Update("app_secrets", wm.Int(old[0]["id"]), map[string]any{"value": "", "last4": "", "updated_ms": ms, "delivered_ms": 0}); uerr != nil {
					return wm.Fail(uerr)
				}
			}
		}
	}
	// New calendar links: drop today's cached events so the next dashboard reads them.
	if _, ok := vals["dash.ics_url"]; ok {
		old, qerr := wm.QueryOpts("ics_cache", map[string]any{"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": id}}, "limit": 5})
		if qerr != nil {
			return wm.Fail(qerr)
		}
		for _, r := range old {
			if derr := wm.Delete("ics_cache", wm.Int(r["id"])); derr != nil {
				return wm.Fail(derr)
			}
		}
	}
	return map[string]any{"status": 200, "body": map[string]any{"ok": true, "saved": vals, "ms": ms}}
}

// coord accepts a latitude/longitude the web picked from its city list (a decimal in range).
func coord(v string, max float64) (string, bool) {
	f, err := strconv.ParseFloat(strings.TrimSpace(v), 64)
	if err != nil || f < -max || f > max {
		return "", false
	}
	return strconv.FormatFloat(f, 'f', 4, 64), true
}

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
