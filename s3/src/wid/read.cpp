#pragma GCC optimize("Os")  // built when a page is made (Home shows snapshots), never per frame: small over fast
// The reading family (docs/widgets.md §3): what the person reads on their EspOS e-reader, which the server adds to
// the colour desk's dashboard as `reading` (the books read lately, the first one being read; today's seconds, the
// goal, the streaks and the last 7 days): reading (Đang đọc), stats (Thống kê đọc), books (Thư viện sách); and the
// overview (time, date, weather, next event, battery). web/app/src/os/wid/read.ts draws the same: change both.
#include <ArduinoJson.h>

#include <cstdarg>

#include "../data.h"
#include "../desk.h"
#include "../power.h"
#include "kit.h"

namespace wid {

using namespace kit;

const std::vector<Style> READING_STYLES = {{"cover", N_("Covers")}, {"text", N_("Text only")}};
const std::vector<Style> STATS_STYLES = {{"ring", N_("Goal ring")}, {"week", N_("7 days")}};
const std::vector<Style> BOOKS_STYLES = {{"covers", N_("Covers")}, {"list", N_("List")}};
const std::vector<Style> BOOKS_FLAGS = {{"s", N_("Skip the book being read")}};

namespace {

// ---------------------------------------------------------------- shared
constexpr uint32_t BAR = 0xff9500, DONE = 0x34c759;
const uint32_t COVERS[8] = {0x1e3a8a, 0x7c2d12, 0x14532d, 0x581c87, 0x9f1239, 0x0f766e, 0x92400e, 0x334155};
// a over b, k of 255.
uint32_t mix(uint32_t a, uint32_t b, uint8_t k) { return lv_color_to_u32(lv_color_mix(C(a), C(b), k)) & 0xffffff; }
// The thin lines and empty tracks of a card: a quarter of GREY over the paper.
uint32_t sep() { return mix(GREY, PAPER, 64); }
// The headings: the family's orange-brown, or the skin's dim colour.
uint32_t acc() { return SKIN.own ? 0xc2410c : GREY; }
lv_obj_t* rect(lv_obj_t* p, int x, int y, int w, int h, uint32_t color, int r = 0, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t* o = box(p, color, r);
  if (opa != LV_OPA_COVER) lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, x, y);
  return o;
}
// One line centred in [x, x + w).
void mid(lv_obj_t* p, const char* text, int x, int y, int w, int px, bool bold, uint32_t color) {
  at(p, text, x + (w - textW(text, px, bold)) / 2, y, px, bold, color);
}
// A progress bar: the track, then the filled part (at least as long as it is high).
void bar(lv_obj_t* p, int x, int y, int w, int h, int pct, uint32_t color) {
  rect(p, x, y, w, h, sep(), h / 2);
  if (pct > 0) rect(p, x, y, max(h, w * min(100, pct) / 100), h, color, h / 2);
}
// "45 min", "3 h", "3 h 55 min" into out.
const char* dur(char* out, size_t n, int m) {
  if (m < 60) snprintf(out, n, T_("%d min"), m);
  else if (m % 60) snprintf(out, n, T_("%d h %d min"), m / 60, m % 60);
  else snprintf(out, n, T_("%d h"), m / 60);
  return out;
}
// printf into a small buffer of the caller's (the widgets' short lines). A format in the firmware image is translated
// first (mark it N_).
struct Buf {
  char s[64];
  const char* operator()(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vsnprintf(s, sizeof s, i18n::trl(fmt), a);
    va_end(a);
    return s;
  }
};

struct Book {
  String t, a;
  int p;  // percent, -1 unknown (a book from the reader's card)
};
struct R {
  bool ok = false;
  int today = 0, goal = 30, streak = 0, best = 0;
  int week[7] = {0, 0, 0, 0, 0, 0, 0};
  std::vector<Book> books;
};
R readingOf() {
  R r;
  JsonObjectConst o = desk::dash()["reading"];
  r.ok = !o.isNull();
  if (!r.ok) return r;
  r.today = o["today"] | 0;
  r.goal = o["goal"] | 30;
  if (r.goal <= 0) r.goal = 30;
  r.streak = o["streak"] | 0;
  r.best = o["best"] | 0;
  for (int i = 0; i < 7; i++) r.week[i] = o["week"][i] | 0;
  for (JsonObjectConst b : o["books"].as<JsonArrayConst>()) {
    const char* t = b["t"] | "";
    if (*t) r.books.push_back({t, b["a"] | "", b["p"].isNull() ? -1 : (int)(b["p"] | 0)});
  }
  return r;
}
// Marked text or f's digits: the helpers translate.
const char* stateOf(const Book& b, Buf& f) {
  return b.p >= 100 ? N_("Finished") : b.p > 0 ? f("%d%%", b.p) : b.p == 0 ? N_("New") : "";
}

// The first letter of a text, upper case (a cover too small for its title shows it).
String initial(const String& s) {
  int n = 1;
  if (s.length() && ((uint8_t)s[0] & 0xc0) == 0xc0)
    while (n < (int)s.length() && ((uint8_t)s[n] & 0xc0) == 0x80) n++;
  return upperVi(s.substring(0, n));
}
// A made-up cover (the server has no pictures of the reader's books): a colour picked by the title, a hinge line, the
// title in white (or its first letter when it does not fit whole), the author at the foot of a tall one.
void cover(lv_obj_t* p, int x, int y, int w, int h, const Book& b) {
  uint32_t hs = 0;
  for (const char* c = b.t.c_str(); *c; c++) hs = hs * 31 + (uint8_t)*c;
  rect(p, x, y, w, h, COVERS[hs % 8], 4);
  const int hinge = max(4, w / 10);
  rect(p, x + hinge, y, 2, h, WHITE, 0, 50);
  if (w < 44) return;
  const int tx = x + hinge + 6, tw = x + w - 5 - tx;
  const int ah = b.a.length() && h >= 120 ? 20 : 0;
  if (!fitIn(p, b.t, tx, y + 6, tw, h - 12 - ah, w >= 90 ? 16 : 13, 12, true, WHITE, 0))
    mid(p, initial(b.t).c_str(), tx, y + 6, tw, 24, true, WHITE);
  if (ah) fitIn(p, b.a, tx, y + h - 22, tw, 16, 12, 12, false, 0xe8eaef, 0);
}
// The title (shrinking to fit w x h) and the author under it when both fit, at the top of the box or in its middle;
// returns the height used (0: the title does not fit whole, nothing drawn).
int titleBlock(lv_obj_t* p, const Book& b, int x, int y, int w, int h, int maxPx, bool center = false) {
  const int ah = b.a.length() ? lineH(12, false) + 2 : 0;
  const bool withA = ah > 0 && textW(b.a.c_str(), 12) <= w && measure(p, b.t, w, h - ah, maxPx, 12, true) >= 0;
  const int bh = withA ? h - ah : h;
  const int th = measure(p, b.t, w, bh, maxPx, 12, true);
  if (th < 0) return 0;
  const int used = th + (withA ? ah : 0);
  const int y0 = center ? y + (h - used) / 2 : y;
  fit(p, b.t, x, y0, w, bh, maxPx, 12, true, INK);
  if (withA) at(p, b.a, x, y0 + th + 2, 12, false, GREY);
  return used;
}
// The big percentage: its digits, then a smaller "%" sitting on their baseline. Returns its width.
int bigPct(lv_obj_t* p, int pct, int x, int y, int px, uint32_t color) {
  Buf n;
  at(p, n("%d", pct), x, y, px, true, color);
  const int nw = textW(n.s, px, true);
  at(p, "%", x + nw + 2, y + lineH(px, true) - lineH(20, true) - 4, 20, true, color);
  return nw + 2 + textW("%", 20, true);
}
// A book's progress bar: green once read through.
void bookBar(lv_obj_t* p, int x, int y, int w, int h, const Book& b) { bar(p, x, y, w, h, b.p, b.p >= 100 ? DONE : BAR); }

}  // namespace

