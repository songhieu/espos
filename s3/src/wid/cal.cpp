// The calendars family (docs/widgets.md): date, cal, lunar, count, quote, and the lunar bits they show (data.cpp).
// web/app/src/os/wid/cal.ts draws the same, line by line: change both.
#include <algorithm>

#include "../data.h"
#include "../fonts.h"
#include "kit.h"

namespace wid {

using namespace kit;

// Style names (the gallery and the editors show them through the kit helpers, which translate them).
const std::vector<Style> DATE_STYLES = {{"sheet", N_("Sheet")}, {"compact", N_("Compact")}, {"block", N_("Big day")}};
const std::vector<Style> CAL_STYLES = {{"today", N_("Today")}, {"events", N_("Events")}, {"week", N_("Week")},
                                       {"month", N_("Month")}};
const std::vector<Style> LUNAR_STYLES = {{"canchi", N_("Zodiac")},  // i18n: the stems and branches (Can chi) style
                                         {"detail", N_("Details")},
                                         {"month", N_("Lunar month")},
                                         {"big", N_("Big lunar day")}};
const std::vector<Style> COUNT_STYLES = {{"tet", N_("New Year countdown")},  // i18n: days to the Lunar New Year (vi "Tết")
                                         {"date", N_("Your date")},
                                         {"holiday", N_("Next holiday")}};
const std::vector<Style> QUOTE_STYLES = {{"day", N_("A saying a day")}, {"big", N_("Big text")}};

namespace {

using data::Day;
using data::Lunar;

// ---------------------------------------------------------------- shared
constexpr uint32_t SH_RED = 0xd7261e, NAVY = 0x1f2d5c, GOLD = 0xffd979, PALE = 0xffe2c2, CREAM = 0xffe9a8;
// i18n: the shortest weekday names, Sunday first, heading the columns of month grids (one or two letters)
const char* const WD_MIN_EN[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
const i18n::List WD_MIN = {"@weekdays_min", WD_MIN_EN, 7};
// Monday first: the grids' column i.
const char* monFirst(int i) { return WD_MIN[(i + 1) % 7]; }
const uint32_t EV_COL[4] = {0x0a84ff, 0x34c759, 0xff9500, 0xaf52de};

Day plus(const Day& d, int n) { return data::fromJulian(data::julian(d) + n); }
String dmy(const Day& x) { return i18n::date(N_("{M} {d}, {y}"), x.d, x.m, x.y); }
String dm(const Day& x) { return i18n::date(N_("{M} {d}"), x.d, x.m); }
// "2026-11-16" -> the day (wday set); false (out untouched) when it is not a date.
bool parseIso(const String& s, Day& out) {
  if (s.length() < 10 || s[4] != '-' || s[7] != '-') return false;
  const int y = s.substring(0, 4).toInt(), m = s.substring(5, 7).toInt(), dd = s.substring(8, 10).toInt();
  if (!(y > 1900 && y < 2200 && m >= 1 && m <= 12 && dd >= 1 && dd <= 31)) return false;
  const Day x = data::fromJulian(data::julian({dd, m, y, 0}));
  if (x.d != dd) return false;
  out = x;
  return true;
}
// a over b, k of 255.
uint32_t mix(uint32_t a, uint32_t b, uint8_t k) { return lv_color_to_u32(lv_color_mix(C(a), C(b), k)) & 0xffffff; }
// The thin lines of a card: a quarter of GREY over PAPER.
uint32_t sep() { return mix(GREY, PAPER, 64); }
lv_obj_t* rect(lv_obj_t* p, int x, int y, int w, int h, uint32_t color, int r = 0, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t* o = box(p, color, r);
  if (opa != LV_OPA_COVER) lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, x, y);
  return o;
}
// One line centred in [x, x + w).
lv_obj_t* mid(lv_obj_t* p, const String& text, int x, int y, int w, int px, bool bold, uint32_t color) {
  return at(p, text, x + (w - textW(text.c_str(), px, bold)) / 2, y, px, bold, color);
}
// The height of a fitted label's block.
int blockH(lv_obj_t* l) {
  int n = 1;
  for (const char* s = lv_label_get_text(l); *s; s++) n += *s == '\n';
  return n * lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)) + (n - 1) * 2;
}
// fit(), the block in the middle of the box's height.
lv_obj_t* fitV(lv_obj_t* p, const String& text, int x, int y, int w, int h, int maxPx, int minPx, bool bold, uint32_t color) {
  lv_obj_t* l = fit(p, text, x, y, w, h, maxPx, minPx, bold, color);
  lv_obj_set_y(l, y + (h - blockH(l)) / 2);
  return l;
}
// fit() with every line centred in w, the block in the middle of the box's height.
lv_obj_t* midFit(lv_obj_t* p, const String& text, int x, int y, int w, int h, int maxPx, int minPx, bool bold, uint32_t color) {
  lv_obj_t* l = fitV(p, text, x, y, w, h, maxPx, minPx, bold, color);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}
// The lucky hours (giờ hoàng đạo) by their branches: "Rat, Ox, Rabbit, Horse, Monkey, Rooster".
String goodNames(const Day& day) {
  const uint16_t bits = data::goodHours(day);
  String s;
  for (int i = 0; i < 12; i++)
    if (bits & (1 << i)) s += String(s.length() ? ", " : "") + data::CHI[i];
  return s;
}
// The first day of the tiết khí the day is in.
Day termStart(const Day& day) {
  const int now = data::solarTerm(day);
  int k = 1;
  while (k < 20 && data::solarTerm(plus(day, -k)) == now) k++;
  return plus(day, 1 - k);
}
// "Today", "Tomorrow", "Thu, Nov 19".
String dayLabel(const Day& x, const Day& td) {
  const int n = data::julian(x) - data::julian(td);
  if (n == 0) return T_("Today");
  if (n == 1) return T_("Tomorrow");
  return i18n::date(N_("{w}, {M} {d}"), x.d, x.m, x.y, x.wday);
}
// A lunar day: "7/10" (and " leap").
String lunarDM(const Lunar& l) { return i18n::fmt(N_("{0}/{1}{2}"), String(l.d), String(l.m), l.leap ? T_(" leap") : ""); }
// "... (lunar)": short (vi "âm") and long (vi "âm lịch") forms.
String lunarShort(const String& s) { return i18n::fmt(N_("{0} (lunar)"), s); }
// i18n: the same as "{0} (lunar)" where there is room for a longer form
String lunarLong(const String& s) { return i18n::fmt(TC_("long", "{0} (lunar)"), s); }
// The can chi of a year, month, day: "Fire Horse year".
String yearOf(int y) { return i18n::fmt(N_("{0} year"), data::canChiYear(y)); }
String monthOf(const Lunar& l) { return i18n::fmt(N_("{0} month"), data::canChiMonth(l)); }
String dayOf(const Day& d) { return i18n::fmt(N_("{0} day"), data::canChiDay(d)); }
// "in 5 days" (vi "còn 5 ngày").
String inDays(int n) { return i18n::plural(n, NP_("in {n} day|in {n} days")); }
int eventsOn(const std::vector<data::Event>& evs, const Day& x) {
  const String d = data::isoDate(x);
  int n = 0;
  for (const auto& e : evs) n += e.date == d;
  return n;
}
String eventLine(const data::Event& e) {
  return (e.time.length() ? e.time + " " : String(T_("All day")) + " · ") + e.title;
}
// One event row of a list: `lead` (a day), the time or "All day", the title. Too long for one line (a longer language, a
// long title): "All day" goes first, then the row takes two lines. Its height, or -1 when `room` is too small.
int eventRow(lv_obj_t* p, const String& lead, const data::Event& e, int x, int y, int w, int room, int px, uint32_t color) {
  String t = lead + eventLine(e);
  if (!e.time.length() && textW(t.c_str(), 12) > w) t = lead + e.title;
  const int h = textW(t.c_str(), 12) <= w ? 18 : 36;  // one line at some size, else two at 12 px
  if (h > room) return -1;
  return heightOf(fit(p, t, x, y, w, h, px, 12, false, color));
}
int evMin(const data::Event& e, int allDay) {
  return e.time.length() ? e.time.substring(0, 2).toInt() * 60 + e.time.substring(3, 5).toInt() : allDay;
}

