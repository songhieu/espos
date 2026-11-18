// Package news builds the EspOS morning paper: VnExpress RSS → articles → an EPUB with
// small grayscale pictures for the X4. Plain Go (no SDK) so it runs and tests natively.
package news

import (
	"encoding/xml"
	"fmt"
	"html"
	"io"
	"net/http"
	"regexp"
	"strings"
	"time"
)

// Section is one VnExpress RSS channel.
type Section struct {
	Slug string
	Name string
}

// Sections lists the channels the web offers, in reading order.
var Sections = []Section{
	{"tin-moi-nhat", "Tin mới nhất"},
	{"tin-noi-bat", "Tin nổi bật"},
	{"thoi-su", "Thời sự"},
	{"the-gioi", "Thế giới"},
	{"kinh-doanh", "Kinh doanh"},
	{"bat-dong-san", "Bất động sản"},
	{"khoa-hoc-cong-nghe", "Khoa học công nghệ"},
	{"suc-khoe", "Sức khỏe"},
	{"giao-duc", "Giáo dục"},
	{"phap-luat", "Pháp luật"},
	{"gia-dinh", "Đời sống"},
	{"du-lich", "Du lịch"},
	{"giai-tri", "Giải trí"},
	{"the-thao", "Thể thao"},
	{"oto-xe-may", "Xe"},
	{"y-kien", "Ý kiến"},
}

func sectionBySlug(slug string) (Section, bool) {
	for _, s := range Sections {
		if s.Slug == slug {
			return s, true
		}
	}
	return Section{}, false
}

// UserAgent identifies the digest politely.
const UserAgent = "Mozilla/5.0 (compatible; EspOS/1.0; personal e-reader digest)"

func get(c *http.Client, url string, limit int64) ([]byte, error) {
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", UserAgent)
	// Prefer formats the standard decoders read; WebP is decoded too when the CDN insists.
	req.Header.Set("Accept", "image/jpeg,image/png;q=0.9,image/webp;q=0.5,text/html,application/xml;q=0.8,*/*;q=0.1")
	resp, err := c.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("%s: status %d", url, resp.StatusCode)
	}
	return io.ReadAll(io.LimitReader(resp.Body, limit))
}

// Item is one RSS entry.
type Item struct {
	Title string
	Link  string
	Pub   time.Time
}

type rss struct {
	Items []struct {
		Title   string `xml:"title"`
		Link    string `xml:"link"`
		PubDate string `xml:"pubDate"`
	} `xml:"channel>item"`
}

// FetchFeed reads one section's RSS (newest first, as VnExpress orders it).
func FetchFeed(c *http.Client, slug string) ([]Item, error) {
	body, err := get(c, "https://vnexpress.net/rss/"+slug+".rss", 4<<20)
	if err != nil {
		return nil, err
	}
	var doc rss
	if err := xml.Unmarshal(body, &doc); err != nil {
		return nil, fmt.Errorf("rss %s: %w", slug, err)
	}
	out := make([]Item, 0, len(doc.Items))
	for _, it := range doc.Items {
		link := strings.TrimSpace(it.Link)
		// Articles only: videos, podcasts and longform pages have no readable body.
		if !strings.HasPrefix(link, "https://vnexpress.net/") || !strings.HasSuffix(link, ".html") {
			continue
		}
		pub, _ := time.Parse(time.RFC1123Z, strings.TrimSpace(it.PubDate))
		out = append(out, Item{Title: strings.TrimSpace(html.UnescapeString(it.Title)), Link: link, Pub: pub})
	}
	return out, nil
}

// Block is one paragraph (XHTML-safe inline markup) or one picture.
type Block struct {
	Para    string
	ImgURL  string
	Caption string
	ImgFile string // set once the picture is stored in the EPUB
	ImgC    string // in the server's cache: the storage key of the colour desk's copy ("" when none)
	ImgCW   int    // and its size in pixels (0 when none)
	ImgCH   int
}

// Article is one story ready for the EPUB.
type Article struct {
	Section Section
	Title   string
	Lead    string
	URL     string
	Pub     time.Time
	Blocks  []Block
}