// ---------------------------------------------------------------- reading
void wReading(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* p = c.o;
  const R r = readingOf();
  const bool cov = styleOf(it) == "cover";
  if (!r.ok || r.books.empty()) {
    cap(p, N_("Now reading"), acc());
    return empty(c, !r.ok ? noData() : N_("No books read yet on the EspOS e-reader"), GREY, 22);
  }
  const Book& b = r.books[0];
  const int mins = r.today / 60;
  Buf today, f;
  today(N_("%d/%d min today"), mins, r.goal);
  const char* pct = f("%d%%", b.p);
  Buf streak;
  streak(N_("%d-day streak"), r.streak);
  if (it.size == S) {
    if (cov) {
      // The cover, the progress under it.
      const int ch = 92, cw = 61;
      cover(p, (c.w - cw) / 2, 0, cw, ch, b);
      if (b.p >= 0) {
        bookBar(p, 0, 106, c.w - 40, 6, b);
        right(p, pct, 100, 13, true, INK);
      } else mid(p, upperVi(T_("Now reading")).c_str(), 0, 100, c.w, 12, true, acc());
      return;
    }
    cap(p, N_("Now reading"), acc());
    int bottom = c.h;
    if (b.p >= 0) {
      bookBar(p, 0, c.h - 6, c.w, 6, b);
      bottom = c.h - 10 - lineH(28, true);
      bigPct(p, b.p, -1, bottom, 28, INK);
    } else {
      bottom = c.h - lineH(12, false);
      fit(p, today.s, 0, bottom, c.w, lineH(12, false), 12, 12, false, GREY);
    }
    titleBlock(p, b, 0, 18, c.w, bottom - 4 - 18, 16);
    return;
  }
  if (it.size == M) {
    if (cov) {
      cover(p, 0, 0, 78, 118, b);
      const int x = 92, w = c.w - x;
      cap(p, N_("Now reading"), acc(), x, 0);
      int bottom = c.h;
      if (b.p >= 0) {
        bookBar(p, x, c.h - 6, w - 44, 6, b);
        right(p, pct, c.h - 14, 13, true, INK);
        bottom = c.h - 14;
      }
      const int ty = bottom - 2 - lineH(12, false);
      if (textW(today.s, 12) <= w) at(p, today.s, x, ty, 12, false, GREY);
      titleBlock(p, b, x, 18, w, ty - 4 - 18, 18);
      return;
    }
    cap(p, N_("Now reading"), acc());
    if (textW(today.s, 12) + 90 <= c.w) right(p, today.s, 0, 12, false, GREY);
    int bottom = c.h;
    if (b.p >= 0) {
      const int ph = lineH(44, true);
      bottom = c.h - ph;
      const int pw = bigPct(p, b.p, -2, bottom, 44, INK);
      bookBar(p, pw + 12, c.h - 12, c.w - pw - 12, 8, b);
      if (r.streak > 0) at(p, streak.s, pw + 12, c.h - 16 - lineH(12, false), 12, false, GREY);
    }
    titleBlock(p, b, 0, 18, c.w, bottom - 4 - 18, 22);
    return;
  }
  // Large.
  if (cov) {
    const int cw = 112, ch = 168, x = 126, w = c.w - x;
    cover(p, 0, 0, cw, ch, b);
    cap(p, N_("Now reading"), acc(), x, 0);
    const int ph = lineH(44, true);
    titleBlock(p, b, x, 18, w, ch - 18 - (b.p >= 0 ? ph + 4 : 0), 22);
    if (b.p >= 0) {
      bigPct(p, b.p, x - 2, ch - ph, 44, INK);
      bookBar(p, 0, 180, c.w, 10, b);
    }
    at(p, f(N_("Today %d/%d min"), mins, r.goal), 0, 198, 14, true, INK);
    if (r.streak > 0) right(p, streak.s, 199, 13, false, GREY);
    rect(p, 0, 224, c.w, 1, sep());
    cap(p, N_("Up next"), acc(), 0, 230);
    fit(p, r.books.size() > 1 ? r.books[1].t.c_str() : N_("No other books yet"), 0, 246, c.w, 20, 14, 12, false,
        r.books.size() > 1 ? INK : GREY, 1);
    return;
  }
  cap(p, N_("Now reading"), acc());
  const int ph = lineH(64, true);
  const int by = c.h - 26 - 12 - ph;  // the big number's top
  if (b.p >= 0) bigPct(p, b.p, -3, by, 64, INK);
  else {
    at(p, f("%d", mins), -3, by, 64, true, INK);
    at(p, N_("min today"), textW(f.s, 64, true) + 4, by + ph - lineH(16, false) - 12, 16, false, GREY);
  }
  titleBlock(p, b, 0, 22, c.w, by - 8 - 22, 28, true);
  if (b.p >= 0) bookBar(p, 0, c.h - 34, c.w, 10, b);
  at(p, b.p >= 0 ? today.s : f(N_("Goal %d min a day"), r.goal), 0, c.h - lineH(13, false), 13, false, GREY);
  if (r.streak > 0) right(p, streak.s, c.h - lineH(13, false), 13, true, INK);
}

