package news

import (
	"bytes"
	"image"
	"image/color"
	"image/jpeg"
	_ "image/png" // VnExpress serves a few PNG graphics
	"math"

	_ "golang.org/x/image/webp" // its CDN often answers WebP, whatever the URL says
)

// Shrink decodes a JPEG, PNG or WebP, converts it to grayscale, scales it down to at most maxW
// pixels wide (area average, so fine detail does not alias on e-ink) with a gentle contrast
// lift, and encodes a baseline JPEG the X4's decoder handles.
func Shrink(data []byte, maxW int) ([]byte, error) {
	src, _, err := image.Decode(bytes.NewReader(data))
	if err != nil {
		return nil, err
	}
	b := src.Bounds()
	sw, sh := b.Dx(), b.Dy()
	if sw <= 0 || sh <= 0 {
		return nil, image.ErrFormat
	}
	dw := sw
	if dw > maxW {
		dw = maxW
	}
	dh := int(math.Round(float64(sh) * float64(dw) / float64(sw)))
	if dh < 1 {
		dh = 1
	}
	// Luma of the source, once.
	lum := make([]float32, sw*sh)
	switch im := src.(type) {
	case *image.YCbCr: // JPEG: the Y plane is the luma already
		for y := 0; y < sh; y++ {
			for x := 0; x < sw; x++ {
				lum[y*sw+x] = float32(im.Y[im.YOffset(b.Min.X+x, b.Min.Y+y)])
			}
		}
	case *image.Gray:
		for y := 0; y < sh; y++ {
			for x := 0; x < sw; x++ {
				lum[y*sw+x] = float32(im.GrayAt(b.Min.X+x, b.Min.Y+y).Y)
			}
		}
	default:
		for y := 0; y < sh; y++ {
			for x := 0; x < sw; x++ {
				r, g, bl, a := src.At(b.Min.X+x, b.Min.Y+y).RGBA()
				// Transparent PNG areas show as white paper.
				v := float32(0.2126*float64(r)+0.7152*float64(g)+0.0722*float64(bl)) / 257
				v += float32(0xffff-a) / 257
				if v > 255 {
					v = 255
				}
				lum[y*sw+x] = v
			}
		}
	}
	dst := image.NewGray(image.Rect(0, 0, dw, dh))
	fx := float64(sw) / float64(dw)
	fy := float64(sh) / float64(dh)
	for y := 0; y < dh; y++ {
		y0 := int(float64(y) * fy)
		y1 := int(float64(y+1) * fy)
		if y1 <= y0 {
			y1 = y0 + 1
		}
		for x := 0; x < dw; x++ {
			x0 := int(float64(x) * fx)
			x1 := int(float64(x+1) * fx)
			if x1 <= x0 {
				x1 = x0 + 1
			}
			var sum float32
			n := 0
			for yy := y0; yy < y1 && yy < sh; yy++ {
				row := yy * sw
				for xx := x0; xx < x1 && xx < sw; xx++ {
					sum += lum[row+xx]
					n++
				}
			}
			v := float64(sum) / float64(n)
			// Mild S-curve: e-ink grays look washed out otherwise.
			v = 255 * (0.5 + 0.5*math.Tanh(1.6*(v/255-0.5))/math.Tanh(0.8))
			if v < 0 {
				v = 0
			}
			if v > 255 {
				v = 255
			}
			dst.SetGray(x, y, color.Gray{Y: uint8(v)})
		}
	}
	var out bytes.Buffer
	if err := jpeg.Encode(&out, dst, &jpeg.Options{Quality: 72}); err != nil {
		return nil, err
	}
	return out.Bytes(), nil
}
