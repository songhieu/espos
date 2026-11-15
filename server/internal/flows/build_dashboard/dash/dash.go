// Workflow build_dashboard, node dash (imported by tools/importer).
package dash

import (
	"encoding/json"
	"fmt"
	"inkdesk/internal/wm"
	"math"
	"net/url"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Default time zone: Việt Nam, GMT+7. Each device may set sys.tz_min (web setting).
const defaultTzMin = 420

// tzMinutes reads sys.tz_min, a whole-hour offset (or India's +5:30) in minutes.
func tzMinutes(st map[string]string) int {
	v, err := strconv.Atoi(strings.TrimSpace(st["sys.tz_min"]))
	if err != nil || v < -720 || v > 840 {
		return defaultTzMin
	}
	return v
}

// tzName is the IANA zone Open-Meteo reports in for that fixed offset.
func tzName(min int) string {
	if min == 330 {
		return "Asia/Kolkata"
	}
	h := min / 60
	if h == 0 {
		return "GMT"
	}
	if h > 0 {
		return fmt.Sprintf("Etc/GMT-%d", h)
	}
	return fmt.Sprintf("Etc/GMT+%d", -h)
}

var weekdays = []string{"CHỦ NHẬT", "THỨ HAI", "THỨ BA", "THỨ TƯ", "THỨ NĂM", "THỨ SÁU", "THỨ BẢY"}
var can = []string{"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"}
var chi = []string{"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	deviceID := wm.Int(in["device_id"])
	userID := wm.Int(in["user_id"])
	if deviceID == 0 || userID == 0 {
		return wm.Fail("missing device")
	}
	now := time.Now()
	st, err := loadSettings(deviceID)
	if err != nil {
		return wm.Fail(err)
	}
	tzMin := tzMinutes(st)
	loc := time.FixedZone("local", tzMin*60)
	local := now.In(loc)
	blocks := st["dash.blocks"]
	if blocks == "" {
		blocks = "weather,lunar,calendar,notes"
	}
	on := func(b string) bool { return strings.Contains(","+blocks+",", ","+b+",") }

	ld, lm, ly, leap := solar2Lunar(local.Day(), int(local.Month()), local.Year(), 7)
	lunar := fmt.Sprintf("%d/%d", ld, lm)
	if leap {
		lunar += "N"
	}
	d := map[string]any{
		"now_ms":  now.UnixMilli(),
		"tz_min":  tzMin,
		"date":    local.Format("02.01.2006"),
		"weekday": weekdays[int(local.Weekday())],
		"layout":  orDefault(st["dash.layout"], "desk"),
		"next_s":  nextRefresh(st, local),
	}
	if on("lunar") {
		d["lunar"] = lunar
		d["lunar_year"] = can[(ly+6)%10] + " " + chi[(ly+8)%12]
	}
	if on("weather") {
		w, werr := weather(st, now, local)
		if werr != nil {
			wm.Log("weather: ", werr.Error())
		} else {
			d["weather"] = w
		}
	}
	if on("calendar") && st["dash.ics_url"] != "" {
		ev, eerr := calendar(deviceID, st["dash.ics_url"], now, local)
		if eerr != nil {
			wm.Log("calendar: ", eerr.Error())
		} else {
			d["events"] = ev
		}
	}
	if on("notes") {
		n, nerr := notes(userID, now, loc)
		if nerr != nil {
			return wm.Fail(nerr)
		}
		d["notes"] = n
	}
	// Market widgets: only what the layouts hold, to keep the device's copy small.
	mk := marketNeeds(st)
	if len(mk.gold) > 0 {
		g, _, gerr := marketCached("gold")
		if gerr != nil {
			return wm.Fail(gerr)
		}
		if g != nil {
			d["gold"] = map[string]any{"src": g["src"], "at": g["at"], "items": pick(g["items"], "c", mk.gold)}
		}
	}
	if len(mk.fx) > 0 {
		f, _, ferr := marketCached("fx")
		if ferr != nil {
			return wm.Fail(ferr)
		}
		if f != nil {
			d["fx"] = map[string]any{"at": f["at"], "items": pick(f["items"], "c", mk.fx)}
		}
	}
	if mk.news {
		n, nerr := newsTop(userID)
		if nerr != nil {
			return wm.Fail(nerr)
		}
		d["news"] = n
	}
	if len(mk.coins) > 0 {
		cr, _, cerr := marketCached("crypto")
		if cerr != nil {
			return wm.Fail(cerr)
		}
		items, _ := cr["items"].(map[string]any)
		list := []any{}
		for _, c := range mk.coins {
			if v := items[c]; v != nil {
				list = append(list, v)
			}
		}
		d["crypto"] = map[string]any{"at": cr["at"], "items": list}
	}
	if len(mk.tickers) > 0 {
		q, qerr := stockQuotes(mk.tickers)
		if qerr != nil {
			return wm.Fail(qerr)
		}
		d["stocks"] = q
	}
	// Stamped last: the device sets its clock from it, so the time spent building must not count.
	d["now_ms"] = time.Now().UnixMilli()
	return map[string]any{"dashboard": d}
}

func orDefault(v, def string) string {
	if v == "" {
		return def
	}
	return v
}

func cut(s string, n int) string {
	r := []rune(strings.TrimSpace(s))
	if len(r) <= n {
		return string(r)
	}
	return string(r[:n-1]) + "…"
}

func loadSettings(deviceID int) (map[string]string, error) {
	rows, err := wm.QueryOpts("device_settings", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}},
		"limit":   200,
	})
	if err != nil {
		return nil, err
	}
	st := map[string]string{}
	for _, r := range rows {
		st[wm.Str(r["skey"])] = wm.Str(r["svalue"])
	}
	return st, nil
}

