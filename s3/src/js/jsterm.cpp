#pragma GCC optimize("Os")  // cold code: built small
// ui.term (see jsterm.h): a terminal view for JavaScript apps, drawn by LVGL from lines kept in PSRAM.
#include "jsterm.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include <algorithm>
#include <map>
#include <new>
#include <vector>

#include "fonts.h"
#include "jsapp.h"
#include "pkg/pkg.h"
#include "trace.h"
#include "uikit.h"

namespace jsterm {
namespace {

using namespace kit;
using namespace jsapp::node;

struct Spiram : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} spiram;

// ---------------------------------------------------------------- the lines
struct Run {
  uint16_t start, len;  // in characters (code points)
  uint32_t fg, bg;      // 0 default, 1..256 xterm index + 1, >= 0x1000000: 0x1000000 | RGB
  uint8_t attr;         // 1 bold, 2 dim, 4 italic, 8 underline, 16 inverse
};

struct Line {
  int32_t n;
  uint16_t nr;
  uint16_t rowsAt;  // the columns `rows` was counted for (0: not yet)
  uint16_t rows;
  char* t;          // UTF-8, NUL-terminated; the runs follow in the same PSRAM block
  Run* r;
};

constexpr int MAX_TEXT = 1000 * 4;  // 1000 characters
constexpr int MAX_RUNS = 24;

struct Buf {
  Line* ring = nullptr;
  int cap = 0, head = 0, count = 0;  // ring[(head + i) % cap], i < count, sorted by n
  String epoch;
  int64_t seq = 0;
  int cols = 0, rows = 0;  // the sender's
  uint32_t ver = 0;
  // the view
  bool follow = true;
  int32_t anchorN = 0;  // when not following: the line at the top and its visual row
  int anchorSub = 0;
  int fresh = 0;        // lines that came since the person scrolled up
  bool full = false;    // the ring is full: the oldest lines go (traced once)
  int pan = 0;          // wide: the first column shown
};

std::map<String, Buf*> bufs;

Line& at(Buf& b, int i) { return b.ring[(b.head + i) % b.cap]; }

void freeLine(Line& l) {
  heap_caps_free(l.t);
  l.t = nullptr, l.r = nullptr, l.nr = 0;
}

void clearBuf(Buf& b) {
  for (int i = 0; i < b.count; i++) freeLine(at(b, i));
  b.count = 0, b.head = 0, b.fresh = 0, b.follow = true, b.pan = 0;
  b.ver++;
}

Buf* bufOf(const String& key, int cap = 0) {
  auto it = bufs.find(key);
  if (it != bufs.end()) return it->second;
  Buf* b = new Buf;
  b->cap = cap > 0 ? constrain(cap, 50, 5000) : 2000;
  b->ring = (Line*)heap_caps_calloc(b->cap, sizeof(Line), MALLOC_CAP_SPIRAM);
  if (!b->ring) {
    delete b;
    return nullptr;
  }
  bufs[key] = b;
  return b;
}

void dropBuf(const String& key) {
  auto it = bufs.find(key);
  if (it == bufs.end()) return;
  clearBuf(*it->second);
  heap_caps_free(it->second->ring);
  delete it->second;
  bufs.erase(it);
}

// The index of the first line with n >= want (count when none).
int lowerBound(Buf& b, int32_t want) {
  int lo = 0, hi = b.count;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (at(b, mid).n < want) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

// Puts one line in (replacing the same n). false: no memory, or older than everything kept while full.
bool put(Buf& b, int32_t n, const char* text, JsonArrayConst runs) {
  size_t len = strnlen(text, MAX_TEXT);
  while (len && text[len - 1] == ' ') len--;
  const int nr = std::min((int)runs.size(), MAX_RUNS);
  const size_t off = (len + 1 + 3) & ~(size_t)3;
  char* blk = (char*)heap_caps_malloc(off + nr * sizeof(Run), MALLOC_CAP_SPIRAM);
  if (!blk) return false;
  memcpy(blk, text, len);
  blk[len] = 0;
  Run* r = (Run*)(blk + off);
  int k = 0;
  for (JsonArrayConst a : runs) {
    if (k >= nr) break;
    r[k].start = a[0] | 0, r[k].len = a[1] | 0, r[k].fg = a[2] | 0u, r[k].bg = a[3] | 0u, r[k].attr = a[4] | 0;
    k++;
  }
  int i = lowerBound(b, n);
  if (i < b.count && at(b, i).n == n) {  // the same line, changed
    Line& l = at(b, i);
    freeLine(l);
    l.t = blk, l.r = r, l.nr = k, l.rowsAt = 0;
    return true;
  }
  if (b.count == b.cap) {
    if (i == 0) {  // older than everything we keep: not kept
      heap_caps_free(blk);
      return false;
    }
    b.full = true;
    Line& old = at(b, 0);  // the oldest goes
    if (!b.follow && old.n >= b.anchorN) b.anchorN = old.n + 1, b.anchorSub = 0;
    freeLine(old);
    b.head = (b.head + 1) % b.cap, b.count--, i--;
  }
  // make room at i (lines come at the end almost always: nothing to move then)
  for (int j = b.count; j > i; j--) at(b, j) = at(b, j - 1);
  b.count++;
  Line& l = at(b, i);
  l.n = n, l.t = blk, l.r = r, l.nr = k, l.rowsAt = 0, l.rows = 1;
  if (!b.follow && i == b.count - 1) b.fresh++;
  return true;
}

// ---------------------------------------------------------------- characters
// UTF-8 → code points (at most cap); returns how many.
int decode(const char* s, uint32_t* out, int cap) {
  int n = 0;
  const uint8_t* p = (const uint8_t*)s;
  while (*p && n < cap) {
    uint32_t c = *p++;
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (more) c &= 0x3F >> more;
    while (more-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
    out[n++] = c;
  }
  return n;
}

bool emojiWide(uint32_t c) {  // emoji below U+1F300 that a terminal draws two columns wide
  switch (c) {
    case 0x231A: case 0x231B: case 0x23E9: case 0x23EA: case 0x23EB: case 0x23EC: case 0x23F0: case 0x23F3:
    case 0x2614: case 0x2615: case 0x26A1: case 0x26AA: case 0x26AB: case 0x26BD: case 0x26BE: case 0x26D4:
    case 0x2705: case 0x270A: case 0x270B: case 0x2728: case 0x274C: case 0x274E: case 0x2753: case 0x2754:
    case 0x2755: case 0x2757: case 0x2795: case 0x2796: case 0x2797: case 0x27B0: case 0x2B1B: case 0x2B1C:
    case 0x2B50: case 0x2B55:
      return true;
    default:
      return false;
  }
}

int cellsOf(uint32_t c) {  // how many columns a character takes
  if ((c >= 0x300 && c <= 0x36F) || (c >= 0x200B && c <= 0x200F) || (c >= 0xFE00 && c <= 0xFE0F)) return 0;
  if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
      (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
      (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1FAFF) || (c >= 0x20000 && c <= 0x3FFFD) || emojiWide(c))
    return 2;
  return 1;
}

uint32_t substitute(uint32_t c) {  // marks Claude Code prints that the mono font lacks
  switch (c) {
    case 0x23FA: return 0x25CF;  // ⏺ → ●
    case 0x23BF: return 0x2514;  // ⎿ → └
    case 0x23F5: return 0x25B6;  // ⏵ → ▶
    case 0x23AF: return 0x2500;  // ⎯ → ─
    // emoji (the mono font has none): the nearest mark, else a diamond, never a box
    case 0x2705: return 0x2713;  // ✅ → ✓
    case 0x274C: case 0x274E: return 0x2717;  // ❌ → ✗
    case 0x2728: return 0x2726;  // ✨ → ✦
    case 0x2B50: return 0x2605;  // ⭐ → ★
    case 0x26A1: return 0x21AF;  // ⚡ → ↯
    case 0x2753: case 0x2754: return '?';
    case 0x2755: case 0x2757: return '!';
    case 0x2795: return '+';
    case 0x2796: return 0x2212;
    case 0x2797: return 0x00F7;
    default: return (c >= 0x1F300 && c <= 0x1FAFF) || emojiWide(c) ? 0x25C7 : c;  // ◇ in two cells
  }
}

bool isMark(uint32_t c) {  // a line that starts "<mark> " hangs its next rows under the text after the mark
  switch (c) {
    case 0x23FA: case 0x23BF: case 0x25CF: case 0x273B: case 0x2736: case 0x2733: case 0x2722: case 0x273D:
    case '>': case 0x276F: case '-': case '*': case 0x2022: case 0x2713: case 0x2717: case 0x2714: case 0x2718:
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------- wrapping ("fit")
struct Row {
  uint16_t a, b;    // characters [a, b) of the line
  uint8_t indent;   // columns of space before them
  uint8_t kind;     // 0 text, 1 a box's top or bottom drawn again at the view's width, 2 text inside a box's sides
};

// One line's characters while it is laid out or drawn, in PSRAM (internal RAM is the board's scarcest).
uint32_t* cps = nullptr;
int ncp = 0;
bool scratch() {
  if (!cps) cps = (uint32_t*)heap_caps_malloc(1000 * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
  return cps != nullptr;
}
std::vector<Row> rowsTmp;

void wrapRange(int a, int b, int cols, int hang, std::vector<Row>& out, uint8_t kind) {
  if (cols < 4) cols = 4;
  if (a >= b) {
    out.push_back({(uint16_t)a, (uint16_t)a, 0, kind});
    return;
  }
  bool first = true;
  int i = a;
  while (i < b) {
    const int ind = first ? 0 : hang;
    const int room = cols - ind;
    int w = 0, j = i, lastSpace = -1;
    while (j < b) {
      const int cw = cellsOf(cps[j]);
      if (w + cw > room) break;
      if (cps[j] == ' ') lastSpace = j;
      w += cw;
      j++;
    }
    if (j < b && lastSpace > i + room / 3) j = lastSpace;  // break at a space when there is one far enough in
    if (j == i) j = i + 1;                                   // a character wider than the room
    out.push_back({(uint16_t)i, (uint16_t)j, (uint8_t)ind, kind});
    i = j;
    while (i < b && cps[i] == ' ') i++;
    first = false;
  }
}

bool cornerL(uint32_t c) { return c == 0x256D || c == 0x2570 || c == 0x250C || c == 0x2514; }
bool cornerR(uint32_t c) { return c == 0x256E || c == 0x256F || c == 0x2510 || c == 0x2518; }
bool horiz(uint32_t c) { return c == 0x2500 || c == 0x2501 || c == 0x2550 || c == ' '; }

// The visual rows of a line at `cols` (wide: one row, untouched). cps[] holds its characters afterwards.
void layout(const Line& l, int cols, bool wide, std::vector<Row>& out) {
  out.clear();
  ncp = scratch() ? decode(l.t, cps, 1000) : 0;
  if (wide || !ncp) {
    out.push_back({0, (uint16_t)ncp, 0, 0});
    return;
  }
  int t0 = 0, t1 = ncp - 1;
  while (t0 < ncp && cps[t0] == ' ') t0++;
  while (t1 > t0 && cps[t1] == ' ') t1--;
  if (t0 < t1 && cornerL(cps[t0]) && cornerR(cps[t1])) {
    bool plain = true;
    for (int i = t0 + 1; i < t1 && plain; i++) plain = horiz(cps[i]);
    if (plain) {
      out.push_back({(uint16_t)t0, (uint16_t)(t1 + 1), 0, 1});
      return;
    }
  }
  if (t0 < t1 && cps[t0] == 0x2502 && cps[t1] == 0x2502) {  // │ text │: the text wraps inside the box
    int a = t0 + 1, b = t1;
    if (a < b && cps[a] == ' ') a++;
    while (b > a && cps[b - 1] == ' ') b--;
    int hang = 0;
    while (a + hang < b && cps[a + hang] == ' ') hang++;
    wrapRange(a, b, cols - 4, std::min(hang, (cols - 4) / 3), out, 2);
    return;
  }
  int hang = t0;
  if (t0 + 1 < ncp && isMark(cps[t0]) && cps[t0 + 1] == ' ') {
    hang = t0 + 2;
    while (hang < ncp && cps[hang] == ' ') hang++;
  }
  wrapRange(0, ncp, cols, std::min(hang, cols / 3), out, 0);
}

int rowsOf(Line& l, int cols, bool wide) {
  if (wide) return 1;
  if (l.rowsAt != cols) {
    layout(l, cols, false, rowsTmp);
    l.rows = std::max<int>(1, rowsTmp.size()), l.rowsAt = cols;
  }
  return l.rows;
}

// ---------------------------------------------------------------- fonts from the app's package
struct Font {
  uint8_t* data = nullptr;
  const lv_font_t* f = nullptr;
};
std::map<int, Font> monoFonts;  // px * 2 + bold

const lv_font_t* monoFont(int px, bool bold) {
  auto it = monoFonts.find(px * 2 + bold);
  if (it != monoFonts.end()) return it->second.f;
  Font& f = monoFonts[px * 2 + bold];
  char name[24];
  snprintf(name, sizeof name, "fonts/%c%d.bin", bold ? 'b' : 'm', px);
  pkg::File file;
  if (pkg::open(appId().c_str(), name, file) && file.size() < 256 * 1024) {
    f.data = pkg::loadAll(file);
    if (f.data) f.f = fonts::fromFile(f.data, file.size(), px, bold);
    if (!f.f) heap_caps_free(f.data), f.data = nullptr;
  }
  return f.f;
}

// ---------------------------------------------------------------- colours
uint32_t mix(uint32_t a, uint32_t b, int pctA) {
  auto ch = [&](int s) { return ((((a >> s) & 255) * pctA + ((b >> s) & 255) * (100 - pctA)) / 100) & 255; };
  return ch(16) << 16 | ch(8) << 8 | ch(0);
}

float lum(uint32_t c) {
  auto lin = [](float v) { v /= 255; return v <= 0.03928f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); };
  return 0.2126f * lin(c >> 16 & 255) + 0.7152f * lin(c >> 8 & 255) + 0.0722f * lin(c & 255);
}

float contrast(uint32_t a, uint32_t b) {
  const float x = lum(a), y = lum(b);
  return (std::max(x, y) + 0.05f) / (std::min(x, y) + 0.05f);
}

const uint32_t DARK16[16] = {0x5c6370, 0xff6b6b, 0x4ec97b, 0xe5c07b, 0x6cb6ff, 0xc792ea, 0x56d4dd, 0xd0d4da,
                             0x7d8696, 0xff8787, 0x7ee2a0, 0xffd479, 0x8ccbff, 0xdab3ff, 0x8ae6ea, 0xffffff};
const uint32_t LIGHT16[16] = {0x1d2026, 0xc62828, 0x1a7f37, 0x8a6100, 0x1f6feb, 0x8250df, 0x0e7490, 0x57606a,
                              0x6e7781, 0xd1242f, 0x2da44e, 0x9a6700, 0x0969da, 0xa475f9, 0x1b7c83, 0x24292f};

uint32_t xterm(int i, bool dark) {  // 0..255
  if (i < 16) return (dark ? DARK16 : LIGHT16)[i];
  if (i < 232) {
    static const uint8_t v[6] = {0, 95, 135, 175, 215, 255};
    i -= 16;
    return v[i / 36] << 16 | v[(i / 6) % 6] << 8 | v[i % 6];
  }
  const int g = 8 + 10 * (i - 232);
  return g << 16 | g << 8 | g;
}

struct Theme {
  uint32_t bg, fg;
  bool dark;
};

// A foreground readable on the theme's background (3:1 at least): moved towards white (dark) or black (light).
uint32_t readable(uint32_t c, const Theme& th) {
  for (int k = 0; k < 6 && contrast(c, th.bg) < 3.0f; k++) c = mix(c, th.dark ? 0xffffff : 0x000000, 75);
  return c;
}

uint32_t fgOf(uint32_t v, const Theme& th) {
  if (!v) return th.fg;
  if (v >= 0x1000000) return readable(v & 0xffffff, th);
  return readable(xterm(std::min<int>(v - 1, 255), th.dark), th);
}

// A background colour kept in the theme's family: a dark diff colour becomes a light tint on a light theme.
uint32_t bgOf(uint32_t v, const Theme& th) {
  uint32_t c = v >= 0x1000000 ? (v & 0xffffff) : xterm(std::min<int>(v - 1, 255), th.dark);
  const float l = lum(c);
  if (!th.dark && l < 0.35f) c = mix(c, 0xffffff, 22);
  if (th.dark && l > 0.45f) c = mix(c, 0x000000, 30);
  return c;
}

// ---------------------------------------------------------------- the view
struct View {
  String key;
  int px = 12;
  bool wide = false;
  uint32_t bg = 0, fg = 0;  // 0: the theme's
  int onTap = -1, onScroll = -1;
  String pill;
  // touch
  lv_point_t start = {0, 0};
  int accY = 0, accX = 0;
  bool dragged = false;
};

Theme themeOf(const View& v) {
  const bool dark = pal().dark;
  return {v.bg ? v.bg : (dark ? 0x080b10u : 0xffffffu), v.fg ? v.fg : (dark ? 0xe3e7eeu : 0x1d2026u), dark};
}

struct Metrics {
  const lv_font_t* font;
  const lv_font_t* bold;
  int cw, lh, cols, rows, padX, padY;
};

Metrics metricsOf(lv_obj_t* o, const View& v) {
  Metrics m;
  m.font = monoFont(v.px, false);
  m.bold = monoFont(v.px, true);
  if (!m.font) m.font = fonts::text(v.px, false);
  m.cw = std::max<int>(4, lv_font_get_glyph_width(m.font, 'M', 0));
  m.lh = std::max<int>(8, lv_font_get_line_height(m.font));
  m.padX = 4, m.padY = 2;
  m.cols = std::max(10, ((int)lv_obj_get_width(o) - 2 * m.padX - 4) / m.cw);
  m.rows = std::max(1, ((int)lv_obj_get_height(o) - 2 * m.padY) / m.lh);
  return m;
}

int totalRows(Buf& b, const Metrics& m, bool wide) {
  int t = 0;
  for (int i = 0; i < b.count; i++) t += rowsOf(at(b, i), m.cols, wide);
  return t;
}

// The visual row index of the view's top (absolute, from the first line kept).
int topRow(Buf& b, const Metrics& m, bool wide) {
  const int total = totalRows(b, m, wide);
  if (b.follow) return std::max(0, total - m.rows);
  int r = 0;
  const int i = lowerBound(b, b.anchorN);
  for (int k = 0; k < i && k < b.count; k++) r += rowsOf(at(b, k), m.cols, wide);
  if (i < b.count) r += std::min(b.anchorSub, rowsOf(at(b, i), m.cols, wide) - 1);
  return std::min(r, std::max(0, total - m.rows));
}

void anchorAt(Buf& b, const Metrics& m, bool wide, int row) {
  int r = 0;
  for (int k = 0; k < b.count; k++) {
    const int n = rowsOf(at(b, k), m.cols, wide);
    if (row < r + n) {
      b.anchorN = at(b, k).n, b.anchorSub = row - r;
      return;
    }
    r += n;
  }
  b.anchorN = b.count ? at(b, b.count - 1).n : 0, b.anchorSub = 0;
}

// Scrolls by d rows (negative: back in time). Following again once the end is in view.
void scrollBy(lv_obj_t* o, View& v, Buf& b, int d) {
  const Metrics m = metricsOf(o, v);
  const int total = totalRows(b, m, v.wide);
  const int maxTop = std::max(0, total - m.rows);
  const int top = constrain(topRow(b, m, v.wide) + d, 0, maxTop);
  const bool wasFollowing = b.follow;
  if (top >= maxTop) {
    b.follow = true, b.fresh = 0;
  } else {
    b.follow = false;
    anchorAt(b, m, v.wide, top);
  }
  if (wasFollowing != b.follow && v.onScroll >= 0) fire(v.onScroll, b.follow ? 1 : 0);
  lv_obj_invalidate(o);
}

lv_area_t pillArea(lv_obj_t* o, const View& v, const Buf& b) {
  lv_area_t c, a = {0, 0, -1, -1};
  if (b.follow || !b.fresh || !v.pill.length()) return a;
  lv_obj_get_coords(o, &c);
  const int w = 170, h = 28;
  a.x1 = (c.x1 + c.x2) / 2 - w / 2, a.x2 = a.x1 + w;
  a.y2 = c.y2 - 8, a.y1 = a.y2 - h;
  return a;
}

String pillText(const View& v, int n) {
  String s = v.pill;
  s.replace("{n}", String(n));
  return s;
}

// One row's characters: runs of same-styled letters the mono font has (one label each, letter by letter on the grid
// because every one of them is one cell wide), the others (wide, from another font) one by one in their cells.
struct Batch {
  char text[MAX_TEXT + 8];
  int bytes = 0, col = 0, cells = 0;
  uint32_t fg = 0;
  const lv_font_t* font = nullptr;
  bool under = false;
};

void utf8(char* out, int& n, uint32_t c) {
  if (c < 0x80) out[n++] = (char)c;
  else if (c < 0x800) out[n++] = (char)(0xC0 | c >> 6), out[n++] = (char)(0x80 | (c & 0x3F));
  else if (c < 0x10000)
    out[n++] = (char)(0xE0 | c >> 12), out[n++] = (char)(0x80 | (c >> 6 & 0x3F)), out[n++] = (char)(0x80 | (c & 0x3F));
  else
    out[n++] = (char)(0xF0 | c >> 18), out[n++] = (char)(0x80 | (c >> 12 & 0x3F)), out[n++] = (char)(0x80 | (c >> 6 & 0x3F)),
    out[n++] = (char)(0x80 | (c & 0x3F));
}

struct Drawer {
  lv_layer_t* layer;
  const Line* l;
  int x0, y, cols, pan;
  const Metrics* m;
  const Theme* th;
  Batch bt;

  void flush() {
    if (!bt.bytes) return;
    bt.text[bt.bytes] = 0;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.flag = LV_TEXT_FLAG_EXPAND;
    d.text = bt.text;
    d.text_local = 1;
    d.font = bt.font;
    d.color = C(bt.fg);
    d.decor = bt.under ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE;
    lv_area_t a = {(int32_t)(x0 + bt.col * m->cw), (int32_t)y, (int32_t)(x0 + (bt.col + bt.cells + 1) * m->cw),
                   (int32_t)(y + m->lh - 1)};
    lv_draw_label(layer, &d, &a);
    bt.bytes = 0, bt.cells = 0;
  }

  void style(int ci, uint32_t& fg, uint32_t& bg, bool& bold, bool& under) {
    uint32_t f = 0, b = 0;
    uint8_t at = 0;
    for (int k = 0; ci >= 0 && k < l->nr; k++)
      if (ci >= l->r[k].start && ci < l->r[k].start + l->r[k].len) f = l->r[k].fg, b = l->r[k].bg, at = l->r[k].attr;
    fg = fgOf(f, *th);
    bg = b ? bgOf(b, *th) : 0;
    if (at & 16) {
      const uint32_t f2 = bg ? bg : th->bg;
      bg = fg, fg = f2;
    }
    if (at & 2) fg = mix(fg, bg ? bg : th->bg, 60);
    bold = at & 1, under = at & 8;
  }

  // One character at column `col` (of the line), styled as character `ci` (-1: as `fixed`).
  void put(int col, int ci, uint32_t ch, uint32_t fixed) {
    const int vc = col - pan, w = std::max(1, cellsOf(ch));
    if (vc < 0 || vc + w > cols) return;
    uint32_t fg = fixed, bg = 0;
    bool bold = false, under = false;
    if (ci >= 0) style(ci, fg, bg, bold, under);
    if (bg) {
      flush();
      lv_draw_rect_dsc_t r;
      lv_draw_rect_dsc_init(&r);
      r.bg_color = C(bg);
      lv_area_t a = {(int32_t)(x0 + vc * m->cw), (int32_t)y, (int32_t)(x0 + (vc + w) * m->cw - 1), (int32_t)(y + m->lh - 1)};
      lv_draw_rect(layer, &r, &a);
    }
    if (ch == ' ') return;
    const lv_font_t* f = bold && m->bold ? m->bold : m->font;
    if (bold && !m->bold) fg = th->dark ? mix(fg, 0xffffff, 80) : mix(fg, 0x000000, 80);
    const uint32_t c = substitute(ch);
    lv_font_glyph_dsc_t g;
    const bool own = w == 1 && lv_font_get_glyph_dsc(f, &g, c, 0) && g.resolved_font == f;
    if (own) {
      const bool join = bt.bytes && bt.fg == fg && bt.font == f && bt.under == under && !bg && vc >= bt.col + bt.cells &&
                        vc - (bt.col + bt.cells) <= 3 && bt.bytes < MAX_TEXT;
      if (!join) flush();
      if (!bt.bytes) bt.col = vc, bt.fg = fg, bt.font = f, bt.under = under;
      while (bt.col + bt.cells < vc) bt.text[bt.bytes++] = ' ', bt.cells++;  // the spaces between, as spaces
      utf8(bt.text, bt.bytes, c);
      bt.cells++;
      return;
    }
    flush();
    lv_draw_label_dsc_t one;
    lv_draw_label_dsc_init(&one);
    one.font = f;
    one.color = C(fg);
    const int adv = lv_font_get_glyph_width(f, c, 0);
    lv_point_t pt = {(int32_t)(x0 + vc * m->cw + (w * m->cw - adv) / 2), (int32_t)y};
    lv_draw_character(layer, &one, &pt, c);
  }
};

void drawRow(lv_layer_t* layer, const Line& l, const Row& row, int x0, int y, const Metrics& m, const Theme& th, int cols,
             int pan) {
  static Drawer* drp = nullptr;  // UI thread only; its 4 KB batch buffer in PSRAM
  if (!drp) {
    void* m = heap_caps_calloc(1, sizeof(Drawer), MALLOC_CAP_SPIRAM);
    if (!m) return;
    drp = new (m) Drawer();
  }
  Drawer& dr = *drp;
  dr.layer = layer, dr.l = &l, dr.x0 = x0, dr.y = y, dr.cols = cols, dr.pan = pan, dr.m = &m, dr.th = &th;
  dr.bt.bytes = 0, dr.bt.cells = 0;
  uint32_t fg, bg;
  bool bold, under;
  if (row.kind == 1) {  // a box's top or bottom at the view's width: its corners, ─ between
    dr.style(row.a, fg, bg, bold, under);
    dr.put(0, -1, cps[row.a], fg);
    for (int c = 1; c < cols - 1; c++) dr.put(c, -1, 0x2500, fg);
    dr.put(cols - 1, -1, cps[row.b - 1], fg);
    dr.flush();
    return;
  }
  int col = row.indent;
  if (row.kind == 2) {  // inside a box: its sides at the view's edges
    int side = 0;
    while (side < ncp && cps[side] != 0x2502) side++;
    dr.style(side, fg, bg, bold, under);
    dr.put(0, -1, 0x2502, fg);
    col += 2;
    for (int i = row.a; i < row.b; i++) {
      if (!cellsOf(cps[i])) continue;
      dr.put(col, i, cps[i], th.fg);
      col += cellsOf(cps[i]);
    }
    dr.put(cols - 1, -1, 0x2502, fg);
    dr.flush();
    return;
  }
  for (int i = row.a; i < row.b; i++) {
    if (!cellsOf(cps[i])) continue;
    dr.put(col, i, cps[i], th.fg);
    col += cellsOf(cps[i]);
  }
  dr.flush();
}

void onDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  View* v = (View*)lv_obj_get_user_data(o);
  if (!v) return;
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_coords(o, &c);
  const Theme th = themeOf(*v);
  lv_draw_rect_dsc_t r;
  lv_draw_rect_dsc_init(&r);
  r.bg_color = C(th.bg);
  lv_draw_rect(layer, &r, &c);
  auto it = bufs.find(v->key);
  if (it == bufs.end()) return;
  Buf& b = *it->second;
  const Metrics m = metricsOf(o, *v);
  const int cols = v->wide ? std::max(m.cols, b.cols) : m.cols;
  const int viewCols = m.cols;
  const int top = topRow(b, m, v->wide);
  const int total = totalRows(b, m, v->wide);
  // the first line on view
  int r0 = 0, i = 0;
  for (; i < b.count; i++) {
    const int n = rowsOf(at(b, i), m.cols, v->wide);
    if (r0 + n > top) break;
    r0 += n;
  }
  int y = c.y1 + m.padY, shown = 0;
  const int x0 = c.x1 + m.padX;
  for (; i < b.count && shown < m.rows; i++) {
    Line& l = at(b, i);
    layout(l, m.cols, v->wide, rowsTmp);
    for (size_t k = 0; k < rowsTmp.size() && shown < m.rows; k++) {
      if (r0 + (int)k < top) continue;
      drawRow(layer, l, rowsTmp[k], x0, y, m, th, viewCols, v->wide ? b.pan : 0);
      y += m.lh, shown++;
    }
    r0 += rowsTmp.size();
  }
  (void)cols;
  // the scroll bar
  if (total > m.rows) {
    lv_draw_rect_dsc_t s;
    lv_draw_rect_dsc_init(&s);
    s.bg_color = C(mix(th.fg, th.bg, 40));
    s.radius = 2;
    const int h = lv_area_get_height(&c);
    const int bh = std::max(18, h * m.rows / total);
    const int by = c.y1 + (h - bh) * top / std::max(1, total - m.rows);
    lv_area_t a = {c.x2 - 4, by, c.x2 - 2, by + bh};
    lv_draw_rect(layer, &s, &a);
  }
  if (v->wide && b.cols > viewCols) {
    lv_draw_rect_dsc_t s;
    lv_draw_rect_dsc_init(&s);
    s.bg_color = C(mix(th.fg, th.bg, 40));
    s.radius = 2;
    const int w = lv_area_get_width(&c);
    const int bw = std::max(18, w * viewCols / b.cols);
    const int bx = c.x1 + (w - bw) * b.pan / std::max(1, b.cols - viewCols);
    lv_area_t a = {bx, c.y2 - 4, bx + bw, c.y2 - 2};
    lv_draw_rect(layer, &s, &a);
  }
  const lv_area_t pa = pillArea(o, *v, b);
  if (pa.x2 > pa.x1) {
    lv_draw_rect_dsc_t s;
    lv_draw_rect_dsc_init(&s);
    s.bg_color = C(col::IOS_BLUE);
    s.radius = 14;
    lv_draw_rect(layer, &s, &pa);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = m.font;  // the mono font: it has the arrows the OS's text font lacks
    d.color = C(0xffffff);
    d.align = LV_TEXT_ALIGN_CENTER;
    const String t = pillText(*v, b.fresh);
    d.text = t.c_str();
    d.text_local = 1;
    lv_area_t ta = pa;
    ta.y1 += (lv_area_get_height(&pa) - lv_font_get_line_height(d.font)) / 2;
    lv_draw_label(layer, &d, &ta);
  }
}

void onTouch(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  View* v = (View*)lv_obj_get_user_data(o);
  auto it = v ? bufs.find(v->key) : bufs.end();
  if (it == bufs.end()) return;
  Buf& b = *it->second;
  lv_indev_t* in = lv_indev_active();
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    if (in) lv_indev_get_point(in, &v->start);
    v->accX = v->accY = 0, v->dragged = false;
  } else if (code == LV_EVENT_PRESSING && in) {
    lv_point_t d;
    lv_indev_get_vect(in, &d);
    v->accY += d.y, v->accX += d.x;
    const Metrics m = metricsOf(o, *v);
    if (abs(v->accY) >= m.lh) {
      const int rows = v->accY / m.lh;
      v->accY -= rows * m.lh;
      v->dragged = true;
      scrollBy(o, *v, b, -rows);
    }
    if (v->wide && abs(v->accX) >= m.cw) {
      const int c = v->accX / m.cw;
      v->accX -= c * m.cw;
      v->dragged = true;
      b.pan = constrain(b.pan - c, 0, std::max(0, b.cols - m.cols));
      lv_obj_invalidate(o);
    }
  } else if (code == LV_EVENT_RELEASED && !v->dragged) {
    lv_point_t p = v->start;
    const lv_area_t pa = pillArea(o, *v, b);
    if (pa.x2 > pa.x1 && p.x >= pa.x1 && p.x <= pa.x2 && p.y >= pa.y1 && p.y <= pa.y2) {
      b.follow = true, b.fresh = 0;
      if (v->onScroll >= 0) fire(v->onScroll, 1);
      lv_obj_invalidate(o);
    } else if (v->onTap >= 0) {
      fire(v->onTap, -1);
    }
  }
}

std::vector<lv_obj_t*> views;  // on screen now (redrawn when their buffer changes)

void invalidateKey(const String& key) {
  for (lv_obj_t* o : views) {
    View* v = (View*)lv_obj_get_user_data(o);
    if (v && v->key == key) lv_obj_invalidate(o);
  }
}

// ui.term({key, font, wrap, lines, bg, fg, h, pill, onTap, onScroll})
void nTerm(lv_obj_t* parent, JSValueConst o, int width) {
  const String key = str(o, "key");
  if (!key.length()) {
    hint(parent, "ui.term needs a key");
    return;
  }
  lv_obj_t* t = kept(o);
  if (t) {
    lv_obj_set_parent(t, parent);
  } else {
    t = lv_obj_create(parent);
    lv_obj_remove_style_all(t);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_GESTURE_BUBBLE);
    View* v = new View;
    v->key = key;
    lv_obj_set_user_data(t, v);
    lv_obj_add_event_cb(t, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(t, onTouch, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(t, onTouch, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(t, onTouch, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(t, [](lv_event_t* e) {
      lv_obj_t* me = (lv_obj_t*)lv_event_get_current_target(e);
      views.erase(std::remove(views.begin(), views.end(), me), views.end());
      delete (View*)lv_obj_get_user_data(me);
      lv_obj_set_user_data(me, nullptr);
    }, LV_EVENT_DELETE, nullptr);
    views.push_back(t);
    keep(o, t);
  }
  View* v = (View*)lv_obj_get_user_data(t);
  const int px = constrain(num(o, "font", 12), 8, 24);
  const bool wide = str(o, "wrap", "fit") == "wide";
  if (px != v->px || wide != v->wide) lv_obj_invalidate(t);
  v->px = px, v->wide = wide;
  v->bg = color(o, "bg", 0), v->fg = color(o, "fg", 0);
  v->onTap = handler(o, "onTap"), v->onScroll = handler(o, "onScroll");
  v->pill = str(o, "pill");
  bufOf(key, num(o, "lines", 0));
  lv_obj_set_size(t, width, constrain(num(o, "h", 200), 40, 2000));
}

// ---------------------------------------------------------------- natives
String argStr(JSContext* ctx, int argc, JSValueConst* argv, int i) {
  if (i >= argc) return "";
  const char* s = JS_ToCString(ctx, argv[i]);
  String out = s ? s : "";
  JS_FreeCString(ctx, s);
  return out;
}

// __termApply(key, text) -> {epoch, seq, full, starting, lines}: a screen answer (desktop-protocol.md §9.2) or the
// `before` answer ({epoch, lines}) into the key's lines.
JSValue nApply(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const String key = argStr(ctx, argc, argv, 0);
  size_t len = 0;
  const char* text = argc > 1 ? JS_ToCStringLen(ctx, &len, argv[1]) : nullptr;
  if (!text) return JS_ThrowTypeError(ctx, "term.apply(key, text)");
  JsonDocument doc(&spiram);
  const DeserializationError err = deserializeJson(doc, text, len);
  JS_FreeCString(ctx, text);
  if (err) return JS_ThrowTypeError(ctx, "term.apply: not JSON (%s)", err.c_str());
  Buf* b = bufOf(key);
  if (!b) return JS_ThrowRangeError(ctx, "term.apply: out of memory");
  const String epoch = doc["epoch"] | "";
  const bool full = doc["full"] | false;
  JsonArrayConst lines = doc["lines"].as<JsonArrayConst>();
  if (epoch.length() && epoch != b->epoch) {  // the sender started its copy again: everything we have is stale
    clearBuf(*b);
    b->epoch = epoch;
  }
  if (full && lines.size()) {  // a full answer: lines from its first one on are replaced
    int32_t first = INT32_MAX;
    for (JsonObjectConst l : lines) first = std::min<int32_t>(first, l["n"] | INT32_MAX);
    while (b->count && at(*b, b->count - 1).n >= first) freeLine(at(*b, b->count - 1)), b->count--;
  }
  int added = 0;
  for (JsonObjectConst l : lines) {
    if (!l["n"].is<int32_t>()) continue;
    if (put(*b, l["n"].as<int32_t>(), l["t"] | "", l["r"].as<JsonArrayConst>())) added++;
  }
  static bool told = false;
  if (b->full && !told) told = true, trace::add("term %s: %d lines kept, the oldest go now", key.c_str(), b->cap);
  if (doc["seq"].is<int64_t>()) b->seq = doc["seq"].as<int64_t>();
  if (doc["cols"].is<int>()) b->cols = constrain(doc["cols"].as<int>(), 10, 1000);
  if (doc["rows"].is<int>()) b->rows = doc["rows"].as<int>();
  b->ver++;
  invalidateKey(key);
  JSValue r = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, r, "epoch", JS_NewString(ctx, b->epoch.c_str()));
  JS_SetPropertyStr(ctx, r, "seq", JS_NewFloat64(ctx, (double)b->seq));
  JS_SetPropertyStr(ctx, r, "full", JS_NewBool(ctx, full));
  JS_SetPropertyStr(ctx, r, "starting", JS_NewBool(ctx, doc["starting"] | false));
  JS_SetPropertyStr(ctx, r, "lines", JS_NewInt32(ctx, added));
  if (doc["at"].is<double>()) JS_SetPropertyStr(ctx, r, "at", JS_NewFloat64(ctx, doc["at"].as<double>()));  // ms the Mac read it
  return r;
}

// __termDo(key, op, arg): 0 clear, 1 end (follow again), 2 pan by arg columns, 3 forget the key
JSValue nDo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const String key = argStr(ctx, argc, argv, 0);
  int32_t op = 0, arg = 0;
  if (argc > 1) JS_ToInt32(ctx, &op, argv[1]);
  if (argc > 2) JS_ToInt32(ctx, &arg, argv[2]);
  if (op == 3) {
    dropBuf(key);
    invalidateKey(key);
    return JS_UNDEFINED;
  }
  Buf* b = bufOf(key);
  if (!b) return JS_UNDEFINED;
  if (op == 0) clearBuf(*b), b->epoch = "", b->seq = 0;
  if (op == 1) b->follow = true, b->fresh = 0;
  if (op == 2) b->pan = constrain(b->pan + arg, 0, std::max(0, b->cols - 10));
  invalidateKey(key);
  return JS_UNDEFINED;
}

// __termInfo(key) -> {follow, fresh, lines, first, last, seq, epoch, cols}
JSValue nInfo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto it = bufs.find(argStr(ctx, argc, argv, 0));
  JSValue r = JS_NewObject(ctx);
  if (it == bufs.end()) return r;
  Buf& b = *it->second;
  JS_SetPropertyStr(ctx, r, "follow", JS_NewBool(ctx, b.follow));
  JS_SetPropertyStr(ctx, r, "fresh", JS_NewInt32(ctx, b.fresh));
  JS_SetPropertyStr(ctx, r, "lines", JS_NewInt32(ctx, b.count));
  JS_SetPropertyStr(ctx, r, "first", JS_NewInt32(ctx, b.count ? at(b, 0).n : 0));
  JS_SetPropertyStr(ctx, r, "last", JS_NewInt32(ctx, b.count ? at(b, b.count - 1).n : 0));
  JS_SetPropertyStr(ctx, r, "seq", JS_NewFloat64(ctx, (double)b.seq));
  JS_SetPropertyStr(ctx, r, "epoch", JS_NewString(ctx, b.epoch.c_str()));
  JS_SetPropertyStr(ctx, r, "cols", JS_NewInt32(ctx, b.cols));
  return r;
}

// __termTail(key, count) -> the last `count` lines' text (plain, "\n" between), at most 8 KB: for the app's own use
// (the question Claude asked, the context of a reply).
JSValue nTail(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto it = bufs.find(argStr(ctx, argc, argv, 0));
  int32_t count = 20;
  if (argc > 1) JS_ToInt32(ctx, &count, argv[1]);
  String out;
  if (it != bufs.end()) {
    Buf& b = *it->second;
    for (int i = std::max<int>(0, b.count - (int)constrain(count, 1, 200)); i < b.count; i++) {
      if (out.length()) out += '\n';
      out += at(b, i).t;
    }
    if (out.length() > 8192) out = out.substring(out.length() - 8192);
  }
  return JS_NewString(ctx, out.c_str());
}

struct Registrar {
  Registrar() { jsapp::addNode("term", nTerm); }
} registrar;

}  // namespace

void install(JSContext* ctx, const pkg::Package&) {
  JSValue g = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, g, "__termApply", JS_NewCFunction(ctx, nApply, "__termApply", 2));
  JS_SetPropertyStr(ctx, g, "__termDo", JS_NewCFunction(ctx, nDo, "__termDo", 3));
  JS_SetPropertyStr(ctx, g, "__termInfo", JS_NewCFunction(ctx, nInfo, "__termInfo", 1));
  JS_SetPropertyStr(ctx, g, "__termTail", JS_NewCFunction(ctx, nTail, "__termTail", 2));
  JS_FreeValue(ctx, g);
}

void stop() {
  while (!bufs.empty()) dropBuf(bufs.begin()->first);
  for (auto& kv : monoFonts) heap_caps_free(kv.second.data);
  monoFonts.clear();
  views.clear();
}

}  // namespace jsterm
