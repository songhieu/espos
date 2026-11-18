// Workflow get_stats, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"sort"
	"strconv"
	"strings"
	"time"
)

type bookAgg struct {
	key, title, last string
	id, sec, pages   int
}

// Reading statistics for the signed-in person, from every linked reader: per-day totals for
// the last 400 days, this month's books, today against the goal, and the streaks.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	devs, derr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "revoked", "op": "eq", "value": false}},
		"order_by": "id", "limit": 20})
	if derr != nil {
		return wm.Fail(derr)
	}
	// Goal and time zone: the most recent value on any of the person's readers.
	goal, tz := 30, 420
	ids := []any{}
	for _, d := range devs {
		ids = append(ids, wm.Int(d["id"]))
	}
	if len(ids) > 0 {
		set, serr := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "in", "value": ids}, {"column": "skey", "op": "in", "value": []any{"read.goal_min", "sys.tz_min"}}},
			"order_by": "updated_ms", "limit": 100})
		if serr != nil {
			return wm.Fail(serr)
		}
		for _, s := range set { // ascending: the last one wins
			n, err := strconv.Atoi(wm.Str(s["svalue"]))
			if err != nil {
				continue
			}
			if wm.Str(s["skey"]) == "read.goal_min" && n >= 5 && n <= 240 {
				goal = n
			} else if wm.Str(s["skey"]) == "sys.tz_min" && n >= -720 && n <= 840 {
				tz = n
			}
		}
	}
	local := time.Now().UTC().Add(time.Duration(tz) * time.Minute)
	today := local.Format("2006-01-02")
	from := local.AddDate(0, 0, -400).Format("2006-01-02")
	rows, rerr := wm.QueryOpts("reading_days", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "day", "op": "gte", "value": from}},
		"order_by": "day", "limit": 5000})
	if rerr != nil {
		return wm.Fail(rerr)
	}
	type dayAgg struct{ sec, pages int }
	days := map[string]*dayAgg{}
	month := today[:7]
	books := map[string]*bookAgg{}
	for _, r := range rows {
		day := wm.Str(r["day"])
		a := days[day]
		if a == nil {
			a = &dayAgg{}
			days[day] = a
		}
		a.sec += wm.Int(r["seconds"])
		a.pages += wm.Int(r["pages"])
		key := wm.Str(r["book_key"])
		if key == "" || !strings.HasPrefix(day, month) {
			continue
		}
		b := books[key]
		if b == nil {
			b = &bookAgg{key: key}
			books[key] = b
		}
		b.sec += wm.Int(r["seconds"])
		b.pages += wm.Int(r["pages"])
		if t := wm.Str(r["title"]); t != "" {
			b.title = t
		}
		if id := wm.Int(r["book_id"]); id > 0 {
			b.id = id
		}
		if day > b.last {
			b.last = day
		}
	}
	dayList := []any{}
	keys := make([]string, 0, len(days))
	for k := range days {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		dayList = append(dayList, map[string]any{"d": k, "s": days[k].sec, "p": days[k].pages})
	}
	// Streaks: days in a row at or above the goal; today counts once it is reached.
	g := goal * 60
	reached := func(t time.Time) bool {
		a := days[t.Format("2006-01-02")]
		return a != nil && a.sec >= g
	}
	streak := 0
	t := local
	if !reached(t) {
		t = t.AddDate(0, 0, -1)
	}
	for reached(t) {
		streak++
		t = t.AddDate(0, 0, -1)
	}
	best, run := 0, 0
	for d := local.AddDate(0, 0, -400); !d.After(local); d = d.AddDate(0, 0, 1) {
		if reached(d) {
			run++
			if run > best {
				best = run
			}
		} else {
			run = 0
		}
	}
	// This month's books, most read first, with progress for EspOS books.
	needProgress := false
	for _, b := range books {
		needProgress = needProgress || b.id > 0
	}
	progress := map[int]int{}
	if needProgress {
		// (The id column cannot be filtered with "in": read the person's books instead.)
		brows, berr := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
			{"column": "user_id", "op": "eq", "value": uid}}, "limit": 500})
		if berr != nil {
			return wm.Fail(berr)
		}
		for _, b := range brows {
			progress[wm.Int(b["id"])] = wm.Int(b["progress"])
		}
	}
	list := make([]*bookAgg, 0, len(books))
	for _, b := range books {
		list = append(list, b)
	}
	sort.Slice(list, func(i, j int) bool { return list[i].sec > list[j].sec })
	bookOut := []any{}
	for _, b := range list {
		title := b.title
		if title == "" {
			title = b.key[strings.LastIndex(b.key, "/")+1:]
		}
		o := map[string]any{"key": b.key, "title": title, "seconds": b.sec, "pages": b.pages, "last": b.last}
		if p, ok := progress[b.id]; ok {
			o["percent"] = p
		}
		bookOut = append(bookOut, o)
		if len(bookOut) == 20 {
			break
		}
	}
	return map[string]any{"status": 200, "body": map[string]any{
		"goal": goal, "today": today, "days": dayList, "books": bookOut, "streak": streak, "best": best,
		"has_device": len(devs) > 0,
	}}
}
