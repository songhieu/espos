// Hồ cá cảnh: what the screens draw over the water (tank_int.h gfx). Everything is drawn at the frame's scale: on the
// 2.8" board the screens' shapes are made at 2/3 (not shrunk from 320 x 480), and their text is shrunk once per line
// by area (as src/scale.h shrinks the OS's screens), then put on whole pixels, so it stays as sharp as the OS's.
// Glass panels (their soft shadow is a blur) and lines of text are made once and kept; a frame only copies them.
#pragma GCC optimize("Os")  // the pixel loops are -O2 (HOT)
#include <Arduino.h>

#include <cmath>
#include <cstring>

#include "tank_int.h"

#define HOT __attribute__((optimize("O2")))

namespace tank {
namespace gfx {
namespace {

float S = 1;  // frame pixels for one screen unit
uint32_t clock_ = 0;

struct Img {  // straight alpha, no padding
  int w = 0, h = 0;
  uint16_t* c = nullptr;
  uint8_t* a = nullptr;  // nullptr: text (colour given when drawn)
};
Img make(int w, int h, bool colour) {
  Img m;
  m.w = w, m.h = h;
  uint8_t* b = (uint8_t*)heap_caps_calloc(1, w * h * (colour ? 3 : 1) + 4, MALLOC_CAP_SPIRAM);
  if (!b) return Img();
  if (colour) m.c = (uint16_t*)b, m.a = b + w * h * 2;
  else m.a = b;
  return m;
}
void drop(Img& m) {
  heap_caps_free(m.c ? (void*)m.c : (void*)m.a);
  m = Img();
}

// a copy at whole pixels (x, y = its top left on the frame), alpha 0..256, colour for text
HOT void put(const Tgt& t, const Img& m, int x, int y, int alpha, uint16_t colour) {
  const int X0 = std::max(t.x0, x), X1 = std::min(t.x1, x + m.w), Y0 = std::max(t.y0, y), Y1 = std::min(t.y1, y + m.h);
  if (X0 >= X1 || Y0 >= Y1 || alpha <= 0) return;
  const uint32_t ec = ex(colour);
  for (int Y = Y0; Y < Y1; Y++) {
    const int o = (Y - y) * m.w + (X0 - x);
    const uint8_t* A = m.a + o;
    const uint16_t* C = m.c ? m.c + o : nullptr;
    uint16_t* d = t.at(X0, Y);
    for (int X = X0; X < X1; X++, A++, d += t.dx) {
      const int a = (*A * alpha) >> 8;
      if (a < 4) continue;
      const uint32_t c = C ? ex(C[X - X0]) : ec;
      *d = a >= 252 ? pk(c) : pk(mix(ex(*d), c, (a + 4) >> 3));
    }
  }
}

__attribute__((always_inline)) inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
// signed distance to a rounded box (px, py from its centre; half sizes; radius)
__attribute__((always_inline)) inline float sdRR(float px, float py, float hw, float hh, float r) {
  const float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
  if (qx <= 0 && qy <= 0) return (qx > qy ? qx : qy) - r;
  const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

// ---------------------------------------------------------------- glass (made once per size)
struct GlassK {
  int w, h, r, fill, kind;  // kind 0 panel, 1 dock button
  Img m;
  int ox, oy;  // where the box's top left is in the picture
};
std::vector<GlassK> glasses;

void blur(float* a, int w, int h, int rad) {  // three box blurs ~ a gaussian
  std::vector<float> t(std::max(w, h));
  for (int pass = 0; pass < 3; pass++) {
    for (int y = 0; y < h; y++) {
      float* row = a + y * w;
      float s = 0;
      for (int x = -rad; x <= rad; x++) s += row[std::min(w - 1, std::max(0, x))];
      for (int x = 0; x < w; x++) {
        t[x] = s / (2 * rad + 1);
        s += row[std::min(w - 1, x + rad + 1)] - row[std::max(0, x - rad)];
      }
      memcpy(row, t.data(), w * sizeof(float));
    }
    for (int x = 0; x < w; x++) {
      float s = 0;
      for (int y = -rad; y <= rad; y++) s += a[std::min(h - 1, std::max(0, y)) * w + x];
      for (int y = 0; y < h; y++) {
        t[y] = s / (2 * rad + 1);
        s += a[std::min(h - 1, y + rad + 1) * w + x] - a[std::max(0, y - rad) * w + x];
      }
      for (int y = 0; y < h; y++) a[y * w + x] = t[y];
    }
  }
}

// ui.js glass(): shadow rgba(0,20,30,.35) blur 8 down 2, fill rgba(250,252,255,fill), rim white .9
// dockButton: shadow .4, a radial light from up-left (white .95 to rgba(226,238,246,.8)), rim white .95
GlassK* glassOf(int wU, int hU, int rU, int fill, int kind) {
  for (GlassK& g : glasses)
    if (g.w == wU && g.h == hU && g.r == rU && g.fill == fill && g.kind == kind) return &g;
  if (glasses.size() > 24) {
    drop(glasses.front().m);
    glasses.erase(glasses.begin());
  }
  const float w = wU * S, h = hU * S, r = rU * S, blurR = 4 * S;
  const int pad = (int)ceilf(10 * S), W = (int)ceilf(w) + 2 * pad, H = (int)ceilf(h) + 2 * pad + (int)ceilf(2 * S);
  GlassK g = {wU, hU, rU, fill, kind, make(W, H, true), pad, pad};
  if (!g.m.a) return nullptr;
  float* sh = (float*)heap_caps_malloc(W * H * sizeof(float), MALLOC_CAP_SPIRAM);
  if (!sh) return drop(g.m), nullptr;
  const float cx = pad + w / 2, cy = pad + h / 2;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) sh[y * W + x] = clampf(0.5f - sdRR(x + 0.5f - cx, y + 0.5f - cy - 2 * S, w / 2, h / 2, r), 0, 1);
  blur(sh, W, H, std::max(1, (int)lroundf(blurR / 1.7f)));
  const float shA = kind ? 0.4f : 0.35f;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const float px = x + 0.5f - cx, py = y + 0.5f - cy, d = sdRR(px, py, w / 2, h / 2, r), in = clampf(0.5f - d, 0, 1);
      // the fill
      float fr, fg, fb, fa;
      if (kind) {
        const float gx = px + 6 * S, gy = py + 9 * S, k = clampf((sqrtf(gx * gx + gy * gy) - 2 * S) / (r - 2 * S), 0, 1);
        fr = 255 + (226 - 255) * k, fg = 255 + (238 - 255) * k, fb = 255 + (246 - 255) * k, fa = 0.95f + (0.8f - 0.95f) * k;
      } else fr = 250, fg = 252, fb = 255, fa = fill / 255.0f;
      fa *= in;
      // the rim: a 1 px (1.2 on the buttons) white line just inside the edge
      const float lw = (kind ? 1.2f : 1) * std::max(S, 0.8f), rim = clampf(lw / 2 + 0.5f - fabsf(d + lw / 2 + (kind ? 0 : 0.0f)), 0, 1) * (kind ? 0.95f : 0.9f) * in;
      // the fill over the shadow, then the rim over both (straight alpha)
      const float sa = sh[y * W + x] * shA * (1 - in);  // the shadow only shows outside the glass
      float a = fa + sa * (1 - fa), r_ = a > 0 ? (fr * fa + 0 * sa * (1 - fa)) / a : 0, g_ = a > 0 ? (fg * fa + 20 * sa * (1 - fa)) / a : 0,
            b_ = a > 0 ? (fb * fa + 30 * sa * (1 - fa)) / a : 0;
      const float a2 = rim + a * (1 - rim);
      if (a2 > 0) r_ = (255 * rim + r_ * a * (1 - rim)) / a2, g_ = (255 * rim + g_ * a * (1 - rim)) / a2, b_ = (255 * rim + b_ * a * (1 - rim)) / a2;
      g.m.c[y * W + x] = rgb565((int)r_, (int)g_, (int)b_);
      g.m.a[y * W + x] = (uint8_t)clampf(a2 * 255 + 0.5f, 0, 255);
    }
  heap_caps_free(sh);
  glasses.push_back(g);
  return &glasses.back();
}

// ---------------------------------------------------------------- text (one line made once, kept while used)
struct Run {
  String s;
  int px;
  bool bold;
  Img m;          // A8 at the frame's scale
  float lh, w;    // the line box's height and the text's width, in screen units
  uint32_t used;
};
std::vector<Run> runs;
lv_draw_buf_t* glyphBuf = nullptr;

uint32_t utf8Next(const char* s, uint32_t& i) {
  const uint8_t c = s[i];
  if (!c) return 0;
  int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
  uint32_t v = n == 0 ? c : n == 1 ? c & 0x1F : n == 2 ? c & 0x0F : c & 0x07;
  i++;
  while (n-- > 0 && (s[i] & 0xC0) == 0x80) v = v << 6 | (s[i++] & 0x3F);
  return v;
}
// one letter: its advance; g.resolved_font null = a letter no font has (never ask LVGL for its bitmap: it halts)
int advOf(const lv_font_t* f, uint32_t letter, uint32_t next, lv_font_glyph_dsc_t& g) {
  if (!lv_font_get_glyph_dsc(f, &g, letter, next) || !g.resolved_font) {
    g.resolved_font = nullptr, g.box_w = g.box_h = 0;
    return g.adv_w;
  }
  return g.adv_w;
}
float widthOf(const lv_font_t* f, const char* s) {
  int w = 0;
  uint32_t i = 0, letter = utf8Next(s, i);
  while (letter) {
    const uint32_t next = utf8Next(s, i);
    lv_font_glyph_dsc_t g;
    w += advOf(f, letter, next, g);
    letter = next;
  }
  return w;
}

// A8 shrunk by area (k < 1)
void shrinkA8(const uint8_t* s, int sw, int sh, uint8_t* d, int dw, int dh) {
  const float kx = (float)sw / dw, ky = (float)sh / dh;
  for (int y = 0; y < dh; y++)
    for (int x = 0; x < dw; x++) {
      const float x0 = x * kx, x1 = x0 + kx, y0 = y * ky, y1 = y0 + ky;
      float v = 0, ws = 0;
      for (int j = (int)y0; j < (int)ceilf(y1) && j < sh; j++)
        for (int i = (int)x0; i < (int)ceilf(x1) && i < sw; i++) {
          const float w = (fminf(x1, i + 1) - fmaxf(x0, i)) * (fminf(y1, j + 1) - fmaxf(y0, j));
          v += s[j * sw + i] * w, ws += w;
        }
      d[y * dw + x] = (uint8_t)(ws > 0 ? v / ws + 0.5f : 0);
    }
}

Run* runOf(const char* s, int px, bool bold) {
  for (Run& r : runs)
    if (r.px == px && r.bold == bold && r.s == s) return r.used = clock_, &r;
  if (runs.size() >= 96) {  // the one used longest ago goes
    size_t old = 0;
    for (size_t i = 1; i < runs.size(); i++)
      if (runs[i].used < runs[old].used) old = i;
    drop(runs[old].m);
    runs.erase(runs.begin() + old);
  }
  const lv_font_t* f = font(px, bold);
  if (!glyphBuf) glyphBuf = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_A8, 0);
  Run r;
  r.s = s, r.px = px, r.bold = bold, r.used = clock_, r.lh = f->line_height, r.w = widthOf(f, s);
  const int W = (int)ceilf(r.w) + 2, H = f->line_height;
  Img full = make(W, H, false);
  if (full.a && glyphBuf) {
    uint32_t i = 0, letter = utf8Next(s, i);
    int pen = 1;
    while (letter) {
      const uint32_t next = utf8Next(s, i);
      lv_font_glyph_dsc_t g;
      const int adv = advOf(f, letter, next, g);
      if (g.resolved_font && !g.is_placeholder && g.box_w && g.box_h && g.box_w <= 64 && g.box_h <= 64 &&
          g.format >= LV_FONT_GLYPH_FORMAT_A1 && g.format <= LV_FONT_GLYPH_FORMAT_A8) {
        const lv_font_t* rf = g.resolved_font;
        const uint8_t* a = lv_font_get_glyph_bitmap(&g, glyphBuf) ? glyphBuf->data : nullptr;
        if (a) {
          const uint32_t st = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8);
          const int top = (rf->line_height - rf->base_line) - g.box_h - g.ofs_y + (f->line_height - rf->line_height) / 2;
          for (int y = 0; y < g.box_h; y++)
            for (int x = 0; x < g.box_w; x++) {
              const int X = pen + g.ofs_x + x, Y = top + y;
              if (X < 0 || X >= W || Y < 0 || Y >= H) continue;
              uint8_t& d = full.a[Y * W + X];
              d = (uint8_t)std::min(255, d + a[y * st + x]);
            }
        }
        if (rf->release_glyph) rf->release_glyph(rf, &g);
      }
      pen += adv;
      letter = next;
    }
  }
  if (S < 0.99f && full.a) {
    r.m = make(std::max(1, (int)lroundf(W * S)), std::max(1, (int)lroundf(H * S)), false);
    if (r.m.a) shrinkA8(full.a, W, H, r.m.a, r.m.w, r.m.h);
    drop(full);
  } else r.m = full;
  runs.push_back(r);
  return &runs.back();
}

}  // namespace