func hhmm(s string) int {
	p := strings.Split(s, ":")
	if len(p) != 2 {
		return -1
	}
	h, e1 := strconv.Atoi(p[0])
	m, e2 := strconv.Atoi(p[1])
	if e1 != nil || e2 != nil {
		return -1
	}
	return h*60 + m
}

// seconds until the next data refresh, skipping the quiet window
func nextRefresh(st map[string]string, local time.Time) int {
	iv, err := strconv.Atoi(st["dash.interval_min"])
	if err != nil || iv <= 0 {
		iv = 30
	}
	next := iv * 60
	qf := hhmm(orDefault(st["dash.quiet_from"], "23:00"))
	qt := hhmm(orDefault(st["dash.quiet_to"], "06:00"))
	if qf < 0 || qt < 0 || qf == qt {
		return next
	}
	at := (local.Hour()*60 + local.Minute() + iv) % 1440
	inQuiet := func(m int) bool {
		if qf < qt {
			return m >= qf && m < qt
		}
		return m >= qf || m < qt
	}
	if inQuiet(at) {
		cur := local.Hour()*60 + local.Minute()
		wait := (qt - cur + 1440) % 1440
		if wait == 0 {
			wait = 1440
		}
		return wait * 60
	}
	return next
}

// ---------- weather (Open-Meteo, no key) ----------

func wmo(code int, day bool) (string, string) {
	switch {
	case code == 0:
		if day {
			return "sun", "Trời quang"
		}
		return "moon", "Trời quang"
	case code == 1:
		if day {
			return "cloudsun", "Ít mây"
		}
		return "moon", "Ít mây"
	case code == 2:
		if day {
			return "cloudsun", "Mây rải rác"
		}
		return "cloud", "Mây rải rác"
	case code == 3:
		return "cloud", "Nhiều mây"
	case code == 45 || code == 48:
		return "fog", "Sương mù"
	case code >= 51 && code <= 57:
		return "rain", "Mưa phùn"
	case code == 61:
		return "rain", "Mưa nhỏ"
	case code == 63:
		return "rain", "Mưa vừa"
	case code == 65 || code == 66 || code == 67:
		return "rain", "Mưa to"
	case code >= 71 && code <= 77, code == 85, code == 86:
		return "snow", "Tuyết"
	case code >= 80 && code <= 82:
		return "rain", "Mưa rào"
	case code >= 95:
		return "storm", "Dông"
	}
	return "cloud", "Nhiều mây"
}

