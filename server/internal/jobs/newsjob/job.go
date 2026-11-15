// The InkDesk "news" worker.
//
//	{"mode":"refresh"}             every 15 minutes: fetch new VnExpress stories into the server's cache
//	{"mode":"due"}                 hourly tick: enqueue one job per reader whose hour has come
//	{"mode":"one","user_id":123}   pack today's issue for one reader from the cache, add it to their books
//
// Only the refresh talks to VnExpress: readers' issues (and the "make one now" button) are
// packed from the cache, so the source sees one fetch per story, however many readers.
package newsjob

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"time"

	"inkdesk/internal/jobs/newsjob/news"
	"inkdesk/internal/wmjob"
)

type Input struct {
	Mode   string `json:"mode"`
	UserID int64  `json:"user_id"`
}

var defaultSections = []string{"tin-moi-nhat", "thoi-su", "the-gioi", "kinh-doanh", "khoa-hoc-cong-nghe", "suc-khoe"}

func num(v any) int64 {
	switch x := v.(type) {
	case float64:
		return int64(x)
	case int64:
		return x
	case int:
		return int64(x)
	case string:
		var n int64
		fmt.Sscan(x, &n)
		return n
	}
	return 0
}

func str(v any) string {
	if s, ok := v.(string); ok {
		return s
	}
	return ""
}

func fail(err error) {
	wmjob.Log("error", err.Error())
	panic(wmjob.ErrAbort)
}

// Main runs one job (its input comes from wmjob.Input).
func Main() {
	var in Input
	if err := wmjob.Input(&in); err != nil {
		fail(err)
	}
	vn := time.FixedZone("ICT", 7*3600)
	now := time.Now().In(vn)
	if in.Mode == "due" {
		due(now)
		return
	}
	if in.Mode == "refresh" {
		refresh(now)
		return
	}
	if in.UserID <= 0 {
		fail(fmt.Errorf("user_id missing"))
	}
	one(in.UserID, now)
}

// due starts today's issue for every reader whose delivery hour is now.
func due(now time.Time) {
	rows, _, err := wmjob.TableQuery("news_prefs", map[string]any{
		"filters": []map[string]any{{"column": "enabled", "op": "eq", "value": true}}, "limit": 1000,
	})
	if err != nil {
		fail(err)
	}
	today := now.Format("2006-01-02")
	started := 0
	for _, r := range rows {
		if int(num(r["hour"])) != now.Hour() || str(r["last_date"]) == today {
			continue
		}
		uid := num(r["user_id"])
		if _, err := wmjob.Enqueue("news", Input{Mode: "one", UserID: uid}, fmt.Sprintf("news:%d:%s", uid, today)); err != nil {
			wmjob.Log("warn", fmt.Sprintf("enqueue %d: %v", uid, err))
			continue
		}
		started++
	}
	wmjob.SetResult(map[string]any{"started": started})
}

func one(uid int64, now time.Time) {
	prefs, _, err := wmjob.TableQuery("news_prefs", map[string]any{
		"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 1,
	})
	if err != nil {
		fail(err)
	}
	sections := defaultSections
	per, keep, images := 5, 7, true
	var prefID int64
	if len(prefs) == 1 {
		p := prefs[0]
		prefID = num(p["id"])
		if s := strings.TrimSpace(str(p["sections"])); s != "" {
			sections = strings.Split(s, ",")
		}
		if n := int(num(p["per_section"])); n > 0 {
			per = n
		}
		if n := int(num(p["keep"])); n > 0 {
			keep = n
		}
		if b, ok := p["images"].(bool); ok {
			images = b
		}
	}

	res, err := packFromCache(sections, per, images, now)
	if err != nil {
		if prefID > 0 {
			wmjob.TablePatch("news_prefs", prefID, map[string]any{"last_note": "Lỗi: " + err.Error()})
		}
		fail(err)
	}

	date := now.Format("2006-01-02")
	key := fmt.Sprintf("news/u%d/%s.epub", uid, date)
	if _, err := wmjob.StoragePut(key, res.EPUB, "application/epub+zip", "private"); err != nil {
		fail(err)
	}
	// Today's issue made again (the "now" button) replaces the earlier one.
	old, _, err := wmjob.TableQuery("books", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "file_key", "op": "eq", "value": key}}, "limit": 5})
	if err != nil {
		fail(err)
	}
	for _, r := range old {
		if err := wmjob.TableDelete("books", num(r["id"])); err != nil {
			fail(err)
		}
	}
	if _, err := wmjob.TableInsert("books", map[string]any{
		"user_id": uid, "title": res.Title, "author": "VnExpress", "file_key": key,
		"file_name": "Bao-sang-" + date + ".epub", "size": len(res.EPUB), "status": "ready", "kind": "news",
	}); err != nil {
		fail(err)
	}
	// Keep the newest issues only (the device drops the others at its next sync).
	all, _, err := wmjob.TableQuery("books", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "kind", "op": "eq", "value": "news"}},
		"order_by": "id", "desc": true, "limit": 200})
	if err != nil {
		fail(err)
	}
	for i, r := range all {
		if i < keep {
			continue
		}
		if k := str(r["file_key"]); k != "" {
			_ = wmjob.StorageDelete(k)
		}
		if err := wmjob.TableDelete("books", num(r["id"])); err != nil {
			wmjob.Log("warn", err.Error())
		}
	}
	note := fmt.Sprintf("%d bài · %d ảnh · %d KB", res.Articles, res.Images, len(res.EPUB)/1024)
	if prefID > 0 {
		wmjob.TablePatch("news_prefs", prefID, map[string]any{"last_date": date, "last_note": note})
	}
	wmjob.SetResult(map[string]any{"title": res.Title, "articles": res.Articles, "images": res.Images, "bytes": len(res.EPUB)})
}