void begin(float s) {
  if (fabsf(s - S) < 0.001f) return;
  end();
  S = s;
}
void end() {
  for (GlassK& g : glasses) drop(g.m);
  glasses.clear();
  for (Run& r : runs) drop(r.m);
  runs.clear();
}
void frameStart() { clock_++; }

HOT void rrect(const Tgt& t, float x, float y, float w, float h, float r, uint16_t c, int a) {
  if (w <= 0 || h <= 0 || a <= 0) return;
  const float X = x * S, Y = y * S, W = w * S, H = h * S, R = std::min(r * S, std::min(W, H) / 2);
  const int X0 = std::max(t.x0, (int)floorf(X)), X1 = std::min(t.x1, (int)ceilf(X + W)), Y0 = std::max(t.y0, (int)floorf(Y)),
            Y1 = std::min(t.y1, (int)ceilf(Y + H));
  const uint32_t e = ex(c);
  const float cx = X + W / 2, cy = Y + H / 2;
  for (int py = Y0; py < Y1; py++) {
    uint16_t* d = t.at(X0, py);
    for (int px = X0; px < X1; px++, d += t.dx) {
      const float cov = clampf(0.5f - sdRR(px + 0.5f - cx, py + 0.5f - cy, W / 2, H / 2, R), 0, 1);
      const int w = (int)(cov * a / 8 + 0.5f);
      if (w >= 32) *d = c;
      else if (w > 0) *d = pk(mix(ex(*d), e, w));
    }
  }
}
void circle(const Tgt& t, float cx, float cy, float r, uint16_t c, int a) { rrect(t, cx - r, cy - r, 2 * r, 2 * r, r, c, a); }
HOT void ring(const Tgt& t, float cx, float cy, float r, float lw, uint16_t c, int a) {
  const float X = cx * S, Y = cy * S, R = r * S, L = std::max(lw * S, 0.8f);
  const int X0 = std::max(t.x0, (int)floorf(X - R - L)), X1 = std::min(t.x1, (int)ceilf(X + R + L)),
            Y0 = std::max(t.y0, (int)floorf(Y - R - L)), Y1 = std::min(t.y1, (int)ceilf(Y + R + L));
  const uint32_t e = ex(c);
  for (int py = Y0; py < Y1; py++) {
    uint16_t* d = t.at(X0, py);
    for (int px = X0; px < X1; px++, d += t.dx) {
      const float dx = px + 0.5f - X, dy = py + 0.5f - Y, cov = clampf(L / 2 + 0.5f - fabsf(sqrtf(dx * dx + dy * dy) - R), 0, 1);
      const int w = (int)(cov * a / 8 + 0.5f);
      if (w > 0) *d = pk(mix(ex(*d), e, w > 32 ? 32 : w));
    }
  }
}
HOT void line(const Tgt& t, float x0, float y0, float x1, float y1, float lw, uint16_t c, int a) {
  const float ax = x0 * S, ay = y0 * S, bx = x1 * S, by = y1 * S, L = std::max(lw * S, 0.9f) / 2;
  const int X0 = std::max(t.x0, (int)floorf(std::min(ax, bx) - L - 1)), X1 = std::min(t.x1, (int)ceilf(std::max(ax, bx) + L + 1)),
            Y0 = std::max(t.y0, (int)floorf(std::min(ay, by) - L - 1)), Y1 = std::min(t.y1, (int)ceilf(std::max(ay, by) + L + 1));
  const float vx = bx - ax, vy = by - ay, vv = vx * vx + vy * vy + 1e-6f;
  const uint32_t e = ex(c);
  for (int py = Y0; py < Y1; py++) {
    uint16_t* d = t.at(X0, py);
    for (int px = X0; px < X1; px++, d += t.dx) {
      const float qx = px + 0.5f - ax, qy = py + 0.5f - ay, k = clampf((qx * vx + qy * vy) / vv, 0, 1), dx = qx - vx * k,
                  dy = qy - vy * k, cov = clampf(L + 0.5f - sqrtf(dx * dx + dy * dy), 0, 1);  // round caps
      const int w = (int)(cov * a / 8 + 0.5f);
      if (w > 0) *d = pk(mix(ex(*d), e, w > 32 ? 32 : w));
    }
  }
}
HOT void dim(const Tgt& t, float x, float y, float w, float h, uint16_t c, int a) {
  const int X0 = std::max(t.x0, (int)lroundf(x * S)), X1 = std::min(t.x1, (int)lroundf((x + w) * S)),
            Y0 = std::max(t.y0, (int)lroundf(y * S)), Y1 = std::min(t.y1, (int)lroundf((y + h) * S));
  const uint32_t e = ex(c);
  const int wgt = a / 8;
  for (int py = Y0; py < Y1; py++) {
    uint16_t* d = t.at(X0, py);
    for (int px = X0; px < X1; px++, d += t.dx) *d = pk(mix(ex(*d), e, wgt));
  }
}