// ---------------------------------------------------------------- date: sheet (a Vietnamese tear-off calendar page)
void sheetBand(Card c, int band, uint32_t paper) {
  // The red top of the page: a rounded red box, its lower corners under the paper.
  rect(c.o, -12, -10, c.w + 24, band + 20, SH_RED, 20);
  rect(c.o, -12, band - 10, c.w + 24, 20, paper);
}

void sheet(Card c, Size size, const Day& day, const Lunar& l, const String& hol) {
  lv_obj_t* p = c.o;
  const int W = c.w + 24;
  const bool own = SKIN.own;
  const uint32_t paper = PAPER == 0xffffff ? 0xfffaf0 : 0xece4d3;
  if (own) lv_obj_set_style_bg_color(p, C(paper), 0);
  const uint32_t ink = own ? NAVY : INK, grey = own ? 0x86775f : GREY, line = own ? 0xe3d7c0 : sep();
  const uint32_t dayCol = day.wday == 0 || hol.length() ? SH_RED : ink;
  const uint32_t head = own ? WHITE : SH_RED, headDim = own ? PALE : GREY;
  if (size == S) {
    if (own) sheetBand(c, 28, paper);
    mid(p, upperVi(i18n::MONTH[day.m - 1]), -12, -6, W, 13, true, head);
    mid(p, String(day.d), -12, 16, W, 64, true, dayCol);
    mid(p, upperVi(WEEKDAY[day.wday]), -12, 70, W, 13, true, dayCol);
    midFit(p, lunarLong(data::lunarText(l)), -6, 88, W - 12, 16, 12, 12, false, grey);
    if (hol.length()) midFit(p, hol, -6, 104, W - 12, 17, 12, 12, true, SH_RED);
    else midFit(p, dayOf(day), -6, 104, W - 12, 17, 12, 12, false, grey);
    return;
  }
  if (size == M) {
    if (own) sheetBand(c, 28, paper);
    const String mh = upperVi(String(i18n::MONTH[day.m - 1]) + " · " + day.y);
    at(p, mh, 0, -6, 14, true, head);
    const int mw = textW(mh.c_str(), 14, true) + 10;
    lv_obj_t* yl = fit(p, yearOf(l.y), 0, -5, c.w - mw, 18, 13, 12, false, headDim);
    lv_obj_align(yl, LV_ALIGN_TOP_RIGHT, 0, -5);
    mid(p, String(day.d), 0, 14, 124, 96, true, dayCol);
    mid(p, upperVi(WEEKDAY[day.wday]), 0, 92, 124, 15, true, dayCol);
    rect(p, 136, 26, 1, 88, line);
    const int x = 148;
    cap(p, N_("Lunar"), SH_RED, x, 24);
    const String ld = String(l.d) + "/" + l.m;
    at(p, ld, x, 40, 28, true, ink);
    if (l.leap) at(p, N_("leap"), x + textW(ld.c_str(), 28, true) + 4, 50, 12, false, grey);
    fit(p, dayOf(day), x, 68, c.w - x, 18, 13, 12, false, ink);
    fit(p, monthOf(l), x, 85, c.w - x, 18, 13, 12, false, grey);
    if (hol.length()) fit(p, hol, x, 102, c.w - x, 17, 12, 12, true, SH_RED);
    // i18n: the solar term the day is in (vi "Tiết {0}"); English shows the name alone
    else fit(p, i18n::fmt(TC_("term", "{0}"), data::SOLAR_TERM[data::solarTerm(day)]), x, 102, c.w - x, 17, 12, 12, false, grey);
    return;
  }
  if (own) sheetBand(c, 30, paper);
  at(p, upperVi(i18n::MONTH[day.m - 1]), 2, -6, 16, true, head);
  right(p, String(day.y), -6, 16, true, head, -2);
  mid(p, String(day.d), 0, 14, c.w, 96, true, dayCol);
  mid(p, upperVi(WEEKDAY[day.wday]), 0, 90, c.w, 18, true, dayCol);
  if (hol.length()) mid(p, hol, 0, 114, c.w, 13, true, SH_RED);
  else {
    const data::Holiday nh = data::nextHoliday(day);
    midFit(p, dm(nh.day) + " " + nh.name + " · " + inDays(nh.days), 0, 115, c.w, 17, 12, 12, false, grey);
  }
  rect(p, 0, 136, c.w, 1, line);
  // The lunar stamp: the lunar day big, its month under it.
  rect(p, 0, 144, 88, 72, own ? 0xfbe3d9 : mix(SH_RED, PAPER, 40), 12);
  midFit(p, upperVi(T_("Lunar")), 2, 146, 84, 15, 12, 12, true, SH_RED);
  mid(p, String(l.d), 0, 159, 88, 44, true, l.d == 1 || l.d == 15 ? SH_RED : ink);
  // i18n: a lunar month under its day number; {1} is " leap" or nothing (vi "Tháng {0}{1}")
  midFit(p, i18n::fmt(N_("Month {0}{1}"), String(l.m), l.leap ? T_(" leap") : ""), 2, 196, 84, 16, 12, 12, false, grey);
  // i18n: "Season" is the solar term (vi "Tiết")
  const char* const names[4] = {N_("Year"), N_("Month"), N_("Day"), N_("Season")};
  const String vals[4] = {data::canChiYear(l.y), data::canChiMonth(l), data::canChiDay(day),
                          data::SOLAR_TERM[data::solarTerm(day)]};
  int nw = 0;
  for (const char* n : names) nw = std::max(nw, textW(n, 13, false));
  const int vx = std::max(148, 100 + nw + 8);
  for (int i = 0; i < 4; i++) {
    at(p, names[i], 100, 146 + i * 18, 13, false, grey);
    fit(p, vals[i], vx, 146 + i * 18, c.w - vx, 18, 13, 12, true, ink);
  }
  at(p, N_("Lucky hours"), 0, 222, 12, true, SH_RED);
  const int gx = std::max(52, textW(N_("Lucky hours"), 12, true) + 8);
  fit(p, goodNames(day), gx, 222, c.w - gx, 17, 12, 12, false, ink);
  midFit(p, data::quoteOfDay(day), 0, 240, c.w, 34, 13, 12, false, grey);
}