// ---------- the shared cache ----------

const (
	cacheDepth     = 10               // stories kept per section (the most a reader asks for)
	imagesPerRun   = 120              // pictures fetched per refresh, the rest wait for the next one
	refreshBudget  = 11 * time.Minute // stop fetching in time for the next 15-minute run
	lockSlug       = "_lock"
	staleLockAfter = 14 * time.Minute
)

func artKey(id string) string { return "newscache/art/" + id + ".json" }

type cacheRow struct {
	id    int64
	items []map[string]any
	ms    int64
}

func loadCache() (map[string]cacheRow, error) {
	rows, _, err := wmjob.TableQuery("news_cache", map[string]any{"limit": 100})
	if err != nil {
		return nil, err
	}
	out := map[string]cacheRow{}
	for _, r := range rows {
		cr := cacheRow{id: num(r["id"]), ms: num(r["fetched_ms"])}
		if list, ok := r["items"].([]any); ok {
			for _, it := range list {
				if m, ok := it.(map[string]any); ok {
					cr.items = append(cr.items, m)
				}
			}
		}
		out[str(r["slug"])] = cr
	}
	return out, nil
}

func saveRow(cache map[string]cacheRow, slug string, items []any, ms int64) error {
	if row, ok := cache[slug]; ok && row.id > 0 {
		_, err := wmjob.TablePatch("news_cache", row.id, map[string]any{"items": items, "fetched_ms": ms})
		return err
	}
	_, err := wmjob.TableInsert("news_cache", map[string]any{"slug": slug, "items": items, "fetched_ms": ms})
	return err
}

