#pragma GCC optimize("Os")
#include "text.h"

#include <cmath>

#include "dog.h"
#include "packs.h"

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

// A letter's pen advance in 1/16 px, its kerning with the next letter included: fractional like the page's canvas
// (LVGL rounds every letter to whole pixels: a bubble came out 5 px wider than the page's).
int adv16(const lv_font_t* f, uint32_t letter, uint32_t next, lv_font_glyph_dsc_t& g) {
  // A letter no font of the chain has: LVGL answers a placeholder box with no font (resolved_font NULL). Its
  // bitmap must never be asked for (LV_ASSERT_NULL halts the device: PawBoop froze on the board, 2026-11-18);
  // it is left as a gap of LVGL's placeholder width.
  if (!lv_font_get_glyph_dsc(f, &g, letter, next) || !g.resolved_font) {
    g.resolved_font = nullptr, g.box_w = g.box_h = 0;
    return g.adv_w * 16;
  }
  const lv_font_t* rf = g.resolved_font;
  if (!rf || rf->get_glyph_dsc != lv_font_get_glyph_dsc_fmt_txt || !g.gid.index) return g.adv_w * 16;
  const lv_font_fmt_txt_dsc_t* fd = (const lv_font_fmt_txt_dsc_t*)rf->dsc;
  int a = fd->glyph_dsc[g.gid.index].adv_w;
  lv_font_glyph_dsc_t gn;
  if (next && fd->kern_dsc && fd->kern_classes == 1 && lv_font_get_glyph_dsc(rf, &gn, next, 0) && gn.resolved_font == rf) {
    const lv_font_fmt_txt_kern_classes_t* k = (const lv_font_fmt_txt_kern_classes_t*)fd->kern_dsc;
    const uint8_t l = k->left_class_mapping[g.gid.index], r = k->right_class_mapping[gn.gid.index];
    if (l && r) a += (k->class_pair_values[(l - 1) * k->right_class_cnt + (r - 1)] * fd->kern_scale) >> 4;
  }
  return a;
}

// The dog's "…" (its own words, kept as said) as three dots when the font chain lacks it (PawBoop 1.0 without its
// Lexend files: the OS's fonts), so it is never a placeholder box.
String forFont(const lv_font_t* f, const String& s) {
  if (s.indexOf("\xE2\x80\xA6") < 0) return s;
  lv_font_glyph_dsc_t g;
  if (lv_font_get_glyph_dsc(f, &g, 0x2026, 0) && g.resolved_font) return s;
  String t = s;
  t.replace("\xE2\x80\xA6", "...");
  return t;
}

}  // namespace

int width(const lv_font_t* f, const char* s) {
  if (!s || !f) return 0;
  int w16 = 0;
  uint32_t i = 0, letter = utf8Next(s, i);
  while (letter) {
    const uint32_t next = utf8Next(s, i);
    lv_font_glyph_dsc_t g;
    w16 += adv16(f, letter, next, g);
    letter = next;
  }
  return (w16 + 8) >> 4;
}

void draw(const Target& t, const lv_font_t* f, const char* s, int x, float midY, uint16_t color) {
  if (!s || !f) return;
  if (!glyphBuf) glyphBuf = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_A8, 0);
  if (!glyphBuf) return;
  // The page's textBaseline "middle" (the em box's middle) sits about an eighth of a line higher than the middle of
  // LVGL's line box: measured against pet.html's bubbles and the game designer's name/splash pieces (12, 14, 11 px: 2 px).
  const int lineTop = (int)lroundf(midY - f->line_height / 2.0f) - (f->line_height + 4) / 8;
  uint32_t i = 0;
  uint32_t letter = utf8Next(s, i);
  int pen16 = 0;  // fractional pen, each letter at its rounded place (as the page's canvas)
  while (letter) {
    const uint32_t next = utf8Next(s, i);
    lv_font_glyph_dsc_t g;
    const int a16 = adv16(f, letter, next, g);
    const int gx = x + ((pen16 + 8) >> 4);
    if (g.resolved_font && !g.is_placeholder && g.box_w && g.box_h && g.box_w <= 64 && g.box_h <= 64 &&
        g.format >= LV_FONT_GLYPH_FORMAT_A1 && g.format <= LV_FONT_GLYPH_FORMAT_A8) {
      const lv_font_t* rf = g.resolved_font ? g.resolved_font : f;
      // LVGL 9.2 writes the glyph as A8 into the draw buffer and returns the buffer (not its pixels).
      const uint8_t* a = lv_font_get_glyph_bitmap(&g, glyphBuf) ? glyphBuf->data : nullptr;
      if (a) {
        const uint32_t stride = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8);
        // As an LVGL label: the letter's box above the font's base line.
        const int top = lineTop + (rf->line_height - rf->base_line) - g.box_h - g.ofs_y + (f->line_height - rf->line_height) / 2;
        for (int r = 0; r < g.box_h; r++)
          for (int c = 0; c < g.box_w; c++) put(t, gx + g.ofs_x + c, top + r, color, a[r * stride + c]);
      }
      if (rf->release_glyph) rf->release_glyph(rf, &g);
    }
    pen16 += a16;
    letter = next;
  }
}

Bubble layout(const lv_font_t* f, const String& text) {
  Bubble b;
  const String s = forFont(f, text);
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

String wrap(const lv_font_t* f, const String& text, int w, int& lines) {
  const String s = forFont(f, text);
  String out, row;
  lines = 0;
  int at = 0;
  while (at <= (int)s.length()) {
    int sp = s.indexOf(' ', at);
    if (sp < 0) sp = s.length();
    const String word = s.substring(at, sp);
    const String tryRow = row.length() ? row + " " + word : word;
    if (row.length() && lv_text_get_width(tryRow.c_str(), tryRow.length(), f, 0) > w) {
      out += (lines++ ? "\n" : "") + row;
      row = word;
    } else {
      row = tryRow;
    }
    at = sp + 1;
  }
  if (row.length()) out += (lines++ ? "\n" : "") + row;
  return out;
}

int words(lv_obj_t* l, const String& s, int w, int maxLines) {
  int n = 0;
  const lv_font_t* f = pk::font(1);
  String t = wrap(f, s, w, n);
  if (n > maxLines) f = pk::font(0), t = wrap(f, s, w, n);
  lv_obj_set_style_text_font(l, f, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, w);
  lv_label_set_text(l, t.c_str());
  return n * lv_font_get_line_height(f) + (n - 1) * lv_obj_get_style_text_line_space(l, 0);
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