// ---------------------------------------------------------------- stats
namespace {

// The goal's ring: an empty track all round, today's part from the top, clockwise.
void ring(lv_obj_t* p, int x, int y, int d, int wd, int pct, uint32_t color) {
  lv_obj_t* a = lv_arc_create(p);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(a, d, d);
  lv_obj_set_pos(a, x, y);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  const int end = 360 * min(100, max(0, pct)) / 100;
  lv_arc_set_angles(a, 0, end);
  lv_obj_set_style_arc_width(a, wd, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, wd, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C(sep()), LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, C(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, end < 360, LV_PART_INDICATOR);
  if (!end) lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, 0);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
}
// The last 7 days as bars (green where the goal was reached, today orange), the goal as a faint line, the weekdays
// under them; `values` puts each day's minutes over its bar.
void weekBars(lv_obj_t* p, const R& r, int x, int y, int w, int h, bool values) {
  const int labH = lineH(12, false), base = y + h - labH - 2;
  const int gap = w >= 200 ? 10 : 5, bw = (w - 6 * gap) / 7;
  const int goal = r.goal * 60;
  int top = goal;
  for (int v : r.week) top = max(top, v);
  top += top / 8;
  const int barsH = base - y - (values ? labH + 2 : 0);
  rect(p, x, base - goal * barsH / top, w, 1, GREY, 0, 120);
  const int wd0 = data::today().wday;
  for (int i = 0; i < 7; i++) {
    const int bx = x + i * (bw + gap), v = r.week[i];
    const int bh = v > 0 ? max(3, v * barsH / top) : 0;
    if (bh) rect(p, bx, base - bh, bw, bh, v >= goal ? DONE : i == 6 ? BAR : mix(BAR, PAPER, 140), min(4, bw / 2));
    else rect(p, bx, base - 2, bw, 2, sep());
    mid(p, WD_SHORT[(wd0 + i + 1) % 7], bx, base + 2, bw, 12, i == 6, i == 6 ? INK : GREY);
    Buf f;
    if (values && v >= 60) mid(p, f("%d", v / 60), bx, base - bh - labH - 1, bw, 12, true, GREY);
  }
}
// A small square of the large ring look: its name, a number, what it counts.
void tile(lv_obj_t* p, int x, int y, int w, int h, const char* name, int value, const char* unit, uint32_t tint) {
  Buf f;
  rect(p, x, y, w, h, mix(tint, PAPER, 36), 14);
  cap(p, name, GREY, x + 10, y + 10);
  fit(p, f("%d", value), x + 10, y + 28, w - 20, 26, 22, 12, true, INK, 1);
  at(p, unit, x + 10, y + h - 10 - lineH(12, false), 12, false, GREY);
}

}  // namespace

