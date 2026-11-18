#pragma GCC optimize("Os")
#include "text.h"

#include <cmath>

#include "dog.h"

namespace pet {
namespace text {
namespace {

constexpr int SAYMAX = 280;            // the page's SAYMAX
constexpr uint16_t INK565 = 0x2967;    // #2b2f3a
lv_draw_buf_t* glyphBuf = nullptr;     // one glyph as A8 (LVGL writes it here)

inline void put(const Target& t, int x, int y, uint16_t c, int a) {
  if (x < 0 || x >= t.w || y < t.y0 || y >= t.y1 || y < 0 || y >= t.h || a <= 0) return;
  uint16_t& d = t.px[y * t.stride + x];
  d = dog::blend(d, c, a);
}

uint32_t utf8Next(const char* s, uint32_t& i) {  // UTF-8, one letter
  const uint8_t c = s[i];
  if (!c) return 0;
  int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
  uint32_t v = n == 0 ? c : n == 1 ? c & 0x1F : n == 2 ? c & 0x0F : c & 0x07;
  i++;
  while (n-- > 0 && (s[i] & 0xC0) == 0x80) v = v << 6 | (s[i++] & 0x3F);
  return v;
}

}  // namespace

int width(const lv_font_t* f, const char* s) { return s ? lv_text_get_width(s, strlen(s), f, 0) : 0; }

void draw(const Target& t, const lv_font_t* f, const char* s, int x, float midY, uint16_t color) {
  if (!s || !f) return;
  if (!glyphBuf) glyphBuf = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_A8, 0);
  if (!glyphBuf) return;
  const int lineTop = (int)lroundf(midY - f->line_height / 2.0f);
  uint32_t i = 0;
  uint32_t letter = utf8Next(s, i);
  while (letter) {
    const uint32_t next = utf8Next(s, i);
    lv_font_glyph_dsc_t g;
    if (lv_font_get_glyph_dsc(f, &g, letter, next) && g.box_w && g.box_h && g.box_w <= 64 && g.box_h <= 64 &&
        g.format <= LV_FONT_GLYPH_FORMAT_A8) {
      const lv_font_t* rf = g.resolved_font ? g.resolved_font : f;
      // LVGL 9.2 writes the glyph as A8 into the draw buffer and returns the buffer (not its pixels).
      const uint8_t* a = lv_font_get_glyph_bitmap(&g, glyphBuf) ? glyphBuf->data : nullptr;
      if (a) {
        const uint32_t stride = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8);
        // As an LVGL label: the letter's box above the font's base line.
        const int top = lineTop + (rf->line_height - rf->base_line) - g.box_h - g.ofs_y + (f->line_height - rf->line_height) / 2;
        for (int r = 0; r < g.box_h; r++)
          for (int c = 0; c < g.box_w; c++) put(t, x + g.ofs_x + c, top + r, color, a[r * stride + c]);
      }
      if (rf->release_glyph) rf->release_glyph(rf, &g);
    }
    x += g.adv_w;
    letter = next;
  }
}

Bubble layout(const lv_font_t* f, const String& s) {
  Bubble b;
  b.rows[0] = s, b.n = 1;
  if (width(f, s.c_str()) > SAYMAX) {  // the two rows nearest to equal (split at a space)
    int best = 1 << 30;
    for (int k = s.indexOf(' '); k >= 0; k = s.indexOf(' ', k + 1)) {
      const String a = s.substring(0, k), c = s.substring(k + 1);
      const int m = max(width(f, a.c_str()), width(f, c.c_str()));
      if (m < best) best = m, b.rows[0] = a, b.rows[1] = c, b.n = 2;
    }
    if (b.n == 2 && best > 296) {  // two rows would leave the screen: three (never cut)
      best = 1 << 30;
      const String all = s;
      for (int k = all.indexOf(' '); k >= 0; k = all.indexOf(' ', k + 1))
        for (int j = all.indexOf(' ', k + 1); j >= 0; j = all.indexOf(' ', j + 1)) {
          const String a = all.substring(0, k), m2 = all.substring(k + 1, j), c = all.substring(j + 1);
          const int m = max(width(f, a.c_str()), max(width(f, m2.c_str()), width(f, c.c_str())));
          if (m < best) best = m, b.rows[0] = a, b.rows[1] = m2, b.rows[2] = c, b.n = 3;
        }
    }
  }
  int w = 0;
  for (int k = 0; k < b.n; k++) w = max(w, width(f, b.rows[k].c_str()));
  b.w = w + 24, b.h = 10 + 16 * b.n;
  return b;
}

void roundBox(const Target& t, int x, int y, int w, int h, int r, uint16_t color) {
  for (int yy = max(y, t.y0); yy < min(y + h, t.y1); yy++) {
    const float cy = yy + 0.5f;
    for (int xx = max(x, 0); xx < min(x + w, t.w); xx++) {
      const float cx = xx + 0.5f;
      // distance into the corner circle when in a corner square
      const float dx = cx < x + r ? x + r - cx : cx > x + w - r ? cx - (x + w - r) : 0;
      const float dy = cy < y + r ? y + r - cy : cy > y + h - r ? cy - (y + h - r) : 0;
      int a = 255;
      if (dx > 0 && dy > 0) {
        const float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
        if (cov <= 0) continue;
        if (cov < 1) a = (int)(cov * 255);
      }
      put(t, xx, yy, color, a);
    }
  }
}

void bubble(const Target& t, const lv_font_t* f, const Bubble& b, int x, int y) {
  roundBox(t, x, y, b.w, b.h, 13, INK565);
  for (int k = 0; k < b.n; k++) {
    const int w = width(f, b.rows[k].c_str());
    draw(t, f, b.rows[k].c_str(), x + (b.w - w) / 2, y + 5 + 16 * k + 8.5f, 0xFFFF);
  }
}

}  // namespace text
}  // namespace pet