// ---------------------------------------------------------------- date: compact
void compact(Card c, Size size, const Day& day, const Lunar& l, const String& hol) {
  lv_obj_t* p = c.o;
  cap(p, WEEKDAY[day.wday], col::RED);
  const uint32_t dayCol = day.wday == 0 || hol.length() ? col::RED : INK;
  if (size == S) {
    at(p, String(day.d), -2, 12, 56, true, dayCol);
    fit(p, i18n::date(N_("{MM} {y}"), 1, day.m, day.y), 0, 62, c.w, 18, 13, 12, false, INK);
    fit(p, lunarLong(data::lunarText(l)), 0, 82, c.w, 17, 12, 12, false, GREY);
    if (hol.length()) fit(p, hol, 0, 99, c.w, 19, 12, 12, true, col::RED);
    else fit(p, dayOf(day), 0, 99, c.w, 19, 12, 12, false, GREY);
    return;
  }
  // i18n: a lunar date in words: day, month, " leap" or nothing (vi "{0} tháng {1}{2}")
  const String ld = i18n::fmt(N_("Day {0}, month {1}{2}"), String(l.d), String(l.m), l.leap ? T_(" leap") : "");
  if (size == M) {
    at(p, String(day.d), -2, 12, 56, true, dayCol);
    fit(p, i18n::date(N_("{MM} {y}"), 1, day.m, day.y), 0, 64, 108, 18, 13, 12, false, INK);
    fit(p, yearOf(l.y), 0, 82, 108, 17, 12, 12, false, GREY);
    rect(p, 112, 0, 1, c.h, sep());
    const int x = 124;
    const char* const names[4] = {N_("Lunar"), N_("Day"), N_("Month"), hol.length() ? N_("Today") : N_("Solar term")};
    const String vals[4] = {ld, data::canChiDay(day), data::canChiMonth(l),
                            hol.length() ? hol : String(data::SOLAR_TERM[data::solarTerm(day)])};
    for (int i = 0; i < 4; i++) {
      const int y = 2 + i * 29;
      if (i) rect(p, x, y - 6, c.w - x, 1, sep());
      at(p, names[i], x, y, 13, false, GREY);
      lv_obj_t* v = fit(p, vals[i], 0, y, c.w - x - textW(names[i], 13, false) - 8, 18, 13, 12, true,
                        i == 3 && hol.length() ? col::RED : INK);
      lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
    }
    return;
  }
  at(p, String(day.d), -2, 12, 64, true, dayCol);
  const int nx = textW(String(day.d).c_str(), 64, true) + 10;
  fit(p, i18n::MONTH[day.m - 1], nx, 24, c.w - nx, 26, 20, 14, true, INK);
  at(p, String(day.y), nx, 52, 14, false, GREY);
  const data::Holiday nh = data::nextHoliday(day);
  const char* const names[7] = {N_("Lunar"),      N_("Year"),        N_("Month"), N_("Day"),
                                N_("Solar term"), N_("Lucky hours"),
                                // i18n: a row label beside the next holiday's date and name (vi "Lễ tới")
                                hol.length() ? N_("Today") : TC_("short", "Next holiday")};
  const String vals[7] = {ld, data::canChiYear(l.y), data::canChiMonth(l), data::canChiDay(day),
                          data::SOLAR_TERM[data::solarTerm(day)], goodNames(day),
                          hol.length() ? hol : dm(nh.day) + " " + nh.name};
  for (int i = 0; i < 7; i++) {
    const int y = 92 + i * 25;
    if (i) rect(p, 0, y - 4, c.w, 1, sep());
    at(p, names[i], 0, y, 13, false, GREY);
    lv_obj_t* v = fit(p, vals[i], 0, y, c.w - textW(names[i], 13, false) - 8, 18, 13, 12, true,
                      i == 6 && hol.length() ? col::RED : INK);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
  }
}

// ---------------------------------------------------------------- cal: grids
// The first look's month grid (as before the styles came).
void oldGrid(lv_obj_t* p, int x, int y, int w, int h, bool lunarNums, uint32_t fg, uint32_t dim, const Day& td) {
  const Day first = data::fromJulian(data::julian({1, td.m, td.y, 0}));
  const int lead = (first.wday + 6) % 7;  // Monday first
  int daysIn = 31;
  while (daysIn > 28 && plus(first, daysIn - 1).m != td.m) daysIn--;
  const int weeks = (lead + daysIn + 6) / 7;
  const int cw = w / 7, ch = (h - 18) / weeks;
  for (int i = 0; i < 7; i++) mid(p, monFirst(i), x + i * cw, y, cw, 12, true, i == 6 ? col::RED : dim);
  for (int dd = 1; dd <= daysIn; dd++) {
    const int idx = lead + dd - 1;
    const int cx = x + (idx % 7) * cw, cy = y + 18 + (idx / 7) * ch;
    const bool today = dd == td.d;
    if (today) rect(p, cx + (cw - 22) / 2, cy - 2, 22, 22, col::RED, 11);
    mid(p, String(dd), cx, cy, cw, 13, today, today ? WHITE : idx % 7 == 6 ? col::RED : fg);
    if (lunarNums && ch >= 30) {
      const Lunar ln = data::lunar({dd, td.m, td.y, 0});
      mid(p, ln.d == 1 ? String(ln.d) + "/" + ln.m : String(ln.d), cx, cy + 18, cw, 12, false,
          ln.d == 1 || ln.d == 15 ? col::RED : 0xa0a6b1);
    }
  }
}

// A month grid at (x, y) in w x h: weekday heads, the days at px (today in a red disc, Sundays and holidays red),
// the lunar days under them when asked and the rows are tall enough.
void monthGrid(lv_obj_t* p, int x, int y, int w, int h, const Day& td, int px, int disc, bool lunarNums) {
  const Day first = data::fromJulian(data::julian({1, td.m, td.y, 0}));
  const int lead = (first.wday + 6) % 7;
  int daysIn = 31;
  while (daysIn > 28 && plus(first, daysIn - 1).m != td.m) daysIn--;
  const int weeks = (lead + daysIn + 6) / 7;
  const int cw = w / 7, ch = (h - 18) / weeks;
  for (int i = 0; i < 7; i++) mid(p, monFirst(i), x + i * cw, y, cw, 12, true, i == 6 ? col::RED : GREY);
  for (int dd = 1; dd <= daysIn; dd++) {
    const int idx = lead + dd - 1;
    const int cx = x + (idx % 7) * cw, cy = y + 18 + (idx / 7) * ch;
    const Day one = plus(first, dd - 1);
    const Lunar ln = data::lunar(one);
    const bool today = dd == td.d;
    const bool red = idx % 7 == 6 || data::holiday(one, ln).length();
    const int lh = lv_font_get_line_height(fonts::get(px, today));
    if (today) rect(p, cx + (cw - disc) / 2, cy + (lh - disc) / 2, disc, disc, col::RED, disc / 2);
    mid(p, String(dd), cx, cy, cw, px, today, today ? WHITE : red ? col::RED : INK);
    if (lunarNums && ch >= 28)
      mid(p, ln.d == 1 ? String(ln.d) + "/" + ln.m : String(ln.d), cx, cy + lh - 2, cw, 12, false,
          ln.d == 1 || ln.d == 15 ? col::RED : GREY);
  }
}