void wStats(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* p = c.o;
  const R r = readingOf();
  if (!r.ok) {
    cap(p, N_("Reading"), acc());
    return empty(c, noData(), GREY, 22);
  }
  const int mins = r.today / 60;
  const int pct = mins * 100 / r.goal;
  const uint32_t col = mins >= r.goal ? DONE : BAR;
  int weekSec = 0;
  for (int v : r.week) weekSec += v;
  const int wm = weekSec / 60;
  const bool week = styleOf(it) == "week";
  Buf f, g;
  if (it.size == S) {
    if (week) {
      cap(p, N_("7 days"), acc());
      right(p, f(N_("%d min"), wm), 0, 12, true, INK);
      weekBars(p, r, 0, 20, c.w, c.h - 20, false);
      return;
    }
    const int d = 96, x0 = (c.w - d) / 2;
    ring(p, x0, 0, d, 10, pct, col);
    const int nh = lineH(28, true), uh = lineH(12, false);
    const int ny = 48 - (nh + uh) / 2;
    mid(p, f("%d", mins), 0, ny, c.w, 28, true, INK);
    mid(p, f(N_("/%d min"), r.goal), 0, ny + nh, c.w, 12, false, GREY);
    mid(p, r.streak > 0 ? f(N_("%d-day streak"), r.streak) : f(N_("Goal %d min"), r.goal), 0, 101, c.w, 12, true, GREY);
    return;
  }
  if (it.size == M) {
    if (week) {
      cap(p, N_("Last 7 days"), acc());
      char d[24];
      right(p, f(N_("This week %s"), dur(d, sizeof d, wm)), 0, 12, true, INK);
      weekBars(p, r, 0, 20, c.w, c.h - 20, true);
      return;
    }
    ring(p, 0, 0, 118, 12, pct, col);
    const int nh = lineH(34, true), uh = lineH(12, false);
    const int ny = 59 - (nh + uh) / 2;
    mid(p, f("%d", mins), 0, ny, 118, 34, true, INK);
    mid(p, N_("min"), 0, ny + nh, 118, 12, false, GREY);
    const int x = 134, w = c.w - x;
    char d[24];
    const String days = i18n::plural(r.streak, NP_("{n} day|{n} days"));
    const char* const rows[3][2] = {{N_("Today"), f(N_("%d/%d min"), mins, r.goal)},
                                    {N_("This week"), dur(d, sizeof d, wm)},
                                    {N_("Streak"), r.streak > 0 ? days.c_str() : N_("None yet")}};
    for (int i = 0; i < 3; i++) {
      cap(p, rows[i][0], GREY, x, i * 40);
      fit(p, rows[i][1], x, i * 40 + 16, w, 22, 18, 12, true, INK, 1);
    }
    return;
  }
  if (week) {
    cap(p, N_("Last 7 days"), acc());
    right(p, f(N_("%d min"), wm), 0, 12, true, INK);
    weekBars(p, r, 0, 22, c.w, 152, true);
    rect(p, 0, 184, c.w, 1, sep());
    int met = 0, most = 0;
    for (int i = 0; i < 7; i++) {
      if (r.week[i] >= r.goal * 60) met++;
      if (r.week[i] > r.week[most]) most = i;
    }
    const int wd = (data::today().wday + most + 1) % 7;
    Buf h;
    const char* const lines[3][2] = {
        {N_("Daily average"), f(N_("%d min"), wm / 7)},
        {N_("Goal reached"), g(N_("%d/7 days"), met)},
        {N_("Best day"), r.week[most] ? h(N_("%s · %d min"), WD_SHORT[wd], r.week[most] / 60) : N_("None yet")}};
    for (int i = 0; i < 3; i++) {
      const int y = 194 + i * 24;
      at(p, lines[i][0], 0, y, 13, false, GREY);
      right(p, lines[i][1], y, 14, true, INK);
    }
    return;
  }
  const int d = 170;
  ring(p, (c.w - d) / 2, 0, d, 16, pct, col);
  const int nh = lineH(56, true), uh = lineH(14, false);
  const int ny = 85 - (nh + uh) / 2;
  mid(p, f("%d", mins), 0, ny, c.w, 56, true, INK);
  mid(p, f(N_("/ %d min"), r.goal), 0, ny + nh, c.w, 14, false, GREY);
  const int tw = (c.w - 16) / 3;
  tile(p, 0, 184, tw, 82, N_("This week"), wm, N_("min"), BAR);
  tile(p, tw + 8, 184, tw, 82, N_("Streak"), r.streak, N_("days in a row"), DONE);
  tile(p, 2 * (tw + 8), 184, tw, 82, N_("Best"), r.best, N_("days in a row"), 0x0a84ff);
}

