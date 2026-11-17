// Workflow save_device_settings, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"fmt"
	"inkdesk/internal/wm"
	"net/url"
	"strconv"
	"strings"
	"time"
)

// Device-visible keys and their defaults (docs/inkdesk-api.md); web-only keys never reach the device.
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
	// Lite packages on the X4 (s3desk/docs/profiles.md §9.1): [{"id", "code"}, {"id", "rm": true}]; X4 only.
	"x4.pkgs": "",
}

// s3OSJSON: the colour desk's JSON settings and their size limits in bytes.
var s3OSJSON = map[string]int{"s3.home": 6000, "s3.mywidgets": 8000, "s3.notes": 16000, "s3.alarms": 2000, "s3.todo": 4000, "s3.apps": 4000}

// isS3OSKey: the app-OS keys, accepted for colour desks (kind s3) only.
func isS3OSKey(k string) bool {
	if _, ok := s3OSJSON[k]; ok {
		return true
	}
	return k == "s3.theme" || k == "s3.theme_auto" || k == "s3.wall" || k == "s3.cities" || k == "s3.photos" || k == "s3.pkgs" || k == "s3.notif" || k == "s3.notif_preview" || k == "s3.saver_pages" || k == "s3.markets"
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
	"sys.lang": {"vi", "en"}, "sys.sleep_min": {"5", "10", "15", "30"},
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
	"s3.notif_preview": {"", "0", "1"},
	"sys.tz_min":       {"-600", "-480", "-420", "-360", "-300", "-240", "-180", "0", "60", "120", "180", "240", "330", "420", "480", "540", "600", "720"},
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
	case "s3.markets":
		if checkMarkets(v) {
			return ""
		}
		return k + ": tối đa 20 mã dạng gold:SJC, stocks:FPT, fx:USD, crypto:BTC"
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
	// A city picked from the web's list comes with its coordinates: no lookup needed.
	lat, latOK := coord(wm.Str(changes["dash.lat"]), 90)
	lon, lonOK := coord(wm.Str(changes["dash.lon"]), 180)
	for k, v := range changes {
		s := strings.TrimSpace(wm.Str(v))
		if k == "dash.lat" || k == "dash.lon" {
			continue
		}
		if why := checkValue(k, s); why != "" {
			return map[string]any{"status": 422, "body": map[string]any{"error": why}}
		}
		if isS3OSKey(k) && wm.Str(devs[0]["kind"]) != "s3" {
			return map[string]any{"status": 422, "body": map[string]any{"error": k + ": chỉ dành cho máy S3"}}
		}
		// The X4's packages (and faces from them) belong to the X4 (a row without a kind is an X4 from before the column).
		if (k == "x4.pkgs" || (k == "dash.layout" && strings.HasPrefix(s, "pkg:"))) && wm.Str(devs[0]["kind"]) == "s3" {
			return map[string]any{"status": 422, "body": map[string]any{"error": k + ": chỉ dành cho máy X4"}}
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
			return map[string]any{"status": 422, "body": map[string]any{"error": "Không tìm thấy thành phố"}}
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