// ---------------------------------------------------------------- cal: today (the first look)
void calToday(Card c, Size size) {
  lv_obj_t* p = c.o;
  const Day d = data::today();
  const auto evs = data::agenda();
  const String td = data::isoDate(d), tm_ = data::isoDate(plus(d, 1));
  if (size == S) {
    cap(p, WEEKDAY[d.wday], col::RED);
    at(p, String(d.d), -2, 14, 40, false, INK);
    std::vector<data::Event> todays, tomorrow;
    for (const auto& e : evs) (e.date == tm_ ? tomorrow : todays).push_back(e);
    const int now = data::nowMinutes();
    String l1;
    for (const auto& e : todays) {
      if (e.date != td) continue;
      if (evMin(e, 24 * 60) >= now) {
        l1 = (e.time.length() ? e.time + " " : "") + e.title;
        break;
      }
    }
    if (!l1.length()) {
      if (!skeleton(p, 0, 64, c.w, 18, GREY)) fit(p, N_("Nothing today"), 0, 64, c.w, 18, 13, 12, false, GREY);
      if (tomorrow.size()) {
        // i18n: tomorrow's first event time (vi "Mai {0}")
        at(p, i18n::fmt(N_("Tomorrow {0}"), tomorrow[0].time), 0, 84, 13, true, INK);
        fit(p, tomorrow[0].title, 0, 102, c.w, 18, 13, 12, false, INK);
      }
    } else {
      fit(p, l1, 0, 64, c.w, c.h - 64, 14, 12, true, INK, 2);
    }
    return;
  }
  if (size == M) {
    cap(p, WEEKDAY[d.wday], col::RED);
    at(p, String(d.d), -2, 16, 40, false, INK);
    fit(p, lunarShort(data::lunarText(data::lunar(d))), 0, c.h - 16, 84, 17, 12, 12, false, GREY);
    const int x = 96;
    rect(p, x - 12, 0, 1, c.h, PAPER == 0xffffff ? 0xe5e7eb : 0x2c313c);
    int y = 0, shown = 0;
    String lastDay;
    for (const auto& e : evs) {
      if (y + 34 > c.h) break;
      if (e.date != lastDay) {
        lastDay = e.date;
        Day ed;
        const String lbl = e.date == td ? String(T_("Today")) : e.date == tm_ ? String(T_("Tomorrow")) : parseIso(e.date, ed) ? dm(ed) : e.date;
        at(p, upperVi(lbl), x, y, 12, true, GREY);
        y += 17;
      }
      const int h = eventRow(p, "", e, x + 8, y, c.w - x - 8, c.h - y, 14, INK);
      if (h < 0) break;
      rect(p, x, y + 1, 3, h - 1, shown % 2 ? 0x34c759 : 0x0a84ff, 2);
      y += max(22, h + 5);
      shown++;
    }
    if (!shown && !skeleton(p, x, 2, c.w - x, c.h, GREY))
      fit(p, N_("No events in the next 7 days"), x, 2, c.w - x, c.h, 14, 12, false, GREY);
    return;
  }
  // Large: the month with lunar days, then the next events.
  at(p, upperVi(i18n::MONTH[d.m - 1]), 0, 0, 12, true, col::RED);
  right(p, String(d.y), 0, 12, true, GREY);
  oldGrid(p, 0, 20, c.w, 190, true, INK, GREY, d);
  int y = 216;
  for (const auto& e : evs) {
    if (y + 20 > c.h) break;
    Day ed;
    const String lbl = e.date == td ? String() : parseIso(e.date, ed) ? dm(ed) + " " : e.date + " ";
    const int h = eventRow(p, lbl, e, 0, y, c.w, c.h - y, 13, INK);
    if (h < 0) break;
    y += max(20, h + 3);
  }
  if (evs.empty() && !skeleton(p, 0, y, c.w, 18, GREY)) fit(p, N_("No upcoming events"), 0, y, c.w, 18, 13, 12, false, GREY);
}

// ---------------------------------------------------------------- cal: events (the agenda, by day)
void calEvents(Card c, Size size) {
  lv_obj_t* p = c.o;
  const Day day = data::today();
  const auto evs = data::agenda();
  const uint8_t tintK = PAPER == 0xffffff ? 36 : 64;
  if (size == S) {
    cap(p, i18n::date(N_("{w}, {M} {d}"), day.d, day.m, day.y, day.wday).c_str(), col::RED);
    int y = 22, i = 0;
    for (const auto& e : evs) {
      if (y + 30 > c.h + 6) break;
      Day x = day;
      parseIso(e.date, x);
      const uint32_t cl = EV_COL[i % 4];
      rect(p, 0, y, c.w, 30, mix(cl, PAPER, tintK), 7);
      rect(p, 0, y, 3, 30, cl, 1);
      fit(p, data::julian(x) == data::julian(day) ? (e.time.length() ? e.time : String(T_("All day"))) : dayLabel(x, day) + " " + e.time,
          8, y - 1, c.w - 12, 16, 12, 12, true, GREY);
      fit(p, e.title, 8, y + 13, c.w - 12, 17, 13, 12, false, INK);
      y += 33;
      i++;
    }
    if (evs.empty()) empty(c, N_("No events in the next 7 days"), GREY, 24);
    return;
  }
  int y = 0, i = 0;
  String lastDay;
  if (size == L) {
    cap(p, N_("Next 7 days"), col::RED);
    right(p, i18n::date(N_("{w}, {M} {d}"), day.d, day.m, day.y, day.wday), 0, 12, true, GREY);
    y = 24;
  }
  for (const auto& e : evs) {
    const bool head = e.date != lastDay;
    if (y + (head ? 17 : 0) + 24 > c.h) break;
    Day x = day;
    parseIso(e.date, x);
    if (head) {
      lastDay = e.date;
      const int n = data::julian(x) - data::julian(day);
      at(p, upperVi(n < 2 ? dayLabel(x, day) + " · " + i18n::date(N_("{w}, {M} {d}"), x.d, x.m, x.y, x.wday)
                          : i18n::date(N_("{W}, {M} {d}"), x.d, x.m, x.y, x.wday)), 0, y, 12, true, GREY);
      y += 18;
    }
    const uint32_t cl = EV_COL[i % 4];
    rect(p, 0, y, c.w, 24, mix(cl, PAPER, tintK), 7);
    rect(p, 0, y, 3, 24, cl, 1);
    const String tm = e.time.length() ? e.time : String(T_("All day"));
    at(p, tm, 10, y + 3, 13, true, INK);
    const int tx = 18 + std::max(textW(tm.c_str(), 13, true), 38);
    fit(p, e.title, tx, y + 3, c.w - tx - 6, 18, 13, 12, false, INK);
    y += 27;
    i++;
  }
  if (evs.empty()) empty(c, N_("No events in the next 7 days"), GREY, y + 4);
}