func weather(st map[string]string, now time.Time, local time.Time) (map[string]any, error) {
	lat := orDefault(st["dash.lat"], "21.0245")
	lon := orDefault(st["dash.lon"], "105.8412")
	city := orDefault(st["dash.city"], "Hà Nội")
	_, offSec := local.Zone()
	zone := tzName(offSec / 60)
	locKey := lat + "," + lon + "|" + zone
	var body map[string]any
	cached, err := wm.QueryOpts("weather_cache", map[string]any{
		"filters": []map[string]any{{"column": "loc", "op": "eq", "value": locKey}},
		"limit":   1,
	})
	if err != nil {
		return nil, err
	}
	if len(cached) == 1 && now.UnixMilli()-int64(wm.Num(cached[0]["fetched_ms"])) < 25*60*1000 {
		body, _ = cached[0]["data"].(map[string]any)
	}
	if body == nil {
		u := "https://api.open-meteo.com/v1/forecast?latitude=" + url.QueryEscape(lat) + "&longitude=" + url.QueryEscape(lon) +
			"&current=temperature_2m,weather_code,is_day&hourly=temperature_2m,weather_code,is_day" +
			"&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=" + url.QueryEscape(zone) + "&forecast_days=4"
		res, ferr := wm.Fetch("GET", u, nil)
		if ferr != nil {
			return nil, ferr
		}
		if wm.Int(res["status"]) != 200 {
			return nil, fmt.Errorf("open-meteo status %d", wm.Int(res["status"]))
		}
		body, _ = res["body"].(map[string]any)
		if body == nil {
			return nil, fmt.Errorf("open-meteo: no body")
		}
		if _, uerr := wm.Upsert("weather_cache", []string{"loc"}, map[string]any{"loc": locKey, "data": body, "fetched_ms": now.UnixMilli()}, nil); uerr != nil {
			return nil, uerr
		}
	}
	cur, _ := body["current"].(map[string]any)
	daily, _ := body["daily"].(map[string]any)
	hourly, _ := body["hourly"].(map[string]any)
	icon, desc := wmo(wm.Int(cur["weather_code"]), wm.Int(cur["is_day"]) == 1)
	w := map[string]any{
		"city": city,
		"temp": int(math.Round(wm.Num(cur["temperature_2m"]))),
		"desc": desc,
		"icon": icon,
	}
	if daily != nil {
		w["max"] = int(math.Round(first(daily["temperature_2m_max"])))
		w["min"] = int(math.Round(first(daily["temperature_2m_min"])))
		w["rain"] = int(math.Round(first(daily["precipitation_probability_max"])))
	}
	hours := []any{}
	if hourly != nil {
		times, _ := hourly["time"].([]any)
		temps, _ := hourly["temperature_2m"].([]any)
		codes, _ := hourly["weather_code"].([]any)
		days, _ := hourly["is_day"].([]any)
		startH := local.Hour() + 3 - (local.Hour() % 3)
		for i := range times {
			ts, perr := time.ParseInLocation("2006-01-02T15:04", wm.Str(times[i]), local.Location())
			if perr != nil || ts.Before(local.Truncate(time.Hour)) {
				continue
			}
			diff := int(ts.Sub(time.Date(local.Year(), local.Month(), local.Day(), 0, 0, 0, 0, local.Location())).Hours())
			if diff < startH || (diff-startH)%3 != 0 {
				continue
			}
			if i >= len(temps) || i >= len(codes) || i >= len(days) {
				break
			}
			ic, _ := wmo(wm.Int(codes[i]), wm.Int(days[i]) == 1)
			hours = append(hours, map[string]any{"h": fmt.Sprintf("%dh", ts.Hour()), "t": int(math.Round(wm.Num(temps[i]))), "icon": ic})
			if len(hours) == 4 {
				break
			}
		}
	}
	w["hours"] = hours
	// The next three days (for the weather widget's "3 ngày" style).
	days := []any{}
	if daily != nil {
		dt, _ := daily["time"].([]any)
		maxs, _ := daily["temperature_2m_max"].([]any)
		mins, _ := daily["temperature_2m_min"].([]any)
		codes, _ := daily["weather_code"].([]any)
		for i := 1; i < len(dt) && i < len(maxs) && i < len(mins) && i < len(codes) && len(days) < 3; i++ {
			ts, perr := time.Parse("2006-01-02", wm.Str(dt[i]))
			if perr != nil {
				continue
			}
			ic, _ := wmo(wm.Int(codes[i]), true)
			days = append(days, map[string]any{"d": shortDays[int(ts.Weekday())], "min": int(math.Round(wm.Num(mins[i]))),
				"max": int(math.Round(wm.Num(maxs[i]))), "icon": ic})
		}
	}
	w["days"] = days
	return w, nil
}

