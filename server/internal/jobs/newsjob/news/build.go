package news

import (
	"fmt"
	"net/http"
	"time"
)

// Options say what goes into one issue.
type Options struct {
	Sections   []string // section slugs, in the order to read them
	PerSection int      // articles per section
	Images     bool
	Date       time.Time // issue date (local time of the reader)
	Log        func(string)
	Progress   func(stage string, pct float64)
}

// Result is one finished issue.
type Result struct {
	Title    string
	EPUB     []byte
	Articles int
	Images   int
}

const (
	maxImagesPerArticle = 3
	maxImages           = 60
	imageWidth          = 560
)

// Build fetches the sections and makes the EPUB. Articles that fail to load are skipped;
// an issue with no article at all is an error.
func Build(c *http.Client, opt Options) (Result, error) {
	logf := func(f string, a ...any) {
		if opt.Log != nil {
			opt.Log(fmt.Sprintf(f, a...))
		}
	}
	progress := func(stage string, pct float64) {
		if opt.Progress != nil {
			opt.Progress(stage, pct)
		}
	}
	if opt.PerSection <= 0 {
		opt.PerSection = 5
	}
	var articles []Article
	seen := map[string]bool{}
	for si, slug := range opt.Sections {
		sec, ok := sectionBySlug(slug)
		if !ok {
			continue
		}
		progress("tin "+sec.Name, 0.05+0.55*float64(si)/float64(len(opt.Sections)))
		items, err := FetchFeed(c, slug)
		if err != nil {
			logf("feed %s: %v", slug, err)
			continue
		}
		got := 0
		for _, it := range items {
			if got == opt.PerSection {
				break
			}
			if seen[it.Link] {
				continue
			}
			a, err := FetchArticle(c, sec, it)
			if err != nil {
				logf("article: %v", err)
				continue
			}
			seen[it.Link] = true
			articles = append(articles, a)
			got++
		}
	}
	if len(articles) == 0 {
		return Result{}, fmt.Errorf("no article could be read")
	}

	var images []Image
	if opt.Images {
		progress("ảnh", 0.65)
		for ai := range articles {
			n := 0
			for bi := range articles[ai].Blocks {
				b := &articles[ai].Blocks[bi]
				if b.ImgURL == "" || n == maxImagesPerArticle || len(images) == maxImages {
					continue
				}
				raw, err := get(c, b.ImgURL, 12<<20)
				if err != nil {
					logf("image: %v", err)
					continue
				}
				small, err := Shrink(raw, imageWidth)
				if err != nil {
					logf("image %s: %v", b.ImgURL, err)
					continue
				}
				b.ImgFile = fmt.Sprintf("a%03d-%d.jpg", ai+1, n+1)
				images = append(images, Image{File: b.ImgFile, Data: small})
				n++
			}
		}
	}

	progress("đóng sách", 0.9)
	title := fmt.Sprintf("Báo sáng %d/%d", opt.Date.Day(), int(opt.Date.Month()))
	cover, err := Cover(opt.Date, articles)
	if err != nil {
		return Result{}, fmt.Errorf("cover: %w", err)
	}
	epub, err := EPUB(title, opt.Date, articles, cover, images)
	if err != nil {
		return Result{}, err
	}
	return Result{Title: title, EPUB: epub, Articles: len(articles), Images: len(images)}, nil
}