// ---------------------------------------------------------------- cal: week
void calWeek(Card c, Size size) {
  lv_obj_t* p = c.o;
  const Day day = data::today();
  const auto evs = data::agenda();
  const Day mon = plus(day, -((day.wday + 6) % 7));
  if (size == L) {
    const int rh = c.h / 7;
    for (int i = 0; i < 7; i++) {
      const Day x = plus(mon, i);
      const int y = i * rh;
      const Lunar ln = data::lunar(x);
      const String hol = data::holiday(x, ln);
      if (data::julian(x) == data::julian(day)) rect(p, -6, y + 1, c.w + 12, rh - 2, mix(col::RED, PAPER, PAPER == 0xffffff ? 28 : 56), 10);
      else if (i) rect(p, 0, y, c.w, 1, sep());
      const bool red = i == 6 || hol.length();
      at(p, monFirst(i), 0, y + 3, 12, true, red ? col::RED : GREY);
      at(p, String(x.d), 0, y + 15, 18, true, red ? col::RED : INK);
      // The day's events on one line (as many as fit, then "+2"); a long first one takes both lines.
      const String iso = data::isoDate(x);
      std::vector<const data::Event*> on;
      for (const auto& e : evs)
        if (e.date == iso) on.push_back(&e);
      const int w = c.w - 44;
      String line;
      int n = 0;
      for (const data::Event* e : on) {
        const String next = (line.length() ? line + " · " : String()) + (e->time.length() ? e->time + " " : String()) + e->title;
        const int rest = (int)on.size() - n - 1;
        if (n && textW((rest > 0 ? next + " +" + rest : next).c_str(), 13, true) > w) break;
        line = next;
        n++;
      }
      if (n < (int)on.size()) line += " +" + String((int)on.size() - n);
      const bool two = line.length() && textW(line.c_str(), 12, true) > w;
      fit(p, line.length() ? line : String(T_("No events")), 44, y + 3, w, two ? rh - 4 : 18, 13, 12, line.length() > 0,
          line.length() ? INK : GREY);
      if (!two) fit(p, lunarShort(data::lunarText(ln)) + (hol.length() ? " · " + hol : String()), 44, y + 20, w, 17, 12, 12, false,
                    hol.length() ? col::RED : GREY);
    }
    return;
  }
  const bool s = size == S;
  const int gx = s ? -6 : 0, cw = (c.w - 2 * gx) / 7;
  const int hy = s ? 20 : 0, px = s ? 13 : 15, disc = s ? 19 : 26;
  const int ny = hy + (s ? 19 : 20), lh = lv_font_get_line_height(fonts::get(px, true));
  if (s) cap(p, i18n::MONTH[day.m - 1], col::RED);
  for (int i = 0; i < 7; i++) {
    const Day x = plus(mon, i);
    const int cx = gx + i * cw;
    const Lunar ln = data::lunar(x);
    const String hol = data::holiday(x, ln);
    const bool today = data::julian(x) == data::julian(day);
    const bool red = i == 6 || hol.length();
    mid(p, monFirst(i), cx, hy, cw, 12, true, red ? col::RED : GREY);
    if (today) rect(p, cx + (cw - disc) / 2, ny + (lh - disc) / 2, disc, disc, col::RED, disc / 2);
    mid(p, String(x.d), cx, ny, cw, px, true, today ? WHITE : red ? col::RED : INK);
    if (!s) mid(p, ln.d == 1 ? String(ln.d) + "/" + ln.m : String(ln.d), cx, ny + 26, cw, 12, false,
                ln.d == 1 || ln.d == 15 ? col::RED : GREY);
    const int n = std::min(eventsOn(evs, x), 3);
    for (int j = 0; j < n; j++) rect(p, cx + cw / 2 - (n * 6 - 2) / 2 + j * 6, s ? ny + 22 : ny + 46, 4, 4, EV_COL[j], 2);
  }
  // The next event of the week.
  const int y = s ? 70 : 78;
  const int nm = data::nowMinutes();
  const String td = data::isoDate(day);
  const data::Event* next = nullptr;
  for (const auto& e : evs) {
    Day x;
    if (!parseIso(e.date, x) || data::julian(x) - data::julian(mon) >= 7) continue;
    if (e.date == td ? evMin(e, 24 * 60) >= nm : e.date > td) {
      next = &e;
      break;
    }
  }
  if (!next) fit(p, s ? N_("Free this week") : N_("No events this week"), 0, y + 2, c.w, c.h - y, 13, 12, false, GREY);
  else {
    Day x = day;
    parseIso(next->date, x);
    rect(p, 0, y + 4, 3, 34, EV_COL[0], 1);
    fit(p, (data::julian(x) == data::julian(day) ? String() : dayLabel(x, day) + " ") + eventLine(*next), 9, y + 2, c.w - 9,
        c.h - y - 2, 14, 12, false, INK);
  }
}

// ---------------------------------------------------------------- cal: month
void calMonth(Card c, Size size) {
  lv_obj_t* p = c.o;
  const Day day = data::today();
  const Lunar l = data::lunar(day);
  if (size == S) {
    cap(p, i18n::MONTH[day.m - 1], col::RED);
    right(p, String(day.y), 0, 12, true, GREY);
    monthGrid(p, -4, 19, c.w + 8, c.h - 17, day, 12, 17, false);
    return;
  }
  if (size == M) {
    cap(p, N_("Month"), col::RED);
    at(p, String(day.m), -2, 12, 56, true, INK);
    at(p, String(day.y), 0, 64, 13, true, GREY);
    fit(p, lunarShort(lunarDM(l)), 0, 84, 96, 17, 12, 12, false, GREY);
    fit(p, yearOf(l.y), 0, 100, 96, 17, 12, 12, false, GREY);
    monthGrid(p, 100, 0, c.w - 100, c.h, day, 13, 20, false);
    return;
  }
  const String mh = upperVi(i18n::MONTH[day.m - 1]);
  at(p, mh, 0, -2, 16, true, col::RED);
  const int mw = textW(mh.c_str(), 16, true) + 10;
  lv_obj_t* yl = fit(p, String(day.y) + " · " + data::canChiYear(l.y), 0, 0, c.w - mw, 18, 13, 12, false, GREY);
  lv_obj_align(yl, LV_ALIGN_TOP_RIGHT, 0, 0);
  monthGrid(p, 0, 26, c.w, 200, day, 14, 24, true);
  // This month's holidays from today on (else the next one).
  int y = 230, shown = 0;
  String last;
  for (int i = 0; i < 31 && shown < 2; i++) {
    const Day x = plus(day, i);
    if (x.m != day.m) break;
    const String h = data::holiday(x, data::lunar(x));
    if (h.length() && h != last) {
      at(p, dm(x), 0, y, 13, true, col::RED);
      const int hx = std::max(44, textW(dm(x).c_str(), 13, true) + 8);
      fit(p, h, hx, y, c.w - hx, 18, 13, 12, false, INK);
      y += 19;
      shown++;
    }
    last = h;
  }
  if (!shown) {
    const data::Holiday nh = data::nextHoliday(day);
    at(p, dm(nh.day), 0, y, 13, true, col::RED);
    const int hx = std::max(44, textW(dm(nh.day).c_str(), 13, true) + 8);
    fit(p, nh.name, hx, y, c.w - hx, 18, 13, 12, false, INK);
  }
}