var shortDays = []string{"CN", "T2", "T3", "T4", "T5", "T6", "T7"}

// ---------- markets (gold, stocks, exchange rates) ----------

type needs struct {
	news    bool
	gold    []string
	fx      []string
	tickers []string
	coins   []string
}

// The codes a widget asks for (its option), or `def`, de-duplicated across widgets into `into`.
func addCodes(into *[]string, seen map[string]bool, prefix string, it []any, def string, max int) {
	opt := ""
	if len(it) > 6 {
		opt = wm.Str(it[6])
	}
	if strings.TrimSpace(opt) == "" {
		opt = def
	}
	for _, c := range strings.Split(opt, ",") {
		c = strings.ToUpper(strings.TrimSpace(c))
		if c == "" || len(c) > 10 || seen[prefix+c] || len(*into) >= max {
			continue
		}
		seen[prefix+c] = true
		*into = append(*into, c)
	}
}

var indexNames = map[string]bool{"VNINDEX": true, "VN30": true, "HNX": true, "UPCOM": true}

// Which market widgets this device's layouts hold, and the stock symbols they ask for.
func marketNeeds(st map[string]string) needs {
	var n needs
	seen := map[string]bool{}
	for _, key := range []string{"home.w", "sleep.wl", "sleep.wp"} {
		if st[key] == "" {
			continue
		}
		var doc struct {
			I [][]any `json:"i"`
		}
		if json.Unmarshal([]byte(st[key]), &doc) != nil {
			continue
		}
		for _, it := range doc.I {
			if len(it) == 0 {
				continue
			}
			switch wm.Str(it[0]) {
			case "gold":
				addCodes(&n.gold, seen, "gold:", it, "SJC,N24K", 8)
			case "fx":
				addCodes(&n.fx, seen, "fx:", it, "USD,EUR,JPY,CNY", 10)
			case "news":
				n.news = true
			case "crypto":
				addCodes(&n.coins, seen, "coin:", it, "BTC,ETH", 6)
			case "stocks":
				addCodes(&n.tickers, seen, "stk:", it, "VNINDEX,VN30,HNX", 8)
			}
		}
	}
	return n
}

// The list entries whose `field` is one of `codes`, in the codes' order.
func pick(list any, field string, codes []string) []any {
	byCode := map[string]any{}
	arr, _ := list.([]any)
	for _, it := range arr {
		if m, ok := it.(map[string]any); ok {
			byCode[strings.ToUpper(wm.Str(m[field]))] = m
		}
	}
	out := []any{}
	for _, c := range codes {
		if v := byCode[c]; v != nil {
			out = append(out, v)
		}
	}
	return out
}

func marketCached(key string) (map[string]any, int64, error) {
	rows, err := wm.QueryOpts("market_cache", map[string]any{"filters": []map[string]any{{"column": "key", "op": "eq", "value": key}}, "limit": 1})
	if err != nil || len(rows) == 0 {
		return nil, 0, err
	}
	d, _ := rows[0]["data"].(map[string]any)
	return d, int64(wm.Num(rows[0]["fetched_ms"])), nil
}