void glass(const Tgt& t, float x, float y, float w, float h, float r, int fill, int a) {
  GlassK* g = glassOf((int)lroundf(w), (int)lroundf(h), (int)lroundf(r), fill, 0);
  if (g) put(t, g->m, (int)lroundf(x * S) - g->ox, (int)lroundf(y * S) - g->oy, a, 0);
}
void dockButton(const Tgt& t, float cx, float cy, float r, int a) {
  GlassK* g = glassOf((int)lroundf(2 * r), (int)lroundf(2 * r), (int)lroundf(r), 0, 1);
  if (g) put(t, g->m, (int)lroundf((cx - r) * S) - g->ox, (int)lroundf((cy - r) * S) - g->oy, a, 0);
}

void blitScaled(const Tgt& t, const Spr* s, float x, float y, float w, float h, int a, bool flip) {
  if (s) spriteBlit(t, s, x * S, y * S, w * S / s->w, h * S / s->h, a, flip ? 1 : 0);
}
void fit(const Tgt& t, const Spr* s, float x, float y, float w, float h, int a, bool shade) {
  if (!s) return;
  const float k = std::min(w / s->w, h / s->h), dw = s->w * k, dh = s->h * k;
  spriteBlit(t, s, (x + (w - dw) / 2) * S, (y + (h - dh) / 2) * S, k * S, k * S, shade ? a * 72 / 256 : a, shade ? 2 : 0);
}