// ---------------------------------------------------------------- lunar
// The lucky hours' chips (giờ hoàng đạo: branch and hours) in `cols` columns from (x, y) to the right edge.
void goodChips(Card c, const Day& day, int x, int y, int cols) {
  lv_obj_t* p = c.o;
  const uint16_t bits = data::goodHours(day);
  const int cw = (c.w - x - (cols - 1) * 6) / cols, ch = cols == 3 ? 40 : 26;
  int n = 0;
  for (int i = 0; i < 12; i++) {
    if (!(bits & (1 << i))) continue;
    const int cx = x + (n % cols) * (cw + 6), cy = y + (n / cols) * (ch + 6);
    rect(p, cx, cy, cw, ch, WHITE, 8, 41);
    if (cols == 3) {
      fit(p, data::CHI[i], cx + 8, cy + 2, cw - 10, 20, 15, 12, true, onOwn(WHITE));
      // i18n: an hour range like "23-1" (vi "{0} giờ")
      at(p, i18n::fmt(N_("{0}h"), data::hourRange(i)), cx + 8, cy + 21, 12, false, onOwn(PALE));
    } else {
      fit(p, data::CHI[i], cx + 6, cy + 3, cw - 12 - textW(data::hourRange(i).c_str(), 12, false), 18, 13, 12, true, onOwn(WHITE));
      at(p, data::hourRange(i), cx + cw - 6 - textW(data::hourRange(i).c_str(), 12, false), cy + 4, 12, false, onOwn(PALE));
    }
    n++;
  }
}

void lunarCanchi(Card c, Size size, const Day& day, const Lunar& l) {
  lv_obj_t* p = c.o;
  cap(p, N_("Lunar"), onOwn(GOLD));
  at(p, String(l.d) + "/" + l.m, -1, 16, 44, true, onOwn(CREAM));
  fit(p, yearOf(l.y), 0, 66, size == S ? c.w : 132, 18, 13, 12, true, onOwn(WHITE));
  const String hol = data::holiday(day, l);
  const int tet = data::daysToTet(day);
  const String foot = hol.length() ? hol
                      : tet == 0   ? String(T_("Lunar New Year's Day"))
                                   : i18n::plural(tet, NP_("{n} day to Lunar New Year|{n} days to Lunar New Year"));
  if (size == S) {
    fit(p, foot, 0, c.h - 36, c.w, 36, 13, 12, false, onOwn(PALE));
    return;
  }
  const int x = 140;
  fit(p, dayOf(day), x, 20, c.w - x, 20, 14, 12, true, onOwn(WHITE));
  fit(p, monthOf(l), x, 42, c.w - x, 18, 13, 12, false, onOwn(PALE));
  fit(p, foot, x, 66, c.w - x, 40, 13, 12, false, onOwn(PALE));
  at(p, i18n::date(N_("{W}, {M} {d}, {y}"), day.d, day.m, day.y, day.wday), 0, c.h - 16, 12, false, onOwn(PALE));
  if (size == L) {
    cap(p, N_("Auspicious hours"), onOwn(GOLD), 0, 124);
    goodChips(c, day, 0, 144, 3);
  }
}

void lunarDetail(Card c, Size size, const Day& day, const Lunar& l) {
  lv_obj_t* p = c.o;
  const int term = data::solarTerm(day);
  int nt = 0;
  const Day nd = data::nextSolarTerm(day, nt);
  if (size == S) {
    cap(p, N_("Solar term"), onOwn(GOLD));
    fit(p, data::SOLAR_TERM[term], 0, 18, c.w, 24, 18, 13, true, onOwn(WHITE));
    // i18n: the next solar term and its date (vi "Tới {0} {1}")
    fit(p, i18n::fmt(N_("Next: {0} {1}"), data::SOLAR_TERM[nt], dm(nd)), 0, 44, c.w, 17, 12, 12, false, onOwn(PALE));
    cap(p, N_("Lucky hours"), onOwn(GOLD), 0, 66);
    fit(p, goodNames(day), 0, 84, c.w, 36, 13, 12, false, onOwn(WHITE));
    return;
  }
  cap(p, N_("Lunar"), onOwn(GOLD));
  at(p, String(l.d) + "/" + l.m, -1, 16, 44, true, onOwn(CREAM));
  if (size == M) {
    fit(p, dayOf(day), 0, 64, 104, 18, 13, 12, true, onOwn(WHITE));
    fit(p, i18n::fmt(TC_("term", "{0}"), data::SOLAR_TERM[term]), 0, 83, 104, 17, 12, 12, false, onOwn(PALE));
    fit(p, dm(nd) + " " + data::SOLAR_TERM[nt], 0, 100, 104, 17, 12, 12, false, onOwn(PALE));
    cap(p, N_("Auspicious hours"), onOwn(GOLD), 108, 0);
    goodChips(c, day, 108, 22, 2);
    return;
  }
  fit(p, yearOf(l.y), 116, 16, c.w - 116, 20, 14, 12, true, onOwn(WHITE));
  fit(p, monthOf(l), 116, 38, c.w - 116, 18, 13, 12, false, onOwn(PALE));
  fit(p, dayOf(day), 116, 56, c.w - 116, 18, 13, 12, false, onOwn(PALE));
  cap(p, N_("Auspicious hours"), onOwn(GOLD), 0, 86);
  goodChips(c, day, 0, 106, 3);
  cap(p, N_("Solar term"), onOwn(GOLD), 0, 204);
  at(p, data::SOLAR_TERM[term], 0, 220, 16, true, onOwn(WHITE));
  // i18n: the day the current solar term began (vi "từ {0}")
  at(p, i18n::fmt(N_("from {0}"), dm(termStart(day))), textW(data::SOLAR_TERM[term], 16, true) + 8, 223, 12, false, onOwn(PALE));
  // i18n: the next solar term, its date, "in 5 days" (vi "Tiếp theo: {0} {1}, {2}")
  fit(p, i18n::fmt(N_("Next: {0} {1}, {2}"), data::SOLAR_TERM[nt], dm(nd), inDays(data::julian(nd) - data::julian(day))), 0,
      244, c.w, 18, 12, 12, false, onOwn(PALE));
}

// The lunar month as a grid (Monday first): lunar days, today in a gold disc, the 1st and the full moon in gold; the solar days
// under them when asked and the rows are tall enough.
void lunarGrid(lv_obj_t* p, const Lunar& l, const Day& first, int n, int x, int y, int w, int h, int px, int disc, bool solar) {
  const int lead = (first.wday + 6) % 7;
  const int weeks = (lead + n + 6) / 7;
  const int cw = w / 7, ch = (h - 18) / weeks;
  for (int i = 0; i < 7; i++) mid(p, monFirst(i), x + i * cw, y, cw, 12, true, onOwn(0xffc9b8));
  for (int i = 0; i < n; i++) {
    const int idx = lead + i;
    const int cx = x + (idx % 7) * cw, cy = y + 18 + (idx / 7) * ch;
    const bool today = i + 1 == l.d, key = i == 0 || i == 14;
    const int lh = lv_font_get_line_height(fonts::get(px, today || key));
    if (today) rect(p, cx + (cw - disc) / 2, cy + (lh - disc) / 2, disc, disc, onOwn(GOLD), disc / 2);
    mid(p, String(i + 1), cx, cy, cw, px, today || key, today ? 0x9f1217 : key ? onOwn(GOLD) : onOwn(WHITE));
    if (solar && ch >= 28) {
      const Day sd = plus(first, i);
      mid(p, sd.d == 1 || i == 0 ? dm(sd) : String(sd.d), cx, cy + lh - 2, cw, 12, false, onOwn(0xffc9b8));
    }
  }
}

