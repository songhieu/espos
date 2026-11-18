// The clocks family (docs/widgets.md): clock (ten styles), world, alarm. web/app/src/os/wid/clock.ts draws the same,
// line by line: change both. Options of the clock: "12" (12 h), "sec" (seconds hand or digits, moved by tick()).
#include <ArduinoJson.h>

#include <cmath>

#include "../alarm.h"
#include "../data.h"
#include "../desk.h"
#include "../fonts.h"
#include "../theme.h"
#include "kit.h"

namespace wid {

using namespace kit;

namespace {
// ---------------------------------------------------------------- faces
// Every face is drawn by face() from a Look: a row of numbers in faces.h (s3/tools/faces.py writes it and the
// web's faces.ts; the fields are explained there).
struct Look {
  uint32_t face, face2, rim, hour, tick, minor, num, hand, second, cap;
  uint8_t rimW, ticks, tickL, tickW, inset, nums, numKind, numPx, numPxM, numR, hB, hL, hW, mB, mL, mW, sL, handKind, extra,
      mode;
};
#include "faces.h"
}  // namespace

const std::vector<Style> CLOCK_STYLES = [] {
  std::vector<Style> v = {{"digital", N_("Digital")}, {"big", N_("Big digits")}, {"center", N_("Centered")},
                          {"stack", N_("Stacked")}, {"flip", N_("Flip")}};
  for (int i = 0; i < FACE_COUNT; i++) v.push_back({FACE_IDS[i], FACE_NAMES[i]});
  v.insert(v.end(), {{"words", N_("Words")}, {"lcd", N_("LCD")}, {"lunar", N_("With lunar date")}});
  return v;
}();
const std::vector<Style> CLOCK_FLAGS = {{"12", N_("12-hour")}, {"sec", N_("Seconds")}};
const std::vector<Style> WORLD_STYLES = {{"list", N_("List")}, {"analog", N_("Small faces")}, {"big", N_("Big time")}};
const std::vector<Style> ALARM_STYLES = {{"next", N_("Next alarm")}, {"list", N_("All")}};

namespace {

tm nowTm() {
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  return l;
}

int hour12(int h) { return h % 12 ? h % 12 : 12; }
// "07:21", or "7:21" in 12 h (the "AM"/"PM" goes beside it).
String clockText(const tm& l, bool h12) {
  if (!h12) return hhmm(l.tm_hour, l.tm_min);
  char b[8];
  snprintf(b, sizeof b, "%d:%02d", hour12(l.tm_hour), l.tm_min);
  return b;
}
const char* ampm(const tm& l) { return l.tm_hour < 12 ? T_("AM") : T_("PM"); }
String two(int v) {
  char b[4];
  snprintf(b, sizeof b, "%02d", v);
  return b;
}

String dShort(const data::Day& d) { return i18n::date(N_("{w}, {M} {d}"), d.d, d.m, d.y, d.wday); }  // Tue, Nov 18
String dLong(const data::Day& d) { return i18n::date(N_("{W}, {M} {d}"), d.d, d.m, d.y, d.wday); }   // Tuesday, Nov 18
String dFull(const data::Day& d) { return i18n::date(N_("{W}, {MM} {d}"), d.d, d.m, d.y, d.wday); }  // Tuesday, November 18
// i18n: a lunar date ("7/10" or "Full moon, month 10") with "lunar" (vi "{0} âm lịch")
String lunarLine(const data::Day& d) { return i18n::fmt(N_("Lunar {0}"), data::lunarText(data::lunar(d))); }

// Letters in a UTF-8 string (for the letter spacing of a label's width).
int glyphs(const String& s) {
  int n = 0;
  for (unsigned i = 0; i < s.length(); i++) n += ((uint8_t)s[i] & 0xC0) != 0x80;
  return n;
}

// A label centred on x (as lv_obj_align centres: x - w/2).
lv_obj_t* mid(lv_obj_t* p, const String& s, int x, int y, int px, bool bold, uint32_t color) {
  return at(p, s, x - textW(s.c_str(), px, bold) / 2, y, px, bold, color);
}

// ---------------------------------------------------------------- faces
#pragma GCC push_options
#pragma GCC optimize("Os")  // a face is built once a minute: small over fast
constexpr uint32_t NO = NO_COLOUR;
const uint32_t KIDS[6] = {0xe63946, 0xf77f00, 0x2a9d8f, 0x1d4ed8, 0x7b2cbf, 0xd81159};
int faceIndex(const String& style) {
  for (int i = 0; i < FACE_COUNT; i++)
    if (style == FACE_IDS[i]) return i;
  return -1;
}
// The face of a style ("analog" for an unknown one), its colours for the theme: mode 1 turns dark with a dark theme,
// mode 2 takes the card's text colours (fg, dim).
Look lookOf(const String& style, bool dark, uint32_t fg = 0, uint32_t dim = 0) {
  const int i = faceIndex(style);
  Look k = FACE_LOOKS[i < 0 ? 0 : i];
  if (k.mode == 1 && dark) {
    k.face = 0x1c2029;
    if (k.rim != NO) k.rim = 0x2e3440;
    k.tick = k.num = k.hand = 0xeef1f5;
    k.minor = 0x5b6270;
  }
  if (k.mode == 2) {
    k.rim = k.tick = k.num = k.hand = k.cap = fg;
    k.minor = dim;
  }
  return k;
}

struct Face {
  lv_obj_t *hl, *ml, *sl, *disk;
  lv_point_precise_t h[2], m[2], s[2], sub[2];
  float r;
  bool bars;
  uint8_t hB, hL, mB, mL, sL;  // the hands' tails and lengths, % of r (Look)
  lv_point_precise_t* ticks = nullptr;  // 2 points a tick (lv_line keeps a pointer to them)
  ~Face() { delete[] ticks; }
};

// One hand: from `back` behind the centre to `len` along the angle a (radians from 12, clockwise), times r.
void hand(lv_point_precise_t* p, float r, float a, float back, float len) {
  p[0] = {(lv_value_precise_t)(r - sinf(a) * r * back), (lv_value_precise_t)(r + cosf(a) * r * back)};
  p[1] = {(lv_value_precise_t)(r + sinf(a) * r * len), (lv_value_precise_t)(r - cosf(a) * r * len)};
}

void placeHands(Face* f, const tm& l) {
  const float ma = (l.tm_min + (f->sl ? l.tm_sec / 60.0f : 0)) / 60.0f * 2 * PI;
  const float ha = ((l.tm_hour % 12) + l.tm_min / 60.0f) / 12.0f * 2 * PI;
  hand(f->h, f->r, ha, f->hB / 100.0f, f->hL / 100.0f);
  hand(f->m, f->r, ma, f->mB / 100.0f, f->mL / 100.0f);
  lv_line_set_points(f->hl, f->h, 2);
  lv_line_set_points(f->ml, f->m, 2);
  if (!f->sl) return;
  const float sa = l.tm_sec / 60.0f * 2 * PI;
  hand(f->s, f->r, sa, 0.22f, f->sL / 100.0f);
  lv_line_set_points(f->sl, f->s, 2);
  if (f->disk) {
    const int d = lv_obj_get_style_width(f->disk, 0);
    lv_obj_set_pos(f->disk, (int)lroundf(f->r + sinf(sa) * f->r * 0.66f) - d / 2, (int)lroundf(f->r - cosf(sa) * f->r * 0.66f) - d / 2);
  }
}

lv_obj_t* line(lv_obj_t* p, int w, uint32_t c, bool round) {
  lv_obj_t* l = lv_line_create(p);
  lv_obj_set_style_line_width(l, w, 0);
  lv_obj_set_style_line_color(l, C(c), 0);
  lv_obj_set_style_line_rounded(l, round, 0);
  return l;
}

const char* const ROMAN[12] = {"XII", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI"};

// A size x size face at (x, y) of p showing l; with sec a seconds hand (and the station clock's red disc).
Face* face(lv_obj_t* p, int x, int y, int size, const Look& k, bool sec, const tm& l) {
  lv_obj_t* fo = box(p, k.face == NO ? 0 : k.face, size / 2);
  lv_obj_set_size(fo, size, size);
  lv_obj_set_pos(fo, x, y);
  if (k.face == NO) lv_obj_set_style_bg_opa(fo, LV_OPA_TRANSP, 0);
  else if (k.face2 != NO) {
    lv_obj_set_style_bg_grad_color(fo, C(k.face2), 0);
    lv_obj_set_style_bg_grad_dir(fo, LV_GRAD_DIR_VER, 0);
  }
  Face* f = new Face();
  own(fo, f);
  f->r = size / 2.0f;
  f->bars = k.handKind == 1;
  const float r = f->r;
  if (k.rimW && k.rim != NO) {  // a ring child, so the children keep the face's own coordinates
    lv_obj_t* ring = box(fo, 0, size / 2);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, max(2, size / k.rimW), 0);
    lv_obj_set_style_border_color(ring, C(k.rim), 0);
  }
  const int n = k.ticks % 100;
  f->ticks = new lv_point_precise_t[(n + 11) * 2];  // the ticks, then the diver's 11 bezel marks
  for (int i = 0; i < n; i++) {
    const bool hour = n == 12 || i % 5 == 0;
    const bool quarter = hour && (n == 60 ? (i / 5) % 3 == 0 : i % 3 == 0);
    const float a = i / (float)n * 2 * PI;
    if (k.ticks > 100) {
      const int d = quarter ? max(4, size / 22) : max(3, size / 34);
      const float rr = r - size * k.inset / 1000.0f;
      lv_obj_t* dot = box(fo, quarter ? k.tick : k.minor, d / 2);
      lv_obj_set_size(dot, d, d);
      lv_obj_set_pos(dot, (int)lroundf(r + sinf(a) * rr) - d / 2, (int)lroundf(r - cosf(a) * rr) - d / 2);
      continue;
    }
    const float out = r - size * k.inset / 1000.0f, in = out - (hour ? size * k.tickL / 100.0f : size * 0.035f);
    lv_point_precise_t* pt = f->ticks + i * 2;
    pt[0] = {(lv_value_precise_t)(r + sinf(a) * in), (lv_value_precise_t)(r - cosf(a) * in)};
    pt[1] = {(lv_value_precise_t)(r + sinf(a) * out), (lv_value_precise_t)(r - cosf(a) * out)};
    lv_obj_t* tl = line(fo, hour ? max(2, size / k.tickW) : max(1, size / 110), hour ? k.tick : k.minor, !f->bars);
    lv_line_set_points(tl, pt, 2);
  }
  if (k.extra & 8) {
    // The diver's bezel: a mark each hour on the rim, a bright dot at 12.
    for (int i = 1; i < 12; i++) {
      const float a = i / 12.0f * 2 * PI, ro = r - size * 0.02f, ri = r - size * 0.08f;
      lv_point_precise_t* pt = f->ticks + (n + i - 1) * 2;
      pt[0] = {(lv_value_precise_t)(r + sinf(a) * ri), (lv_value_precise_t)(r - cosf(a) * ri)};
      pt[1] = {(lv_value_precise_t)(r + sinf(a) * ro), (lv_value_precise_t)(r - cosf(a) * ro)};
      lv_line_set_points(line(fo, max(1, size / 100), k.tick, true), pt, 2);
    }
    const int d = max(5, size / 14);
    lv_obj_t* dot = box(fo, k.second, d / 2);
    lv_obj_set_size(dot, d, d);
    lv_obj_set_pos(dot, size / 2 - d / 2, (int)lroundf(size * 0.05f) - d / 2);
  }
  // Numerals: 12 3 6 9 or all twelve (not where a window or a sub-dial sits), Arabic or Roman, in the device fonts.
  const int nums = k.numKind == 1 && size < 170 ? 4 : k.nums;
  if (nums) {
    const int px = size >= 170 ? k.numPx : size >= 100 ? k.numPxM : 12;
    const float rr = r - size * k.numR / 100.0f;
    char b[4];
    for (int i = 0; i < 12; i += 12 / nums) {
      if ((i == 3 && (k.extra & 5)) || (i == 6 && (k.extra & 2)) || (i == 9 && (k.extra & 4))) continue;
      const float a = i / 12.0f * 2 * PI;
      const char* s = k.numKind == 1 ? ROMAN[i] : (snprintf(b, sizeof b, "%d", i ? i : 12), b);
      at(fo, s, (int)lroundf(r + sinf(a) * rr) - textW(s, px, true) / 2, (int)lroundf(r - cosf(a) * rr) - lineH(px, true) / 2, px,
         true, k.numKind == 2 ? KIDS[i % 6] : k.num);
    }
  }
  const bool big = size >= 170;
  const data::Day day = data::today();
  char b[8];
  if (k.extra & 1) {
    // The date window at 3.
    const int ww = max(18, size / 5), wh = max(14, size / 8), wx = (int)lroundf(r * 1.56f) - ww / 2, wy = size / 2 - wh / 2;
    lv_obj_t* win = box(fo, 0xffffff, 3);
    lv_obj_set_size(win, ww, wh);
    lv_obj_set_pos(win, wx, wy);
    lv_obj_set_style_border_width(win, 1, 0);
    lv_obj_set_style_border_color(win, C(k.minor), 0);
    snprintf(b, sizeof b, "%d", day.d);
    const int px = big ? 15 : 12;
    at(fo, b, wx + ww / 2 - textW(b, px, true) / 2, wy + wh / 2 - lineH(px, true) / 2, px, true, 0x16181d);
  }
  if ((k.extra & 2) && data::hasWeather()) {
    // The weather at 6: its picture and the temperature.
    JsonObjectConst w = data::weather();
    const int ip = big ? 28 : 20, tp = big ? 16 : 12;
    snprintf(b, sizeof b, "%d°", (int)(w["temp"] | 0));
    const int tw = ip + 2 + textW(b, tp, true), cy = (int)lroundf(r * 1.48f), x0 = size / 2 - tw / 2;
    lv_obj_t* ic = img(fo, data::wxIcon(w["icon"] | "cloud", ip, data::night()));
    lv_obj_set_pos(ic, x0, cy - ip / 2);
    at(fo, b, x0 + ip + 2, cy - lineH(tp, true) / 2, tp, true, k.num);
  }
  if (k.extra & 4) {
    // Sub-dials at 9 and 3: a 24-hour hand, the day of the month.
    const int sd = size / 4, bw = max(1, size / 100);
    for (int side = -1; side <= 1; side += 2) {
      const int cx = (int)lroundf(r + side * r * 0.46f), cy = size / 2;
      lv_obj_t* dial = box(fo, 0, sd / 2);
      lv_obj_set_size(dial, sd, sd);
      lv_obj_set_pos(dial, cx - sd / 2, cy - sd / 2);
      lv_obj_set_style_bg_opa(dial, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(dial, bw, 0);
      lv_obj_set_style_border_color(dial, C(k.minor), 0);
      if (side < 0) {
        const float a = (l.tm_hour * 60 + l.tm_min) / 1440.0f * 2 * PI;
        f->sub[0] = {(lv_value_precise_t)cx, (lv_value_precise_t)cy};
        f->sub[1] = {(lv_value_precise_t)(cx + sinf(a) * sd * 0.38f), (lv_value_precise_t)(cy - cosf(a) * sd * 0.38f)};
        lv_line_set_points(line(fo, max(2, size / 80), k.second, true), f->sub, 2);
      } else {
        snprintf(b, sizeof b, "%d", day.d);
        const int px = big ? 14 : 12;
        at(fo, b, cx - textW(b, px, true) / 2, cy - lineH(px, true) / 2, px, true, k.num);
      }
    }
  }
  f->hl = line(fo, max(3, size / k.hW), k.hour != NO ? k.hour : k.hand, !f->bars);
  f->ml = line(fo, max(2, size / k.mW), k.hand, !f->bars);
  f->sl = sec ? line(fo, max(1, size / 90), k.second, true) : nullptr;
  f->hB = k.hB, f->hL = k.hL, f->mB = k.mB, f->mL = k.mL, f->sL = k.sL;
  f->disk = nullptr;
  if (sec && (k.extra & 16)) {
    const int d = max(6, size / 9);
    f->disk = box(fo, k.second, d / 2);
    lv_obj_set_size(f->disk, d, d);
  }
  const int cd = max(6, size / 18);
  lv_obj_t* capd = box(fo, k.cap, cd / 2);
  lv_obj_set_size(capd, cd, cd);
  lv_obj_set_pos(capd, size / 2 - cd / 2, size / 2 - cd / 2);
  placeHands(f, l);
  return f;
}

#pragma GCC pop_options

// ---------------------------------------------------------------- 7 segments
// Segments a..g of a digit as rounded boxes; the unlit ones stay faintly visible, like a real display.
const uint8_t SEG[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
constexpr uint32_t LCD_ON = 0xff4d3d;

void segDigit(lv_obj_t* p, int x, int y, int w, int h, int th, int digit) {
  const int hh = h / 2, side = hh - th - th / 2;
  const int R[7][4] = {{x + th, y, w - 2 * th, th},           {x + w - th, y + th, th, side},
                       {x + w - th, y + hh + th / 2, th, side}, {x + th, y + h - th, w - 2 * th, th},
                       {x, y + hh + th / 2, th, side},         {x, y + th, th, side},
                       {x + th, y + hh - th / 2, w - 2 * th, th}};
  for (int s = 0; s < 7; s++) {
    lv_obj_t* b = box(p, LCD_ON, th / 2);
    lv_obj_set_pos(b, R[s][0], R[s][1]);
    lv_obj_set_size(b, R[s][2], R[s][3]);
    if (!(SEG[digit] >> s & 1)) lv_obj_set_style_bg_opa(b, 22, 0);
  }
}

int lcdWidth(int dh, bool sec) {
  const int dw = dh * 52 / 100, th = max(3, dh / 9), gap = max(3, dh / 12);
  const int sh = dh * 45 / 100, sw = sh * 52 / 100, st = max(2, sh / 9);
  return 4 * dw + 3 * gap + th + 2 * gap + (sec ? gap + 2 * sw + st + 2 : 0);
}

// HH:MM in 7 segments at (x, y), digits dh tall; the seconds smaller after them.
void lcd(lv_obj_t* p, int x, int y, int dh, const tm& l, bool h12, bool sec) {
  const int dw = dh * 52 / 100, th = max(3, dh / 9), gap = max(3, dh / 12);
  const int hour = h12 ? hour12(l.tm_hour) : l.tm_hour;
  const int ds[4] = {hour / 10, hour % 10, l.tm_min / 10, l.tm_min % 10};
  int cx = x;
  for (int i = 0; i < 4; i++) {
    if (i == 2) {
      for (int k = 0; k < 2; k++) {
        lv_obj_t* d = box(p, LCD_ON, th / 2);
        lv_obj_set_size(d, th, th);
        lv_obj_set_pos(d, cx + gap, y + dh * (k ? 68 : 28) / 100 - th / 2);
      }
      cx += th + 2 * gap;
    }
    if (!(h12 && i == 0 && ds[0] == 0)) segDigit(p, cx, y, dw, dh, th, ds[i]);
    cx += dw + gap;
  }
  if (sec) {
    const int sh = dh * 45 / 100, sw = sh * 52 / 100, st = max(2, sh / 9);
    segDigit(p, cx + gap, y + dh - sh, sw, sh, st, l.tm_sec / 10);
    segDigit(p, cx + gap + sw + st + 2, y + dh - sh, sw, sh, st, l.tm_sec % 10);
  }
}

// ---------------------------------------------------------------- the time in words
// i18n: the numbers 0 to 59 in words, for the clock telling the time in words ("seven twenty-one"); 0 to 23 are also the hours
const char* const NUM_EN[60] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
    "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty", "twenty-one",
    "twenty-two", "twenty-three", "twenty-four", "twenty-five", "twenty-six", "twenty-seven", "twenty-eight",
    "twenty-nine", "thirty", "thirty-one", "thirty-two", "thirty-three", "thirty-four", "thirty-five", "thirty-six",
    "thirty-seven", "thirty-eight", "thirty-nine", "forty", "forty-one", "forty-two", "forty-three", "forty-four",
    "forty-five", "forty-six", "forty-seven", "forty-eight", "forty-nine", "fifty", "fifty-one", "fifty-two",
    "fifty-three", "fifty-four", "fifty-five", "fifty-six", "fifty-seven", "fifty-eight", "fifty-nine"};
const i18n::List NUM_WORDS = {"@number_words", NUM_EN, 60};

// "Seven twenty-one", "Twelve o'clock"; in 12 h with the part of the day (morning, midday, afternoon, evening).
String timeWords(const tm& l, bool h12) {
  const String h = NUM_WORDS[h12 ? hour12(l.tm_hour) : l.tm_hour];
  String s;
  if (!l.tm_min) s = i18n::fmt(h12 ? N_("{0} o'clock") : N_("{0} hundred"), h);  // i18n: the hour on the dot
  else if (l.tm_min < 10) s = i18n::fmt(N_("{0} oh {1}"), h, NUM_WORDS[l.tm_min]);  // i18n: hour and minute 1-9
  else s = i18n::fmt(TC_("time in words", "{0} {1}"), h, NUM_WORDS[l.tm_min]);       // i18n: hour and minute 10-59
  if (h12) {
    const int hr = l.tm_hour;
    s = i18n::fmt(hr < 11 ? N_("{0} in the morning") : hr < 13 ? N_("{0} at midday") : hr < 18 ? N_("{0} in the afternoon")
                                                                                         : N_("{0} in the evening"),
                  s);
  }
  // The first letter in upper case (a whole UTF-8 letter).
  const uint8_t c = s.length() ? (uint8_t)s[0] : 0;
  const int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
  return upperVi(s.substring(0, n)) + s.substring(n);
}

// ---------------------------------------------------------------- a clock card and its live parts
struct Clk {
  Item it;
  lv_obj_t* body;  // the card's content box: redrawn whole at every new minute
  int W, H;
  bool h12, sec;
  int lastMin;
  Face* face;          // a face's seconds hand
  lv_obj_t* secLabel;  // the seconds digits
  void (*draw)(Clk*);
};

void onLive(lv_obj_t* o, const tm& l, bool) {
  Clk* k = (Clk*)lv_obj_get_user_data(o);
  if (!k) return;
  if (l.tm_min != k->lastMin || (k->sec && styleOf(k->it) == "lcd" && k->it.kind == "clock")) return k->draw(k);
  if (k->face && k->face->sl) placeHands(k->face, l);
  if (k->secLabel) setText(k->secLabel, two(l.tm_sec).c_str());
}

void track(const Item& it, Card c, void (*draw)(Clk*), bool everySecond) {
  const int bw = lv_obj_get_style_border_width(c.o, 0);
  lv_obj_t* body = kit::clear(c.o);
  Clk* k = new Clk{it, body, c.w - 2 * bw, c.h - 2 * bw, flag(it, "12"), flag(it, "sec"), -1, nullptr, nullptr, draw};
  lv_obj_set_size(body, k->W, k->H);
  own(body, k);
  lv_obj_set_user_data(body, k);
  live(body, onLive, everySecond);
  draw(k);
}

// The time digits and their small extras: the seconds, then "AM"/"PM" (12 h). Returns the digits' line height.
int timeRow(Clk* k, int x, int y, int px, const tm& l, uint32_t fg, uint32_t dim, int ls) {
  const String s = clockText(l, k->h12);
  lv_obj_t* t = at(k->body, s, x, y, px, true, fg);
  lv_obj_set_style_text_letter_space(t, ls, 0);
  int ex = x + textW(s.c_str(), px, true) + ls * ((int)s.length() - 1) + 4;
  const int small = px >= 56 ? 18 : px >= 40 ? 14 : 12;
  const int by = y + lineH(px, true) - lineH(small, true) - px / 8;
  if (k->sec) {
    k->secLabel = at(k->body, two(l.tm_sec), ex, by, small, true, dim);
    ex += textW("00", small, true) + 4;
  }
  if (k->h12) at(k->body, ampm(l), ex, by - (k->sec ? lineH(small, true) : 0), small, true, dim);
  return lineH(px, true);
}

#pragma GCC push_options
#pragma GCC optimize("Os")  // posters are built once a minute
// Posters for across the room: the time centred (or its hours over its minutes in orange), the date under it.
void posterClock(Clk* k, bool stack, const tm& l, const data::Day& d, uint32_t fg, uint32_t dim) {
  const bool sm = k->it.size == S;
  char date[48], hh[8], mm[4];
  // "Mon, Nov 16" (small) or "Monday, Nov 16", after "AM · " in 12 h.
  snprintf(date, sizeof date, "%s%s%s", k->h12 ? ampm(l) : "", k->h12 ? " · " : "", (sm ? dShort(d) : dLong(d)).c_str());
  const String lun = lunarLine(d);
  if (stack) snprintf(hh, sizeof hh, k->h12 ? "%d" : "%02d", k->h12 ? hour12(l.tm_hour) : l.tm_hour);
  else snprintf(hh, sizeof hh, k->h12 ? "%d:%02d" : "%02d:%02d", k->h12 ? hour12(l.tm_hour) : l.tm_hour, l.tm_min);
  snprintf(mm, sizeof mm, "%02d", l.tm_min);
  const PLine center[3] = {{hh, 96, true, fg}, {date, sm ? 14 : 18, true, fg}, {lun.c_str(), 14, false, dim}};
  const PLine two_[3] = {{hh, 96, true, fg}, {mm, 96, true, 0xff9500}, {date, 14, true, dim}};
  poster(k->body, stack ? two_ : center, stack || !sm ? 3 : 2, 0, 0, k->W, k->H);
}
#pragma GCC pop_options

void drawClock(Clk* k) {
  lv_obj_t* b = k->body;
  lv_obj_clean(b);
  k->face = nullptr, k->secLabel = nullptr;
  const Item& it = k->it;
  const int W = k->W, H = k->H;
  const theme::Home& th = theme::home();
  const uint32_t fg = onOwn(th.fg), dim = onOwnDim(th.light ? 0x4b5160 : 0xd9dcea);
  const bool dark = theme::kind() == theme::Kind::Dark || !th.light;
  const tm l = nowTm();
  k->lastMin = l.tm_min;
  const data::Day d = data::today();
  const String st = styleOf(it);
  if (faceIndex(st) >= 0) {
    const Look look = lookOf(st, dark, fg, dim);
    if (it.size == S) {
      k->face = face(b, W / 2 - 50, -2, 100, look, k->sec, l);
      mid(b, dShort(d), W / 2, H - lineH(13, true) + 2, 13, true, fg);
    } else if (it.size == M) {
      k->face = face(b, -2, H / 2 - 58, 116, look, k->sec, l);
      at(b, WEEKDAY[d.wday], 128, 8, 20, true, fg);
      at(b, i18n::date(N_("{MM} {d}"), d.d, d.m), 128, 36, 16, false, fg);  // November 18
      fit(b, lunarLine(d), 128, 62, W - 128, 20, 13, 12, false, dim);
      at(b, clockText(l, k->h12) + (k->h12 ? String(" ") + ampm(l) : String("")), 128, H - 30, 20, true, dim);
    } else {
      k->face = face(b, W / 2 - 98, 0, 196, look, k->sec, l);
      mid(b, dFull(d), W / 2, 206, 18, true, fg);
      mid(b, lunarLine(d) + " · " + data::canChiYear(data::lunar(d).y), W / 2, 232, 13, false, dim);
    }
    return;
  }
  if (st == "center" || st == "stack") return posterClock(k, st == "stack", l, d, fg, dim);
  if (st == "big") {
    const String hh = k->h12 ? String(hour12(l.tm_hour)) : two(l.tm_hour), mm = two(l.tm_min);
    if (it.size == M) {
      timeRow(k, -3, 8, 64, l, fg, dim, -2);
      at(b, dLong(d) + " · " + lunarLine(d), 0, H - 18, 13, false, dim);
      return;
    }
    const bool big = it.size == L;
    const int px = big ? 96 : 56, lh = lineH(px, true);
    lv_obj_t* a = at(b, hh, -3, big ? 0 : -4, px, true, fg);
    lv_obj_t* m2 = at(b, mm, -3, (big ? 0 : -4) + lh - (big ? 10 : 4), px, true, big ? 0xff9500 : fg);
    lv_obj_set_style_text_letter_space(a, -2, 0);
    lv_obj_set_style_text_letter_space(m2, -2, 0);
    if (big) {
      at(b, dFull(d), 0, H - 44, 18, true, fg);
      at(b, lunarLine(d), 0, H - 18, 13, false, dim);
      if (k->h12) right(b, ampm(l), 8, 18, true, dim);
      if (k->sec) k->secLabel = right(b, two(l.tm_sec), 34, 28, true, dim);
    } else {
      if (k->h12) right(b, ampm(l), 2, 12, true, dim);
      if (k->sec) k->secLabel = right(b, two(l.tm_sec), H - 18, 14, true, dim);
    }
    return;
  }
  if (st == "flip") {
    const int n = k->sec && it.size != S ? 3 : 2;
    const int gap = 6, cw = (W - gap * (n - 1)) / n, r = it.size == S ? 8 : 12;
    const int ch = it.size == S ? 72 : it.size == M ? H : 190;
    const int px = it.size == L ? (n == 3 ? 64 : 96) : it.size == M ? 64 : 44;
    const bool bold = true;
    const String v[3] = {two(k->h12 ? hour12(l.tm_hour) : l.tm_hour), two(l.tm_min), two(l.tm_sec)};
    for (int i = 0; i < n; i++) {
      lv_obj_t* card = box(b, 0x15181e, r);
      lv_obj_set_size(card, cw, ch);
      lv_obj_set_pos(card, i * (cw + gap), 0);
      lv_obj_t* lo = box(card, 0x1d2128, r);  // the lower leaf, a shade lighter
      lv_obj_set_size(lo, cw, ch / 2);
      lv_obj_set_pos(lo, 0, ch / 2);
      const int dpx = i == 2 ? px * 2 / 3 : px;
      lv_obj_t* t = at(card, v[i], cw / 2 - (textW(v[i].c_str(), dpx, bold) - 1) / 2, ch / 2 - lineH(dpx, bold) / 2, dpx, bold, 0xf2f4f7);
      lv_obj_set_style_text_letter_space(t, -1, 0);
      if (i == 2) k->secLabel = t;
      lv_obj_t* fold = box(card, 0x0d0f13, 0);
      lv_obj_set_size(fold, cw, 2);
      lv_obj_set_pos(fold, 0, ch / 2 - 1);
    }
    if (k->h12) right(b, ampm(l), ch + 6, 13, true, dim);
    if (it.size != M) {
      at(b, it.size == S ? dShort(d) : dFull(d), 0, ch + 8, it.size == S ? 14 : 18, true, fg);
      if (it.size == L) at(b, lunarLine(d), 0, ch + 36, 13, false, dim);
    }
    if (it.size == S && k->sec) k->secLabel = right(b, two(l.tm_sec), ch + 8, 14, true, dim, k->h12 ? -26 : 0);
    return;
  }
  if (st == "lcd") {
    const int ph = it.size == S ? 70 : it.size == M ? H : 150;
    lv_obj_t* panel = box(b, 0x0b0f14, 10);
    lv_obj_set_size(panel, W, ph);
    int dh = it.size == S ? 44 : it.size == M ? 74 : 100;
    while (dh > 20 && lcdWidth(dh, k->sec) > W - 12) dh -= 2;
    lcd(panel, (W - lcdWidth(dh, k->sec)) / 2, (ph - dh) / 2, dh, l, k->h12, k->sec);
    if (k->h12) at(panel, ampm(l), 6, 4, 12, true, LCD_ON);
    if (it.size != M) {
      at(b, it.size == S ? dShort(d) : dFull(d), 0, ph + 8, it.size == S ? 14 : 18, true, fg);
      if (it.size == L) at(b, lunarLine(d), 0, ph + 36, 13, false, dim);
    }
    return;
  }
  if (st == "words") {
    const int bh = it.size == S ? 86 : it.size == M ? 78 : 190;
    lv_obj_t* wl = label(b, "", 24, true, fg);
    const int px = fitText(wl, timeWords(l, k->h12).c_str(), W, bh, it.size == L ? 24 : 22, 14, true, 2);
    int lines = 1;
    for (const char* q = lv_label_get_text(wl); *q; q++) lines += *q == '\n';
    const int th = lines * lineH(px, true) + (lines - 1) * 2;
    lv_obj_set_pos(wl, 0, it.size == L ? (bh - th) / 2 : 0);
    if (it.size == L) {
      at(b, dFull(d), 0, H - 44, 18, true, fg);
      at(b, lunarLine(d), 0, H - 18, 13, false, dim);
    } else {
      at(b, dShort(d) + " · " + data::lunarText(data::lunar(d)), 0, H - 18, 13, false, dim);
    }
    return;
  }
  if (st == "lunar") {
    const data::Lunar lu = data::lunar(d);
    const String lm = String(lu.d) + "/" + lu.m;
    const uint32_t red = onOwn(0xff6b5a);
    if (it.size == S) {
      timeRow(k, -2, 0, 34, l, fg, dim, -1);
      cap(b, N_("Lunar"), red, 0, 42);
      at(b, lm, -1, 58, 34, true, red);
      fit(b, i18n::fmt(N_("{0} year"), data::canChiYear(lu.y)), 0, H - 18, W, 18, 13, 12, false, dim);
    } else if (it.size == M) {
      timeRow(k, -2, 6, 56, l, fg, dim, -2);
      at(b, dLong(d), 0, H - 18, 13, true, fg);
      const int rx = W - 104;
      cap(b, N_("Lunar"), red, rx, 4);
      at(b, lm, rx - 1, 22, 44, true, red);
      fit(b, i18n::fmt(N_("{0} year"), data::canChiYear(lu.y)), rx, H - 18, 104, 18, 13, 12, false, dim);
    } else {
      timeRow(k, -3, 4, 64, l, fg, dim, -2);
      at(b, dFull(d), 0, 76, 18, true, fg);
      lv_obj_t* panel = box(b, 0xb3161c, 16);
      lv_obj_set_size(panel, W, H - 112);
      lv_obj_set_pos(panel, 0, 112);
      cap(panel, N_("Lunar"), 0xffe9a8, 12, 12);
      lv_obj_set_style_text_letter_space(at(panel, lm, 10, 32, 64, true, 0xffffff), -1, 0);
      fit(panel, i18n::fmt(N_("{0} day · {1} month"), data::canChiDay(d), data::canChiMonth(lu)), 12, 104, W - 24, 20, 14,
          12, false, 0xffe9a8);
      fit(panel, i18n::fmt(N_("Year of the {0}"), data::canChiYear(lu.y)), 12, 128, W - 24, 20, 14, 12, true, 0xffffff);
    }
    return;
  }
  // digital, the default: the time, the date, the lunar date and the weather.
  JsonObjectConst w = data::weather();
  const bool wx = data::hasWeather();
  const bool atNight = data::night();
  if (it.size == S) {
    timeRow(k, -2, 0, k->sec ? 34 : 44, l, fg, dim, -1);
    at(b, dLong(d), 0, 52, 14, true, fg);
    at(b, lunarLine(d), 0, 72, 13, false, dim);
    if (wx) {
      lv_obj_t* i = img(b, data::wxIcon(w["icon"] | "cloud", 28, atNight));
      lv_obj_set_pos(i, -2, H - 28);
      at(b, String((int)(w["temp"] | 0)) + "°  " + (const char*)(w["desc"] | ""), 30, H - 22, 13, true, fg);
    }
    return;
  }
  if (it.size == M) {
    timeRow(k, -2, 4, 56, l, fg, dim, -2);
    at(b, dShort(d) + " · " + lunarLine(d), 0, H - 18, 13, false, dim);
    if (wx) {
      const int rx = W - 112;
      const String city = upperVi(data::cityShort(w["city"] | ""));
      lv_obj_set_style_text_letter_space(at(b, city, rx + 112 - textW(city.c_str(), 12, true) - (glyphs(city) - 1), 4, 12, true, dim), 1, 0);
      const String temp = String((int)(w["temp"] | 0)) + "°";
      const int tx = rx + 112 - textW(temp.c_str(), 28, true);
      at(b, temp, tx, 22, 28, true, fg);
      lv_obj_t* ic = img(b, data::wxIcon(w["icon"] | "cloud", 40, atNight));
      lv_obj_set_pos(ic, tx - 42, 22 + lineH(28, true) / 2 - 20);
      lv_obj_t* ds = label(b, "", 13, false, fg);
      const int dpx = fitText(ds, w["desc"] | "", 112, 18, 13, 12);
      lv_obj_set_pos(ds, rx + 112 - textW(lv_label_get_text(ds), dpx, false), 62);
      String rng = String((int)(w["min"] | 0)) + "–" + (int)(w["max"] | 0) + "°";
      if (!w["rain"].isNull()) rng += String(" · ") + i18n::fmt(N_("rain {0}%"), String((int)(w["rain"] | 0)));
      lv_obj_t* rl = label(b, "", 12, false, dim);
      const int rpx = fitText(rl, rng.c_str(), 112, 16, 12, 12);
      lv_obj_set_pos(rl, rx + 112 - textW(lv_label_get_text(rl), rpx, false), 82);
    }
    return;
  }
  timeRow(k, -3, 6, 64, l, fg, dim, -2);
  const data::Lunar lu = data::lunar(d);
  at(b, dFull(d), 0, 84, 18, true, fg);
  // i18n: a lunar date and the year's name (vi "{0} âm lịch · năm {1}")
  at(b, i18n::fmt(N_("Lunar {0} · {1} year"), data::lunarText(lu), data::canChiYear(lu.y)), 0, 110, 13, false, dim);
  int y = 138;
  if (wx && y + 40 <= H) {
    lv_obj_t* i = img(b, data::wxIcon(w["icon"] | "cloud", 40, atNight));
    lv_obj_set_pos(i, -4, y - 4);
    at(b, String((int)(w["temp"] | 0)) + "°", 40, y, 28, true, fg);
    fit(b, String(w["desc"] | "") + " · " + (int)(w["min"] | 0) + "–" + (int)(w["max"] | 0) + "°", 100, y + 6, W - 100, 20,
        13, 12, false, dim);
    y += 44;
  }
  if (y + 36 <= H) {
    const int now = data::nowMinutes();
    String next;
    for (const auto& e : data::eventsOn(d)) {
      const int m = e.time.length() ? e.time.substring(0, 2).toInt() * 60 + e.time.substring(3, 5).toInt() : -1;
      if (m < 0 || m >= now) {
        next = (e.time.length() ? e.time + " " : "") + e.title;
        break;
      }
    }
    if (!next.length()) next = T_("No more events today");
    lv_obj_t* bar = box(b, 0xff9500, 2);
    lv_obj_set_size(bar, 4, 32);
    lv_obj_set_pos(bar, 0, y + 2);
    at(b, NC_("upcoming", "Next"), 12, y, 12, true, dim);
    fit(b, next, 12, y + 16, W - 12, 20, 14, 12, false, fg);
  }
}

// ---------------------------------------------------------------- world
struct City {
  const char* name;   // the id kept in s3.cities (the web's list): never translated
  const char* label;  // shown (translated)
  int offsetMin;
};
const City CITIES[] = {{"Hà Nội", N_("Hanoi"), 420},         {"Tokyo", N_("Tokyo"), 540},
                       {"Seoul", N_("Seoul"), 540},          {"Bắc Kinh", N_("Beijing"), 480},
                       {"Singapore", N_("Singapore"), 480},  {"Sydney", N_("Sydney"), 660},
                       {"Dubai", N_("Dubai"), 240},          {"Paris", N_("Paris"), 60},
                       {"London", N_("London"), 0},          {"Berlin", N_("Berlin"), 60},
                       {"New York", N_("New York"), -300},   {"Los Angeles", N_("Los Angeles"), -480},
                       {"Bangkok", N_("Bangkok"), 420},      {"Moskva", N_("Moscow"), 180}};

void drawWorld(Clk* k) {
  lv_obj_t* b = k->body;
  lv_obj_clean(b);
  const Item& it = k->it;
  const int W = k->W;
  k->lastMin = nowTm().tm_min;
  const uint32_t fg = onOwn(WHITE), dim = onOwnDim(0x94a3b8);
  cap(b, N_("World clock"), dim);
  const auto names = data::codesOf(it.opt.length() ? it.opt : desk::setting("s3.cities", ""), "Tokyo,Paris,New York,Sydney");
  const time_t now = time(nullptr);
  const String wst = styleOf(it);
  const bool faces = wst == "analog", big = wst == "big";
  const int most = big ? (it.size == S ? 1 : it.size == M ? 2 : 4) : it.size == S ? 2 : 4;
  int shown = 0;
  for (const String& n : names) {
    const City* ct = nullptr;
    for (const City& x : CITIES)
      if (n == x.name) ct = &x;
    if (!ct || shown >= most) continue;
    const time_t t = now + ct->offsetMin * 60;
    tm g;
    gmtime_r(&t, &g);
    const int diff = (ct->offsetMin - 420) / 60;
    // i18n: the difference with Vietnam's time in hours, e.g. "+2 h" (vi "{0} giờ")
    const String rel = diff == 0 ? String(T_("same time")) : i18n::fmt(N_("{0} h"), (diff > 0 ? "+" : "") + String(diff));
    if (big) {
      // A poster a city: its name, its time as big as the cell allows, the difference.
      const int colW = it.size == S ? W : W / 2, rowH = it.size == L ? (k->H - 22) / 2 : k->H - 22;
      const String tt = k->h12 ? clockText(g, true) : hhmm(g.tm_hour, g.tm_min), sub = k->h12 ? String(ampm(g)) + " · " + rel : rel;
      PLine lines[3] = {{ct->label, 14, true, fg}, {tt.c_str(), 56, true, fg}, {sub.c_str(), 13, false, dim}};
      poster(b, lines, 3, (shown % 2) * colW, 22 + shown / 2 * rowH, colW, rowH);
    } else if (faces) {  // small faces, dark at the city's night
      const int fs = it.size == S ? 50 : 56, colW = W / most, x0 = shown * colW;
      face(b, x0 + (colW - fs) / 2, 22, fs, lookOf("minimal", g.tm_hour < 6 || g.tm_hour >= 18), false, g);
      mid(b, T_(ct->label), x0 + colW / 2, 26 + fs, 12, true, fg);
      mid(b, rel, x0 + colW / 2, 42 + fs, 12, false, dim);
    } else {
      const int x = it.size == S ? 0 : (shown % 2) * (W / 2 + 6);
      const int y = 22 + (it.size == S ? shown : shown / 2) * 48;
      at(b, ct->label, x, y, 13, true, fg);
      at(b, k->h12 ? clockText(g, true) + " " + ampm(g) : hhmm(g.tm_hour, g.tm_min), x, y + 16, 24, true, fg);
      at(b, rel, x + 70, y + 26, 12, false, dim);
    }
    shown++;
  }
}

// ---------------------------------------------------------------- alarm
void drawAlarm(Clk* k) {
  lv_obj_t* b = k->body;
  lv_obj_clean(b);
  const int W = k->W, H = k->H;
  k->lastMin = nowTm().tm_min;
  const uint32_t fg = onOwn(WHITE), dim = onOwnDim(0x9aa3b2), off = onOwnDim(0x6b7280);
  cap(b, N_("Alarm"), onOwn(0xff9f0a));
  if (styleOf(k->it) == "list") {
    const auto all = alarms::list();
    if (all.empty()) {
      fit(b, N_("No alarms. Tap to set one."), 0, 22, W, H - 22, 13, 12, false, dim);
      return;
    }
    const int cols = k->it.size == S ? 1 : 2, colW = W / cols;
    for (size_t i = 0; i < all.size() && (int)i < cols * 3; i++) {
      const alarms::Alarm& a = all[i];
      const int x = (i / 3) * colW, y = 22 + (i % 3) * 32;
      at(b, hhmm(a.h, a.m), x, y, 22, true, a.on ? fg : off);
      if (k->it.size == S) continue;  // a small card: only the times
      fit(b, a.label.length() ? a.label : alarms::daysText(a.days), x + 68, y + 2, colW - 72, 28, 12, 12, false,
          a.on ? dim : off, 0);
    }
    return;
  }
  String when, day;
  alarms::next(when, day);
  if (!when.length()) {
    at(b, "--:--", 0, 22, 34, true, off);
    fit(b, N_("No alarms. Tap to set one."), 0, 66, W, H - 66, 13, 12, false, dim);
    return;
  }
  at(b, when, -1, 22, 34, true, fg);
  fit(b, day, 0, 66, W, H - 66, 14, 12, false, onOwnDim(0xc9d0d8));
}

}  // namespace

void wClock(lv_obj_t*, const Item& it, Card c) {
  if (SKIN.own) glass(c.o);
  track(it, c, drawClock, flag(it, "sec"));
}

void wWorld(lv_obj_t*, const Item& it, Card c) { track(it, c, drawWorld, false); }

void wAlarm(lv_obj_t*, const Item& it, Card c) { track(it, c, drawAlarm, false); }

}  // namespace wid
