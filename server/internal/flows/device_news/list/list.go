// Workflow device_news, node list (imported by tools/importer).
package list

import (
	"inkdesk/internal/wm"
	"sort"
	"strings"
	"time"
)

// VnExpress sections (news worker's catalogue): slug → the name shown.
var sectionNames = map[string]string{
	"tin-moi-nhat": "Tin mới nhất", "tin-noi-bat": "Tin nổi bật", "thoi-su": "Thời sự", "the-gioi": "Thế giới",
	"kinh-doanh": "Kinh doanh", "bat-dong-san": "Bất động sản", "khoa-hoc-cong-nghe": "Khoa học công nghệ",
	"suc-khoe": "Sức khỏe", "giao-duc": "Giáo dục", "phap-luat": "Pháp luật", "gia-dinh": "Đời sống",
	"du-lich": "Du lịch", "giai-tri": "Giải trí", "the-thao": "Thể thao", "oto-xe-may": "Xe", "y-kien": "Ý kiến",
}

// The web's (and the news worker's) default choice of sections.
var defaultSections = []string{"tin-moi-nhat", "thoi-su", "the-gioi", "kinh-doanh", "khoa-hoc-cong-nghe", "suc-khoe"}

func clip(s string, n int) string {
	r := []rune(strings.TrimSpace(s))
	if len(r) <= n {
		return string(r)
	}
	return strings.TrimSpace(string(r[:n]))
}

// The colour desk's News app: the newest cached stories of one section (sec), or of every
// section the reader follows, newest first, at most 30.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["user_id"])
	if uid <= 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	sec := strings.TrimSpace(wm.Str(in["sec"]))
	sections := defaultSections
	if sec != "" {
		if _, ok := sectionNames[sec]; !ok {
			return map[string]any{"status": 404, "body": map[string]any{"error": "not_found"}}
		}
		sections = []string{sec}
	} else {
		prefs, err := wm.QueryOpts("news_prefs", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
		if err != nil {
			return wm.Fail(err)
		}
		if len(prefs) == 1 && strings.TrimSpace(wm.Str(prefs[0]["sections"])) != "" {
			sections = strings.Split(wm.Str(prefs[0]["sections"]), ",")
		}
	}
	rows, err := wm.QueryOpts("news_cache", map[string]any{"limit": 40})
	if err != nil {
		return wm.Fail(err)
	}
	bySlug := map[string][]any{}
	for _, r := range rows {
		list, _ := r["items"].([]any)
		bySlug[wm.Str(r["slug"])] = list
	}
	items := []map[string]any{}
	seen := map[string]bool{}
	soon := time.Now().Add(time.Hour).UnixMilli()
	for _, s := range sections {
		s = strings.TrimSpace(s)
		name, ok := sectionNames[s]
		if !ok {
			continue
		}
		for _, it := range bySlug[s] {
			m, isMap := it.(map[string]any)
			id := wm.Str(m["id"])
			if !isMap || id == "" || wm.Str(m["title"]) == "" || seen[id] {
				continue
			}
			seen[id] = true
			out := map[string]any{"id": id, "title": clip(wm.Str(m["title"]), 160), "lead": clip(wm.Str(m["lead"]), 400),
				"sec": name, "pub_ms": int64(wm.Num(m["pub_ms"])), "img": m["img"] == true}
			// The thumbnail's signed storage address (the news worker signs it for days): the device
			// fetches it directly, without the /thumb redirect. Only while it has an hour left.
			if u := wm.Str(m["thumb"]); u != "" && int64(wm.Num(m["thumb_exp"])) >= soon {
				out["thumb"] = u
			}
			items = append(items, out)
		}
	}
	// Newest first; stories cached before publish times were kept (pub_ms 0) keep their cache order, last.
	sort.SliceStable(items, func(i, j int) bool { return wm.Num(items[i]["pub_ms"]) > wm.Num(items[j]["pub_ms"]) })
	if len(items) > 30 {
		items = items[:30]
	}
	out := make([]any, 0, len(items))
	for _, it := range items {
		out = append(out, it)
	}
	return map[string]any{"status": 200, "body": map[string]any{"items": out}}
}
