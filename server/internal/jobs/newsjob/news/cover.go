package news

import (
	"bytes"
	_ "embed"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/jpeg"
	"strings"
	"sync"
	"time"

	"golang.org/x/image/font"
	"golang.org/x/image/font/opentype"
	"golang.org/x/image/math/fixed"
)

// Noto fonts (SIL Open Font License, fonts/OFL.txt) for the cover's Vietnamese text.
var (
	//go:embed fonts/NotoSans-Bold.ttf
	sansBoldTTF []byte
	//go:embed fonts/NotoSans-Regular.ttf
	sansTTF []byte
	//go:embed fonts/NotoSerif-Bold.ttf
	serifBoldTTF []byte
)

var (
	fontsOnce              sync.Once
	sansBold, sans, serifB *opentype.Font
	fontsErr               error
)

func loadFonts() error {
	fontsOnce.Do(func() {
		if sansBold, fontsErr = opentype.Parse(sansBoldTTF); fontsErr != nil {
			return
		}
		if sans, fontsErr = opentype.Parse(sansTTF); fontsErr != nil {
			return
		}
		serifB, fontsErr = opentype.Parse(serifBoldTTF)
	})
	return fontsErr
}

func face(f *opentype.Font, px float64) font.Face {
	fc, _ := opentype.NewFace(f, &opentype.FaceOptions{Size: px, DPI: 72, Hinting: font.HintingFull})
	return fc
}

var weekdaysVI = []string{"Chủ nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"}

// text draws s with its baseline at y; letter spacing in px. Returns the advance.
func text(dst draw.Image, fc font.Face, x, y int, s string, spacing int) int {
	d := &font.Drawer{Dst: dst, Src: image.NewUniform(color.Gray{Y: 17}), Face: fc, Dot: fixed.P(x, y)}
	if spacing == 0 {
		d.DrawString(s)
		return d.Dot.X.Round() - x
	}
	for _, r := range s {
		d.DrawString(string(r))
		d.Dot.X += fixed.I(spacing)
	}
	return d.Dot.X.Round() - x
}

// wrapLines breaks s into at most max lines of width w (the last one ellipsized).
func wrapLines(fc font.Face, s string, w, max int) []string {
	words := strings.Fields(s)
	var lines []string
	cur := ""
	for i, word := range words {
		try := word
		if cur != "" {
			try = cur + " " + word
		}
		if font.MeasureString(fc, try).Round() <= w {
			cur = try
			continue
		}
		if cur != "" {
			lines = append(lines, cur)
		}
		cur = word
		if len(lines) == max-1 {
			rest := strings.Join(words[i:], " ")
			for font.MeasureString(fc, rest+"…").Round() > w && len([]rune(rest)) > 1 {
				r := []rune(rest)
				rest = string(r[:len(r)-1])
			}
			if rest != strings.Join(words[i:], " ") {
				rest = strings.TrimSpace(rest) + "…"
			}
			return append(lines, rest)
		}
	}
	if cur != "" {
		lines = append(lines, cur)
	}
	return lines
}

// Cover draws the issue's 480x800 cover: masthead, big date, weekday and count, then the
// lead headline of each section, as far as they fit.
func Cover(date time.Time, articles []Article) ([]byte, error) {
	if err := loadFonts(); err != nil {
		return nil, err
	}
	const W, H, M = 480, 800, 34
	img := image.NewGray(image.Rect(0, 0, W, H))
	draw.Draw(img, img.Bounds(), image.NewUniform(color.Gray{Y: 255}), image.Point{}, draw.Src)
	ink := image.NewUniform(color.Gray{Y: 17})

	y := 70
	text(img, face(sansBold, 19), M, y, "VNEXPRESS · BÁO SÁNG", 3)
	y += 128
	text(img, face(serifB, 128), M-4, y, fmt.Sprintf("%d/%d", date.Day(), int(date.Month())), 0)
	y += 46
	text(img, face(sans, 26), M, y, fmt.Sprintf("%s, %d · %d bài", weekdaysVI[date.Weekday()], date.Year(), len(articles)), 0)
	y += 26
	draw.Draw(img, image.Rect(M, y, W-M, y+5), ink, image.Point{}, draw.Src)
	y += 24

	kick := face(sansBold, 16)
	head := face(sans, 23)
	seen := map[string]bool{}
	for _, a := range articles {
		if seen[a.Section.Slug] {
			continue
		}
		lines := wrapLines(head, a.Title, W-2*M, 2)
		need := 22 + len(lines)*30 + 16
		if y+need > H-30 {
			break
		}
		seen[a.Section.Slug] = true
		y += 18
		text(img, kick, M, y, strings.ToUpper(a.Section.Name), 1)
		y += 6
		for _, l := range lines {
			y += 30
			text(img, head, M, y, l, 0)
		}
		y += 14
		draw.Draw(img, image.Rect(M, y, W-M, y+2), ink, image.Point{}, draw.Src)
	}
	var out bytes.Buffer
	if err := jpeg.Encode(&out, img, &jpeg.Options{Quality: 88}); err != nil {
		return nil, err
	}
	return out.Bytes(), nil
}