// Quotes for the symbols, in order, from the 15-minute cache (market_tick): never fetched here.
func stockQuotes(tickers []string) (map[string]any, error) {
	idx, _, err := marketCached("index")
	if err != nil {
		return nil, err
	}
	stk, _, err := marketCached("stk")
	if err != nil {
		return nil, err
	}
	idxItems, _ := idx["items"].(map[string]any)
	stkItems, _ := stk["items"].(map[string]any)
	out := []any{}
	for _, t := range tickers {
		if indexNames[t] {
			if v := idxItems[t]; v != nil {
				out = append(out, v)
			}
		} else if v := stkItems[t]; v != nil {
			out = append(out, v)
		}
	}
	at := wm.Str(idx["at"])
	if s := wm.Str(stk["at"]); s > at {
		at = s
	}
	return map[string]any{"at": at, "items": out}, nil
}

// The newest headlines of the reader's news sections (from the shared 15-minute news cache).
func newsTop(userID int) (map[string]any, error) {
	sections := []string{"tin-moi-nhat", "thoi-su", "the-gioi"}
	prefs, err := wm.QueryOpts("news_prefs", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}}, "limit": 1})
	if err != nil {
		return nil, err
	}
	if len(prefs) == 1 && strings.TrimSpace(wm.Str(prefs[0]["sections"])) != "" {
		sections = strings.Split(wm.Str(prefs[0]["sections"]), ",")
	}
	rows, err := wm.QueryOpts("news_cache", map[string]any{"limit": 40})
	if err != nil {
		return nil, err
	}
	bySlug := map[string][]any{}
	for _, r := range rows {
		list, _ := r["items"].([]any)
		bySlug[wm.Str(r["slug"])] = list
	}
	top := []any{}
	// The first story of each section, then the second ones, up to three headlines.
	for depth := 0; depth < 3 && len(top) < 3; depth++ {
		for _, s := range sections {
			list := bySlug[strings.TrimSpace(s)]
			if depth < len(list) && len(top) < 3 {
				if m, ok := list[depth].(map[string]any); ok && wm.Str(m["title"]) != "" {
					top = append(top, cut(wm.Str(m["title"]), 120))
				}
			}
		}
	}
	return map[string]any{"top": top}, nil
}

func first(v any) float64 {
	a, _ := v.([]any)
	if len(a) == 0 {
		return 0
	}
	return wm.Num(a[0])
}

// ---------- calendar (secret iCal address) ----------

type event struct {
	min   int
	time  string
	title string
}

// icsLinks splits dash.ics_url: up to 5 links (Google, iCloud, Outlook… any iCal feed),
// separated by spaces or new lines; webcal:// is the same feed over https.
func icsLinks(v string) []string {
	var out []string
	for _, u := range strings.Fields(v) {
		if strings.HasPrefix(u, "webcal://") {
			u = "https://" + strings.TrimPrefix(u, "webcal://")
		}
		if strings.HasPrefix(u, "https://") && len(out) < 5 {
			out = append(out, u)
		}
	}
	return out
}

// calendar merges today's events from every linked calendar (cached 15 minutes per device).
func calendar(deviceID int, icsURL string, now time.Time, local time.Time) ([]any, error) {
	day := local.Format("2006-01-02")
	cached, err := wm.QueryOpts("ics_cache", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}},
		"limit":   1,
	})
	if err != nil {
		return nil, err
	}
	if len(cached) == 1 && wm.Str(cached[0]["day"]) == day && now.UnixMilli()-int64(wm.Num(cached[0]["fetched_ms"])) < 15*60*1000 {
		if list, ok := cached[0]["data"].([]any); ok {
			return list, nil
		}
	}
	links := icsLinks(icsURL)
	if len(links) == 0 {
		return nil, fmt.Errorf("ics url must be https or webcal")
	}
	var evs []event
	var lastErr error
	got := 0
	for _, u := range links {
		res, ferr := wm.Fetch("GET", u, nil)
		if ferr != nil {
			lastErr = ferr
			continue
		}
		if wm.Int(res["status"]) != 200 {
			lastErr = fmt.Errorf("ics status %d", wm.Int(res["status"]))
			continue
		}
		got++
		evs = append(evs, parseICS(wm.Str(res["body"]), local)...)
	}
	if got == 0 {
		return nil, lastErr
	}
	sort.SliceStable(evs, func(i, j int) bool { return evs[i].min < evs[j].min })
	out := []any{}
	for _, e := range evs {
		out = append(out, map[string]any{"time": e.time, "title": cut(e.title, 60)})
		if len(out) == 6 {
			break
		}
	}
	if _, uerr := wm.Upsert("ics_cache", []string{"device_id"}, map[string]any{"device_id": deviceID, "day": day, "data": out, "fetched_ms": now.UnixMilli()}, nil); uerr != nil {
		return nil, uerr
	}
	return out, nil
}