void lunarMonth(Card c, Size size, const Day& day, const Lunar& l) {
  lv_obj_t* p = c.o;
  bool ok;
  Day first = data::lunarToSolar({1, l.m, l.y, l.leap}, ok);
  if (!ok) first = day;
  const int n = data::lunarMonthDays(l);
  // i18n: a lunar month's title; {1} is " leap" or nothing (vi "Tháng {0}{1} âm")
  const String title = i18n::fmt(N_("Lunar month {0}{1}"), String(l.m), l.leap ? T_(" leap") : "");
  if (size == S) {
    cap(p, title.c_str(), onOwn(GOLD));
    lunarGrid(p, l, first, n, -4, 19, c.w + 8, c.h - 17, 12, 17, false);
    return;
  }
  const Day full = plus(first, 14);
  if (size == M) {
    cap(p, title.c_str(), onOwn(GOLD));
    fit(p, data::canChiMonth(l), 0, 18, 108, 24, 18, 13, true, onOwn(WHITE));
    fit(p, yearOf(l.y), 0, 44, 108, 17, 12, 12, false, onOwn(PALE));
    // i18n: a lunar month of 30 days (vi "Tháng đủ") or 29 days (vi "Tháng thiếu")
    fit(p, n == 30 ? N_("Long month") : N_("Short month"), 0, 62, 108, 17, 12, 12, false, onOwn(PALE));
    // i18n: the solar date of the lunar month's first day (vi "Mùng 1: {0}")
    fit(p, i18n::fmt(N_("Starts: {0}"), dm(first)), 0, 82, 108, 17, 12, 12, false, onOwn(PALE));
    fit(p, i18n::fmt(N_("Full moon: {0}"), dm(full)), 0, 100, 108, 17, 12, 12, false, onOwn(PALE));
    lunarGrid(p, l, first, n, 112, 0, c.w - 112, c.h, 13, 20, false);
    return;
  }
  // i18n: the same title where there is room (vi "Tháng {0}{1} âm lịch")
  const String th = upperVi(i18n::fmt(TC_("long", "Lunar month {0}{1}"), String(l.m), l.leap ? T_(" leap") : ""));
  at(p, th, 0, -2, 16, true, onOwn(GOLD));
  const int tw = textW(th.c_str(), 16, true) + 10;
  lv_obj_t* cm = fit(p, data::canChiMonth(l) + " · " + data::canChiYear(l.y), 0, 0, c.w - tw, 18, 13, 12, false, onOwn(PALE));
  lv_obj_align(cm, LV_ALIGN_TOP_RIGHT, 0, 0);
  lunarGrid(p, l, first, n, 0, 26, c.w, 204, 15, 26, true);
  // i18n: {0} the first day's date, {1} the full moon's, {2} "Long month, 30 days" (vi "Mùng 1 là {0}, Rằm là {1}. {2}.")
  fit(p, i18n::fmt(N_("Starts {0}, full moon {1}. {2}."), dmy(first), dmy(full),
                   n == 30 ? T_("Long month, 30 days") : T_("Short month, 29 days")),
      0, 234, c.w, 34, 12, 12, false, onOwn(PALE));
}

// ---------------------------------------------------------------- count
// The number of days (or "Today") and its unit under it.
void countBig(Card c, int days, const String& unit, uint32_t fg) {
  if (days == 0) fit(c.o, N_("Today"), 0, 26, c.w, 30, 24, 14, true, fg);
  else at(c.o, data::num(abs(days), 0), -2, 22, 44, true, fg);
  if (days) fit(c.o, unit, 0, 72, c.w > 130 ? 130 : c.w, 20, 14, 12, false, fg);
}
// i18n: under a big number of days still to come (vi "ngày nữa")
String daysToGo(int n) { return i18n::plural(n, NP_("day to go|days to go")); }

}  // namespace

// ---------------------------------------------------------------- the kinds
namespace {
#pragma GCC push_options
#pragma GCC optimize("Os")  // posters are built when a page is made
// date block: the weekday, the day as big as fits, the month (and the lunar day, a holiday's name, the can chi).
void dateBlock(Card c, Size size, const Day& day, const Lunar& l, const String& hol) {
  const bool sm = size == S, h = hol.length();
  char dd[4];
  snprintf(dd, sizeof dd, "%d", day.d);
  const String mo = i18n::date(N_("{MM} {y}"), 1, day.m, day.y);
  const String lu = h ? hol : lunarLong(data::lunarText(l));
  // i18n: the day's and the year's stems and branches (vi "Ngày {0} · năm {1}")
  const String cc = i18n::fmt(N_("{0} day · {1} year"), data::canChiDay(day), data::canChiYear(l.y));
  const String wd = upperVi(WEEKDAY[day.wday]);
  const PLine lines[5] = {{wd.c_str(), sm ? 13 : 16, true, SH_RED},
                          {dd, 96, true, day.wday == 0 || h ? SH_RED : INK},
                          {mo.c_str(), sm ? 13 : 16, true, INK},
                          {lu.c_str(), 13, false, h ? SH_RED : GREY},
                          {cc.c_str(), 13, false, GREY}};
  poster(c.o, lines, sm ? 3 : size == M ? 4 : 5, 0, 0, c.w, c.h);
}
// lunar big: the lunar day and month as big as fit, the year (and the day's and month's can chi).
void lunarBig(Card c, Size size, const Day& day, const Lunar& l) {
  const bool sm = size == S;
  char lm[8];
  snprintf(lm, sizeof lm, "%d/%d", l.d, l.m);
  const String yr = yearOf(l.y), head = upperVi(T_("Lunar"));
  // i18n: the day's and the month's stems and branches (vi "Ngày {0} · tháng {1}")
  const String cc = i18n::fmt(N_("{0} day · {1} month"), data::canChiDay(day), data::canChiMonth(l));
  const PLine lines[4] = {{head.c_str(), sm ? 13 : 16, true, onOwn(CREAM)},
                          {lm, 96, true, onOwn(WHITE)},
                          {yr.c_str(), sm ? 13 : 16, true, onOwn(WHITE)},
                          {cc.c_str(), 13, false, onOwn(PALE)}};
  poster(c.o, lines, sm ? 3 : 4, 0, 0, c.w, c.h);
}
#pragma GCC pop_options
}  // namespace

void wDate(lv_obj_t*, const Item& it, Card c) {
  const Day day = data::today();
  const Lunar l = data::lunar(day);
  const String hol = data::holiday(day, l);
  const String st = styleOf(it);
  if (st == "compact") compact(c, it.size, day, l, hol);
  else if (st == "block") dateBlock(c, it.size, day, l, hol);
  else sheet(c, it.size, day, l, hol);
}

void wCal(lv_obj_t*, const Item& it, Card c) {
  const String st = styleOf(it);
  if (st == "events") calEvents(c, it.size);
  else if (st == "week") calWeek(c, it.size);
  else if (st == "month") calMonth(c, it.size);
  else calToday(c, it.size);
}

void wLunar(lv_obj_t*, const Item& it, Card c) {
  if (SKIN.own) grad(c.o, 0xd7261e, 0x9f1217);
  const Day day = data::today();
  const Lunar l = data::lunar(day);
  const String st = styleOf(it);
  if (st == "detail") lunarDetail(c, it.size, day, l);
  else if (st == "month") lunarMonth(c, it.size, day, l);
  else if (st == "big") lunarBig(c, it.size, day, l);
  else lunarCanchi(c, it.size, day, l);
}

