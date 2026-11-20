// Workflow get_device_settings, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
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
	"sys.tz_min":    {"-600", "-480", "-420", "-360", "-300", "-240", "-180", "0", "60", "120", "180", "240", "330", "420", "480", "540", "600", "720"},
}

var widgetKinds = map[string]bool{"clock": true, "date": true, "month": true, "events": true, "countdown": true, "weather": true,
	"reading": true, "stats": true, "news": true, "notify": true, "battery": true, "app": true, "gold": true,
	"stocks": true, "fx": true, "crypto": true, "books": true, "photo": true, "overview": true,
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
		!strings.ContainsRune("cn", rune(head[2])) || !strings.ContainsRune("at", rune(head[3])) {
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
	if k == "sleep.wl" {
		cols, rows = 6, 4
	}
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

// checkValue answers "" when (k, v) is acceptable, else the reason.
func checkValue(k, v string) string {
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
	case "home.w", "sleep.wl", "sleep.wp":
		return checkLayout(k, v)
	case "dash.city":
		if n := len([]rune(v)); n >= 1 && n <= 60 {
			return ""
		}
		return "dash.city"
	case "dash.ics_url":
		if v == "" || (strings.HasPrefix(v, "https://") && len(v) <= 600) {
			return ""
		}
		return "dash.ics_url: cần địa chỉ https://"
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

func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	p, _ := wmIn["params"].(map[string]any)
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
	rows, err := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": id}}, "limit": 200})
	if err != nil {
		return wm.Fail(err)
	}
	settings := map[string]any{}
	if wm.Str(devs[0]["kind"]) == "bot" {
		// The EspOS Bot: its own keys and city / time zone only (as device_sync answers it).
		for k, v := range botDefaults {
			settings[k] = map[string]any{"v": v, "source": "default", "ms": 0}
		}
		for _, k := range botShared {
			settings[k] = map[string]any{"v": webDefaults[k], "source": "default", "ms": 0}
		}
	} else {
		for k, v := range deviceDefaults {
			settings[k] = map[string]any{"v": v, "source": "default", "ms": 0}
		}
		for k, v := range webDefaults {
			settings[k] = map[string]any{"v": v, "source": "default", "ms": 0}
		}
	}
	for _, r := range rows {
		k := wm.Str(r["skey"])
		if _, known := settings[k]; !known && wm.Str(devs[0]["kind"]) == "bot" {
			continue // a bot's answer holds only its own keys
		}
		settings[k] = map[string]any{"v": wm.Str(r["svalue"]), "source": wm.Str(r["source"]), "ms": wm.Num(r["updated_ms"])}
	}
	_ = strings.TrimSpace
	d := devs[0]
	device := map[string]any{"id": id, "name": wm.Str(d["name"]), "last_sync": wm.Str(d["last_sync"]), "battery": d["battery"], "fw": wm.Str(d["fw"]), "fonts": d["fonts"]}
	// The colour desk's packages report from its last sync (app store; docs/ecosystem.md §6): null until it sends one.
	if wm.Str(d["kind"]) == "s3" || strings.HasPrefix(wm.Str(d["fw"]), "espos-s3-") {
		device["pkgs"] = d["pkgs"]
		device["pkgs_at"] = wm.Int(d["pkgs_at"])
	} else if wm.Str(d["kind"]) == "bot" {
		// The EspOS Bot has no packages.
		device["pkgs"] = nil
	} else {
		// An X4's Lite packages (s3desk/docs/profiles.md §9): what is on its card, and what it can run (caps, rt,
		// screen); both null until its firmware sends them. x4.pkgs, what the user wants, is "" until set.
		device["pkgs"] = d["pkgs"]
		device["pkgs_at"] = wm.Int(d["pkgs_at"])
		if _, set := settings["x4.pkgs"]; !set {
			settings["x4.pkgs"] = map[string]any{"v": "", "source": "default", "ms": 0}
		}
	}
	device["caps"] = d["caps"]
	return map[string]any{"status": 200, "body": map[string]any{
		"device":   device,
		"settings": settings,
	}}
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