// icsTime reads a DTSTART-style value; floating and TZID times are taken as Vietnam local time.
func icsTime(params, val string, loc *time.Location) (time.Time, bool, bool) {
	val = strings.TrimSpace(val)
	if strings.Contains(params, "VALUE=DATE") || len(val) == 8 {
		t, err := time.ParseInLocation("20060102", val[:minInt(8, len(val))], loc)
		return t, true, err == nil
	}
	if strings.HasSuffix(val, "Z") {
		t, err := time.Parse("20060102T150405Z", val)
		return t.In(loc), false, err == nil
	}
	t, err := time.ParseInLocation("20060102T150405", val, loc)
	return t, false, err == nil
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func parseICS(raw string, local time.Time) []event {
	loc := local.Location()
	raw = strings.ReplaceAll(raw, "\r\n", "\n")
	raw = strings.ReplaceAll(raw, "\n ", "")
	raw = strings.ReplaceAll(raw, "\n\t", "")
	today := time.Date(local.Year(), local.Month(), local.Day(), 0, 0, 0, 0, loc)
	var out []event
	inEv := false
	var props map[string][2]string
	var exdates []string
	for _, line := range strings.Split(raw, "\n") {
		switch {
		case line == "BEGIN:VEVENT":
			inEv = true
			props = map[string][2]string{}
			exdates = nil
		case line == "END:VEVENT":
			inEv = false
			if e, ok := occursToday(props, exdates, today, loc); ok {
				out = append(out, e)
			}
		case inEv:
			idx := strings.Index(line, ":")
			if idx < 0 {
				continue
			}
			left, val := line[:idx], line[idx+1:]
			name := left
			params := ""
			if p := strings.Index(left, ";"); p >= 0 {
				name = left[:p]
				params = left[p+1:]
			}
			if name == "EXDATE" {
				for _, x := range strings.Split(val, ",") {
					if len(x) >= 8 {
						exdates = append(exdates, x[:8])
					}
				}
				continue
			}
			props[name] = [2]string{params, val}
		}
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].min < out[j].min })
	return out
}

func occursToday(p map[string][2]string, exdates []string, today time.Time, loc *time.Location) (event, bool) {
	ds, ok := p["DTSTART"]
	if !ok {
		return event{}, false
	}
	if p["STATUS"][1] == "CANCELLED" {
		return event{}, false
	}
	start, allDay, good := icsTime(ds[0], ds[1], loc)
	if !good {
		return event{}, false
	}
	title := strings.ReplaceAll(strings.ReplaceAll(p["SUMMARY"][1], "\\,", ","), "\\n", " ")
	if title == "" {
		title = "(Không tiêu đề)"
	}
	sday := time.Date(start.Year(), start.Month(), start.Day(), 0, 0, 0, 0, loc)
	todayKey := today.Format("20060102")
	for _, x := range exdates {
		if x == todayKey {
			return event{}, false
		}
	}
	match := false
	if rr, has := p["RRULE"]; has {
		match = ruleHits(rr[1], sday, today, loc)
	} else if allDay {
		end := sday.AddDate(0, 0, 1)
		if de, hasEnd := p["DTEND"]; hasEnd {
			if t, _, ok2 := icsTime(de[0], de[1], loc); ok2 {
				end = t
			}
		}
		match = !today.Before(sday) && today.Before(end)
	} else {
		match = sday.Equal(today)
	}
	if !match {
		return event{}, false
	}
	if allDay {
		return event{min: -1, time: "Cả ngày", title: title}, true
	}
	return event{min: start.Hour()*60 + start.Minute(), time: start.Format("15:04"), title: title}, true
}

var byday = map[string]time.Weekday{"SU": 0, "MO": 1, "TU": 2, "WE": 3, "TH": 4, "FR": 5, "SA": 6}

