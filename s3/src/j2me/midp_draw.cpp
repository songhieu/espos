// MIDP drawing on ARGB pixels (midp_draw.h). Written for speed where games spend their time (fill, blit, text);
// arcs and triangles test each pixel of their box (rarely big).
#include "midp_draw.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

namespace j2me {
namespace draw {

namespace {

inline uint32_t opaque(uint32_t rgb) { return 0xFF000000u | (rgb & 0xFFFFFFu); }

// src over dst (dst opaque), a = 0..255
inline uint32_t blend(uint32_t d, uint32_t s, uint32_t a) {
  const uint32_t ia = 255 - a;
  const uint32_t rb = (((s & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080) >> 8) & 0xFF00FF;
  const uint32_t g = (((s & 0x00FF00) * a + (d & 0x00FF00) * ia + 0x008000) >> 8) & 0x00FF00;
  return 0xFF000000u | rb | g;
}

inline bool inClip(const Target& t, int x, int y) {
  return x >= t.cx && y >= t.cy && x < t.cx + t.cw && y < t.cy + t.ch;
}

inline void plot(const Target& t, int x, int y, uint32_t c) {
  if (inClip(t, x, y)) t.px[y * t.w + x] = c;
}

// The clip intersected with x, y, w, h: false when nothing is left.
inline bool clipRect(const Target& t, int& x1, int& y1, int& x2, int& y2) {
  x1 = std::max(x1, t.cx);
  y1 = std::max(y1, t.cy);
  x2 = std::min(x2, t.cx + t.cw);
  y2 = std::min(y2, t.cy + t.ch);
  return x1 < x2 && y1 < y2;
}

void hspan(const Target& t, int x1, int x2, int y, uint32_t c) {  // [x1, x2)
  if (y < t.cy || y >= t.cy + t.ch) return;
  x1 = std::max(x1, t.cx);
  x2 = std::min(x2, t.cx + t.cw);
  if (x1 >= x2) return;
  uint32_t* p = t.px + y * t.w;
  std::fill(p + x1, p + x2, c);
}

}  // namespace

void fill(const Target& t, int x, int y, int w, int h, uint32_t rgb) {
  int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
  if (w <= 0 || h <= 0 || !clipRect(t, x1, y1, x2, y2)) return;
  const uint32_t c = opaque(rgb);
  for (int r = y1; r < y2; r++) std::fill(t.px + r * t.w + x1, t.px + r * t.w + x2, c);
}

void line(const Target& t, int x1, int y1, int x2, int y2, uint32_t rgb, bool dotted) {
  const uint32_t c = opaque(rgb);
  if (y1 == y2 && !dotted) {
    hspan(t, std::min(x1, x2), std::max(x1, x2) + 1, y1, c);
    return;
  }
  const int dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
  int err = dx + dy, n = 0;
  for (;;) {
    if (!dotted || (n & 2) == 0) plot(t, x1, y1, c);  // dotted: 2 on, 2 off
    n++;
    if (x1 == x2 && y1 == y2) break;
    const int e2 = 2 * err;
    if (e2 >= dy) err += dy, x1 += sx;
    if (e2 <= dx) err += dx, y1 += sy;
  }
}

namespace {
// Is the angle a (degrees, 0..360) inside [start, start + sweep) (sweep may be negative)?
bool inSweep(double a, int start, int sweep) {
  if (sweep >= 360 || sweep <= -360) return true;
  double s = start, e = start + sweep;
  if (sweep < 0) std::swap(s, e);
  s = std::fmod(std::fmod(s, 360.0) + 360.0, 360.0);
  e = s + std::abs(sweep);
  return (a >= s && a <= e) || (a + 360 >= s && a + 360 <= e);
}
}  // namespace

void arc(const Target& t, int x, int y, int w, int h, int start, int sweep, uint32_t rgb, bool doFill) {
  if (w < 0 || h < 0 || sweep == 0) return;
  const uint32_t c = opaque(rgb);
  if (!doFill) {
    // the outline: points along the ellipse, joined (box w + 1 x h + 1)
    const double cx = x + w / 2.0, cy = y + h / 2.0, rx = w / 2.0, ry = h / 2.0;
    const int sw = std::max(-360, std::min(360, sweep));
    const int steps = std::max(8, (int)(std::abs(sw) / 360.0 * (rx + ry) * 3.2));
    int px = 0, py = 0;
    for (int i = 0; i <= steps; i++) {
      const double a = (start + (double)sw * i / steps) * kPi / 180.0;
      const int qx = (int)std::lround(cx + rx * std::cos(a)), qy = (int)std::lround(cy - ry * std::sin(a));
      if (i) line(t, px, py, qx, qy, rgb, false);
      else plot(t, qx, qy, c);
      px = qx, py = qy;
    }
    return;
  }
  if (w == 0 || h == 0) return;
  int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
  if (!clipRect(t, x1, y1, x2, y2)) return;
  const double cx = x + w / 2.0, cy = y + h / 2.0, rx = w / 2.0, ry = h / 2.0;
  const bool full = sweep >= 360 || sweep <= -360;
  for (int py = y1; py < y2; py++) {
    const double ny = (py + 0.5 - cy) / ry;
    if (ny * ny > 1) continue;
    const double half = std::sqrt(1 - ny * ny) * rx;  // the ellipse's span on this row
    int sx1 = std::max(x1, (int)std::ceil(cx - half - 0.5)), sx2 = std::min(x2, (int)std::floor(cx + half - 0.5) + 1);
    if (full) {
      hspan(t, sx1, sx2, py, c);
      continue;
    }
    for (int px = sx1; px < sx2; px++) {
      const double nx = (px + 0.5 - cx) / rx;
      double a = std::atan2(-ny, nx) * 180.0 / kPi;  // relative to the box: 45 is the corner
      if (a < 0) a += 360;
      if (inSweep(a, start, sweep)) t.px[py * t.w + px] = c;
    }
  }
}

void triangle(const Target& t, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t rgb) {
  const uint32_t c = opaque(rgb);
  // sort by y, then fill scanlines between the edges (pixel centres)
  if (y2 < y1) std::swap(x1, x2), std::swap(y1, y2);
  if (y3 < y1) std::swap(x1, x3), std::swap(y1, y3);
  if (y3 < y2) std::swap(x2, x3), std::swap(y2, y3);
  if (y1 == y3) {
    hspan(t, std::min({x1, x2, x3}), std::max({x1, x2, x3}) + 1, y1, c);
    return;
  }
  auto xAt = [](int xa, int ya, int xb, int yb, double y) { return yb == ya ? (double)xa : xa + (xb - xa) * (y - ya) / (yb - ya); };
  const int ys = std::max(y1, t.cy), ye = std::min(y3, t.cy + t.ch - 1);
  for (int y = ys; y <= ye; y++) {
    const double fy = y + 0.5 > y3 ? y3 : y + 0.5 < y1 ? y1 : y + 0.5;
    double a = xAt(x1, y1, x3, y3, fy);
    double b = fy < y2 ? xAt(x1, y1, x2, y2, fy) : xAt(x2, y2, x3, y3, fy);
    if (a > b) std::swap(a, b);
    hspan(t, (int)std::floor(a + 0.5), (int)std::floor(b + 0.5) + 1, y, c);
  }
}

void roundRect(const Target& t, int x, int y, int w, int h, int aw, int ah, uint32_t rgb, bool doFill) {
  aw = std::min(aw, w), ah = std::min(ah, h);
  if (aw <= 1 || ah <= 1) {
    if (doFill) fill(t, x, y, w, h, rgb);
    else {
      line(t, x, y, x + w, y, rgb, false);
      line(t, x, y + h, x + w, y + h, rgb, false);
      line(t, x, y, x, y + h, rgb, false);
      line(t, x + w, y, x + w, y + h, rgb, false);
    }
    return;
  }
  if (doFill) {
    const uint32_t c = opaque(rgb);
    const double rx = aw / 2.0, ry = ah / 2.0;
    for (int r = 0; r < h; r++) {
      double inset = 0;
      const double fy = r + 0.5;
      if (fy < ry) inset = rx - rx * std::sqrt(std::max(0.0, 1 - ((ry - fy) / ry) * ((ry - fy) / ry)));
      else if (fy > h - ry) inset = rx - rx * std::sqrt(std::max(0.0, 1 - ((fy - (h - ry)) / ry) * ((fy - (h - ry)) / ry)));
      hspan(t, x + (int)std::lround(inset), x + w - (int)std::lround(inset), y + r, c);
    }
    return;
  }
  // outline: four lines and four quarter arcs (box w + 1 x h + 1)
  line(t, x + aw / 2, y, x + w - aw / 2, y, rgb, false);
  line(t, x + aw / 2, y + h, x + w - aw / 2, y + h, rgb, false);
  line(t, x, y + ah / 2, x, y + h - ah / 2, rgb, false);
  line(t, x + w, y + ah / 2, x + w, y + h - ah / 2, rgb, false);
  arc(t, x, y, aw, ah, 90, 90, rgb, false);
  arc(t, x + w - aw, y, aw, ah, 0, 90, rgb, false);
  arc(t, x, y + h - ah, aw, ah, 180, 90, rgb, false);
  arc(t, x + w - aw, y + h - ah, aw, ah, 270, 90, rgb, false);
}

void blit(const Target& t, const uint32_t* src, int sw, int sh, int sx, int sy, int w, int h, int tr, int dx, int dy,
          bool alpha) {
  if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || sx + w > sw || sy + h > sh) return;
  const bool swap = (tr & 4) != 0;
  const int dW = swap ? h : w, dH = swap ? w : h;
  int x1 = dx, y1 = dy, x2 = dx + dW, y2 = dy + dH;
  if (!clipRect(t, x1, y1, x2, y2)) return;
  if (tr == 0) {  // the common case: rows straight through
    for (int y = y1; y < y2; y++) {
      const uint32_t* s = src + (sy + y - dy) * sw + sx + (x1 - dx);
      uint32_t* d = t.px + y * t.w + x1;
      const int n = x2 - x1;
      if (!alpha) {
        for (int i = 0; i < n; i++) d[i] = s[i] | 0xFF000000u;
      } else {
        for (int i = 0; i < n; i++) {
          const uint32_t p = s[i], a = p >> 24;
          if (a == 255) d[i] = p;
          else if (a) d[i] = blend(d[i], p, a);
        }
      }
    }
    return;
  }
  for (int y = y1; y < y2; y++) {
    const int v = y - dy;
    uint32_t* d = t.px + y * t.w;
    for (int x = x1; x < x2; x++) {
      const int u = x - dx;
      int qx, qy;  // in the region
      switch (tr) {
        case 2: qx = w - 1 - u, qy = v; break;          // MIRROR
        case 3: qx = w - 1 - u, qy = h - 1 - v; break;  // ROT180
        case 1: qx = u, qy = h - 1 - v; break;          // MIRROR_ROT180
        case 5: qx = v, qy = h - 1 - u; break;          // ROT90
        case 6: qx = w - 1 - v, qy = u; break;          // ROT270
        case 7: qx = w - 1 - v, qy = h - 1 - u; break;  // MIRROR_ROT90
        case 4: qx = v, qy = u; break;                  // MIRROR_ROT270
        default: qx = u, qy = v; break;
      }
      const uint32_t p = src[(sy + qy) * sw + sx + qx];
      if (!alpha) d[x] = p | 0xFF000000u;
      else {
        const uint32_t a = p >> 24;
        if (a == 255) d[x] = p;
        else if (a) d[x] = blend(d[x], p, a);
      }
    }
  }
}

void rgb(const Target& t, const uint32_t* src, int srcLen, int offset, int scan, int x, int y, int w, int h, bool alpha) {
  int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
  if (w <= 0 || h <= 0 || !clipRect(t, x1, y1, x2, y2)) return;
  for (int r = y1; r < y2; r++) {
    const long base = (long)offset + (long)(r - y) * scan + (x1 - x);
    if (base < 0 || base + (x2 - x1) > srcLen) continue;  // out of the array: skipped (Java checked the corners)
    const uint32_t* s = src + base;
    uint32_t* d = t.px + r * t.w + x1;
    for (int i = 0; i < x2 - x1; i++) {
      const uint32_t p = s[i];
      if (!alpha) d[i] = p | 0xFF000000u;
      else {
        const uint32_t a = p >> 24;
        if (a == 255) d[i] = p;
        else if (a) d[i] = blend(d[i], p, a);
      }
    }
  }
}

// ---- text
namespace {
Font fonts[6];
bool fontsReady = false;
inline const Glyph* glyphOf(const Font& f, char16_t ch) {
  if (ch < f.glyphs.size() && f.glyphs[ch].adv) return &f.glyphs[ch];
  return '?' < f.glyphs.size() && f.glyphs['?'].adv ? &f.glyphs['?'] : nullptr;
}
}  // namespace

const Font* font(int id) { return fontsReady && id >= 0 && id < 6 ? &fonts[id] : nullptr; }
Font* fontSlot(int id) { return &fonts[id]; }   // midp_font.cpp fills them
void fontsDone() { fontsReady = true; }

int textWidth(const char16_t* s, int n, int id) {
  const Font* f = font(id);
  if (!f) return n * 6;
  int w = 0;
  for (int i = 0; i < n; i++)
    if (const Glyph* g = glyphOf(*f, s[i])) w += g->adv;
  return w;
}

int text(const Target& t, const char16_t* s, int n, int id, int x, int y, uint32_t rgbc) {
  const Font* f = font(id);
  if (!f) return 0;
  const uint32_t c = opaque(rgbc);
  int pen = x;
  for (int i = 0; i < n; i++) {
    const Glyph* g = glyphOf(*f, s[i]);
    if (!g) continue;
    const int gx = pen + g->ofsX, gy = y + g->ofsY;
    int x1 = gx, y1 = gy, x2 = gx + g->w, y2 = gy + g->h;
    if (g->w && clipRect(t, x1, y1, x2, y2)) {
      for (int r = y1; r < y2; r++) {
        const uint8_t* a = f->bits.data() + g->off + (r - gy) * g->w + (x1 - gx);
        uint32_t* d = t.px + r * t.w + x1;
        for (int k = 0; k < x2 - x1; k++) {
          const uint32_t av = a[k];
          if (av >= 250) d[k] = c;
          else if (av > 4) d[k] = blend(d[k], c, av);
        }
      }
    }
    pen += g->adv;
  }
  return pen - x;
}

}  // namespace draw
}  // namespace j2me
