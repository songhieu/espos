package news

import (
	"fmt"
	"hash/fnv"
	"net/http"
	"time"
)

// CachedArticle is a story as the server keeps it between issues: fetched once by the
// 15-minute refresh, its pictures already shrunk and stored (Block.ImgFile holds their
// storage keys). Issues are packed from these without going back to VnExpress.
type CachedArticle struct {
	ID     string
	Slug   string
	Title  string
	Lead   string
	URL    string
	Pub    time.Time
	Blocks []Block
}

// ArticleID names a story by its link (stable across feeds and runs).
func ArticleID(link string) string {
	h := fnv.New64a()
	_, _ = h.Write([]byte(link))
	return fmt.Sprintf("%016x", h.Sum64())
}

// SectionBySlug finds a section of the catalogue.
func SectionBySlug(slug string) (Section, bool) { return sectionBySlug(slug) }

// FetchImage downloads a picture and shrinks it to the issue's width (grayscale JPEG).
func FetchImage(c *http.Client, url string) ([]byte, error) {
	raw, err := get(c, url, 12<<20)
	if err != nil {
		return nil, err
	}
	return Shrink(raw, imageWidth)
}

// Pack makes the issue (cover, chapters, table of contents) from ready articles and pictures.
func Pack(date time.Time, articles []Article, images []Image) (Result, error) {
	if len(articles) == 0 {
		return Result{}, fmt.Errorf("no article")
	}
	title := fmt.Sprintf("Báo sáng %d/%d", date.Day(), int(date.Month()))
	cover, err := Cover(date, articles)
	if err != nil {
		return Result{}, fmt.Errorf("cover: %w", err)
	}
	epub, err := EPUB(title, date, articles, cover, images)
	if err != nil {
		return Result{}, err
	}
	return Result{Title: title, EPUB: epub, Articles: len(articles), Images: len(images)}, nil
}

// MaxImagesPerArticle and MaxImages bound an issue's pictures.
const (
	MaxImagesPerArticle = maxImagesPerArticle
	MaxImages           = maxImages
)