// refresh fetches the stories of every section someone reads that the cache does not hold yet.
func refresh(now time.Time) {
	start := time.Now()
	cache, err := loadCache()
	if err != nil {
		fail(err)
	}
	// One refresh at a time: a run still going (and not stale) means this one has nothing to do.
	if lock, ok := cache[lockSlug]; ok && len(lock.items) > 0 && lock.items[0]["running"] == true &&
		start.UnixMilli()-lock.ms < staleLockAfter.Milliseconds() {
		wmjob.SetResult(map[string]any{"skipped": "another refresh is running"})
		return
	}
	if err := saveRow(cache, lockSlug, []any{map[string]any{"running": true}}, start.UnixMilli()); err != nil {
		fail(err)
	}
	defer func() {
		cache, _ = loadCache()
		_ = saveRow(cache, lockSlug, []any{map[string]any{"running": false}}, time.Now().UnixMilli())
	}()

	// Sections someone reads (the defaults when nobody has chosen yet).
	want := map[string]bool{}
	prefs, _, err := wmjob.TableQuery("news_prefs", map[string]any{
		"filters": []map[string]any{{"column": "enabled", "op": "eq", "value": true}}, "limit": 1000})
	if err != nil {
		fail(err)
	}
	for _, p := range prefs {
		for _, s := range strings.Split(str(p["sections"]), ",") {
			if s = strings.TrimSpace(s); s != "" {
				want[s] = true
			}
		}
	}
	if len(want) == 0 {
		for _, s := range defaultSections {
			want[s] = true
		}
	}

	known := map[string]bool{}
	for slug, row := range cache {
		if slug == lockSlug {
			continue
		}
		for _, it := range row.items {
			known[str(it["id"])] = true
		}
	}
	client := &http.Client{Transport: wmjob.Transport{Timeout: 60 * time.Second}}
	kept := map[string]bool{}
	fetched, pictures := 0, 0
	for _, sec := range news.Sections {
		if !want[sec.Slug] {
			continue
		}
		items, ferr := news.FetchFeed(client, sec.Slug)
		if ferr != nil {
			wmjob.Log("warn", fmt.Sprintf("feed %s: %v", sec.Slug, ferr))
			for _, it := range cache[sec.Slug].items {
				kept[str(it["id"])] = true // keep what the section had
			}
			continue
		}
		list := []any{}
		for _, it := range items {
			if len(list) == cacheDepth {
				break
			}
			id := news.ArticleID(it.Link)
			if !known[id] {
				if time.Since(start) > refreshBudget {
					break
				}
				a, aerr := news.FetchArticle(client, sec, it)
				if aerr != nil {
					wmjob.Log("warn", fmt.Sprintf("article: %v", aerr))
					continue
				}
				n := 0
				for bi := range a.Blocks {
					b := &a.Blocks[bi]
					b.ImgFile = ""
					if b.ImgURL == "" || n == news.MaxImagesPerArticle || pictures >= imagesPerRun {
						continue
					}
					small, ierr := news.FetchImage(client, b.ImgURL)
					if ierr != nil {
						continue
					}
					key := fmt.Sprintf("newscache/img/%s-%d.jpg", id, n+1)
					if _, perr := wmjob.StoragePut(key, small, "image/jpeg", "private"); perr != nil {
						continue
					}
					b.ImgFile = key
					n++
					pictures++
				}
				data, _ := json.Marshal(news.CachedArticle{ID: id, Slug: sec.Slug, Title: a.Title, Lead: a.Lead, URL: a.URL, Pub: a.Pub, Blocks: a.Blocks})
				if _, perr := wmjob.StoragePut(artKey(id), data, "application/json", "private"); perr != nil {
					wmjob.Log("warn", perr.Error())
					continue
				}
				known[id] = true
				fetched++
			}
			kept[id] = true
			list = append(list, map[string]any{"id": id, "title": it.Title})
		}
		if err := saveRow(cache, sec.Slug, list, time.Now().UnixMilli()); err != nil {
			fail(err)
		}
	}
	// Stories no section lists any more leave the cache (a day or two after they appeared).
	dropped := 0
	for id := range known {
		if kept[id] {
			continue
		}
		_ = wmjob.StorageDelete(artKey(id))
		for i := 1; i <= news.MaxImagesPerArticle; i++ {
			_ = wmjob.StorageDelete(fmt.Sprintf("newscache/img/%s-%d.jpg", id, i))
		}
		dropped++
	}
	wmjob.SetResult(map[string]any{"fetched": fetched, "pictures": pictures, "dropped": dropped,
		"seconds": int(time.Since(start).Seconds())})
}

// packFromCache makes a reader's issue from the cached stories of their sections.
func packFromCache(sections []string, per int, images bool, now time.Time) (news.Result, error) {
	cache, err := loadCache()
	if err != nil {
		return news.Result{}, err
	}
	var articles []news.Article
	var pics []news.Image
	seen := map[string]bool{}
	for _, slug := range sections {
		sec, ok := news.SectionBySlug(slug)
		if !ok {
			continue
		}
		got := 0
		for _, it := range cache[slug].items {
			if got == per {
				break
			}
			id := str(it["id"])
			if id == "" || seen[id] {
				continue
			}
			raw, _, gerr := wmjob.StorageGet(artKey(id))
			if gerr != nil {
				continue
			}
			var ca news.CachedArticle
			if json.Unmarshal(raw, &ca) != nil {
				continue
			}
			a := news.Article{Section: sec, Title: ca.Title, Lead: ca.Lead, URL: ca.URL, Pub: ca.Pub, Blocks: ca.Blocks}
			n := 0
			for bi := range a.Blocks {
				b := &a.Blocks[bi]
				key := b.ImgFile
				b.ImgFile = ""
				if !images || key == "" || len(pics) >= news.MaxImages {
					continue
				}
				data, _, ierr := wmjob.StorageGet(key)
				if ierr != nil {
					continue
				}
				n++
				b.ImgFile = fmt.Sprintf("a%03d-%d.jpg", len(articles)+1, n)
				pics = append(pics, news.Image{File: b.ImgFile, Data: data})
			}
			seen[id] = true
			articles = append(articles, a)
			got++
		}
	}
	if len(articles) == 0 {
		return news.Result{}, fmt.Errorf("chưa có tin trong bộ đệm, thử lại sau 15 phút")
	}
	return news.Pack(now, articles, pics)
}