var (
	reTitle    = regexp.MustCompile(`(?s)<h1 class="title-detail[^"]*"[^>]*>(.*?)</h1>`)
	reLead     = regexp.MustCompile(`(?s)<p class="description[^"]*"[^>]*>(.*?)</p>`)
	reStamp    = regexp.MustCompile(`(?s)<span class="location-stamp"[^>]*>(.*?)</span>`)
	reBody     = regexp.MustCompile(`(?s)<article class="fck_detail[^"]*"[^>]*>(.*?)</article>`)
	reBlock    = regexp.MustCompile(`(?s)<p class="Normal"[^>]*>(.*?)</p>|<figure[^>]*>(.*?)</figure>`)
	reDataSrc  = regexp.MustCompile(`<img[^>]+data-src="(https://[^"]+)"`)
	reSrcset   = regexp.MustCompile(`data-srcset="(https://[^" ]+)`)
	reMetaURL  = regexp.MustCompile(`itemprop="url" content="(https://[^"]+)"`)
	reCaption  = regexp.MustCompile(`(?s)<figcaption[^>]*>(.*?)</figcaption>`)
	reBr       = regexp.MustCompile(`(?i)<br\s*/?>`)
	reBoldOpen = regexp.MustCompile(`(?i)<(strong|b)(\s[^>]*)?>`)
	reBoldEnd  = regexp.MustCompile(`(?i)</(strong|b)>`)
	reItOpen   = regexp.MustCompile(`(?i)<(em|i)(\s[^>]*)?>`)
	reItEnd    = regexp.MustCompile(`(?i)</(em|i)>`)
	reTag      = regexp.MustCompile(`(?s)<[^>]*>`)
	reSpace    = regexp.MustCompile(`[ \t\r\n\x{00a0}]+`)
)

// inline turns source HTML into XHTML-safe text keeping bold, italic and line breaks.
func inline(s string) string {
	s = reBr.ReplaceAllString(s, "\x01")
	s = reBoldOpen.ReplaceAllString(s, "\x02")
	s = reBoldEnd.ReplaceAllString(s, "\x03")
	s = reItOpen.ReplaceAllString(s, "\x04")
	s = reItEnd.ReplaceAllString(s, "\x05")
	s = reTag.ReplaceAllString(s, "")
	s = html.UnescapeString(s)
	s = strings.TrimSpace(reSpace.ReplaceAllString(s, " "))
	s = html.EscapeString(s)
	// Keep only balanced pairs, so the XHTML stays well-formed.
	if strings.Count(s, "\x02") != strings.Count(s, "\x03") {
		s = strings.NewReplacer("\x02", "", "\x03", "").Replace(s)
	}
	if strings.Count(s, "\x04") != strings.Count(s, "\x05") {
		s = strings.NewReplacer("\x04", "", "\x05", "").Replace(s)
	}
	return strings.NewReplacer("\x01", "<br/>", "\x02", "<b>", "\x03", "</b>", "\x04", "<i>", "\x05", "</i>").Replace(s)
}

// plain is inline without markup (titles, captions).
func plain(s string) string {
	s = reTag.ReplaceAllString(s, " ")
	s = html.UnescapeString(s)
	return strings.TrimSpace(reSpace.ReplaceAllString(s, " "))
}

// ParseArticle extracts the title, lead and body blocks from an article page.
func ParseArticle(page string) (title, lead string, blocks []Block) {
	if m := reTitle.FindStringSubmatch(page); m != nil {
		title = plain(m[1])
	}
	if m := reLead.FindStringSubmatch(page); m != nil {
		l := reStamp.ReplaceAllString(m[1], "$1 – ")
		lead = plain(l)
	}
	body := page
	if m := reBody.FindStringSubmatch(page); m != nil {
		body = m[1]
	}
	for _, m := range reBlock.FindAllStringSubmatch(body, -1) {
		if m[1] != "" {
			if p := inline(m[1]); p != "" {
				blocks = append(blocks, Block{Para: p})
			}
			continue
		}
		fig := m[2]
		url := ""
		if u := reDataSrc.FindStringSubmatch(fig); u != nil {
			url = u[1]
		} else if u := reSrcset.FindStringSubmatch(fig); u != nil {
			url = u[1]
		} else if u := reMetaURL.FindStringSubmatch(fig); u != nil {
			url = u[1]
		}
		if url == "" {
			continue
		}
		cap := ""
		if c := reCaption.FindStringSubmatch(fig); c != nil {
			cap = plain(c[1])
		}
		blocks = append(blocks, Block{ImgURL: html.UnescapeString(url), Caption: cap})
	}
	return title, lead, blocks
}

// FetchArticle downloads and parses one story. Pages with almost no text are refused.
func FetchArticle(c *http.Client, sec Section, it Item) (Article, error) {
	page, err := get(c, it.Link, 6<<20)
	if err != nil {
		return Article{}, err
	}
	title, lead, blocks := ParseArticle(string(page))
	paras := 0
	for _, b := range blocks {
		if b.Para != "" {
			paras++
		}
	}
	if paras < 2 {
		return Article{}, fmt.Errorf("%s: no readable body", it.Link)
	}
	if title == "" {
		title = it.Title
	}
	return Article{Section: sec, Title: title, Lead: lead, URL: it.Link, Pub: it.Pub, Blocks: blocks}, nil
}
