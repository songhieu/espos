// Workflow confirm_pair, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"net/url"
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

// placeOf (2026-11-19): the city, its coordinates and the time zone of the person's newest other device, so a device
// paired again (or a second one) starts where the person is, not in Hà Nội: the dog assistant and the weather widget
// read these. Empty when the person has no other device (the web's defaults stay).
func placeOf(uid, newID int) (map[string]string, error) {
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "revoked", "op": "eq", "value": false}},
		"order_by": "id", "desc": true, "limit": 10})
	if err != nil {
		return nil, err
	}
	for _, d := range devs {
		id := wm.Int(d["id"])
		if id == newID {
			continue
		}
		rows, qerr := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": id},
			{"column": "skey", "op": "in", "value": []any{"dash.city", "dash.lat", "dash.lon", "sys.tz_min"}}}, "limit": 10})
		if qerr != nil {
			return nil, qerr
		}
		got := map[string]string{}
		for _, r := range rows {
			got[wm.Str(r["skey"])] = wm.Str(r["svalue"])
		}
		if got["dash.city"] != "" && got["dash.lat"] != "" && got["dash.lon"] != "" {
			return got, nil
		}
	}
	return map[string]string{}, nil
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
	b, _ := in["body"].(map[string]any)
	email := wm.Str(in["email"])
	code := strings.TrimSpace(strings.ReplaceAll(wm.Str(b["code"]), " ", ""))
	if len(code) != 6 {
		return map[string]any{"status": 422, "body": map[string]any{"error": "Mã gồm 6 chữ số", "code": "pair_code_format"}}
	}
	now := time.Now().UnixMilli()
	rows, err := wm.QueryOpts("pairings", map[string]any{"filters": []map[string]any{{"column": "code", "op": "eq", "value": code}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 || wm.Str(rows[0]["status"]) != "pending" || int64(wm.Num(rows[0]["expires_ms"])) < now {
		return map[string]any{"status": 404, "body": map[string]any{"error": "Mã không đúng hoặc đã hết hạn", "code": "pair_code_invalid"}}
	}
	pr := rows[0]
	name := strings.TrimSpace(wm.Str(b["name"]))
	if name == "" || len([]rune(name)) > 40 {
		name = wm.Str(pr["name"])
	}
	if name == "" {
		name = "Xteink X4"
	}
	kind := wm.Str(pr["kind"])
	if kind != "s3" && kind != "bot" && kind != "desktop" {
		kind = "x4" // the EspOS Bot (kind "bot", 2026-11-20), the S3 and the EspOS Desktop Mac app (kind "desktop",
		// 2026-11-21) say so; anything else is an X4
	}
	dev, err := wm.Insert("devices", map[string]any{
		"user_id": uid, "user_email": email, "name": name, "token_hash": wm.Str(pr["token_hash"]), "revoked": false,
		"kind": kind,
	})
	if err != nil {
		return wm.Fail(err)
	}
	id := wm.Int(dev["id"])
	for k, v := range deviceDefaults {
		if kind == "desktop" {
			break // a desktop has no device settings (s3desk/docs/desktop-protocol.md §1): only its place, below
		}
		if perr := putSetting(id, uid, k, v, now); perr != nil {
			return wm.Fail(perr)
		}
	}
	place, perr := placeOf(uid, id)
	if perr != nil {
		return wm.Fail(perr)
	}
	for k, v := range webDefaults {
		if p, ok := place[k]; ok && p != "" {
			v = p
		}
		if kind == "desktop" && k != "dash.city" && k != "dash.lat" && k != "dash.lon" && k != "sys.tz_min" {
			continue
		}
		if perr := putSetting(id, uid, k, v, now); perr != nil {
			return wm.Fail(perr)
		}
	}
	if _, uerr := wm.Update("pairings", pr["id"], map[string]any{"status": "linked"}); uerr != nil {
		return wm.Fail(uerr)
	}
	_ = checkValue
	_ = geocode
	_ = fmt.Sprintf
	_ = url.QueryEscape
	city := webDefaults["dash.city"]
	if place["dash.city"] != "" {
		city = place["dash.city"]
	}
	// city: where its weather (and the dog assistant's) is for, taken from the person's other device when there is one.
	return map[string]any{"status": 201, "body": map[string]any{"device": map[string]any{"id": id, "name": name, "city": city, "kind": kind}}}
}