// ---------------------------------------------------------------- books
void wBooks(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* p = c.o;
  const R r = readingOf();
  const bool skip = flag(it, "s");
  std::vector<Book> list(r.books.begin() + (skip && !r.books.empty() ? 1 : 0), r.books.end());
  const int n = list.size();
  const bool covers = styleOf(it) == "covers";
  cap(p, it.size == S ? N_("Library") : N_("Book library"), acc());
  if (!n) return empty(c, !r.ok ? noData() : N_("No books yet. Send books from the EspOS web"), GREY, 22);
  Buf f;
  right(p, i18n::plural(n, NP_("{n} book|{n} books")), 0, 12, true, GREY);
  if (covers) {
    if (it.size == S) {
      if (n == 1) return cover(p, (c.w - 61) / 2, 20, 61, 92, list[0]);
      for (int i = 0; i < 2; i++) {
        cover(p, i * 67, 18, 53, 80, list[i]);
        mid(p, stateOf(list[i], f), i * 67, 100, 53, 12, true, INK);
      }
      return;
    }
    if (it.size == M && n == 1) {
      const Book& b = list[0];
      const int x = 80, w = c.w - x;
      cover(p, 0, 18, 66, 100, b);
      if (b.p >= 0) {
        bookBar(p, x, c.h - 6, w, 6, b);
        at(p, stateOf(b, f), x, c.h - 12 - lineH(13, true), 13, true, INK);
      }
      titleBlock(p, b, x, 18, w, c.h - 18 - 34, 18);
      return;
    }
    if (it.size == M) {
      for (int i = 0; i < n && i < 4; i++) {
        cover(p, i * 73, 18, 55, 82, list[i]);
        mid(p, stateOf(list[i], f), i * 73, 102, 55, 12, true, INK);
      }
      return;
    }
    if (n <= 2) {
      // One or two big covers, each with its title, author, state and bar beside it.
      for (int i = 0; i < n; i++) {
        const Book& b = list[i];
        const int y = 22 + i * 124, x = 90, w = c.w - x;
        cover(p, 0, y, 76, 114, b);
        titleBlock(p, b, x, y, w, 70, 20);
        if (b.p >= 0) {
          at(p, stateOf(b, f), x, y + 80, 13, true, INK);
          bookBar(p, x, y + 102, w, 6, b);
        }
      }
      return;
    }
    for (int i = 0; i < n && i < 6; i++) {
      const int x = (i % 3) * 103, y = 20 + (i / 3) * 123;
      cover(p, x, y, 70, 105, list[i]);
      mid(p, stateOf(list[i], f), x, y + 106, 70, 12, true, INK);
    }
    return;
  }
  // The list: title (and author beside or under it), the state on the right, the rest counted at the foot.
  const bool lg = it.size == L;
  const int px = it.size == S ? 13 : lg ? 15 : 14;
  int y = 22, shown = 0;
  for (int i = 0; i < n; i++) {
    const Book& b = list[i];
    const char* st = stateOf(b, f);
    const int sw = *st ? textW(st, 12, true) + 8 : 0;
    const int tw = c.w - sw;
    Buf both;
    both("%s · %s", b.t.c_str(), b.a.c_str());
    const char* text = b.a.length() && it.size == M && textW(both.s, px, true) <= tw ? both.s : b.t.c_str();
    const int th = measure(p, text, tw, 40, px, 12, true);
    if (th < 0) break;  // longer than two lines even small: it and the rest are counted
    const int ah = lg && b.a.length() && textW(b.a.c_str(), 12) <= tw ? lineH(12, false) : 0;
    const int rowH = th + ah + (lg && b.p >= 0 ? 8 : 0) + 7;
    if (y + rowH - 7 + (i < n - 1 ? 16 : 0) > c.h) break;
    fit(p, text, 0, y, tw, 40, px, 12, true, INK);
    if (ah) at(p, b.a, 0, y + th, 12, false, GREY);
    if (*st) right(p, st, y + 1, 12, true, b.p >= 100 ? DONE : INK);
    if (lg && b.p >= 0) bookBar(p, 0, y + th + ah + 3, c.w, 4, b);
    y += rowH;
    shown++;
  }
  if (shown < n) at(p, i18n::plural(n - shown, NP_("+{n} more|+{n} more")), 0, min(y, c.h - 16), 12, true, GREY);
}

