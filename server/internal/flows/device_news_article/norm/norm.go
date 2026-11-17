// Workflow device_news_article, node norm (imported by tools/importer).
package norm

import (
	"encoding/json"
	"inkdesk/internal/wm"
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

// The article as the news worker caches it (newscache/art/<id>.json, Go field names).
type cachedBlock struct {
	Para, ImgURL, Caption, ImgFile, ImgC string
	ImgCW, ImgCH                         int
}
type cachedArticle struct {
	ID, Slug, Title, Lead, URL, Pub string
	Blocks                          []cachedBlock
}

// plainPara turns a cached paragraph (escaped text with <b>, <i>, <br/>) into plain text, line breaks kept.
func plainPara(s string) string {
	s = strings.NewReplacer("<br/>", "\n", "<b>", "", "</b>", "", "<i>", "", "</i>", "").Replace(s)
	s = strings.NewReplacer("&lt;", "<", "&gt;", ">", "&#34;", "\"", "&quot;", "\"", "&#39;", "'", "&amp;", "&").Replace(s)
	return strings.TrimSpace(s)
}

// The colour desk's reader: the cached story as plain blocks. Picture blocks carry the storage key
// of their colour copy ("key", signed next); pictures without one are left out.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	var a cachedArticle
	if err := json.Unmarshal([]byte(wm.Str(in["raw"])), &a); err != nil {
		return wm.Fail("article: " + err.Error())
	}
	var pubMs int64
	if t, err := time.Parse(time.RFC3339, a.Pub); err == nil && t.Year() > 1 {
		pubMs = t.UnixMilli()
	}
	sec := sectionNames[a.Slug]
	blocks := []any{}
	keys := []any{}
	for _, b := range a.Blocks {
		if b.Para != "" {
			if p := plainPara(b.Para); p != "" {
				blocks = append(blocks, map[string]any{"p": p})
			}
			continue
		}
		if b.ImgC == "" {
			continue
		}
		img := map[string]any{"key": b.ImgC, "cap": strings.TrimSpace(b.Caption)}
		if b.ImgCW > 0 && b.ImgCH > 0 {
			img["w"] = b.ImgCW
			img["h"] = b.ImgCH
		}
		blocks = append(blocks, img)
		keys = append(keys, b.ImgC)
	}
	id := wm.Str(in["id"])
	if a.ID != "" {
		id = a.ID
	}
	return map[string]any{
		"article": map[string]any{"id": id, "title": strings.TrimSpace(a.Title), "lead": strings.TrimSpace(a.Lead), "sec": sec,
			"pub_ms": pubMs, "url": a.URL, "blocks": blocks},
		"keys": keys,
	}
}