float textW(const char* s, int px, bool bold) { return s && *s ? widthOf(font(px, bold), s) : 0; }

float text(const Tgt& t, const char* s, float x, float y, int px, bool bold, uint16_t c, int align, int a) {
  if (!s || !*s) return 0;
  Run* r = runOf(s, px, bold);
  if (!r->m.a) return r->w;
  const float left = align == 1 ? x - r->w / 2 : align == 2 ? x - r->w : x;
  // canvas textBaseline "middle": the em box's middle, an eighth of a line above the middle of LVGL's line box
  const float top = y - r->lh / 2 - (r->lh + 4) / 8;
  put(t, r->m, (int)lroundf((left - 1) * S), (int)lroundf(top * S), a, c);
  return r->w;
}

// one line that must fit: drop whole " · " items from the end, then whole words (never half a word, never "…")
String fit1(const char* s, int px, bool bold, float w) {
  String t = s;
  if (textW(t.c_str(), px, bold) <= w) return t;
  for (const char* sep : {" \xC2\xB7 ", ", "}) {
    std::vector<String> parts;
    int from = 0, k;
    while ((k = t.indexOf(sep, from)) >= 0) parts.push_back(t.substring(from, k)), from = k + strlen(sep);
    parts.push_back(t.substring(from));
    auto join = [&]() {
      String j;
      for (size_t i = 0; i < parts.size(); i++) j += (i ? sep : "") + parts[i];
      return j;
    };
    while (parts.size() > 1 && textW(join().c_str(), px, bold) > w) parts.pop_back();
    t = join();
  }
  while (textW(t.c_str(), px, bold) > w) {
    const int k = t.lastIndexOf(' ');
    if (k <= 0) break;
    t = t.substring(0, k);
  }
  return t;
}
void wrap2(const char* s, int px, bool bold, float w, String& a, String& b) {
  a = b = "";
  String t = s;
  int from = 0;
  while (from <= (int)t.length()) {
    int k = t.indexOf(' ', from);
    if (k < 0) k = t.length();
    const String wd = t.substring(from, k);
    if (!b.length() && textW((a + (a.length() ? " " : "") + wd).c_str(), px, bold) <= w) a += (a.length() ? " " : "") + wd;
    else b += (b.length() ? " " : "") + wd;
    from = k + 1;
  }
  if (b.length()) b = fit1(b.c_str(), px, bold, w);
}

}  // namespace gfx
}  // namespace tank