// ---------------------------------------------------------------- overview
namespace {

// The battery: a 22 x 11 outline with its tip and charge, then the percentage.
void battery(lv_obj_t* p, int x, int y, int pct, bool usb, uint32_t color, uint32_t dim) {
  lv_obj_t* shell = rect(p, x, y, 22, 11, dim, 3);
  lv_obj_set_style_bg_opa(shell, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(shell, 2, 0);
  lv_obj_set_style_border_color(shell, C(dim), 0);
  rect(p, x + 23, y + 3, 2, 5, dim, 1);
  rect(p, x + 3, y + 3, max(2, 16 * pct / 100), 5, usb || pct > 20 ? DONE : 0xff3b30, 1);
  Buf f;
  at(p, f("%d%%", pct), x + 30, y + 5 - lineH(12, true) / 2, 12, true, color);
}

}  // namespace

namespace {

// The overview into its box (rebuilt every minute: the time, the coming events, the battery). Large: user data set.
void fillOverview(lv_obj_t* p, const tm&, bool) {
  lv_obj_clean(p);
  const bool large = lv_obj_get_user_data(p) != nullptr;
  const struct {
    int w, h;
  } c = {(int)lv_obj_get_style_width(p, 0), (int)lv_obj_get_style_height(p, 0)};
  const data::Day day = data::today();
  const String hm = hmNow();
  // The time as big as leaves the date 70 px, a line, the date (solar and lunar), the weather at the right.
  JsonObjectConst w = data::weather();
  Buf temp, f;
  if (data::hasWeather()) temp("%d°", (int)(w["temp"] | 0));
  else temp.s[0] = 0;
  const int tw = *temp.s ? textW(temp.s, 28, true) : 0;
  const int ww = *temp.s ? 28 + 4 + tw + 6 : 0;
  const int px = textW(hm.c_str(), 44, true) + 19 + 70 + ww <= c.w ? 44 : 34;
  at(p, hm, -2, (72 - lineH(px, true)) / 2, px, true, INK);
  const int x = textW(hm.c_str(), px, true) + 19;
  rect(p, x - 11, 10, 1, 52, sep());
  if (*temp.s) {
    lv_obj_t* ic = img(p, data::wxIcon(w["icon"] | "cloud", 28, data::night()));
    lv_obj_set_pos(ic, c.w - tw - 32, 22);
    at(p, temp.s, c.w - tw, (72 - lineH(28, true)) / 2, 28, true, INK);
  }
  const int dw = c.w - ww - x;
  const data::Lunar lu = data::lunar(day);
  String full = i18n::date(N_("{M} {d}, {y}"), day.d, day.m, day.y);
  at(p, textW(WEEKDAY[day.wday], 14, true) <= dw ? WEEKDAY[day.wday] : WD_SHORT[day.wday], x, 8, 14, true,
     SKIN.own ? 0xd7261e : INK);
  if (textW(full.c_str(), 13) > dw) full = i18n::date(N_("{M} {d}"), day.d, day.m);
  at(p, full, x, 28, 13, false, INK);
  at(p, f(N_("%d/%d lunar"), lu.d, lu.m), x, 46, 12, false, GREY);  // i18n: a lunar day/month (vi "%d/%d âm")
  rect(p, 0, 78, c.w, 1, sep());
  const int pct = power::percent();
  const bool usb = power::usb();
  // The coming events: today's not yet over, then the next days'.
  const String td = data::isoDate(day), tm = data::isoDate(data::addDays(day, 1));
  const int nm = data::nowMinutes();
  std::vector<data::Event> next;
  for (const data::Event& e : data::agenda())
    if (e.date != td || (e.time.length() ? e.time.substring(0, 2).toInt() * 60 + e.time.substring(3, 5).toInt() : 24 * 60) >= nm)
      next.push_back(e);
  // "19:00" today, "Tmrw 19:00", "Nov 19 14:00", "Nov 20"; "All day" for a day-long one today.
  Buf when;
  auto whenOf = [&](const data::Event& e) {
    String d;
    if (e.date == tm) d = TC_("short", "Tomorrow");  // i18n: short (vi "Mai")
    else if (e.date != td) d = i18n::date(N_("{M} {d}"), e.date.substring(8, 10).toInt(), e.date.substring(5, 7).toInt());
    if (!d.length()) when("%s", e.time.length() ? e.time.c_str() : T_("All day"));
    else when(e.time.length() ? "%s %s" : "%s", d.c_str(), e.time.c_str());
    return when.s;
  };
  const uint32_t evCol = SKIN.own ? 0x0a84ff : INK;
  if (!large) {
    // The next event, the battery at the right.
    const int bw = 30 + textW(f("%d%%", pct), 12, true);
    battery(p, c.w - bw, 96, pct, usb, INK, GREY);
    const int ew = c.w - bw - 12;
    if (next.empty()) {
      fit(p, N_("No upcoming events"), 0, 92, ew, 18, 14, 12, false, GREY, 1);
      return;
    }
    whenOf(next[0]);
    const int wn = textW(when.s, 14, true);
    at(p, when.s, 0, 92, 14, true, evCol);
    fit(p, next[0].title, wn + 8, 92, ew - wn - 8, 18, 14, 12, false, INK, 1);
    return;
  }
  // Large: the next hours, the coming events, the battery and the last sync.
  JsonArrayConst hours = w["hours"];
  const int cw = c.w / 4;
  int i = 0;
  for (JsonObjectConst h : hours) {
    if (i == 4) break;
    const int cx = i * cw;
    const char* hl = h["h"] | "";
    mid(p, hl, cx, 86, cw, 12, false, GREY);
    lv_obj_t* ic = img(p, data::wxIcon(h["icon"] | "cloud", 28, data::hourIsNight(hl)));
    lv_obj_set_pos(ic, cx + (cw - 28) / 2, 104);
    mid(p, f("%d°", (int)(h["t"] | 0)), cx, 134, cw, 14, true, INK);
    i++;
  }
  if (!i) at(p, N_("No hourly weather yet"), 0, 110, 13, false, GREY);
  rect(p, 0, 160, c.w, 1, sep());
  int y = 168;
  for (const data::Event& e : next) {
    if (y + 20 > 238) break;
    whenOf(e);
    const int wn = textW(when.s, 13, true);
    at(p, when.s, 0, y, 13, true, evCol);
    fit(p, e.title, wn + 8, y, c.w - wn - 8, 18, 13, 12, false, INK, 1);
    y += 22;
  }
  if (next.empty()) at(p, N_("No upcoming events"), 0, y, 13, false, GREY);
  battery(p, 0, 250, pct, usb, INK, GREY);
  const int64_t last = desk::lastSyncMs();
  right(p, last ? i18n::fmt(N_("Synced {0}"), ago(last, desk::nowMs())) : String(T_("Not synced yet")),
        250 + 5 - lineH(12, false) / 2, 12, false, GREY);
}

}  // namespace

void wOverview(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* b = clear(c.o);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, c.w, c.h);
  lv_obj_set_user_data(b, it.size == L ? (void*)1 : nullptr);
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  fillOverview(b, l, false);
  live(b, fillOverview);
}

}  // namespace wid