func ruleHits(rule string, sday, today time.Time, loc *time.Location) bool {
	if today.Before(sday) {
		return false
	}
	parts := map[string]string{}
	for _, kv := range strings.Split(rule, ";") {
		if i := strings.Index(kv, "="); i > 0 {
			parts[kv[:i]] = kv[i+1:]
		}
	}
	if u := parts["UNTIL"]; len(u) >= 8 {
		if until, err := time.ParseInLocation("20060102", u[:8], loc); err == nil && today.After(until) {
			return false
		}
	}
	interval := 1
	if v, err := strconv.Atoi(parts["INTERVAL"]); err == nil && v > 0 {
		interval = v
	}
	days := int(math.Round(today.Sub(sday).Hours() / 24))
	switch parts["FREQ"] {
	case "DAILY":
		return days%interval == 0
	case "WEEKLY":
		wd := []time.Weekday{sday.Weekday()}
		if b := parts["BYDAY"]; b != "" {
			wd = nil
			for _, x := range strings.Split(b, ",") {
				x = strings.TrimSpace(x)
				if len(x) >= 2 {
					if w, ok := byday[x[len(x)-2:]]; ok {
						wd = append(wd, w)
					}
				}
			}
		}
		hit := false
		for _, w := range wd {
			if w == today.Weekday() {
				hit = true
			}
		}
		weekStart := sday.AddDate(0, 0, -int(sday.Weekday()))
		weeks := int(math.Round(today.Sub(weekStart).Hours()/24)) / 7
		return hit && weeks%interval == 0
	case "MONTHLY":
		months := (today.Year()-sday.Year())*12 + int(today.Month()) - int(sday.Month())
		return today.Day() == sday.Day() && months%interval == 0
	case "YEARLY":
		return today.Day() == sday.Day() && today.Month() == sday.Month() && (today.Year()-sday.Year())%interval == 0
	}
	return false
}

// ---------- notes ----------

func notes(userID int, now time.Time, loc *time.Location) ([]any, error) {
	rows, err := wm.QueryOpts("notifications", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": userID}, {"column": "created_at", "op": "gte", "value": now.Add(-24 * time.Hour).UTC().Format(time.RFC3339)}},
		"order_by": "created_at", "desc": true, "limit": 2,
	})
	if err != nil {
		return nil, err
	}
	out := []any{}
	for _, r := range rows {
		t := ""
		if ts, perr := time.Parse(time.RFC3339, wm.Str(r["created_at"])); perr == nil {
			t = ts.In(loc).Format("15:04")
		}
		out = append(out, map[string]any{"title": cut(wm.Str(r["title"]), 60), "time": t})
	}
	return out, nil
}

// ---------- âm lịch (thuật toán Hồ Ngọc Đức) ----------

func jdFromDate(dd, mm, yy int) int {
	a := (14 - mm) / 12
	y := yy + 4800 - a
	m := mm + 12*a - 3
	jd := dd + (153*m+2)/5 + 365*y + y/4 - y/100 + y/400 - 32045
	if jd < 2299161 {
		jd = dd + (153*m+2)/5 + 365*y + y/4 - 32083
	}
	return jd
}

