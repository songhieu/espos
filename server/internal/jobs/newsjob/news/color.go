package news

import (
	"bytes"
	"image"
	"image/draw"
	"image/jpeg"
	"net/http"
)

// The colour desk (ES3C35P, 320x480) reads its News app's pictures as baseline JPEGs: its
// decoder (TJpgDec) cannot read progressive ones, which is all Go's encoder writes anyway.
const (
	ColorWidth    = 320       // the most a colour picture is wide
	ColorMaxH     = 1600      // and tall (very long infographics are scaled down further)
	ColorMaxBytes = 60 * 1024 // the most one colour picture weighs
	ThumbSize     = 96        // the list's square thumbnails
)

// Pictures are the stored versions of one picture of a story.
type Pictures struct {
	Gray           []byte // the X4's grayscale JPEG (Shrink)
	Color          []byte // the colour desk's, ≤ ColorWidth wide, ≤ ColorMaxBytes; nil when it could not be made
	ColorW, ColorH int    // its size in pixels
	Thumb          []byte // a ThumbSize square, cut from the middle; only when asked, nil when it could not be made
}

// FetchPictures downloads a picture once and makes the X4's grayscale version (an error when
// that fails, as FetchImage), the colour desk's and, when thumb is set, its square thumbnail.
func FetchPictures(c *http.Client, url string, thumb bool) (Pictures, error) {
	raw, err := get(c, url, 12<<20)
	if err != nil {
		return Pictures{}, err
	}
	gray, err := Shrink(raw, imageWidth)
	if err != nil {
		return Pictures{}, err
	}
	p := Pictures{Gray: gray}
	src, _, derr := image.Decode(bytes.NewReader(raw))
	if derr != nil {
		return p, nil
	}
	if col, w, h, cerr := ColorJPEG(src, ColorWidth, ColorMaxH, ColorMaxBytes); cerr == nil {
		p.Color, p.ColorW, p.ColorH = col, w, h
	}
	if thumb {
		if t, terr := ThumbJPEG(src, ThumbSize); terr == nil {
			p.Thumb = t
		}
	}
	return p, nil
}

// flatten draws src on white (transparent PNG areas show as white) into plain RGBA.
func flatten(src image.Image, r image.Rectangle) *image.RGBA {
	dst := image.NewRGBA(image.Rect(0, 0, r.Dx(), r.Dy()))
	draw.Draw(dst, dst.Bounds(), image.White, image.Point{}, draw.Src)
	draw.Draw(dst, dst.Bounds(), src, r.Min, draw.Over)
	return dst
}

// boxScale shrinks src to dw x dh by averaging every source pixel a target pixel covers
// (no aliasing on fine detail). Never enlarges: dw, dh must be ≤ the source size.
func boxScale(src *image.RGBA, dw, dh int) *image.RGBA {
	sw, sh := src.Rect.Dx(), src.Rect.Dy()
	dst := image.NewRGBA(image.Rect(0, 0, dw, dh))
	fx := float64(sw) / float64(dw)
	fy := float64(sh) / float64(dh)
	for y := 0; y < dh; y++ {
		y0 := int(float64(y) * fy)
		y1 := int(float64(y+1) * fy)
		if y1 <= y0 {
			y1 = y0 + 1
		}
		if y1 > sh {
			y1 = sh
		}
		for x := 0; x < dw; x++ {
			x0 := int(float64(x) * fx)
			x1 := int(float64(x+1) * fx)
			if x1 <= x0 {
				x1 = x0 + 1
			}
			if x1 > sw {
				x1 = sw
			}
			var r, g, b, n uint32
			for yy := y0; yy < y1; yy++ {
				row := src.Pix[yy*src.Stride:]
				for xx := x0; xx < x1; xx++ {
					r += uint32(row[xx*4])
					g += uint32(row[xx*4+1])
					b += uint32(row[xx*4+2])
					n++
				}
			}
			o := y*dst.Stride + x*4
			dst.Pix[o] = uint8((r + n/2) / n)
			dst.Pix[o+1] = uint8((g + n/2) / n)
			dst.Pix[o+2] = uint8((b + n/2) / n)
			dst.Pix[o+3] = 255
		}
	}
	return dst
}

// encodeUnder writes a baseline JPEG at quality 80, lowering it until the file fits in maxBytes;
// nil when even quality 40 does not.
func encodeUnder(img image.Image, maxBytes int) []byte {
	for _, q := range []int{80, 72, 64, 56, 48, 40} {
		var out bytes.Buffer
		if jpeg.Encode(&out, img, &jpeg.Options{Quality: q}) != nil {
			return nil
		}
		if out.Len() <= maxBytes {
			return out.Bytes()
		}
	}
	return nil
}

// ColorJPEG scales a picture down to at most maxW x maxH (keeping its shape, never enlarging)
// and encodes a baseline colour JPEG of at most maxBytes, shrinking further when needed;
// it answers the JPEG and its size.
func ColorJPEG(src image.Image, maxW, maxH, maxBytes int) ([]byte, int, int, error) {
	b := src.Bounds()
	sw, sh := b.Dx(), b.Dy()
	if sw <= 0 || sh <= 0 {
		return nil, 0, 0, image.ErrFormat
	}
	flat := flatten(src, b)
	scale := 1.0
	if sw > maxW {
		scale = float64(maxW) / float64(sw)
	}
	if float64(sh)*scale > float64(maxH) {
		scale = float64(maxH) / float64(sh)
	}
	for try := 0; try < 6; try++ {
		dw := int(float64(sw)*scale + 0.5)
		dh := int(float64(sh)*scale + 0.5)
		if dw < 1 {
			dw = 1
		}
		if dh < 1 {
			dh = 1
		}
		img := flat
		if dw != sw || dh != sh {
			img = boxScale(flat, dw, dh)
		}
		if data := encodeUnder(img, maxBytes); data != nil {
			return data, dw, dh, nil
		}
		scale *= 0.8
	}
	return nil, 0, 0, image.ErrFormat
}

// ThumbJPEG cuts the middle square of a picture (cover crop) and scales it to size x size.
func ThumbJPEG(src image.Image, size int) ([]byte, error) {
	b := src.Bounds()
	side := b.Dx()
	if b.Dy() < side {
		side = b.Dy()
	}
	if side <= 0 {
		return nil, image.ErrFormat
	}
	x0 := b.Min.X + (b.Dx()-side)/2
	y0 := b.Min.Y + (b.Dy()-side)/2
	sq := flatten(src, image.Rect(x0, y0, x0+side, y0+side))
	img := sq
	if side > size {
		img = boxScale(sq, size, size)
	} else if side < size {
		// A tiny picture: stretch it to the square (nearest pixel) so every thumbnail has one size.
		img = image.NewRGBA(image.Rect(0, 0, size, size))
		for y := 0; y < size; y++ {
			for x := 0; x < size; x++ {
				img.Set(x, y, sq.At(x*side/size, y*side/size))
			}
		}
	}
	var out bytes.Buffer
	if err := jpeg.Encode(&out, img, &jpeg.Options{Quality: 80}); err != nil {
		return nil, err
	}
	return out.Bytes(), nil
}
