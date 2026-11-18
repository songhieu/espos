// The run's pixel loops (draw.h), built for speed (the rest of the run is built for size).
#include "draw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace run {
namespace draw {
namespace {

using media::ROAD_H;
using media::ROAD_W;

// Smooth scale: the standard 2x interpolation. A screen pixel on a source pixel is that pixel; one between two is
// their average (per colour, two pixels at once in a 32-bit word). Odd screen rows: the average of the rows around.
inline uint32_t avg2(uint32_t a, uint32_t b) { return (a & b) + (((a ^ b) & 0xF7DEF7DEu) >> 1); }
inline uint16_t avg1(uint16_t a, uint16_t b) { return (uint16_t)((a & b) + (((a ^ b) & 0xF7DEu) >> 1)); }

// Source row j across, 320 screen pixels from column X0 of the wide world.
void hRow(const uint16_t* s, int X0, uint16_t* out) {
  const int k0 = X0 >> 1, last = ROAD_W - 1;
  uint32_t* o = (uint32_t*)out;
  if (!(X0 & 1)) {  // screen pixel 0 on source pixel k0: (k, between k and k+1) pairs
    for (int i = 0; i < W / 2; i++) {
      const int k = k0 + i;
      const uint16_t a = s[k], b = s[k < last ? k + 1 : last];
      o[i] = a | (uint32_t)avg1(a, b) << 16;
    }
  } else {  // screen pixel 0 between k0 and k0+1: (between, k+1) pairs
    for (int i = 0; i < W / 2; i++) {
      const int k = k0 + i, k1 = k < last ? k + 1 : last;
      o[i] = avg1(s[k], s[k1]) | (uint32_t)s[k1] << 16;
    }
  }
}

const uint16_t* hOf(Smooth& s, int j) {  // source row j across, kept for the next screen rows
  j = std::max(0, std::min(ROAD_H - 1, j));
  const int k = j & 1;  // rows j and j+1 are needed together: two buffers
  uint16_t* r = (uint16_t*)(s.rows + k * W);
  if (s.tag[k] != j) hRow(s.src + j * ROAD_W, s.X0, r), s.tag[k] = j;
  return r;
}

uint8_t blobLut[257];

// Fast blend for many pixels: alpha in 32 steps, the three colours spread in one 32-bit word (5 spare bits each),
// so one multiply-add does all three. Within 1/32 of the exact blend (UI pieces, shadows, sprite edges).
inline uint32_t spread5(uint16_t c) { return (c | (uint32_t)c << 16) & 0x07E0F81Fu; }
inline uint16_t mix5(uint16_t d, uint32_t sSpread, int a5) {  // a5 = 0..32
  const uint32_t v = ((spread5(d) * (32 - a5) + sSpread * a5) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(v | v >> 16);
}

}  // namespace

void roadPlain(const Canvas& c, const uint16_t* src, int X0) {
  for (int y = c.y0; y < c.y1; y++) {
    uint16_t* d = c.row(y);
    if ((y & 1) && y > c.y0) {  // the second row of a source row: a copy
      memcpy(d, d - W, W * 2);
      continue;
    }
    const uint16_t* s = src + (y >> 1) * ROAD_W + (X0 >> 1);
    int x = 0;
    if (X0 & 1) d[x++] = *s++;
    uint32_t* d2 = (uint32_t*)(d + x);  // x even from here: 4-byte aligned
    for (int k = 0; k < (W - x) / 2; k++) d2[k] = s[k] | (uint32_t)s[k] << 16;
    if ((W - x) & 1) d[W - 1] = s[(W - x) / 2];
  }
}

void roadSmooth(const Canvas& c, Smooth& s) {
  for (int y = c.y0; y < c.y1; y++) {
    const int j = y >> 1;
    uint32_t* d = (uint32_t*)c.row(y);
    const uint32_t* a = (const uint32_t*)hOf(s, j);
    if (!(y & 1)) {
      memcpy(d, a, W * 2);
    } else {
      const uint32_t* b = (const uint32_t*)hOf(s, j + 1);
      for (int i = 0; i < W / 2; i++) d[i] = avg2(a[i], b[i]);
    }
  }
}

void fill(const Canvas& c, uint16_t color) {
  for (int y = c.y0; y < c.y1; y++) {
    uint16_t* d = c.row(y);
    for (int x = 0; x < W; x++) d[x] = color;
  }
}

void blobInit() {
  for (int i = 0; i <= 256; i++) {
    const float d = sqrtf(i / 256.0f);
    float t = (d - 0.2f) / 0.8f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    blobLut[i] = (uint8_t)lroundf(255 * (1 - t));
  }
}

// The page's blob(): a radial gradient (alpha a at 0.2 rx, nothing at rx), squashed to ry. Per row the
// normalised distance squared steps along in 16.16 fixed point (two adds a pixel). Float only: the chip has no
// double unit.
const uint8_t* blobAlpha(float a) {  // alpha 0..32 by q (0..256) for this strength; a few strengths kept
  static float keyA[4] = {-1, -1, -1, -1};
  static uint8_t lut[4][257];
  static int next = 0;
  for (int k = 0; k < 4; k++)
    if (keyA[k] == a) return lut[k];
  const int k = next++ & 3;
  keyA[k] = a;
  for (int i = 0; i <= 256; i++) lut[k][i] = (uint8_t)(blobLut[i] * a * (32.0f / 255.0f) + 0.5f);
  return lut[k];
}

void blob(const Canvas& c, double cxd, double cyd, double rxd, double ryd, double ad) {
  const float cx = (float)cxd, cy = (float)cyd, rx = (float)rxd, ry = (float)ryd;
  if (rx <= 0 || ry <= 0) return;
  const int y0 = std::max(c.y0, (int)floorf(cy - ry)), y1 = std::min(c.y1 - 1, (int)ceilf(cy + ry));
  const int x0 = std::max(0, (int)floorf(cx - rx)), x1 = std::min(W - 1, (int)ceilf(cx + rx));
  if (y0 > y1 || x0 > x1) return;
  const uint8_t* lut = blobAlpha((float)ad);
  static const uint32_t COL = spread5(rgb565(40, 30, 20));
  const float k = rx / ry, inv = 256.0f / (rx * rx), f16 = inv * 65536.0f;
  const int32_t ddq = (int32_t)(2 * f16);
  for (int y = y0; y <= y1; y++) {
    const float dy = (y + 0.5f - cy) * k, r2 = rx * rx - dy * dy;  // inside the ellipse: |dx| < sqrt(r2)
    if (r2 <= 0) continue;
    const float hw = sqrtf(r2);
    const int xa = std::max(x0, (int)floorf(cx - hw)), xb = std::min(x1, (int)ceilf(cx + hw));
    if (xa > xb) continue;
    const float dx = xa + 0.5f - cx;
    int32_t q = (int32_t)((dx * dx + dy * dy) * f16), dq = (int32_t)((2 * dx + 1) * f16);
    uint16_t* row = c.row(y);
    for (int x = xa; x <= xb; x++, q += dq, dq += ddq) {
      const uint32_t qi = (uint32_t)q >> 16;
      if (qi >= 256) continue;
      const int v = lut[qi];
      if (v) row[x] = mix5(row[x], COL, v);
    }
  }
}

void sprite(const Canvas& c, const media::Sprite* s, int X, int Y, int opa) {
  if (!s) return;
  const int c0 = std::max(0, -X), c1 = std::min(s->w, W - X);
  const int r0 = std::max(0, c.y0 - Y), r1 = std::min(s->h, c.y1 - Y);
  for (int r = r0; r < r1; r++) {
    const uint16_t* px = s->rgb + r * s->w;
    const uint8_t* a = s->a + r * s->w;
    uint16_t* d = c.row(Y + r) + X;
    if (opa >= 255) {
      for (int k = c0; k < c1; k++) {
        const int al = a[k];
        if (al == 255) d[k] = px[k];
        else if (al) d[k] = mix5(d[k], spread5(px[k]), (al + 4) >> 3);
      }
    } else {
      for (int k = c0; k < c1; k++)
        if (a[k]) d[k] = mix5(d[k], spread5(px[k]), (a[k] * opa / 255 + 4) >> 3);
    }
  }
}

void spriteScaled(const Canvas& c, const media::Sprite* s, int X, int Y, int dw, int dh, int opa) {
  if (!s || dw <= 0 || dh <= 0) return;
  if (dw == s->w && dh == s->h) return sprite(c, s, X, Y, opa);
  const int c0 = std::max(0, -X), c1 = std::min(dw, W - X);
  const int r0 = std::max(0, c.y0 - Y), r1 = std::min(dh, c.y1 - Y);
  if (c0 >= c1) return;
  // 16.16 steps through the source; each output pixel takes the source pixel under its centre (always inside).
  const uint32_t stepX = ((uint32_t)s->w << 16) / dw, stepY = ((uint32_t)s->h << 16) / dh;
  for (int r = r0; r < r1; r++) {
    const int sr = (int)(((uint32_t)r * stepY + stepY / 2) >> 16);
    const uint16_t* px = s->rgb + sr * s->w;
    const uint8_t* a = s->a + sr * s->w;
    uint16_t* d = c.row(Y + r) + X;
    uint32_t sx = (uint32_t)c0 * stepX + stepX / 2;
    for (int k = c0; k < c1; k++, sx += stepX) {
      const int i = (int)(sx >> 16);
      const int al = opa >= 255 ? a[i] : a[i] * opa / 255;
      if (al >= 255) d[k] = px[i];
      else if (al) d[k] = mix5(d[k], spread5(px[i]), (al + 4) >> 3);
    }
  }
}

void piece(const Canvas& c, const media::Ui& ui, const pbu_piece_t* p, int x, int y) {
  if (!p) return;
  const uint8_t* px = ui.pixels(p);
  const uint8_t* al = ui.alpha(p);
  if (!px) return;  // a start card that could not be read
  const int c0 = std::max(0, -x), c1 = std::min((int)p->w, W - x);
  const int r0 = std::max(0, c.y0 - y), r1 = std::min((int)p->h, c.y1 - y);
  for (int r = r0; r < r1; r++) {
    uint16_t* d = c.row(y + r) + x;
    const uint16_t* pr = (const uint16_t*)px + r * p->w;  // Ui keeps every piece 2-byte aligned
    const uint8_t* ar = al ? al + r * p->w : nullptr;
    for (int k = c0; k < c1; k++) {
      const int a = ar ? ar[k] : 255;
      if (!a) continue;
      d[k] = a == 255 ? pr[k] : mix5(d[k], spread5(pr[k]), (a + 4) >> 3);
    }
  }
}

void bar(const Canvas& c, int x, int y, int w, int h, uint16_t color, int opa) {
  for (int r = std::max(0, c.y0 - y); r < std::min(h, c.y1 - y); r++) {
    const int in = r == 0 || r == h - 1;  // rounded ends
    uint16_t* d = c.row(y + r);
    for (int k = x + in; k < x + w - in; k++) d[k] = mix5(d[k], spread5(color), (opa + 4) >> 3);
  }
}

}  // namespace draw
}  // namespace run