void wCount(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* p = c.o;
  const Day day = data::today();
  const String st = styleOf(it);
  if (st == "date") {
    // A day of one's own: "YYYY-MM-DD|label" (days to it, or since it).
    if (SKIN.own) grad(p, 0x4f46e5, 0x7c3aed);
    const int bar = it.opt.indexOf('|');
    Day target;
    const bool ok = parseIso(bar >= 0 ? it.opt.substring(0, bar) : it.opt, target);
    const String label = bar >= 0 ? it.opt.substring(bar + 1) : String();
    const uint32_t dim = onOwn(0xe0e7ff);
    cap(p, label.length() ? label.c_str() : N_("Chosen date"), dim);
    if (!ok) {
      empty(c, N_("Pick a date in the widget's settings"), dim, 24);
      return;
    }
    const int days = data::julian(target) - data::julian(day);
    // i18n: under a big number of days gone by (vi "ngày đã qua")
    countBig(c, days, days > 0 ? daysToGo(days) : i18n::plural(-days, NP_("day ago|days ago")), onOwn(WHITE));
    if (it.size == S) {
      fit(p, i18n::date(N_("{w}, {M} {d}, {y}"), target.d, target.m, target.y, target.wday), 0, c.h - 16, c.w, 17, 12, 12,
          false, dim);
      return;
    }
    const Lunar tl = data::lunar(target);
    fit(p, label.length() ? label : dmy(target), 140, 20, c.w - 140, 44, 18, 13, true, onOwn(WHITE));
    fit(p, i18n::date(N_("{W}, {M} {d}, {y}"), target.d, target.m, target.y, target.wday), 140, 66, c.w - 140, 18, 13, 12,
        false, dim);
    // i18n: a lunar date and its year's stems and branches (vi "Âm lịch {0}, {1}")
    fit(p, i18n::fmt(N_("Lunar {0}, {1}"), lunarDM(tl), data::canChiYear(tl.y)), 140, 86, c.w - 140, 17, 12, 12, false, dim);
    return;
  }
  if (st == "holiday") {
    if (SKIN.own) grad(p, 0x0f766e, 0x0d9488);
    const data::Holiday h = data::nextHoliday(day);
    const uint32_t dim = onOwn(0xccfbf1);
    cap(p, N_("Next holiday"), dim);
    countBig(c, h.days, daysToGo(h.days), onOwn(WHITE));
    if (it.size == S) {
      fit(p, h.name, 0, h.days ? 92 : 58, c.w, h.days ? 26 : 60, 13, 12, true, onOwn(WHITE));
      return;
    }
    fit(p, h.name, 130, 18, c.w - 130, 48, 18, 13, true, onOwn(WHITE));
    fit(p, i18n::date(N_("{W}, {M} {d}, {y}"), h.day.d, h.day.m, h.day.y, h.day.wday), 130, 68, c.w - 130, 18, 13, 12, false,
        dim);
    fit(p, lunarLong(data::lunarText(data::lunar(h.day))), 130, 86, c.w - 130, 17, 12, 12, false, dim);
    return;
  }
  // Tết (the first look): drawn like a my-widget countdown.
  if (SKIN.own) grad(p, 0xd7261e, 0xf97316);
  const Lunar l = data::lunar(day);
  // i18n: the coming Lunar New Year by its year's stems and branches (vi "Tết {0}")
  cap(p, i18n::fmt(TC_("tet", "{0} year"), data::canChiYear(l.y + 1)).c_str(), onOwn(CREAM));
  const int days = data::daysToTet(day);
  at(p, String(days), -2, 22, 44, true, onOwn(WHITE));
  fit(p, days ? daysToGo(days) : String(T_("Happy New Year")), 0, 72, it.size == M ? 130 : c.w, 20, 14, 12, false, onOwn(WHITE));
  if (it.size == M) {
    bool ok;
    Day tet = data::lunarToSolar({1, 1, l.y + 1, false}, ok);
    if (!ok) tet = day;
    fit(p, N_("Lunar New Year's Day"), 140, 20, c.w - 140, 20, 14, 12, true, onOwn(WHITE));
    fit(p, i18n::date(N_("{W}, {M} {d}, {y}"), tet.d, tet.m, tet.y, tet.wday), 140, 42, c.w - 140, 18, 13, 12, false,
        onOwn(PALE));
    fit(p, yearOf(l.y + 1), 140, 62, c.w - 140, 18, 13, 12, false, onOwn(PALE));
    // i18n: Lunar New Year's Eve and its date (vi "Giao thừa {0}")
    fit(p, i18n::fmt(N_("Eve: {0}"), dmy(plus(tet, -1))), 140, 82, c.w - 140, 17, 12, 12, false, onOwn(PALE));
  }
}

// ---------------------------------------------------------------- quote (a saying a day: ca dao, tục ngữ in Vietnamese)
void wQuote(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* p = c.o;
  const Day day = data::today();
  const Lunar l = data::lunar(day);
  const bool dark = PAPER != 0xffffff;
  if (SKIN.own) lv_obj_set_style_bg_color(p, C(dark ? 0x2a2117 : 0xfff6e6), 0);
  const uint32_t ink = SKIN.own ? (dark ? 0xf3e3c3 : 0x4a3218) : INK, grey = SKIN.own ? (dark ? 0xb8a283 : 0x93765a) : GREY;
  const String q = data::quoteOfDay(day);
  if (styleOf(it) == "big") {
    // The saying in big bold type, centred; the date under a short red line.
    const bool lg = it.size == L;
    midFit(p, q, 4, lg ? 34 : 0, c.w - 8, c.h - (lg ? 80 : 22), lg ? 24 : 22, 14, true, ink);
    if (lg) mid(p, "“", 0, -4, c.w, 24, true, SH_RED);
    rect(p, c.w / 2 - 16, c.h - 24, 32, 2, SH_RED, 1);
    midFit(p, i18n::date(N_("{w}, {M} {d}"), day.d, day.m, day.y, day.wday) + " · " + lunarShort(data::lunarText(l)), 0, c.h - 16,
           c.w, 16, 12, 12, false, grey);
    return;
  }
  if (it.size != L) {
    at(p, "“", -2, -6, 24, true, SH_RED);
    cap(p, N_("Saying of the day"), grey, 18, 0);
    fitV(p, q, 0, 24, c.w, 70, 20, 13, false, ink);
    fit(p, i18n::date(N_("{w}, {M} {d}"), day.d, day.m, day.y, day.wday) + " · " + lunarShort(data::lunarText(l)), 0, c.h - 16,
        c.w, 17, 12, 12, false, grey);
    return;
  }
  // i18n: the large card's heading (vi "Ca dao · tục ngữ")
  mid(p, upperVi(TC_("title", "Saying of the day")), 0, 2, c.w, 12, true, SH_RED);
  mid(p, "“", 0, 22, c.w, 24, true, SH_RED);
  midFit(p, q, 8, 56, c.w - 16, 140, 20, 14, false, ink);
  rect(p, c.w / 2 - 20, 210, 40, 2, SH_RED, 1);
  mid(p, i18n::date(N_("{W}, {M} {d}, {y}"), day.d, day.m, day.y, day.wday), 0, 222, c.w, 13, true, ink);
  // i18n: the lunar date and the day's stems and branches (vi "{0} âm lịch · ngày {1}")
  midFit(p, i18n::fmt(N_("{0} (lunar) · {1} day"), data::lunarText(l), data::canChiDay(day)), 0, 242, c.w, 17, 12, 12, false,
         grey);
}

}  // namespace wid