func newMoon(k int) float64 {
	kf := float64(k)
	T := kf / 1236.85
	T2 := T * T
	T3 := T2 * T
	dr := math.Pi / 180
	jd1 := 2415020.75933 + 29.53058868*kf + 0.0001178*T2 - 0.000000155*T3
	jd1 = jd1 + 0.00033*math.Sin((166.56+132.87*T-0.009173*T2)*dr)
	M := 359.2242 + 29.10535608*kf - 0.0000333*T2 - 0.00000347*T3
	Mpr := 306.0253 + 385.81691806*kf + 0.0107306*T2 + 0.00001236*T3
	F := 21.2964 + 390.67050646*kf - 0.0016528*T2 - 0.00000239*T3
	c1 := (0.1734-0.000393*T)*math.Sin(M*dr) + 0.0021*math.Sin(2*dr*M)
	c1 = c1 - 0.4068*math.Sin(Mpr*dr) + 0.0161*math.Sin(dr*2*Mpr)
	c1 = c1 - 0.0004*math.Sin(dr*3*Mpr)
	c1 = c1 + 0.0104*math.Sin(dr*2*F) - 0.0051*math.Sin(dr*(M+Mpr))
	c1 = c1 - 0.0074*math.Sin(dr*(M-Mpr)) + 0.0004*math.Sin(dr*(2*F+M))
	c1 = c1 - 0.0004*math.Sin(dr*(2*F-M)) - 0.0006*math.Sin(dr*(2*F+Mpr))
	c1 = c1 + 0.0010*math.Sin(dr*(2*F-Mpr)) + 0.0005*math.Sin(dr*(2*Mpr+M))
	var deltat float64
	if T < -11 {
		deltat = 0.001 + 0.000839*T + 0.0002261*T2 - 0.00000845*T3 - 0.000000081*T*T3
	} else {
		deltat = -0.000278 + 0.000265*T + 0.000262*T2
	}
	return jd1 + c1 - deltat
}

func newMoonDay(k int, tz float64) int {
	return int(math.Floor(newMoon(k) + 0.5 + tz/24))
}

func sunLong(jdn float64) float64 {
	T := (jdn - 2451545.0) / 36525
	T2 := T * T
	dr := math.Pi / 180
	M := 357.52910 + 35999.05030*T - 0.0001559*T2 - 0.00000048*T*T2
	L0 := 280.46645 + 36000.76983*T + 0.0003032*T2
	DL := (1.914600 - 0.004817*T - 0.000014*T2) * math.Sin(dr*M)
	DL = DL + (0.019993-0.000101*T)*math.Sin(dr*2*M) + 0.000290*math.Sin(dr*3*M)
	L := (L0 + DL) * dr
	L = L - math.Pi*2*math.Floor(L/(math.Pi*2))
	return L
}

func sunLongitude(dayNumber int, tz float64) int {
	return int(math.Floor(sunLong(float64(dayNumber)-0.5-tz/24) / math.Pi * 6))
}

func lunarMonth11(yy int, tz float64) int {
	off := jdFromDate(31, 12, yy) - 2415021
	k := int(math.Floor(float64(off) / 29.530588853))
	nm := newMoonDay(k, tz)
	if sunLongitude(nm, tz) >= 9 {
		nm = newMoonDay(k-1, tz)
	}
	return nm
}

func leapMonthOffset(a11 int, tz float64) int {
	k := int(math.Floor((float64(a11)-2415021.076998695)/29.530588853 + 0.5))
	i := 1
	arc := sunLongitude(newMoonDay(k+i, tz), tz)
	last := arc
	for {
		last = arc
		i++
		arc = sunLongitude(newMoonDay(k+i, tz), tz)
		if arc == last || i >= 14 {
			break
		}
	}
	return i - 1
}

func solar2Lunar(dd, mm, yy int, tz float64) (int, int, int, bool) {
	dayNumber := jdFromDate(dd, mm, yy)
	k := int(math.Floor((float64(dayNumber) - 2415021.076998695) / 29.530588853))
	monthStart := newMoonDay(k+1, tz)
	if monthStart > dayNumber {
		monthStart = newMoonDay(k, tz)
	}
	a11 := lunarMonth11(yy, tz)
	b11 := a11
	lunarYear := 0
	if a11 >= monthStart {
		lunarYear = yy
		a11 = lunarMonth11(yy-1, tz)
	} else {
		lunarYear = yy + 1
		b11 = lunarMonth11(yy+1, tz)
	}
	lunarDay := dayNumber - monthStart + 1
	diff := int(math.Floor(float64(monthStart-a11) / 29))
	leap := false
	lunarMonth := diff + 11
	if b11-a11 > 365 {
		lmd := leapMonthOffset(a11, tz)
		if diff >= lmd {
			lunarMonth = diff + 10
			if diff == lmd {
				leap = true
			}
		}
	}
	if lunarMonth > 12 {
		lunarMonth -= 12
	}
	if lunarMonth >= 11 && diff < 4 {
		lunarYear--
	}
	return lunarDay, lunarMonth, lunarYear, leap
}
