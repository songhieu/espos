#include "Widgets.h"

#include <ArduinoJson.h>
#include <Bitmap.h>
#include <Epub.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <FsHelpers.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/wifi.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/transfer.h"
#include "inkdesk/DashDraw.h"
#include "inkdesk/DeclGfx.h"
#include "inkdesk/InkDeskStore.h"
#include "inkdesk/InkUi.h"
#include "inkdesk/Lunar.h"
#include "util/BookProgress.h"

namespace inkdesk::widgets {
namespace {

using namespace inkdesk::draw;
namespace iu = inkdesk::ui;

constexpr int RADIUS = 12;
constexpr int P = 12;  // inner padding
// Box heights: one grid row is H_LOW and under, two rows reach H_BIG, three H_HUGE.
constexpr int H_LOW = 140, H_BIG = 180, H_HUGE = 280;
constexpr size_t DASH_CAP = 16 * 1024;
constexpr size_t MAX_ITEMS = 20;
constexpr int LUNAR_TZ = 7;  // the Vietnamese lunar calendar is computed for UTC+7

struct KindName {
  const char* name;
  Kind kind;
};
constexpr KindName KINDS[] = {
    {"clock", Kind::Clock},     {"date", Kind::Date},     {"month", Kind::Month},   {"events", Kind::Events},
    {"countdown", Kind::Countdown}, {"weather", Kind::Weather}, {"reading", Kind::Reading}, {"stats", Kind::Stats},
    {"news", Kind::News},       {"notify", Kind::Notify}, {"battery", Kind::Battery}, {"app", Kind::App},
    {"gold", Kind::Gold},       {"stocks", Kind::Stocks}, {"fx", Kind::Fx},         {"crypto", Kind::Crypto},
    {"books", Kind::Books},     {"photo", Kind::Photo},   {"overview", Kind::Overview}, {"pkg", Kind::Pkg},
};

// A widget's chosen codes ("SJC,N24K"), or `def` when it chose none.
std::vector<std::string> codes(const std::string& opt, const char* def) {
  std::vector<std::string> out;
  const std::string src = opt.empty() ? def : opt;
  size_t start = 0;
  while (start <= src.size()) {
    size_t end = src.find(',', start);
    if (end == std::string::npos) end = src.size();
    std::string c = src.substr(start, end - start);
    for (char& ch : c) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    if (!c.empty()) out.push_back(c);
    start = end + 1;
  }
  return out;
}

// The widget's codes in order, each with its entry in `all` (null when the server has not sent it
// yet). With no keys in the data (an older server copy) every entry is taken as it is.
template <typename T>
struct Pick {
  std::string code;
  const T* v;
};
template <typename T, typename Key>
std::vector<Pick<T>> chosen(const std::vector<T>& all, const std::vector<std::string>& want, Key key) {
  std::vector<Pick<T>> out;
  bool any = false;
  for (const auto& c : want) {
    const T* hit = nullptr;
    for (const auto& x : all) {
      if (key(x) == c) {
        hit = &x;
        break;
      }
    }
    any = any || hit != nullptr;
    out.push_back({c, hit});
  }
  if (!any) {
    out.clear();
    for (const auto& x : all) {
      if (key(x).empty()) out.push_back({std::string(), &x});
    }
  }
  return out;
}

Kind kindOf(const char* name) {
  for (const auto& k : KINDS) {
    if (strcmp(k.name, name) == 0) return k.kind;
  }
  return Kind::Unknown;
}

const char* settingKey(const Screen s) {
  switch (s) {
    case Screen::SleepLandscape:
      return "sleep.wl";
    case Screen::SleepPortrait:
      return "sleep.wp";
    default:
      return "home.w";
  }
}

void gridOf(const Screen s, int& cols, int& rows) {
  cols = s == Screen::SleepLandscape ? 6 : 4;
  rows = s == Screen::SleepLandscape ? 4 : 7;
}

Item item(const Kind k, const int x, const int y, const int w, const int h, const char* style, const char* opt = "") {
  Item it;
  it.kind = k;
  it.x = x;
  it.y = y;
  it.w = w;
  it.h = h;
  it.style = style;
  it.opt = opt;
  return it;
}

// "1734.93" -> "1.734,93" (Vietnamese grouping); `dec` decimals.
std::string num(const double v, const int dec) {
  char raw[32];
  snprintf(raw, sizeof(raw), "%.*f", dec, std::fabs(v));
  std::string s(raw);
  const size_t dot = s.find('.');
  std::string intPart = dot == std::string::npos ? s : s.substr(0, dot);
  std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
  std::string grouped;
  for (size_t i = 0; i < intPart.size(); i++) {
    if (i > 0 && (intPart.size() - i) % 3 == 0) grouped.push_back('.');
    grouped.push_back(intPart[i]);
  }
  if (!frac.empty()) grouped += "," + frac;
  return (v < 0 ? "-" : "") + grouped;
}

void triangle(const GfxRenderer& r, const int x, const int y, const int s, const bool up, const bool black = true) {
  // Up: filled; down: outline, so the two read apart in black and white.
  const int xs[3] = {x, x + s, x + s / 2};
  const int ys[3] = {up ? y + s : y, up ? y + s : y, up ? y : y + s};
  if (up) {
    r.fillPolygon(xs, ys, 3, black);
  } else {
    r.drawLine(xs[0], ys[0], xs[1], ys[1], 2, black);
    r.drawLine(xs[1], ys[1], xs[2], ys[2], 2, black);
    r.drawLine(xs[2], ys[2], xs[0], ys[0], 2, black);
  }
}

void caps(const GfxRenderer& r, const int x, const int y, const char* text, const bool black = true, const int maxW = 0) {
  if (maxW > 0) {
    drawFit(r, CAPS_FONT, x, y, text, maxW, kBold, black);
  } else {
    r.drawText(CAPS_FONT, x, y, text, black, kBold);
  }
}

// The frame style of the screen being drawn (set by draw(); the sheet and tiles follow it).
Skin gSkin = Skin::Card;
// Pictures drawn into the current widget (covers, photos): an inverted widget flips them back.
std::vector<Box> gImages;

bool rounded(const Skin s) { return s == Skin::Card || s == Skin::Shadow; }

// The widget's frame in the screen's style; `shadow` is drawn by draw() before the widget.
void frame(const GfxRenderer& r, const Box& b, const bool focused) {
  switch (gSkin) {
    case Skin::Card:
    case Skin::Shadow:
      r.drawRoundedRect(b.x, b.y, b.w, b.h, focused ? 4 : 2, RADIUS, true);
      break;
    case Skin::Bold:
      r.drawRect(b.x, b.y, b.w, b.h, focused ? 5 : 3, true);
      break;
    case Skin::Line:
      r.fillRect(b.x, b.y, b.w, 3, true);
      break;
    case Skin::Bare:
      break;
  }
  if (focused && b.h > 50) r.fillRoundedRect(b.x, b.y + 14, 6, b.h - 28, 3, Color::Black);
}

// The shadow style: a black block offset down-right, the widget's own area white on top of it.
void shadowUnder(const GfxRenderer& r, const Box& b) {
  r.fillRect(b.x + SHADOW, b.y + SHADOW, b.w, b.h, true);
  r.fillRoundedRect(b.x, b.y, b.w, b.h, RADIUS, Color::White);
}

// Puts back what lies outside a rounded box's corners (white, or the shadow under the bottom right).
void clearCorners(const GfxRenderer& r, const Box& b, const int rad) {
  for (int dy = 0; dy < rad; dy++) {
    for (int dx = 0; dx < rad; dx++) {
      const int ox = rad - dx, oy = rad - dy;  // distance from the arc centre, pixel centres
      if (ox * ox + oy * oy <= rad * rad) continue;
      const int xs[2] = {b.x + dx, b.x + b.w - 1 - dx};
      const int ys[2] = {b.y + dy, b.y + b.h - 1 - dy};
      for (const int x : xs) {
        for (const int y : ys) {
          const bool underShadow = gSkin == Skin::Shadow && x >= b.x + SHADOW && y >= b.y + SHADOW;
          r.drawPixel(x, y, underShadow);
        }
      }
    }
  }
}

// The biggest digits font whose `text` fits w x h.
int digitsFont(const GfxRenderer& r, const char* text, const int w, const int h) {
  for (const int f : {INK_60_FONT_ID, INK_44_FONT_ID, INK_30_FONT_ID}) {
    if (r.getTextWidth(f, text, kBold) <= w && digitHeight(f) <= h) return f;
  }
  return INK_30_FONT_ID;
}

// A number as big as fits w x maxH: hero digits, or a text font when even the smallest digits are
// too wide. Returns the height drawn.
int heroNumber(const GfxRenderer& r, const char* text, const int x, const int top, const int w, const int maxH) {
  const int f = digitsFont(r, text, w, maxH);
  if (r.getTextWidth(f, text, kBold) <= w) {
    drawDigits(r, f, x, top, text);
    return digitHeight(f);
  }
  const int tf = iu::fitFont(r, INK_24_FONT_ID, text, w, kBold);
  r.drawText(tf, x, top, text, true, kBold);
  return r.getLineHeight(tf);
}

bool twelveHour() { return SETTINGS.clockFormat == 1; }

// The biggest digits font whose `text` fits w x h; 0 when even the smallest does not.
int digitsFontOr0(const GfxRenderer& r, const char* text, const int w, const int h) {
  for (const int f : {INK_60_FONT_ID, INK_44_FONT_ID, INK_30_FONT_ID}) {
    if (r.getTextWidth(f, text, kBold) <= w && digitHeight(f) <= h) return f;
  }
  return 0;
}

// Weekday, date and lunar date stacked in a column beside a clock face (fewer lines in a low box).
void dateColumn(const GfxRenderer& r, const Data& d, const int x, const int y, const int w, const int h) {
  if (!d.timeValid || w < 60) return;
  const int capsH = r.getLineHeight(CAPS_FONT);
  char date[16], buf[48];
  snprintf(date, sizeof(date), "%d/%d", d.now.tm_mday, d.now.tm_mon + 1);
  // The date in digits when they fit the width, else in bold text.
  const bool digits = r.getTextWidth(INK_30_FONT_ID, date, kBold) <= w;
  const int dateH = digits ? digitHeight(INK_30_FONT_ID) : r.getLineHeight(NOTOSANS_16_FONT_ID);
  if (!d.lunar.empty() && h >= capsH * 2 + dateH + 10) {
    const bool year = !d.lunarYear.empty() && h >= capsH * 3 + dateH + 10;
    const int total = capsH + 4 + dateH + 6 + capsH + (year ? capsH : 0);
    int cy = y + (h - total) / 2;
    drawFit(r, CAPS_FONT, x, cy, iu::weekdayCaps(d.now.tm_wday), w, kBold);
    cy += capsH + 4;
    if (digits) {
      drawDigits(r, INK_30_FONT_ID, x, cy, date);
    } else {
      drawFit(r, NOTOSANS_16_FONT_ID, x, cy, date, w, kBold);
    }
    cy += dateH + 6;
    snprintf(buf, sizeof(buf), tr(STR_IK_W_LUNAR_SHORT_CAPS_FMT), d.lunar.c_str());
    drawFit(r, CAPS_FONT, x, cy, buf, w, kBold);
    if (year) drawFit(r, CAPS_FONT, x, cy + capsH, d.lunarYear.c_str(), w);
    return;
  }
  // Low: the weekday over "15/11 · Âm 7/10".
  const int f = NOTOSANS_14_FONT_ID;
  std::string line = date;
  if (!d.lunar.empty()) {
    snprintf(buf, sizeof(buf), " · %s %s", tr(STR_IK_W_LUNAR_WORD), d.lunar.c_str());
    if (r.getTextWidth(f, (line + buf).c_str(), kBold) <= w) line += buf;
  }
  const int total = capsH + r.getLineHeight(f);
  const int cy = y + (h - total) / 2;
  drawFit(r, CAPS_FONT, x, cy, iu::weekdayCaps(d.now.tm_wday), w, kBold);
  drawFit(r, f, x, cy + capsH, line.c_str(), w, kBold);
}

// Text wrapped and centred in a box, in `font` or smaller: empty states and notes.
void centerText(const GfxRenderer& r, const int font, const int x, const int y, const int w, const int h, const char* text,
                const EpdFontFamily::Style style = kBold) {
  iu::wrapCentered(r, font, x, y, w, h, text, true, style);
}

void timeText(const Data& d, char* out, const size_t n) {
  if (!d.timeValid) {
    snprintf(out, n, "--:--");
    return;
  }
  if (!d.showTime) {
    snprintf(out, n, "%d/%d", d.now.tm_mday, d.now.tm_mon + 1);
    return;
  }
  int h = d.now.tm_hour;
  if (twelveHour()) h = h % 12 == 0 ? 12 : h % 12;
  snprintf(out, n, "%02d:%02d", h, d.now.tm_min);
}

// ---- clock ---------------------------------------------------------------------------

std::string viNumber(const int n) {
  static const char* const u[10] = {"", "một", "hai", "ba", "bốn", "năm", "sáu", "bảy", "tám", "chín"};
  if (n < 10) return u[n];
  const int t = n / 10, o = n % 10;
  std::string s = t == 1 ? "mười" : std::string(u[t]) + " mươi";
  if (o == 0) return s;
  if (o == 1 && t > 1) return s + " mốt";
  if (o == 5) return s + " lăm";
  if (o == 4 && t > 1) return s + " tư";
  return s + " " + u[o];
}

// A line `w` px thick in any direction (GfxRenderer's thick lines only stack downwards, so steep ones
// stay one pixel wide).
void thickLine(const GfxRenderer& r, const int x1, const int y1, const int x2, const int y2, const int w) {
  const bool steep = std::abs(y2 - y1) > std::abs(x2 - x1);
  for (int i = 0; i < w; i++) {
    const int o = i - (w - 1) / 2;
    if (steep) {
      r.drawLine(x1 + o, y1, x2 + o, y2, true);
    } else {
      r.drawLine(x1, y1 + o, x2, y2 + o, true);
    }
  }
}

void analog(const GfxRenderer& r, const int cx, const int cy, const int rad, const int hour, const int minute,
            const bool classic, const bool hands) {
  constexpr double kPi = 3.14159265358979;
  if (classic) strokeCircle(r, cx, cy, rad, 4);
  const int inset = classic ? 8 : 2;
  for (int i = 0; i < 60; i++) {
    const bool big = i % 5 == 0;
    if (!classic && !big) continue;
    const double a = i * kPi / 30;
    const int r2 = rad - inset;
    const int r1 = r2 - (big ? std::max(8, rad / 7) : std::max(4, rad / 18));
    thickLine(r, cx + static_cast<int>(r1 * std::sin(a)), cy - static_cast<int>(r1 * std::cos(a)),
              cx + static_cast<int>(r2 * std::sin(a)), cy - static_cast<int>(r2 * std::cos(a)), big ? 4 : 2);
  }
  if (classic && rad >= 60) {
    const int font = rad >= 110 ? NOTOSANS_16_FONT_ID : SMALL_FONT;
    // Small faces show only 12, 3, 6 and 9.
    for (int h = 1; h <= 12; h++) {
      if (rad < 110 && h % 3 != 0) continue;
      const double a = h * kPi / 6;
      const int rr = rad - inset - std::max(8, rad / 7) - rad / 9;
      char n[4];
      snprintf(n, sizeof(n), "%d", h);
      const int tw = r.getTextWidth(font, n, kBold);
      const int th = r.getFontAscenderSize(font);
      r.drawText(font, cx + static_cast<int>(rr * std::sin(a)) - tw / 2,
                 cy - static_cast<int>(rr * std::cos(a)) - r.getLineHeight(font) / 2 + (r.getLineHeight(font) - th) / 2,
                 n, true, kBold);
    }
  }
  if (!hands) return;
  auto hand = [&](const double turns, const double len, const int width) {
    const double a = turns * 2 * kPi;
    thickLine(r, cx, cy, cx + static_cast<int>(len * std::sin(a)), cy - static_cast<int>(len * std::cos(a)), width);
  };
  hand(((hour % 12) + minute / 60.0) / 12.0, rad * 0.5, std::max(5, rad / 14));
  hand(minute / 60.0, rad * 0.78, std::max(3, rad / 22));
  fillCircle(r, cx, cy, std::max(4, rad / 16), Color::Black);
}

// Draws the first of `texts` that fits `w` on one line in `font` (the last one shrunk otherwise).
void drawFirstFit(const GfxRenderer& r, const int font, const int x, const int y, const std::vector<std::string>& texts,
                  const int w, const EpdFontFamily::Style style = kBold) {
  for (const auto& t : texts) {
    if (r.getTextWidth(font, t.c_str(), style) <= w) {
      r.drawText(font, x, y, t.c_str(), true, style);
      return;
    }
  }
  if (!texts.empty()) drawFit(r, font, x, y, texts.back().c_str(), w, style);
}

// "ÂM LỊCH 6/10 · Bính Ngọ", shorter in a narrow box.
void lunarLine(const GfxRenderer& r, const Data& d, const int x, const int y, const int w) {
  char full[80], mid[64], tiny[32];
  snprintf(full, sizeof(full), tr(STR_IK_W_LUNAR_FMT), d.lunar.c_str(), d.lunarYear.c_str());
  snprintf(tiny, sizeof(tiny), tr(STR_IK_W_LUNAR_SHORT_CAPS_FMT), d.lunar.c_str());
  snprintf(mid, sizeof(mid), "%s · %s", tiny, d.lunarYear.c_str());
  drawFirstFit(r, CAPS_FONT, x, y, {full, mid, tiny}, w);
}

void drawClock(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  char t[12];
  timeText(d, t, sizeof(t));
  const std::string& st = it.style;
  if (st == "analog" || st == "analog_min") {
    const bool hands = d.timeValid && d.showTime;
    if (b.w * 5 >= b.h * 8) {
      // Wide: the face on the left, the date beside it.
      const int rad = b.h / 2 - 6;
      analog(r, b.x + P + rad, b.y + b.h / 2, rad, d.now.tm_hour, d.now.tm_min, st == "analog", hands);
      const int x = b.x + P + rad * 2 + 22;
      dateColumn(r, d, x, b.y + P, b.x + b.w - P - x, b.h - 2 * P);
      return;
    }
    const int capsH = r.getLineHeight(CAPS_FONT);
    if (b.h - b.w >= capsH * 2 + 16 && d.timeValid) {
      // Tall: the face on top, the date and lunar date under it.
      const int rad = b.w / 2 - 6;
      const int below = capsH * 2 + 4;
      const int cy = b.y + (b.h - below - 8) / 2;
      analog(r, b.x + b.w / 2, cy, rad, d.now.tm_hour, d.now.tm_min, st == "analog", hands);
      char line[48];
      snprintf(line, sizeof(line), "%s · %d/%d", iu::weekdayCaps(d.now.tm_wday), d.now.tm_mday, d.now.tm_mon + 1);
      const int ty = std::min(cy + rad + 10, b.y + b.h - P - below);
      drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, ty, line, b.w - 2 * P, kBold);
      if (!d.lunar.empty()) {
        snprintf(line, sizeof(line), tr(STR_IK_W_SHEET_LUNAR_FMT), d.lunar.c_str(), d.lunarYear.c_str());
        drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, ty + capsH + 4, line, b.w - 2 * P);
      }
      return;
    }
    const int rad = std::min(b.w, b.h) / 2 - 6;
    analog(r, b.x + b.w / 2, b.y + b.h / 2, rad, d.now.tm_hour, d.now.tm_min, st == "analog", hands);
    return;
  }
  if (st == "flip") {
    if (!d.timeValid || !d.showTime) {
      const int f = digitsFont(r, t, b.w - 2 * P, b.h - 2 * P);
      drawDigits(r, f, b.x + (b.w - r.getTextWidth(f, t, kBold)) / 2, b.y + (b.h - digitHeight(f)) / 2, t);
      return;
    }
    const char hh[3] = {t[0], t[1], 0};
    const char mm[3] = {t[3], t[4], 0};
    // Two cards side by side, or one over the other in a tall box: the bigger cards win.
    int f = INK_30_FONT_ID;
    bool vertical = false;
    for (const int cand : {INK_60_FONT_ID, INK_44_FONT_ID, INK_30_FONT_ID}) {
      const int cw = r.getTextWidth(cand, "88", kBold) + 24;
      const int ch = digitHeight(cand) + 28;
      if (cw * 2 + 12 <= b.w - 2 * P && ch <= b.h - 2 * P) {
        f = cand;
        break;
      }
      if (cw <= b.w - 2 * P && ch * 2 + 12 <= b.h - 2 * P) {
        f = cand;
        vertical = true;
        break;
      }
    }
    const int cw = r.getTextWidth(f, "88", kBold) + 24;
    const int ch = digitHeight(f) + 28;
    const int x0 = b.x + (b.w - (vertical ? cw : cw * 2 + 12)) / 2;
    const int y0 = b.y + (b.h - (vertical ? ch * 2 + 12 : ch)) / 2;
    for (int i = 0; i < 2; i++) {
      const int cx = vertical ? x0 : x0 + i * (cw + 12);
      const int cy = vertical ? y0 + i * (ch + 12) : y0;
      const char* s = i == 0 ? hh : mm;
      r.fillRoundedRect(cx, cy, cw, ch, 12, Color::Black);
      drawDigits(r, f, cx + (cw - r.getTextWidth(f, s, kBold)) / 2, cy + 14, s, false);
      r.fillRect(cx, cy + ch / 2 - 1, cw, 3, false);  // the flap line
    }
    return;
  }
  if (st == "words") {
    static const char* const kDays[7] = {"chủ nhật", "thứ hai", "thứ ba", "thứ tư", "thứ năm", "thứ sáu", "thứ bảy"};
    if (!d.timeValid) return;
    const int h = d.now.tm_hour, m = d.now.tm_min;
    const char* part = h < 4 ? "đêm" : h < 11 ? "sáng" : h < 13 ? "trưa" : h < 18 ? "chiều" : h < 22 ? "tối" : "đêm";
    const std::string l1 = viNumber(h % 12 == 0 ? 12 : h % 12) + " giờ";
    const std::string l2 = !d.showTime ? "" : m == 0 ? "đúng" : viNumber(m);
    const std::string l3 = std::string(m == 0 || !d.showTime ? "" : "phút ") + part + " · " + kDays[d.now.tm_wday];
    // The hour over the minutes (each wrapping when long) in the biggest font that fits the box; the
    // "phút sáng · chủ nhật" line goes when nothing fits with it. A low box puts them on one line.
    const int w = b.w - 2 * P - 4;
    const std::string joined = l2.empty() ? l1 : l1 + " " + l2;
    const int small = b.h >= H_LOW ? NOTOSANS_14_FONT_ID : SMALL_FONT;
    const int subH = r.getLineHeight(small);
    if (b.h >= H_LOW) {
      const std::string text = l2.empty() ? l1 : l1 + "\n" + l2;
      int used = 0;
      int f = iu::wrapInFont(r, INK_SERIF_36_FONT_ID, w, b.h - P - subH, text.c_str(), kBold, &used);
      const bool sub = used <= b.h - P - subH;
      if (!sub) f = iu::wrapInFont(r, INK_SERIF_36_FONT_ID, w, b.h - P, text.c_str(), kBold, &used);
      int y = b.y + (b.h - used - (sub ? subH : 0)) / 2;
      y += iu::wrapIn(r, f, b.x + P + 4, y, w, used, text.c_str(), true, kBold);
      if (sub) drawFit(r, small, b.x + P + 4, y, l3.c_str(), w, EpdFontFamily::ITALIC);
      return;
    }
    int big = 0;
    bool sub = true;
    for (const bool withSub : {true, false}) {
      for (int f = INK_SERIF_36_FONT_ID; f != 0 && big == 0; f = iu::smallerFont(f)) {
        if (r.getTextWidth(f, joined.c_str(), kBold) <= w && r.getLineHeight(f) + (withSub ? subH : 0) <= b.h - P) big = f;
      }
      if (big != 0) {
        sub = withSub;
        break;
      }
    }
    if (big == 0) {
      big = SMALL_FONT_ID;
      sub = false;
    }
    int y = b.y + (b.h - r.getLineHeight(big) - (sub ? subH : 0)) / 2;
    drawFit(r, big, b.x + P + 4, y, joined.c_str(), w, kBold);
    y += r.getLineHeight(big);
    if (sub) drawFit(r, small, b.x + P + 4, y, l3.c_str(), w, EpdFontFamily::ITALIC);
    return;
  }
  if (st == "lunar" && b.w >= 300 && b.h >= H_LOW && b.w >= b.h * 2) {
    // Wide: the time on the left, the date column beside it.
    int f = digitsFontOr0(r, t, (b.w - 2 * P) * 68 / 100, b.h - 2 * P);
    if (f == 0) f = INK_30_FONT_ID;
    const int tw = r.getTextWidth(f, t, kBold);
    drawDigits(r, f, b.x + P, b.y + (b.h - digitHeight(f)) / 2, t);
    const int rx = b.x + P + tw + 16;
    r.fillRect(rx, b.y + P + 4, 2, b.h - 2 * P - 8, true);
    dateColumn(r, d, rx + 16, b.y + P, b.x + b.w - P - rx - 16, b.h - 2 * P);
    return;
  }
  if (st == "lunar") {
    char line[80];
    const int capsH = r.getLineHeight(CAPS_FONT);
    // Date line, digits, lunar line; a low box keeps only the digits and the lunar line.
    const bool dateLine = d.timeValid && b.h - 2 * P - capsH * 2 - 8 >= digitHeight(INK_30_FONT_ID);
    const int room = b.h - 2 * P - capsH * (dateLine ? 2 : 1) - (dateLine ? 8 : 4);
    // A wide box centres the three lines; a narrow one keeps them on the left.
    const bool centre = b.w >= 300;
    if (dateLine) {
      snprintf(line, sizeof(line), "%s · %02d/%02d", iu::weekdayCaps(d.now.tm_wday), d.now.tm_mday, d.now.tm_mon + 1);
      if (centre) {
        drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, b.y + P, line, b.w - 2 * P, kBold);
      } else {
        drawFit(r, CAPS_FONT, b.x + P, b.y + P, line, b.w - 2 * P, kBold);
      }
    }
    const int top = b.y + P + (dateLine ? capsH + 4 : 0);
    if (room >= digitHeight(INK_30_FONT_ID)) {
      const int f = digitsFont(r, t, b.w - 2 * P, room);
      const int x = centre ? b.x + (b.w - r.getTextWidth(f, t, kBold)) / 2 : b.x + P - 2;
      drawDigits(r, f, x, top + (room - digitHeight(f)) / 2, t);
      if (centre) {
        if (!d.lunar.empty()) {
          snprintf(line, sizeof(line), tr(STR_IK_W_LUNAR_FMT), d.lunar.c_str(), d.lunarYear.c_str());
          drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, b.y + b.h - P - capsH, line, b.w - 2 * P, kBold);
        }
        return;
      }
    } else {
      const int f = iu::fitFont(r, INK_24_FONT_ID, t, b.w - 2 * P, kBold);
      r.drawText(f, b.x + P, top + (room - r.getLineHeight(f)) / 2, t, true, kBold);
    }
    if (!d.lunar.empty()) lunarLine(r, d, b.x + P, b.y + b.h - P - capsH, b.w - 2 * P);
    return;
  }
  // Big digits, centred: one line, or the hours over the minutes when that is bigger (tall boxes).
  const int f1 = digitsFontOr0(r, t, b.w - 2 * P, b.h - 2 * P);
  if (d.timeValid && d.showTime && strlen(t) == 5) {
    const char hh[3] = {t[0], t[1], 0};
    const char mm[3] = {t[3], t[4], 0};
    const int f2 = digitsFontOr0(r, "88", b.w - 2 * P, (b.h - 2 * P - 16) / 2);
    if (f2 != 0 && (f1 == 0 || digitHeight(f2) > digitHeight(f1))) {
      const int dh = digitHeight(f2);
      const int y0 = b.y + (b.h - dh * 2 - 16) / 2;
      drawDigits(r, f2, b.x + (b.w - r.getTextWidth(f2, hh, kBold)) / 2, y0, hh);
      drawDigits(r, f2, b.x + (b.w - r.getTextWidth(f2, mm, kBold)) / 2, y0 + dh + 16, mm);
      return;
    }
  }
  const int f = f1 != 0 ? f1 : INK_30_FONT_ID;
  drawDigits(r, f, b.x + (b.w - r.getTextWidth(f, t, kBold)) / 2, b.y + (b.h - digitHeight(f)) / 2, t);
}

// ---- calendar ------------------------------------------------------------------------

void drawDate(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  if (!d.timeValid) return;
  char buf[64];
  const int capsH = r.getLineHeight(CAPS_FONT);
  if (it.style == "compact") {
    // Weekday on top, the date in big digits, the lunar date under them (or beside the weekday when low).
    const int w = b.w - 2 * P;
    const bool low = b.h < H_LOW;
    caps(r, b.x + P, b.y + P, iu::weekdayCaps(d.now.tm_wday));
    if (low && !d.lunar.empty()) {
      snprintf(buf, sizeof(buf), tr(STR_IK_W_LUNAR_SHORT_CAPS_FMT), d.lunar.c_str());
      if (r.getTextWidth(CAPS_FONT, iu::weekdayCaps(d.now.tm_wday), kBold) + 10 + r.getTextWidth(CAPS_FONT, buf, kBold) <= w) {
        drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, buf, kBold);
      }
    }
    snprintf(buf, sizeof(buf), "%d/%d", d.now.tm_mday, d.now.tm_mon + 1);
    const int top = b.y + P + capsH + 2;
    const int bottom = b.y + b.h - (low ? P / 2 : P + capsH + 4);
    const int f = digitsFont(r, buf, w, bottom - top);
    drawDigits(r, f, b.x + P, top + std::max(0, (bottom - top - digitHeight(f)) / 2), buf);
    if (!low && !d.lunar.empty()) lunarLine(r, d, b.x + P, b.y + b.h - P - capsH, w);
    return;
  }
  if (b.h < H_LOW) {
    // Low sheet: a black day tile on the left, the weekday and lunar date beside it.
    const int tileW = std::min(b.w * 45 / 100, b.h + 24);
    if (rounded(gSkin) || gSkin == Skin::Bare) {
      r.fillRoundedRect(b.x, b.y, tileW, b.h, RADIUS, Color::Black);
    } else {
      r.fillRect(b.x, b.y, tileW, b.h, true);
    }
    snprintf(buf, sizeof(buf), tr(STR_IK_MONTH_FMT), d.now.tm_mon + 1);
    drawFitCentered(r, CAPS_FONT, b.x + tileW / 2, b.y + 6, buf, tileW - 8, kBold, false);
    snprintf(buf, sizeof(buf), "%d", d.now.tm_mday);
    const int room = b.h - capsH - 12;
    const int f = digitsFont(r, buf, tileW - 12, room);
    drawDigits(r, f, b.x + (tileW - r.getTextWidth(f, buf, kBold)) / 2, b.y + capsH + 6 + std::max(0, (room - digitHeight(f)) / 2), buf,
               false);
    const int x = b.x + tileW + 12;
    const int w = b.x + b.w - P - x;
    const int lf = NOTOSANS_14_FONT_ID;
    std::string lun;
    if (!d.lunar.empty()) {
      snprintf(buf, sizeof(buf), "%s %s", tr(STR_IK_W_LUNAR_WORD), d.lunar.c_str());
      lun = buf;
    }
    const bool year = !d.lunarYear.empty() && b.h - 12 >= capsH * 2 + r.getLineHeight(lf);
    const int total = capsH + (lun.empty() ? 0 : r.getLineHeight(lf)) + (year ? capsH : 0);
    int y = b.y + (b.h - total) / 2;
    drawFit(r, CAPS_FONT, x, y, iu::weekdayCaps(d.now.tm_wday), w, kBold);
    y += capsH;
    if (!lun.empty()) {
      drawFit(r, lf, x, y, lun.c_str(), w, kBold);
      y += r.getLineHeight(lf);
    }
    if (year) drawFit(r, CAPS_FONT, x, y, d.lunarYear.c_str(), w);
    return;
  }
  // Tear-off sheet: black month band, big day, weekday, lunar line.
  const int bandH = capsH + 8;
  if (gSkin == Skin::Bold || gSkin == Skin::Line) {
    r.fillRect(b.x, b.y, b.w, bandH, true);
    if (gSkin == Skin::Bold) r.drawRect(b.x, b.y, b.w, b.h, 3, true);
  } else {
    r.fillRoundedRect(b.x, b.y, b.w, bandH + RADIUS, RADIUS, Color::Black);
    r.fillRect(b.x, b.y + bandH, b.w, RADIUS, false);
    if (gSkin != Skin::Bare) r.drawRoundedRect(b.x, b.y, b.w, b.h, 2, RADIUS, true);
  }
  snprintf(buf, sizeof(buf), tr(STR_IK_MONTH_FMT), d.now.tm_mon + 1);
  drawCentered(r, CAPS_FONT, b.x + b.w / 2, b.y + 4, buf, kBold, false);
  const int lunarH = d.lunar.empty() ? 0 : r.getLineHeight(SMALL_FONT) + 8;
  snprintf(buf, sizeof(buf), "%d", d.now.tm_mday);
  const int room = b.h - bandH - capsH - lunarH - 20;
  const int f = digitsFont(r, buf, b.w - 2 * P, room);
  // The day and weekday centred in the room between the band and the lunar line.
  int y = b.y + bandH + 6 + std::max(0, (room - digitHeight(f)) / 2);
  drawDigits(r, f, b.x + (b.w - r.getTextWidth(f, buf, kBold)) / 2, y, buf);
  y += digitHeight(f) + 6;
  drawCentered(r, CAPS_FONT, b.x + b.w / 2, y, iu::weekdayCaps(d.now.tm_wday), kBold);
  if (lunarH) {
    y = b.y + b.h - lunarH - 2;
    r.fillRect(b.x + P, y, b.w - 2 * P, 2, true);
    snprintf(buf, sizeof(buf), tr(STR_IK_W_SHEET_LUNAR_FMT), d.lunar.c_str(), d.lunarYear.c_str());
    drawFitCentered(r, SMALL_FONT, b.x + b.w / 2, y + 4, buf, b.w - 2 * P, kBold);
  }
}

void drawMonth(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  if (!d.timeValid) return;
  const int year = d.now.tm_year + 1900, month = d.now.tm_mon + 1, today = d.now.tm_mday;
  char buf[48];
  snprintf(buf, sizeof(buf), tr(STR_IK_W_MONTH_YEAR_FMT), month, year);
  caps(r, b.x + P, b.y + P, buf);
  if (!d.lunar.empty()) {
    snprintf(buf, sizeof(buf), tr(STR_IK_W_LUNAR_SHORT_CAPS_FMT), d.lunar.c_str());
    drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, buf, kBold);
  }
  // Weekday header, Monday first.
  char names[64];
  snprintf(names, sizeof(names), "%s", tr(STR_IK_WDAYS_SHORT));
  const char* wd[7] = {};
  {
    char* save = nullptr;
    int i = 0;
    for (char* t = strtok_r(names, ",", &save); t && i < 7; t = strtok_r(nullptr, ",", &save)) wd[i++] = t;
  }
  const int colW = (b.w - 2 * P) / 7;
  int y = b.y + P + r.getLineHeight(CAPS_FONT) + 6;
  for (int c = 0; c < 7; c++) {
    const char* n = wd[(c + 1) % 7] ? wd[(c + 1) % 7] : "";
    drawCentered(r, CAPS_FONT, b.x + P + c * colW + colW / 2, y, n, kBold);
  }
  y += r.getLineHeight(CAPS_FONT) + 2;
  r.fillRect(b.x + P, y, b.w - 2 * P, 2, true);
  y += 4;
  // Day of week of the 1st (Monday = 0) and month length.
  const int jd1 = lunar::julianDay(1, month, year);
  const int firstCol = jd1 % 7;  // JDN mod 7: 0 = Monday
  const int nextMonthJd = month == 12 ? lunar::julianDay(1, 1, year + 1) : lunar::julianDay(1, month + 1, year);
  const int days = nextMonthJd - jd1;
  const int weeks = (firstCol + days + 6) / 7;
  const int rowH = (b.y + b.h - P - y) / weeks;
  // With the lunar day under it the solar day uses the small caps font so both fit the row.
  const bool showLunar = it.style != "solar" && rowH >= 30;
  // Short rows put the lunar day beside the solar one, smaller and lower, instead of under it.
  const bool beside = showLunar && rowH < 40;
  // Stacked digits: Ubuntu 10 bold digits sit 6..20 px below their line top, Ubuntu 8 ones 5..16;
  // the pair is centred in the row so neither leaves the "today" box.
  const int lunarGap = rowH >= 40 ? 18 : 16;
  const int solarDy = (rowH - (lunarGap + 10)) / 2 - 6;
  const int numFont = showLunar || rowH < 30 ? CAPS_FONT : SMALL_FONT;
  // Lunar days walk forward from the 1st; a new lunar month is looked up only past day 28.
  lunar::Date ld = lunar::fromSolar(1, month, year, LUNAR_TZ);
  for (int day = 1; day <= days; day++) {
    if (day > 1) {
      if (ld.day >= 29) {
        ld = lunar::fromSolar(day, month, year, LUNAR_TZ);
      } else {
        ld.day++;
      }
    }
    const int idx = firstCol + day - 1;
    const int cx = b.x + P + (idx % 7) * colW + colW / 2;
    const int cy = y + (idx / 7) * rowH;
    snprintf(buf, sizeof(buf), "%d", day);
    const bool isToday = day == today;
    const int nh = r.getLineHeight(numFont);
    if (isToday) r.fillRoundedRect(cx - colW / 2 + 3, cy + 1, colW - 6, rowH - 2, 8, Color::Black);
    if (beside) {
      const int ny = cy + (rowH - nh) / 2;
      drawRight(r, numFont, cx + 3, ny, buf, kBold, !isToday);
      snprintf(buf, sizeof(buf), "%d", ld.day);
      r.drawText(SMALL_FONT_ID, cx + 6, ny + 6, buf, !isToday, ld.day == 1 ? kBold : kReg);
      continue;
    }
    drawCentered(r, numFont, cx, cy + (showLunar ? solarDy : (rowH - nh) / 2), buf, kBold, !isToday);
    if (showLunar) {
      if (ld.day == 1) {
        snprintf(buf, sizeof(buf), "%d/%d", ld.day, ld.month);
      } else {
        snprintf(buf, sizeof(buf), "%d", ld.day);
      }
      drawCentered(r, SMALL_FONT_ID, cx, cy + solarDy + lunarGap, buf, ld.day == 1 ? kBold : kReg, !isToday);
    }
  }
}

void drawEvents(const GfxRenderer& r, const Item&, const Box& b, const Data& d) {
  char cap[64];
  if (d.eventCount > 0) {
    snprintf(cap, sizeof(cap), "%s · %d", tr(STR_ID_EVENTS_CAPS), d.eventCount);
  } else {
    snprintf(cap, sizeof(cap), "%s", tr(STR_ID_EVENTS_CAPS));
  }
  caps(r, b.x + P + 6, b.y + P, cap);
  int y = b.y + P + r.getLineHeight(CAPS_FONT) + 6;
  const int w = b.w - 2 * P - 12;
  const int x = b.x + P + 6;
  if (d.events.empty()) {
    centerText(r, b.h < H_LOW ? BODY_FONT : NOTOSANS_16_FONT_ID, x, y, w, b.y + b.h - P - y,
               d.eventsOld ? tr(STR_IK_EVENTS_OLD) : tr(STR_IK_NO_EVENTS));
    return;
  }
  int next = -1;
  if (d.timeValid) {
    const int nowMin = d.now.tm_hour * 60 + d.now.tm_min;
    for (size_t i = 0; i < d.events.size(); i++) {
      if (minutesOfDay(d.events[i].time.c_str(), -1) >= nowMin) {
        next = static_cast<int>(i);
        break;
      }
    }
  }
  // Each row is as tall as its title needs (wrapped, never cut); events that do not fit are
  // counted on a last "+n" line. The list starts at the next event.
  const int font = b.w >= 300 ? BODY_FONT : LIST_FONT;
  const int lineH = r.getLineHeight(font);
  const int timeW = r.getTextWidth(font, "00:00", kBold) + 12;
  const int titleW = w - timeW;
  const int bottom = b.y + b.h - P / 2;
  const int moreH = r.getLineHeight(CAPS_FONT) + 2;
  const int n = static_cast<int>(d.events.size());
  auto linesOf = [&](const std::string& title) {
    // Words packed into lines of titleW (the same breaking the wrap helper uses).
    int lines = 1, cur = 0;
    const int space = r.getTextWidth(font, " ");
    size_t i = 0;
    while (i < title.size()) {
      size_t j = title.find(' ', i);
      if (j == std::string::npos) j = title.size();
      const std::string word = title.substr(i, j - i);
      const int ww = r.getTextWidth(font, word.c_str());
      if (cur > 0 && cur + space + ww > titleW) {
        lines++;
        cur = ww;
      } else {
        cur += (cur > 0 ? space : 0) + ww;
      }
      i = j + 1;
    }
    return lines;
  };
  const int start = next >= 0 ? next : std::max(0, n - 1);
  // Few events in a tall box: an agenda, each event a band with the time above the title.
  const int left = n - start;
  const int agendaTimeF = NOTOSANS_16_FONT_ID;
  if (left > 0 && left <= 4 && (bottom - y) / left >= r.getLineHeight(agendaTimeF) + lineH + 10) {
    const int rowH = (bottom - y) / left;
    for (int i = start; i < n; i++) {
      const bool inv = i == next;
      if (inv) {
        r.fillRoundedRect(x - 8, y + 1, w + 16, rowH - 2, 8, Color::Black);
      } else if (i > start) {
        r.fillRect(x - 8, y, w + 16, 2, true);
      }
      const int titleRoom = rowH - r.getLineHeight(agendaTimeF) - 8;
      int used = 0;
      iu::wrapInFont(r, font, w, titleRoom, d.events[i].title.c_str(), kReg, &used);
      const int top = y + (rowH - r.getLineHeight(agendaTimeF) - used) / 2;
      r.drawText(agendaTimeF, x, top, d.events[i].time.c_str(), !inv, kBold);
      iu::wrapIn(r, font, x, top + r.getLineHeight(agendaTimeF), w, titleRoom, d.events[i].title.c_str(), !inv, kReg);
      y += rowH;
    }
    return;
  }
  int shown = 0;
  for (int i = start; i < n; i++) {
    const int lines = std::min(3, linesOf(d.events[i].title));
    const int rowH = lines * lineH + 6;
    const int reserve = i < n - 1 ? moreH : 0;
    if (shown > 0 && y + rowH + reserve > bottom) break;
    const bool inv = i == next;
    if (inv) r.fillRoundedRect(x - 8, y, w + 16, rowH, 8, Color::Black);
    r.drawText(font, x, y + 3, d.events[i].time.c_str(), !inv, kBold);
    iu::wrap(r, font, x + timeW, y + 3, titleW, d.events[i].title.c_str(), 3, !inv, kReg, 0);
    y += rowH;
    shown++;
  }
  const int hidden = n - shown;
  if (hidden > 0) {
    char more[48];
    snprintf(more, sizeof(more), tr(STR_IK_W_MORE_EVENTS_FMT), hidden);
    drawFit(r, CAPS_FONT, x, std::min(y + 2, bottom - moreH), more, w, kBold);
  }
}

void drawCountdown(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  if (!d.timeValid) return;
  const int y0 = d.now.tm_year + 1900, m0 = d.now.tm_mon + 1, d0 = d.now.tm_mday;
  int days = -1;
  std::string label = tr(STR_IK_W_TO_TET);
  if (it.style == "date" && it.opt.size() >= 10) {
    int ty = 0, tm = 0, td = 0;
    if (sscanf(it.opt.c_str(), "%d-%d-%d", &ty, &tm, &td) == 3) days = lunar::julianDay(td, tm, ty) - lunar::julianDay(d0, m0, y0);
    const size_t bar = it.opt.find('|');
    label = bar == std::string::npos ? std::string() : it.opt.substr(bar + 1);
  } else {
    days = lunar::daysToTet(d0, m0, y0, LUNAR_TZ);
  }
  if (days < 0) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "%d", days);
  if (b.h < H_LOW) {
    // Low: the number big on the left, "ngày" and what it counts to beside it.
    int f = digitsFontOr0(r, buf, (b.w - 2 * P) / 2, b.h - 16);
    int nw;
    if (f != 0) {
      nw = r.getTextWidth(f, buf, kBold);
      drawDigits(r, f, b.x + P, b.y + (b.h - digitHeight(f)) / 2, buf);
    } else {
      f = NOTOSANS_18_FONT_ID;
      nw = r.getTextWidth(f, buf, kBold);
      r.drawText(f, b.x + P, b.y + (b.h - r.getLineHeight(f)) / 2, buf, true, kBold);
    }
    const int x = b.x + P + nw + 14;
    const int w = b.x + b.w - P - x;
    const int wf = NOTOSANS_16_FONT_ID;
    const int capsH = r.getLineHeight(CAPS_FONT);
    const int ty = b.y + (b.h - r.getLineHeight(wf) - capsH) / 2;
    // "83 ngày nữa / là Tết" (or the label chosen for the date).
    drawFit(r, wf, x, ty, tr(STR_IK_W_DAYS_MORE), w, kBold);
    drawFit(r, CAPS_FONT, x, ty + r.getLineHeight(wf), it.style == "date" ? label.c_str() : tr(STR_IK_W_IS_TET), w, kBold);
    return;
  }
  const int capsH = r.getLineHeight(CAPS_FONT);
  const int f = digitsFont(r, buf, b.w - 2 * P, b.h - 2 * P - capsH * 2 - 8);
  const int total = capsH * 2 + digitHeight(f) + 8;
  int y = b.y + (b.h - total) / 2;
  drawCentered(r, CAPS_FONT, b.x + b.w / 2, y, tr(STR_IK_W_LEFT_CAPS), kBold);
  y += capsH + 2;
  drawDigits(r, f, b.x + (b.w - r.getTextWidth(f, buf, kBold)) / 2, y, buf);
  y += digitHeight(f) + 6;
  drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, y, label.c_str(), b.w - 2 * P, kBold);
}

// ---- weather ---------------------------------------------------------------------------

// Hours (or the next days) in a row of cells: the label, an icon when there is room, the temperature.
void weatherCells(const GfxRenderer& r, const Data& d, const bool days, const int x, const int y, const int w, const int h) {
  const int n = days ? static_cast<int>(std::min<size_t>(d.days.size(), 3)) : static_cast<int>(std::min<size_t>(d.hours.size(), 4));
  if (n == 0 || h < 30) return;
  const int cw = w / n;
  const int capsH = r.getLineHeight(CAPS_FONT);
  const int valF = h >= 110 ? SMALL_FONT : CAPS_FONT;
  const int valH = r.getLineHeight(valF);
  for (int i = 0; i < n; i++) {
    const int cx = x + i * cw;
    if (i > 0) r.fillRect(cx, y + 4, 2, h - 8, true);
    char label[24], value[24];
    const char* icon;
    if (days) {
      snprintf(label, sizeof(label), "%s", d.days[i].d.c_str());
      snprintf(value, sizeof(value), "%d–%d°", d.days[i].min, d.days[i].max);
      icon = d.days[i].icon.c_str();
    } else {
      snprintf(label, sizeof(label), "%s", d.hours[i].h.c_str());
      snprintf(value, sizeof(value), "%d°", d.hours[i].t);
      icon = d.hours[i].icon.c_str();
    }
    const int is = std::min(std::min(h - capsH - valH - 8, cw - 16), 64);
    if (is >= 18) {
      drawFitCentered(r, CAPS_FONT, cx + cw / 2, y, label, cw - 8, kReg);
      drawIcon(r, icon, cx + cw / 2, y + capsH + (h - capsH - valH) / 2, is);
      drawFitCentered(r, valF, cx + cw / 2, y + h - valH, value, cw - 8, kBold);
    } else {
      const int top = y + (h - capsH - valH) / 2;
      drawFitCentered(r, CAPS_FONT, cx + cw / 2, top, label, cw - 8, kReg);
      drawFitCentered(r, valF, cx + cw / 2, top + capsH, value, cw - 8, kBold);
    }
  }
}

// The icon and the temperature as big as fit h (and maxW); returns the x after them.
int weatherNow(const GfxRenderer& r, const Data& d, const int x, const int y, const int h, const int maxW, const int maxIcon = 64) {
  char temp[16];
  snprintf(temp, sizeof(temp), "%d°", d.temp);
  const int s = std::min(h, maxIcon);
  int f = digitsFontOr0(r, temp, maxW - s - 8, h);
  if (f == 0) f = INK_30_FONT_ID;
  drawIcon(r, d.icon.c_str(), x + s / 2, y + h / 2, s);
  drawDigits(r, f, x + s + 8, y + (h - digitHeight(f)) / 2, temp);
  return x + s + 8 + r.getTextWidth(f, temp, kBold);
}

void drawWeather(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  if (!d.hasWeather) {
    centerText(r, SMALL_FONT, b.x + P, b.y + P, b.w - 2 * P, b.h - 2 * P, tr(STR_IK_NO_WEATHER));
    return;
  }
  char range[64];
  snprintf(range, sizeof(range), "%d–%d° · %d%%", d.tmin, d.tmax, d.rain);
  // Styles: now, hourly, days ("strip", an older name, is now).
  const bool days = it.style == "days" && !d.days.empty();
  const bool cells = days || (it.style == "hourly" && !d.hours.empty());
  const int capsH = r.getLineHeight(CAPS_FONT);
  const int right = b.x + b.w - P;
  if (b.h < H_LOW) {
    const int top = b.y + 8, ih = b.h - 16;
    if (cells && b.w < 300) {
      weatherCells(r, d, days, b.x + P, top, b.w - 2 * P, ih);
      return;
    }
    const int x = weatherNow(r, d, b.x + P, top, ih, b.w < 300 ? b.w - 2 * P : (b.w - 2 * P) * 40 / 100, 58) + 12;
    if (cells) {
      r.fillRect(x - 2, top + 4, 2, ih - 8, true);
      weatherCells(r, d, days, x + 6, top, right - x - 6, ih);
      return;
    }
    int rr = right;
    if (b.w >= 300 && !d.lunar.empty()) {
      // The lunar date in a box at the right.
      char l[48];
      snprintf(l, sizeof(l), tr(STR_IK_W_LUNAR_SHORT_CAPS_FMT), d.lunar.c_str());
      const int lw = std::max(r.getTextWidth(CAPS_FONT, l, kBold), r.getTextWidth(CAPS_FONT, d.lunarYear.c_str())) + 16;
      const int lh = capsH * 2 + 8;
      const int ly = b.y + (b.h - lh) / 2;
      r.drawRoundedRect(rr - lw, ly, lw, lh, 2, 8, true);
      drawCentered(r, CAPS_FONT, rr - lw / 2, ly + 4, l, kBold);
      drawCentered(r, CAPS_FONT, rr - lw / 2, ly + 4 + capsH, d.lunarYear.c_str());
      rr -= lw + 10;
    }
    const int w = rr - x;
    if (w >= 50) {
      const int mid = b.y + b.h / 2;
      drawFit(r, SMALL_FONT, x, mid - r.getLineHeight(SMALL_FONT), (d.desc.empty() ? d.city : d.desc).c_str(), w, kBold);
      drawFit(r, CAPS_FONT, x, mid + 2, range, w);
    }
    return;
  }
  if (b.w < 300) {
    if (cells) {
      // The current temperature and description on top, the hours (or days) under a rule.
      const int hh = digitHeight(INK_30_FONT_ID);
      const int x = weatherNow(r, d, b.x + P, b.y + P, hh, b.w - 2 * P, 44) + 10;
      if (right - x >= 40) {
        drawFit(r, SMALL_FONT, x, b.y + P + (hh - r.getLineHeight(SMALL_FONT)) / 2, (d.desc.empty() ? d.city : d.desc).c_str(),
                right - x, kBold);
      }
      const int top = b.y + P + hh + 8;
      r.fillRect(b.x + P, top, b.w - 2 * P, 2, true);
      weatherCells(r, d, days, b.x + P, top + 6, b.w - 2 * P, b.y + b.h - P - top - 6);
      return;
    }
    // Now: the city, then the icon and temperature as big as fit, the description and range at the bottom.
    caps(r, b.x + P, b.y + P, d.city.c_str(), true, b.w - 2 * P);
    const int bottomH = r.getLineHeight(SMALL_FONT) + capsH;
    const int midTop = b.y + P + capsH + 4;
    const int midH = b.y + b.h - P - bottomH - 6 - midTop;
    const int hh = std::min(midH, digitHeight(INK_60_FONT_ID));
    weatherNow(r, d, b.x + P, midTop + (midH - hh) / 2, hh, b.w - 2 * P, 72);
    const int by = b.y + b.h - P - bottomH;
    drawFit(r, SMALL_FONT, b.x + P, by, d.desc.c_str(), b.w - 2 * P, kBold);
    drawFit(r, CAPS_FONT, b.x + P, by + r.getLineHeight(SMALL_FONT), range, b.w - 2 * P);
    return;
  }
  if (cells) {
    // Wide: a compact head (icon, temperature, place and conditions over the range), then the hours or days.
    const int hh = std::max(digitHeight(INK_30_FONT_ID), r.getLineHeight(SMALL_FONT) + capsH);
    const int x = weatherNow(r, d, b.x + P, b.y + P, hh, (b.w - 2 * P) / 2, 52) + 16;
    const int tw = right - x;
    const int ty = b.y + P + (hh - r.getLineHeight(SMALL_FONT) - capsH) / 2;
    const std::string place = d.desc.empty() ? d.city : d.city.empty() ? d.desc : d.city + " · " + d.desc;
    drawFit(r, SMALL_FONT, x, ty, place.c_str(), tw, kBold);
    drawFit(r, CAPS_FONT, x, ty + r.getLineHeight(SMALL_FONT), range, tw);
    const int top = b.y + P + hh + 8;
    r.fillRect(b.x + P, top, b.w - 2 * P, 2, true);
    weatherCells(r, d, days, b.x + P, top + 6, b.w - 2 * P, b.y + b.h - P - top - 6);
    return;
  }
  // Wide, now: the icon and temperature big on the left, the city and conditions beside them; a tall box
  // adds low, high and rain in cells along the bottom.
  const int statsH = b.h >= H_HUGE ? capsH + digitHeight(INK_30_FONT_ID) + 18 : 0;
  const int topH = b.h - 2 * P - (statsH ? statsH + 10 : 0);
  const int hh = std::min(topH, digitHeight(INK_60_FONT_ID));
  const int x = weatherNow(r, d, b.x + P, b.y + P + (topH - hh) / 2, hh, (b.w - 2 * P) * 58 / 100, 96) + 18;
  const int w = right - x;
  char line[80];
  snprintf(line, sizeof(line), tr(STR_IK_WEATHER_RANGE_FMT), d.tmin, d.tmax, d.rain);
  int used = 0;
  const int rangeF = iu::wrapInFont(r, CAPS_FONT, w, capsH * 2, line, kReg, &used);
  const int textH = r.getLineHeight(BODY_FONT) + (d.desc.empty() ? 0 : r.getLineHeight(SMALL_FONT)) + (statsH ? 0 : used + 4);
  int ty = b.y + P + std::max(0, (topH - textH) / 2);
  drawFit(r, BODY_FONT, x, ty, d.city.c_str(), w, kBold);
  ty += r.getLineHeight(BODY_FONT);
  if (!d.desc.empty()) {
    drawFit(r, SMALL_FONT, x, ty, d.desc.c_str(), w);
    ty += r.getLineHeight(SMALL_FONT);
  }
  if (!statsH) {
    iu::wrapIn(r, rangeF, x, ty + 4, w, capsH * 2, line, true, kReg);
    return;
  }
  const int sy = b.y + b.h - P - statsH;
  r.fillRect(b.x + P, sy, b.w - 2 * P, 2, true);
  const int cw = (b.w - 2 * P) / 3;
  const char* labels[3] = {tr(STR_IK_W_LOW_CAPS), tr(STR_IK_W_HIGH_CAPS), tr(STR_IK_W_RAIN_CAPS)};
  char vals[3][12];
  snprintf(vals[0], sizeof(vals[0]), "%d°", d.tmin);
  snprintf(vals[1], sizeof(vals[1]), "%d°", d.tmax);
  snprintf(vals[2], sizeof(vals[2]), "%d%%", d.rain);
  for (int i = 0; i < 3; i++) {
    const int cx = b.x + P + i * cw;
    if (i > 0) r.fillRect(cx, sy + 8, 2, statsH - 10, true);
    drawCentered(r, CAPS_FONT, cx + cw / 2, sy + 8, labels[i], kBold);
    const int f = digitsFont(r, vals[i], cw - 12, digitHeight(INK_30_FONT_ID));
    drawDigits(r, f, cx + (cw - r.getTextWidth(f, vals[i], kBold)) / 2, sy + 10 + capsH, vals[i]);
  }
}

// ---- reading -------------------------------------------------------------------------

bool drawCoverAt(GfxRenderer& r, const std::string& path, const int x, const int y, const int w, const int h) {
  bool drawn = false;
  if (!path.empty()) {
    HalFile f;
    if (Storage.openFileForRead("WGT", path, f)) {
      Bitmap bmp(f);
      if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0) drawn = GUI.drawCoverThumbFill(r, bmp, Rect{x, y, w, h});
      f.close();
    }
  }
  if (!drawn) GUI.drawCoverPlaceholder(r, Rect{x, y, w, h});
  r.drawRect(x, y, w, h, 2, true);
  gImages.push_back({x, y, w, h});
  return drawn;
}

bool drawCover(GfxRenderer& r, const Data& d, const int x, const int y, const int w, const int h) {
  return drawCoverAt(r, d.coverPath, x, y, w, h);
}

// The cover height the reading widget draws (its thumbnail is made at this height); 0 = no cover.
int readingCoverH(const Item& it, const Box& b) {
  if (it.style == "text") return 0;
  if (b.w < 300) return b.h - 2 * P - 22;
  const int maxW = (b.w - 2 * P - 10) * (b.h >= H_HUGE ? 50 : 36) / 100;
  return std::min(b.h - 2 * P, maxW * 3 / 2);
}

void drawReading(GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  const bool cover = it.style != "text";
  char line[80];
  if (b.w < 300 && cover) {
    // 2x2: cover and progress.
    const int ch = readingCoverH(it, b);
    const int cw = ch * 2 / 3;
    drawCover(r, d, b.x + (b.w - cw) / 2, b.y + P, cw, ch);
    if (d.bookPercent >= 0) {
      bar(r, b.x + P, b.y + b.h - P - 12, b.w - 2 * P - 46, 12, d.bookPercent, true);
      snprintf(line, sizeof(line), "%d%%", d.bookPercent);
      drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + b.h - P - 16, line, kBold);
    }
    return;
  }
  const bool big = b.h >= H_BIG, huge = b.h >= H_HUGE;
  int tx = b.x + P + 10;
  if (cover) {
    const int ch = readingCoverH(it, b);
    const int cw = ch * 2 / 3;
    drawCover(r, d, tx, b.y + (b.h - ch) / 2, cw, ch);
    tx += cw + 16;
  }
  const int tw = b.x + b.w - P - tx;
  int ty = b.y + P + 2;
  caps(r, tx, ty, tr(STR_IK_READING));
  ty += r.getLineHeight(CAPS_FONT) + 2;
  if (!d.hasBook) {
    iu::wrap(r, NOTOSANS_14_FONT_ID, tx, ty, tw, tr(STR_IK_NO_BOOK), 2, true, kBold, 0);
    return;
  }
  // Bottom block: the percentage in big digits (tall boxes), the bar, today's minutes.
  char pct[12];
  snprintf(pct, sizeof(pct), "%d%%", std::max(0, d.bookPercent));
  const int pctF = huge ? INK_44_FONT_ID : INK_30_FONT_ID;
  const bool showPct = big && d.bookPercent >= 0;
  const bool statsLine = d.stats.dated;
  const int lineH = r.getLineHeight(CAPS_FONT);
  const int blockH = (showPct ? digitHeight(pctF) + 8 : 0) + (d.bookPercent >= 0 ? 18 : 0) + (statsLine ? lineH : 0) +
                     (!showPct && d.bookPercent >= 0 ? lineH : 0);
  const int by = b.y + b.h - P - blockH;
  // Title wrapped into the room above (the author goes when the title needs that line too).
  const int titleF = huge ? (cover ? INK_24_FONT_ID : INK_SERIF_36_FONT_ID) : big ? NOTOSANS_18_FONT_ID : NOTOSANS_16_FONT_ID;
  const int authorF = big ? BODY_FONT : SMALL_FONT;
  const int room = by - 6 - ty;
  int authorH = d.bookAuthor.empty() ? 0 : r.getLineHeight(authorF) + 2;
  int used = 0;
  iu::wrapInFont(r, titleF, tw, room - authorH, d.bookTitle.c_str(), kBold, &used);
  if (used > room - authorH) authorH = 0;
  // In a tall box the title and author sit in the middle of their room.
  if (big) ty += std::max(0, (room - authorH - used) / 2);
  ty += iu::wrapIn(r, titleF, tx, ty, tw, room - authorH, d.bookTitle.c_str(), true, kBold);
  if (authorH > 0) drawFit(r, authorF, tx, ty, d.bookAuthor.c_str(), tw);
  int y = by;
  if (showPct) {
    drawDigits(r, pctF, tx, y, pct);
    y += digitHeight(pctF) + 8;
  }
  if (d.bookPercent >= 0) {
    bar(r, tx, y, tw, 12, d.bookPercent, true);
    y += 18;
    if (!showPct) {
      snprintf(line, sizeof(line), tr(STR_IK_PROGRESS_FMT), d.bookPercent);
      r.drawText(CAPS_FONT, tx, y - 2, line, true, kBold);
      y += lineH;
    }
  }
  if (statsLine) {
    const int n = snprintf(line, sizeof(line), tr(STR_IK_TODAY_READ_FMT), static_cast<int>(d.stats.todaySec / 60),
                           d.stats.goalMin);
    if (d.stats.streak > 0 && n > 0 && n < static_cast<int>(sizeof(line))) {
      snprintf(line + n, sizeof(line) - n, tr(STR_IK_STREAK_SUFFIX_FMT), d.stats.streak);
    }
    drawFit(r, CAPS_FONT, tx, y - 2, line, tw, kBold);
  }
}

// A thick ring filled clockwise to `pct`.
void ring(const GfxRenderer& r, const int cx, const int cy, const int rad, const int thick, const int pct) {
  constexpr double kPi = 3.14159265358979;
  strokeCircle(r, cx, cy, rad, 2);
  const int steps = 90 * std::min(100, std::max(0, pct)) / 100;
  for (int i = 0; i < steps; i++) {
    const double a0 = i * 2 * kPi / 90, a1 = (i + 1) * 2 * kPi / 90;
    const int rm = rad - thick / 2;
    r.drawLine(cx + static_cast<int>(rm * std::sin(a0)), cy - static_cast<int>(rm * std::cos(a0)),
               cx + static_cast<int>(rm * std::sin(a1)), cy - static_cast<int>(rm * std::cos(a1)), thick, true);
  }
  strokeCircle(r, cx, cy, rad - thick, 2);
}

void drawStats(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  const auto& s = d.stats;
  char buf[48];
  if (!s.dated) {
    centerText(r, SMALL_FONT, b.x + P, b.y + P, b.w - 2 * P, b.h - 2 * P, tr(STR_IK_STATS_SUB_NOCLOCK));
    return;
  }
  const int mins = static_cast<int>(s.todaySec / 60);
  const int capsH = r.getLineHeight(CAPS_FONT);
  uint32_t weekSec = 0;
  for (const uint32_t v : s.last7) weekSec += v;
  if (it.style == "week") {
    // The last 7 days as bars (filled when the goal was met), the week's minutes in the title line.
    caps(r, b.x + P + 6, b.y + P, tr(STR_IK_STATS_WEEK_CAPS));
    snprintf(buf, sizeof(buf), tr(STR_IK_W_MIN_CAPS_FMT), static_cast<int>(weekSec / 60));
    drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, buf, kBold);
    char names[64];
    snprintf(names, sizeof(names), "%s", tr(STR_IK_WDAYS_SHORT));
    const char* wd[7] = {};
    char* save = nullptr;
    int i = 0;
    for (char* t = strtok_r(names, ",", &save); t && i < 7; t = strtok_r(nullptr, ",", &save)) wd[i++] = t;
    const uint32_t goal = static_cast<uint32_t>(s.goalMin) * 60;
    uint32_t top = goal;
    for (const uint32_t v : s.last7) top = std::max(top, v);
    top += top / 8;
    const int base = b.y + b.h - P - capsH - 4;
    const int barsH = base - (b.y + P + capsH + 8);
    const int gap = b.w >= 300 ? 8 : 5;
    const int bw = (b.w - 2 * P - 12 - gap * 6) / 7;
    // The goal as a dashed line across the bars.
    const int gy = base - static_cast<int>(static_cast<uint64_t>(goal) * barsH / std::max<uint32_t>(1, top));
    for (int gx = b.x + P + 6; gx < b.x + b.w - P - 6; gx += 10) r.fillRect(gx, gy, 5, 1, true);
    // One size for every day label: the caps font, or the small one when a label does not fit its bar.
    int labF = CAPS_FONT;
    for (int k = 0; k < 7; k++) {
      if (wd[k] && r.getTextWidth(CAPS_FONT, wd[k], kBold) > bw + gap - 2) labF = SMALL_FONT_ID;
    }
    for (int k = 0; k < 7; k++) {
      const int bx = b.x + P + 6 + k * (bw + gap);
      const uint32_t v = s.last7[k];
      const int h = v > 0 ? std::max(3, static_cast<int>(static_cast<uint64_t>(v) * barsH / top)) : 0;
      if (h > 0) {
        if (v >= goal) {
          r.fillRect(bx, base - h, bw, h, true);
        } else {
          r.drawRect(bx, base - h, bw, h, 2, true);
        }
      }
      r.fillRect(bx, base, bw, 2, true);
      const int wday = (s.todayWday - 6 + k + 14) % 7;
      drawCentered(r, labF, bx + bw / 2, base + 3 + (r.getLineHeight(CAPS_FONT) - r.getLineHeight(labF)) / 2, wd[wday] ? wd[wday] : "",
                   k == 6 ? kBold : kReg);
    }
    return;
  }
  // Ring: today's minutes against the goal.
  const int pct = s.goalMin > 0 ? mins * 100 / s.goalMin : 0;
  if (b.w >= 300) {
    // Wide: the ring on the left, today, this week and the streak beside it.
    const int rad = (b.h - 2 * P) / 2;
    const int cx = b.x + P + 6 + rad, cy = b.y + b.h / 2;
    ring(r, cx, cy, rad, std::max(8, rad / 4), pct);
    snprintf(buf, sizeof(buf), "%d%%", std::min(999, pct));
    const int pf = rad >= 60 ? NOTOSANS_18_FONT_ID : SMALL_FONT;
    drawCentered(r, pf, cx, cy - r.getLineHeight(pf) / 2, buf, kBold);
    const int x = cx + rad + 22;
    const int w = b.x + b.w - P - x;
    const int vf = NOTOSANS_16_FONT_ID;
    const int lineH = capsH + r.getLineHeight(vf) + 6;
    const bool streak = s.streak > 0 && b.h - 2 * P >= lineH * 2 + capsH;
    const int total = lineH * 2 + (streak ? capsH : 0);
    int y = b.y + (b.h - total) / 2;
    caps(r, x, y, tr(STR_IK_STATS_TODAY_CAPS));
    snprintf(buf, sizeof(buf), "%d %s", mins, "");
    char of[32];
    snprintf(of, sizeof(of), tr(STR_IK_STATS_OF_GOAL_FMT), s.goalMin);
    snprintf(buf, sizeof(buf), "%d %s", mins, of);
    drawFit(r, vf, x, y + capsH, buf, w, kBold);
    y += lineH;
    caps(r, x, y, tr(STR_IK_STATS_THIS_WEEK_CAPS));
    const int wm = static_cast<int>(weekSec / 60);
    if (wm >= 60) {
      snprintf(buf, sizeof(buf), tr(STR_IK_DUR_HM_FMT), wm / 60, wm % 60);
    } else {
      snprintf(buf, sizeof(buf), tr(STR_IK_DUR_M_FMT), wm);
    }
    drawFit(r, vf, x, y + capsH, buf, w, kBold);
    y += lineH;
    if (streak) {
      snprintf(buf, sizeof(buf), tr(STR_IK_DASH_STREAK_FMT), s.streak);
      drawFit(r, CAPS_FONT, x, y, buf, w, kBold);
    }
    return;
  }
  const int rad = std::min(b.w - 2 * P, b.h - 2 * P - capsH - 6) / 2;
  const int cx = b.x + b.w / 2;
  const int cy = b.y + P + rad;
  ring(r, cx, cy, rad, std::max(8, rad / 4), pct);
  snprintf(buf, sizeof(buf), "%d", mins);
  const int f = rad >= 60 ? INK_30_FONT_ID : NOTOSANS_18_FONT_ID;
  if (f == INK_30_FONT_ID) {
    drawDigits(r, f, cx - r.getTextWidth(f, buf, kBold) / 2, cy - digitHeight(f) / 2, buf);
  } else {
    drawCentered(r, f, cx, cy - r.getLineHeight(f) / 2, buf, kBold);
  }
  snprintf(buf, sizeof(buf), tr(STR_IK_W_STATS_UNIT_FMT), s.goalMin);
  std::string line = buf;
  if (s.streak > 0) {
    char st[24];
    snprintf(st, sizeof(st), tr(STR_IK_DASH_STREAK_FMT), s.streak);
    line += " · ";
    line += st;
  }
  drawFitCentered(r, CAPS_FONT, cx, b.y + b.h - P - capsH, line.c_str(), b.w - 2 * P, kBold);
}

void drawNews(const GfxRenderer& r, const Item&, const Box& b, const Data& d) {
  char buf[64];
  if (d.newsDay.empty()) {
    snprintf(buf, sizeof(buf), "%s", tr(STR_IK_W_NEWS_CAPS));
  } else {
    snprintf(buf, sizeof(buf), tr(STR_IK_W_NEWS_CAPS_FMT), d.newsDay.c_str());
  }
  caps(r, b.x + P + 6, b.y + P, buf);
  if (d.newsCount > 0) {
    snprintf(buf, sizeof(buf), tr(STR_IK_W_NEWS_COUNT_FMT), d.newsCount);
    drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, buf, kBold);
  }
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 12;
  int y = b.y + P + r.getLineHeight(CAPS_FONT) + 6;
  if (d.newsTop.empty() && d.newsPath.empty()) {
    centerText(r, BODY_FONT, x, y, w, b.y + b.h - P - y, tr(STR_IK_NEWS_NONE), kReg);
    return;
  }
  if (d.newsTop.empty()) {
    centerText(r, b.h >= H_BIG ? NOTOSANS_18_FONT_ID : NOTOSANS_16_FONT_ID, x, y, w, b.y + b.h - P - y, tr(STR_IK_W_NEWS_READY));
    return;
  }
  // The first headline as big as the box allows (three lines at most), then the others under rules.
  const int maxY = b.y + b.h - P / 2;
  const int headF = b.h >= H_HUGE ? INK_24_FONT_ID : b.h >= H_BIG ? NOTOSANS_18_FONT_ID : NOTOSANS_16_FONT_ID;
  const int subF = b.h >= H_HUGE ? BODY_FONT : SMALL_FONT;
  const int others = static_cast<int>(d.newsTop.size()) - 1;
  const int headRoom = std::min(r.getLineHeight(headF) * 3, others > 0 ? (maxY - y) * 3 / 5 : maxY - y);
  y += iu::wrapIn(r, headF, x, y, w, headRoom, d.newsTop[0].c_str(), true, kBold) + 6;
  for (size_t i = 1; i < d.newsTop.size(); i++) {
    const int lh = r.getLineHeight(subF);
    if (y + 6 + lh > maxY) break;
    r.fillRect(x, y, w, 2, true);
    y += 6;
    y += iu::wrapIn(r, subF, x, y, w, std::min(lh * 2, maxY - y), d.newsTop[i].c_str(), true, kReg) + 4;
  }
}

void drawNotify(const GfxRenderer& r, const Item&, const Box& b, const Data& d) {
  char cap[48];
  snprintf(cap, sizeof(cap), "%s · %d", tr(STR_ID_NOTES_CAPS), static_cast<int>(d.notes.size()));
  caps(r, b.x + P + 6, b.y + P, d.notes.empty() ? tr(STR_ID_NOTES_CAPS) : cap);
  int y = b.y + P + r.getLineHeight(CAPS_FONT) + 4;
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 12;
  if (d.notes.empty()) {
    const int h = b.y + b.h - P / 2 - y;
    const int hintH = r.getLineHeight(CAPS_FONT);
    if (h >= r.getLineHeight(NOTOSANS_16_FONT_ID) + hintH + 8) {
      const int used = r.getLineHeight(NOTOSANS_16_FONT_ID);
      const int top = y + (h - used - hintH - 4) / 2;
      drawFitCentered(r, NOTOSANS_16_FONT_ID, b.x + b.w / 2, top, tr(STR_IK_W_NO_NOTES), w, kBold);
      drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, top + used + 4, tr(STR_IK_W_NOTES_HINT), w);
    } else {
      centerText(r, SMALL_FONT, x, y, w, h, tr(STR_IK_W_NO_NOTES));
    }
    return;
  }
  // One line per note (bigger when few fit); the ones that do not fit are counted on a last "+n" line.
  const int noteF = (b.y + b.h - P / 2 - y) / static_cast<int>(d.notes.size()) >= r.getLineHeight(BODY_FONT) + 8 ? BODY_FONT : SMALL_FONT;
  const int lh = r.getLineHeight(noteF) + 4;
  const int moreH = r.getLineHeight(CAPS_FONT) + 2;
  const int bottom = b.y + b.h - P / 2;
  const int n = static_cast<int>(d.notes.size());
  int shown = 0;
  for (int i = 0; i < n; i++) {
    const int reserve = i < n - 1 ? moreH : 0;
    if (shown > 0 && y + lh + reserve > bottom) break;
    const auto& note = d.notes[i];
    const int tw = note.time.empty() ? 0 : r.getTextWidth(CAPS_FONT, note.time.c_str(), kBold) + 10;
    drawFit(r, noteF, x, y, note.title.c_str(), w - tw, kBold);
    if (tw) drawRight(r, CAPS_FONT, x + w, y + (r.getLineHeight(noteF) - r.getLineHeight(CAPS_FONT)) / 2, note.time.c_str(), kBold);
    y += lh;
    shown++;
  }
  if (shown < n) {
    char more[48];
    snprintf(more, sizeof(more), tr(STR_IK_W_MORE_NOTES_FMT), n - shown);
    drawFit(r, CAPS_FONT, x, std::min(y, bottom - moreH), more, w, kBold);
  }
}

void drawBattery(const GfxRenderer& r, const Item&, const Box& b, const Data& d) {
  // The battery and its percentage as big as fit, the last sync time under them.
  char buf[32];
  snprintf(buf, sizeof(buf), "%d%%", d.battery);
  const int capsH = r.getLineHeight(CAPS_FONT);
  const bool sync = d.lastSyncMs > 0;
  const int bw = 52, bh = 26;
  const int room = b.h - 16 - (sync ? capsH + 2 : 0);
  const int f = digitsFontOr0(r, buf, b.w - 2 * P - bw - 20, room);
  const int rowH = f ? digitHeight(f) : r.getLineHeight(NOTOSANS_18_FONT_ID);
  const int top = b.y + (b.h - rowH - (sync ? capsH + 2 : 0)) / 2;
  const int x = b.x + P;
  const int by = top + (rowH - bh) / 2;
  r.drawRect(x, by, bw, bh, 2, true);
  r.fillRect(x + bw, by + 8, 4, bh - 16, true);
  const int fill = (bw - 6) * std::min(100, std::max(0, d.battery)) / 100;
  if (fill > 0) r.fillRect(x + 3, by + 3, fill, bh - 6, true);
  if (f) {
    drawDigits(r, f, x + bw + 14, top, buf);
  } else {
    r.drawText(NOTOSANS_18_FONT_ID, x + bw + 14, top, buf, true, kBold);
  }
  if (sync) {
    char clock[8];
    INKDESK.formatClock(d.lastSyncMs, clock, sizeof(clock));
    snprintf(buf, sizeof(buf), tr(STR_IK_W_SYNC_AT_FMT), clock);
    drawFit(r, CAPS_FONT, x, top + rowH + 2, buf, b.w - 2 * P);
  }
}

struct AppInfo {
  const char* style;
  StrId label;
  const uint8_t* icon;
  Action action;
};
const AppInfo APPS[] = {
    {"library", StrId::STR_LIBRARY, LibraryIcon, Action::Library},
    {"news", StrId::STR_IK_NEWS, icon_file_text_32_bits, Action::News},
    {"stats", StrId::STR_IK_W_APP_STATS, icon_book_open_32_bits, Action::Stats},
    {"sync", StrId::STR_IK_TILE_SYNC, WifiIcon, Action::Sync},
    {"transfer", StrId::STR_IK_W_APP_TRANSFER, TransferIcon, Action::Transfer},
    {"apps", StrId::STR_ID_APPS, BlocksIcon, Action::Apps},
    {"settings", StrId::STR_SETTINGS_TITLE, Settings2Icon, Action::Settings},
    {"update", StrId::STR_IK_W_APP_UPDATE, icon_download_32_bits, Action::Update},
};
const AppInfo* findApp(const std::string& style) {
  for (const auto& a : APPS) {
    if (style == a.style) return &a;
  }
  return nullptr;
}
const AppInfo* appOf(const std::string& style) {
  const AppInfo* a = findApp(style);
  return a ? a : findApp("apps");
}

void drawApp(const GfxRenderer& r, const Item& it, const Box& b, const Data&, const bool focused) {
  const AppInfo* a = appOf(it.style);
  if (focused) {
    if (gSkin == Skin::Bold || gSkin == Skin::Line) {
      r.fillRect(b.x, b.y, b.w, b.h, true);
    } else {
      r.fillRoundedRect(b.x, b.y, b.w, b.h, RADIUS, Color::Black);
    }
    r.fillRoundedRect(b.x + (b.w - 40) / 2, b.y + 10, 40, 40, 8, Color::White);
  } else if (gSkin == Skin::Bold) {
    r.drawRect(b.x, b.y, b.w, b.h, 3, true);
  } else if (gSkin == Skin::Card || gSkin == Skin::Shadow) {
    r.drawRoundedRect(b.x, b.y, b.w, b.h, 2, RADIUS, true);
  }
  r.drawIcon(a->icon, b.x + (b.w - 32) / 2, b.y + 14, 32);
  const char* label = I18N.get(a->label);
  drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, b.y + b.h - r.getLineHeight(CAPS_FONT) - 8, label, b.w - 8, kBold, !focused);
}

// ---- markets ---------------------------------------------------------------------------

void changeText(const GfxRenderer& r, const int x, const int y, const double change, const char* text,
                const int font = CAPS_FONT) {
  const int s = r.getLineHeight(font) / 2;
  triangle(r, x, y + (r.getLineHeight(font) - s) / 2, s, change >= 0);
  r.drawText(font, x + s + 5, y, text, true, kBold);
}

// Font and row height so `n` rows fit `avail` px: the body font when they do, smaller when not
// (every chosen entry is shown).
struct Rows {
  int font;
  int h;
};
Rows rowsFor(const GfxRenderer& r, const int n, const int avail) {
  // The biggest font whose rows fit; the rows then share the height (up to twice a line each).
  for (const int f : {NOTOSANS_18_FONT_ID, BODY_FONT, NOTOSANS_12_FONT_ID, UI_10_FONT_ID, SMALL_FONT_ID}) {
    const int h = r.getLineHeight(f) + 4;
    if (n * h <= avail) return {f, std::min(h * 2, avail / std::max(1, n))};
  }
  return {SMALL_FONT_ID, std::max(1, avail / std::max(1, n))};
}

// Caps title on the left, the data time on the right; the time goes when both do not fit.
void marketHeader(const GfxRenderer& r, const Box& b, const char* title, const std::string& at) {
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 6;
  int aw = at.empty() ? 0 : r.getTextWidth(CAPS_FONT, at.c_str()) + 10;
  if (aw > 0 && r.getTextWidth(CAPS_FONT, title, kBold) + aw > w) aw = 0;
  if (aw > 0) drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, at.c_str());
  drawFit(r, CAPS_FONT, x, b.y + P, title, w - aw, kBold);
}

// A narrow list: name on the left, value on the right, a rise/fall mark before the name. Every row
// is shown: the font is the biggest in which all rows fit the height and the width.
struct PairRow {
  std::string left, right;
  int sign;  // 1 up, -1 down, 0 none
};
void pairList(const GfxRenderer& r, const int x, int y, const int w, const int avail, const std::vector<PairRow>& rows) {
  const int n = static_cast<int>(rows.size());
  int font = SMALL_FONT_ID;
  for (const int f : {NOTOSANS_18_FONT_ID, BODY_FONT, NOTOSANS_12_FONT_ID, UI_10_FONT_ID, SMALL_FONT_ID}) {
    int widest = 0;
    for (const auto& row : rows) {
      const int tri = row.sign != 0 ? std::max(8, r.getLineHeight(f) / 3) + 6 : 0;
      widest = std::max(widest, tri + r.getTextWidth(f, row.left.c_str(), kBold) + 10 +
                                    r.getTextWidth(f, row.right.c_str(), kBold));
    }
    font = f;
    if (widest <= w && n * (r.getLineHeight(f) + 4) <= avail) break;
  }
  const int lh = r.getLineHeight(font);
  const int step = std::min(lh * 2, std::max(lh + 4, avail / std::max(1, n)));
  y += std::max(0, (step - lh - 4) / 2);
  for (const auto& row : rows) {
    int lx = x;
    if (row.sign != 0) {
      // Centred on the digits, which sit in the lower part of the line.
      const int ts = std::max(8, lh / 3);
      triangle(r, x, y + r.getFontAscenderSize(font) * 2 / 3 - ts / 2, ts, row.sign > 0);
      lx += ts + 6;
    }
    r.drawText(font, lx, y, row.left.c_str(), true, kBold);
    drawRight(r, font, x + w, y, row.right.c_str(), kBold);
    y += step;
  }
}

void noData(const GfxRenderer& r, const Box& b, const int y) {
  iu::wrap(r, SMALL_FONT, b.x + P + 6, y, b.w - 2 * P - 12, tr(STR_IK_W_NO_DATA), 2);
}

template <typename T>
bool anyData(const std::vector<Pick<T>>& list) {
  for (const auto& p : list) {
    if (p.v) return true;
  }
  return false;
}

int signOf(const double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

constexpr const char* kDash = "—";  // a chosen code the server has not sent yet

void drawGold(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  char buf[64];
  if (b.w >= 300 && b.h < H_LOW) {
    // 4x1: a cell per quote, the name over the sell price as big as fits (no title line).
    const auto list = chosen(d.gold, codes(it.opt, "SJC,N24K"), [](const Data::Quote& x) { return x.code; });
    if (!anyData(list)) {
      snprintf(buf, sizeof(buf), tr(STR_IK_W_GOLD_CAPS_FMT), d.goldSrc.empty() ? "PNJ" : d.goldSrc.c_str());
      marketHeader(r, b, buf, d.goldAt);
      return noData(r, b, b.y + P + r.getLineHeight(CAPS_FONT) + 4);
    }
    const int x = b.x + P + 6;
    const int w = b.w - 2 * P - 12;
    const int n = static_cast<int>(list.size());
    const int cw = w / n;
    const int capsH = r.getLineHeight(CAPS_FONT);
    int vf = NOTOSANS_12_FONT_ID;
    for (const int f : {NOTOSANS_18_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID}) {
      if (capsH + r.getLineHeight(f) > b.h - 10) continue;
      bool fits = true;
      for (const auto& p : list) fits = fits && r.getTextWidth(f, p.v ? num(p.v->sell, 1).c_str() : kDash, kBold) + 22 <= cw - 12;
      if (fits) {
        vf = f;
        break;
      }
    }
    const int top = b.y + (b.h - capsH - r.getLineHeight(vf)) / 2;
    for (int i = 0; i < n; i++) {
      const auto& p = list[i];
      const int cx = x + i * cw;
      if (i > 0) r.fillRect(cx - 8, b.y + 10, 2, b.h - 20, true);
      drawFit(r, CAPS_FONT, cx, top, p.v ? p.v->name.c_str() : p.code.c_str(), cw - 12);
      const std::string sell = p.v ? num(p.v->sell, 1) : kDash;
      r.drawText(vf, cx, top + capsH, sell.c_str(), true, kBold);
      if (p.v && p.v->change != 0) {
        const int ts = std::max(8, r.getLineHeight(vf) / 3);
        triangle(r, cx + r.getTextWidth(vf, sell.c_str(), kBold) + 8, top + capsH + r.getFontAscenderSize(vf) * 2 / 3 - ts / 2, ts,
                 p.v->change >= 0);
      }
    }
    return;
  }
  snprintf(buf, sizeof(buf), tr(STR_IK_W_GOLD_CAPS_FMT), d.goldSrc.empty() ? "PNJ" : d.goldSrc.c_str());
  marketHeader(r, b, buf, d.goldAt);
  const int y0 = b.y + P + r.getLineHeight(CAPS_FONT) + 4;
  const auto list = chosen(d.gold, codes(it.opt, "SJC,N24K"), [](const Data::Quote& x) { return x.code; });
  if (!anyData(list)) return noData(r, b, y0);
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 12;
  auto nameOf = [](const Pick<Data::Quote>& p) { return p.v ? p.v->name : p.code; };
  if (b.w < 300 && list.size() == 1) {
    // 2x2 with one quote: sell price big, buy and change under it.
    const auto& q = *list.front().v;
    drawFit(r, SMALL_FONT, x, y0, q.name.c_str(), w, kBold);
    const std::string sell = num(q.sell, 1);
    const int dy = y0 + r.getLineHeight(SMALL_FONT) + 2;
    int y = dy + heroNumber(r, sell.c_str(), x, dy, w, b.h / 3) + 6;
    snprintf(buf, sizeof(buf), "%s %s · %s", tr(STR_IK_W_BUY), num(q.buy, 1).c_str(), tr(STR_IK_W_GOLD_UNIT));
    drawFit(r, CAPS_FONT, x, y, buf, w);
    y += r.getLineHeight(CAPS_FONT);
    if (q.change != 0 && y + r.getLineHeight(CAPS_FONT) <= b.y + b.h - P / 2) {
      changeText(r, x, y, q.change, num(q.change, 1).c_str());
    }
    return;
  }
  if (b.w < 300) {
    // 2x2 with several: name and sell price per row.
    std::vector<PairRow> rows;
    for (const auto& p : list) rows.push_back({nameOf(p), p.v ? num(p.v->sell, 1) : kDash, p.v ? signOf(p.v->change) : 0});
    pairList(r, x, y0 + 2, w, b.y + b.h - P / 2 - y0, rows);
    return;
  }
  if (b.h < 140) {
    // 4x1: a cell per quote: name and sell price on one line, or stacked when that does not fit.
    const int n = static_cast<int>(list.size());
    const int cw = w / n;
    int nameW = 0, valW = 0;
    for (const auto& p : list) {
      nameW = std::max(nameW, r.getTextWidth(CAPS_FONT, nameOf(p).c_str()));
      valW = std::max(valW, r.getTextWidth(BODY_FONT, p.v ? num(p.v->sell, 1).c_str() : kDash, kBold));
    }
    const bool oneLine = nameW + 8 + valW <= cw - 12;
    const int bottom = b.y + b.h - P / 2;
    for (int i = 0; i < n; i++) {
      const auto& p = list[i];
      const int cx = x + i * cw;
      if (i > 0) r.fillRect(cx - 6, y0 + 2, 2, bottom - y0 - 4, true);
      const std::string sell = p.v ? num(p.v->sell, 1) : kDash;
      if (oneLine) {
        // Baselines lined up: Noto Sans 14 rises 32 px above its baseline, Ubuntu 10 20 px.
        const int vy = y0 + (bottom - y0 - r.getLineHeight(BODY_FONT)) / 2 + 2;
        const int base = vy + r.getFontAscenderSize(BODY_FONT);
        r.drawText(CAPS_FONT, cx, base - r.getFontAscenderSize(CAPS_FONT), nameOf(p).c_str());
        drawRight(r, BODY_FONT, cx + cw - 12, vy, sell.c_str(), kBold);
      } else {
        drawFit(r, CAPS_FONT, cx, y0, nameOf(p).c_str(), cw - 12);
        drawFit(r, NOTOSANS_12_FONT_ID, cx, y0 + r.getLineHeight(CAPS_FONT) - 6, sell.c_str(), cw - 12, kBold);
      }
    }
    return;
  }
  // 4x2 and up: one row per quote: name, buy, sell, change.
  int y = y0 + r.getLineHeight(CAPS_FONT);
  const Rows rows = rowsFor(r, static_cast<int>(list.size()), b.y + b.h - P / 2 - y);
  // Columns after the widest name and price (big rows would run together).
  int nameW = 0, buyW = 0, sellW = 0;
  for (const auto& p : list) {
    nameW = std::max(nameW, r.getTextWidth(rows.font, nameOf(p).c_str(), kBold));
    if (p.v) {
      buyW = std::max(buyW, r.getTextWidth(rows.font, num(p.v->buy, 1).c_str()));
      sellW = std::max(sellW, r.getTextWidth(rows.font, num(p.v->sell, 1).c_str(), kBold));
    }
  }
  const int colBuy = std::max(x + w * 42 / 100, x + nameW + 14);
  const int colSell = std::max(x + w * 64 / 100, colBuy + buyW + 18);
  const int colChg = std::max(x + w * 86 / 100, colSell + sellW + 14);
  r.drawText(CAPS_FONT, colBuy, y0, tr(STR_IK_W_BUY), true, kBold);
  r.drawText(CAPS_FONT, colSell, y0, tr(STR_IK_W_SELL), true, kBold);
  for (const auto& p : list) {
    drawFit(r, rows.font, x, y, nameOf(p).c_str(), colBuy - x - 8, kBold);
    if (p.v) {
      const auto& g = *p.v;
      r.drawText(rows.font, colBuy, y, num(g.buy, 1).c_str());
      r.drawText(rows.font, colSell, y, num(g.sell, 1).c_str(), true, kBold);
      if (g.change != 0) changeText(r, colChg, y + (r.getLineHeight(rows.font) - r.getLineHeight(CAPS_FONT)) / 2, g.change, num(g.change, 1).c_str());
    } else {
      r.drawText(rows.font, colBuy, y, kDash);
      r.drawText(rows.font, colSell, y, kDash, true, kBold);
    }
    y += rows.h;
  }
}

void drawStocks(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  marketHeader(r, b, tr(STR_IK_W_STOCKS_CAPS), d.stocksAt);
  const int y0 = b.y + P + r.getLineHeight(CAPS_FONT) + 4;
  const auto list = chosen(d.stocks, codes(it.opt, "VNINDEX,VN30,HNX"), [](const Data::Ticker& x) { return x.sym; });
  if (!anyData(list)) return noData(r, b, y0);
  char buf[48];
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 12;
  if (b.w < 300 && list.size() == 1) {
    const auto& t = *list.front().v;
    drawFit(r, SMALL_FONT, x, y0, t.sym.c_str(), w, kBold);
    const std::string price = num(t.price, 2);
    const int dy = y0 + r.getLineHeight(SMALL_FONT) + 2;
    const int hh = heroNumber(r, price.c_str(), x, dy, w, b.h / 3);
    snprintf(buf, sizeof(buf), "%s (%s%%)", num(t.change, 2).c_str(), num(t.pct, 2).c_str());
    changeText(r, x, dy + hh + 8, t.change, buf);
    return;
  }
  if (b.w < 300) {
    std::vector<PairRow> rows;
    for (const auto& p : list) rows.push_back({p.code.empty() ? p.v->sym : p.code, p.v ? num(p.v->price, 2) : kDash, p.v ? signOf(p.v->change) : 0});
    pairList(r, x, y0 + 2, w, b.y + b.h - P / 2 - y0, rows);
    return;
  }
  const int colPrice = x + w * 34 / 100;
  int y = y0;
  const Rows rows = rowsFor(r, static_cast<int>(list.size()), b.y + b.h - P / 2 - y);
  // The change column starts after the widest price (big rows would run into it).
  int priceW = 0;
  for (const auto& p : list) {
    if (p.v) priceW = std::max(priceW, r.getTextWidth(rows.font, num(p.v->price, 2).c_str(), kBold));
  }
  const int colChg = std::max(x + w * 66 / 100, colPrice + priceW + 14);
  for (const auto& p : list) {
    drawFit(r, rows.font, x, y, (p.code.empty() ? p.v->sym : p.code).c_str(), colPrice - x - 8, kBold);
    if (p.v) {
      const auto& t = *p.v;
      r.drawText(rows.font, colPrice, y, num(t.price, 2).c_str(), true, kBold);
      snprintf(buf, sizeof(buf), "%s%%", num(t.pct, 2).c_str());
      changeText(r, colChg, y + (r.getLineHeight(rows.font) - r.getLineHeight(CAPS_FONT)) / 2, t.change, buf, CAPS_FONT);
    } else {
      r.drawText(rows.font, colPrice, y, kDash, true, kBold);
    }
    y += rows.h;
  }
}

void drawFx(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  marketHeader(r, b, b.w < 300 ? tr(STR_IK_W_FX_SHORT) : tr(STR_IK_W_FX_CAPS), "");
  const int y0 = b.y + P + r.getLineHeight(CAPS_FONT) + 4;
  const auto list = chosen(d.fx, codes(it.opt, "USD,EUR,JPY,CNY"), [](const Data::Rate& x) { return x.code; });
  if (!anyData(list)) return noData(r, b, y0);
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 12;
  auto codeOf = [](const Pick<Data::Rate>& p) { return p.code.empty() ? p.v->code : p.code; };
  auto fmt = [](const double v) { return num(v, v < 1000 ? 2 : 0); };
  if (b.w < 300) {
    std::vector<PairRow> rows;
    for (const auto& p : list) rows.push_back({codeOf(p), p.v ? fmt(p.v->sell) : kDash, 0});
    pairList(r, x, y0 + 2, w, b.y + b.h - P / 2 - y0, rows);
    return;
  }
  int y = y0;
  const int colBuy = x + w * 30 / 100, colSell = x + w * 66 / 100;
  if (b.h >= 140) {
    r.drawText(CAPS_FONT, colBuy, y, tr(STR_IK_W_BUY), true, kBold);
    r.drawText(CAPS_FONT, colSell, y, tr(STR_IK_W_SELL), true, kBold);
    y += r.getLineHeight(CAPS_FONT);
  }
  const Rows rows = rowsFor(r, static_cast<int>(list.size()), b.y + b.h - P / 2 - y);
  for (const auto& p : list) {
    r.drawText(rows.font, x, y, codeOf(p).c_str(), true, kBold);
    r.drawText(rows.font, colBuy, y, p.v ? fmt(p.v->buy).c_str() : kDash);
    r.drawText(rows.font, colSell, y, p.v ? fmt(p.v->sell).c_str() : kDash, true, kBold);
    y += rows.h;
  }
}

// "66.821" (USD, fewer decimals as the price grows); VND as "1,72 tỷ" / "48,7 tr".
std::string usd(const double v) { return num(v, v >= 1000 ? 0 : v >= 1 ? 2 : 4); }
std::string vnd(const double v) {
  char buf[32];
  if (v >= 1e9) {
    snprintf(buf, sizeof(buf), "%s %s", num(v / 1e9, 2).c_str(), tr(STR_IK_W_BILLION));
  } else if (v >= 1e6) {
    snprintf(buf, sizeof(buf), "%s %s", num(v / 1e6, 1).c_str(), tr(STR_IK_W_MILLION));
  } else {
    snprintf(buf, sizeof(buf), "%s", num(v, 0).c_str());
  }
  return buf;
}

void drawCrypto(const GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  marketHeader(r, b, tr(STR_IK_W_CRYPTO_CAPS), d.cryptoAt);
  const int y0 = b.y + P + r.getLineHeight(CAPS_FONT) + 4;
  const auto list = chosen(d.crypto, codes(it.opt, "BTC,ETH"), [](const Data::Coin& x) { return x.sym; });
  if (!anyData(list)) return noData(r, b, y0);
  char buf[64];
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 12;
  auto symOf = [](const Pick<Data::Coin>& p) { return p.code.empty() ? p.v->sym : p.code; };
  if (b.w < 300 && list.size() == 1) {
    // 2x2 with one coin: the price big, in USD, with VND and the 24 h change under it.
    const auto& c = *list.front().v;
    drawFit(r, SMALL_FONT, x, y0, c.sym.c_str(), w, kBold);
    const std::string p = usd(c.usd);
    const int dy = y0 + r.getLineHeight(SMALL_FONT) + 2;
    int y = dy + heroNumber(r, p.c_str(), x, dy, w, b.h / 3) + 6;
    snprintf(buf, sizeof(buf), "USD · ~%s đ", vnd(c.vnd).c_str());
    drawFit(r, CAPS_FONT, x, y, buf, w);
    y += r.getLineHeight(CAPS_FONT);
    snprintf(buf, sizeof(buf), "%s%% · 24h", num(std::fabs(c.pct), 2).c_str());
    if (y + r.getLineHeight(CAPS_FONT) <= b.y + b.h - P / 2) changeText(r, x, y, c.pct, buf);
    return;
  }
  if (b.w < 300) {
    std::vector<PairRow> rows;
    for (const auto& p : list) rows.push_back({symOf(p), p.v ? "$" + usd(p.v->usd) : kDash, p.v ? signOf(p.v->pct) : 0});
    pairList(r, x, y0 + 2, w, b.y + b.h - P / 2 - y0, rows);
    return;
  }
  // Rows: coin, USD, VND, 24 h change.
  const int colUsd = x + w * 20 / 100, colVnd = x + w * 52 / 100, colChg = x + w * 80 / 100;
  int y = y0;
  const Rows rows = rowsFor(r, static_cast<int>(list.size()), b.y + b.h - P / 2 - y);
  for (const auto& p : list) {
    r.drawText(rows.font, x, y, symOf(p).c_str(), true, kBold);
    if (p.v) {
      const auto& c = *p.v;
      drawFit(r, rows.font, colUsd, y, ("$" + usd(c.usd)).c_str(), colVnd - colUsd - 8, kBold);
      drawFit(r, rows.font, colVnd, y, vnd(c.vnd).c_str(), colChg - colVnd - 8);
      snprintf(buf, sizeof(buf), "%s%%", num(std::fabs(c.pct), 2).c_str());
      changeText(r, colChg, y + (r.getLineHeight(rows.font) - r.getLineHeight(CAPS_FONT)) / 2, c.pct, buf);
    } else {
      r.drawText(rows.font, colUsd, y, kDash, true, kBold);
    }
    y += rows.h;
  }
}

// ---- library and photos ------------------------------------------------------------------

// The shelf of the books widget: cover height (thumbnails are made at it) and whether titles fit under
// the covers (covers narrower than 80 px show none: the cover carries the title).
struct Shelf {
  int ch;
  bool titles;
  bool feature;  // one or two books, each a big cover with its details beside it
  bool card;     // one book in a narrow box: a big cover with its details under it
};
Shelf shelfOf(const Box& b, const int count) {
  constexpr int CAPS_H = 24, SMALL_H = 19;  // Ubuntu 10 and 8 line heights
  const int base = b.h - P - CAPS_H - 6 - 12 - P / 2;
  const int withTitles = std::min(150, base - CAPS_H * 2 - (b.h >= H_HUGE ? SMALL_H * 2 + 4 : 0));
  Shelf s = withTitles * 2 / 3 >= 80 ? Shelf{withTitles, true, false, false} : Shelf{std::max(40, std::min(150, base)), false, false, false};
  // One or two books where a row of covers would leave the shelf half empty: bigger covers, the title,
  // author and progress beside each.
  const int w = b.w - 2 * P - 12;
  const int per = std::max(1, (w + 10) / (s.ch * 2 / 3 + 10));
  if (count > 0 && count <= 2 && count < per) {
    const int ch = b.h - P - CAPS_H - 6 - P;
    const int colW = (w - (count - 1) * 16) / count;
    if (colW - ch * 2 / 3 - 12 >= 80) return {ch, true, true, false};
    if (count == 1 && b.h >= H_BIG) {
      // The text under the cover: two title lines, the state and the bar.
      constexpr int TEXT_H = 45 * 2 + CAPS_H + 16;
      const int cw = std::min(w * 70 / 100, (ch - TEXT_H) * 2 / 3);
      if (cw >= 60) return {cw * 3 / 2, true, false, true};
    }
  }
  return s;
}

void bookState(const Data::Book& bk, char* out, const size_t n) {
  if (bk.percent >= 100) {
    snprintf(out, n, "%s", tr(STR_IK_W_BOOK_DONE));
  } else if (bk.percent <= 0) {
    snprintf(out, n, "%s", tr(STR_IK_W_BOOK_NEW));
  } else {
    snprintf(out, n, "%d%%", bk.percent);
  }
}

// The books a library widget lists: the recent ones, without the one being read when the widget asks
// (option "s": the reading widget already shows it).
std::vector<const Data::Book*> booksOf(const Item& it, const Data& d) {
  std::vector<const Data::Book*> out;
  const bool skip = it.opt.find('s') != std::string::npos;
  for (size_t i = 0; i < d.library.size(); i++) {
    if (skip && i == 0 && d.hasBook && d.library[0].path == d.bookPath) continue;
    out.push_back(&d.library[i]);
  }
  return out;
}

// Draws the library widget (or only counts, `dry`); returns how many books it shows. The book at
// d.bookFocus is marked (Home steps through them).
int drawBooks(GfxRenderer& r, const Item& it, const Box& b, const Data& d, const bool dry = false) {
  const int x = b.x + P + 6;
  const int w = b.w - 2 * P - 12;
  const auto list = booksOf(it, d);
  const int count = static_cast<int>(list.size());
  if (!dry) {
    caps(r, x, b.y + P, tr(STR_IK_W_BOOKS_CAPS));
    if (b.w >= 300) drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, tr(STR_IK_W_BOOKS_RECENT));
  }
  int y = b.y + P + r.getLineHeight(CAPS_FONT) + 6;
  if (list.empty()) {
    if (!dry) centerText(r, SMALL_FONT, x, y, w, b.y + b.h - P - y, tr(STR_IK_W_BOOKS_EMPTY), kReg);
    return 0;
  }
  char st[24];
  auto mark = [&](const int i, const int mx, const int my, const int mw, const int mh) {
    if (!dry && i == d.bookFocus) r.drawRoundedRect(mx - 5, my - 5, mw + 10, mh + 10, 3, 8, true);
  };
  if (it.style != "list") {
    const Shelf sh = shelfOf(b, count);
    const int ch = sh.ch;
    const int cw = ch * 2 / 3;
    if (sh.card) {
      // One book in a narrow box: the cover centred, the title, state and bar under it.
      if (dry) return 1;
      const auto& bk = *list[0];
      const int cx = b.x + (b.w - cw) / 2;
      drawCoverAt(r, bk.thumb, cx, y, cw, ch);
      mark(0, cx, y, cw, ch);
      int ty = y + ch + 8;
      const int bottom = b.y + b.h - P;
      const int barY = bottom - 12;
      const int stY = barY - r.getLineHeight(CAPS_FONT) - 2;
      ty += iu::wrapCentered(r, NOTOSANS_16_FONT_ID, x, ty, w, stY - 4 - ty, bk.title.c_str(), true, kBold);
      bookState(bk, st, sizeof(st));
      drawFitCentered(r, CAPS_FONT, b.x + b.w / 2, stY, st, w, kBold);
      if (bk.percent >= 0) bar(r, x, barY, w, 12, std::min(100, bk.percent), true);
      return 1;
    }
    if (sh.feature) {
      // One or two books: a big cover with the title, author, state and bar beside it.
      const int colW = (w - (count - 1) * 16) / count;
      if (dry) return count;
      for (int i = 0; i < count; i++) {
        const auto& bk = *list[i];
        const int cx = x + i * (colW + 16);
        drawCoverAt(r, bk.thumb, cx, y, cw, ch);
        mark(i, cx, y, cw, ch);
        const int tx = cx + cw + 12;
        const int tw = colW - cw - 12;
        const int barY = y + ch - 12;
        bookState(bk, st, sizeof(st));
        const int stY = barY - r.getLineHeight(CAPS_FONT) - 2;
        if (bk.percent >= 0) bar(r, tx, barY, tw, 12, std::min(100, bk.percent), true);
        drawFit(r, CAPS_FONT, tx, stY, st, tw, kBold);
        const int authorH = bk.author.empty() || r.getTextWidth(CAPS_FONT, bk.author.c_str()) > tw ? 0 : r.getLineHeight(CAPS_FONT);
        const int room = stY - 6 - y - authorH;
        const int used = iu::wrapIn(r, NOTOSANS_16_FONT_ID, tx, y, tw, room, bk.title.c_str(), true, kBold);
        if (authorH) r.drawText(CAPS_FONT, tx, y + used + 2, bk.author.c_str(), true);
      }
      return count;
    }
    // A row of covers, a progress bar and the title under each; tall boxes add the author and state.
    const int per = std::max(1, (w + 10) / (cw + 10));
    const int n = std::min(per, count);
    if (dry) return n;
    for (int i = 0; i < n; i++) {
      const auto& bk = *list[i];
      const int cx = x + i * (cw + 10);
      drawCoverAt(r, bk.thumb, cx, y, cw, ch);
      mark(i, cx, y, cw, ch);
      int ty = y + ch + 3;
      if (bk.percent >= 0) bar(r, cx, ty, cw, 8, std::min(100, bk.percent), true);
      ty += 12;
      if (!sh.titles) continue;
      // The whole title in two lines (smaller type if needed), or none: never a cut one.
      int used = 0;
      const int titleH = r.getLineHeight(CAPS_FONT) * 2;
      iu::wrapInFont(r, CAPS_FONT, cw, titleH, bk.title.c_str(), kBold, &used);
      if (used <= titleH) ty += iu::wrapIn(r, CAPS_FONT, cx, ty, cw, titleH, bk.title.c_str(), true, kBold);
      if (b.h >= H_HUGE) {
        // The author on one line when it fits there (else left out, never cut or wrapped over the state).
        if (!bk.author.empty() && r.getTextWidth(SMALL_FONT_ID, bk.author.c_str()) <= cw) {
          r.drawText(SMALL_FONT_ID, cx, ty, bk.author.c_str(), true);
          ty += r.getLineHeight(SMALL_FONT_ID);
        }
        bookState(bk, st, sizeof(st));
        drawFit(r, SMALL_FONT_ID, cx, ty, st, cw);
      }
    }
    return n;
  }
  // List: title (the author under it when there is room, else beside it), state on the right; rows grow to
  // two lines for long titles, in one type size for the whole list; the books that do not fit are counted.
  const int bottom = b.y + b.h - P / 2;
  const int moreH = r.getLineHeight(CAPS_FONT) + 2;
  int stateW = 0;
  for (const auto* bk : list) {
    bookState(*bk, st, sizeof(st));
    stateW = std::max(stateW, r.getTextWidth(CAPS_FONT, st, kBold));
  }
  const int titleW = w - stateW - 10;
  // The biggest type in which every title takes two lines at most (three for very long ones); bigger when
  // the list is short.
  const int first = b.w >= 300 ? (count <= 3 ? NOTOSANS_16_FONT_ID : BODY_FONT) : (count <= 2 ? BODY_FONT : SMALL_FONT);
  int font = 0;
  for (const int lines : {2, 3}) {
    for (int cand = first; cand != 0 && font == 0; cand = iu::smallerFont(cand)) {
      bool ok = true;
      for (int i = 0; i < count && i < 8 && ok; i++) {
        ok = iu::wrapInFont(r, cand, titleW, r.getLineHeight(cand) * lines, list[i]->title.c_str(), kBold) == cand;
      }
      if (ok) font = cand;
    }
    if (font != 0) break;
  }
  if (font == 0) font = SMALL_FONT_ID;
  const int authorH = r.getLineHeight(CAPS_FONT);
  const bool authorLine = b.h >= H_BIG && count * (r.getLineHeight(font) + authorH + 8) <= bottom - y;
  int shown = 0;
  for (int i = 0; i < count; i++) {
    const auto& bk = *list[i];
    int used = 0;
    iu::wrapInFont(r, font, titleW, r.getLineHeight(font) * 3, bk.title.c_str(), kBold, &used);
    const bool authorBelow = authorLine && !bk.author.empty();
    const int rowH = std::max(used, r.getLineHeight(font)) + (authorBelow ? authorH : 0) + 6;
    const int reserve = i < count - 1 ? moreH : 0;
    if (shown > 0 && y + rowH + reserve > bottom) break;
    if (!dry) {
      bookState(bk, st, sizeof(st));
      const bool oneLine = used <= r.getLineHeight(font);
      const std::string titleAuthor = bk.title + " · " + bk.author;
      if (!authorLine && b.w >= 300 && oneLine && !bk.author.empty() && r.getTextWidth(font, titleAuthor.c_str(), kBold) <= titleW) {
        r.drawText(font, x, y, titleAuthor.c_str(), true, kBold);
      } else {
        const int th = iu::wrapIn(r, font, x, y, titleW, r.getLineHeight(font) * 3, bk.title.c_str(), true, kBold);
        if (authorBelow) drawFit(r, CAPS_FONT, x, y + th, bk.author.c_str(), titleW);
      }
      drawRight(r, CAPS_FONT, x + w, y + (r.getLineHeight(font) - r.getLineHeight(CAPS_FONT)) / 2, st, kBold);
      r.fillRect(x, y + rowH - 3, w, 1, true);
      mark(i, x, y, w, rowH - 6);
    }
    y += rowH;
    shown++;
  }
  if (!dry && shown < count) {
    char more[48];
    snprintf(more, sizeof(more), tr(STR_IK_W_MORE_BOOKS_FMT), count - shown);
    drawFit(r, CAPS_FONT, x, std::min(y, bottom - moreH), more, w, kBold);
  }
  return shown;
}

// A photo widget's option: "<mode><every><fit><ink><brightness>|id,id,…" (see the web's photo.ts).
struct PhotoOpt {
  char mode = 'r', every = 'h';
  std::string params = "ca50";
  std::vector<int32_t> ids;
};
PhotoOpt photoOpt(const std::string& opt) {
  PhotoOpt o;
  const size_t bar = opt.find('|');
  if (bar != 6) return o;
  o.mode = opt[0];
  o.every = opt[1];
  o.params = opt.substr(2, 4);
  size_t i = bar + 1;
  while (i < opt.size() && o.ids.size() < 12) {
    size_t j = opt.find(',', i);
    if (j == std::string::npos) j = opt.size();
    const int32_t id = static_cast<int32_t>(atol(opt.substr(i, j - i).c_str()));
    if (id > 0) o.ids.push_back(id);
    i = j + 1;
  }
  return o;
}

std::string photoPath(const int32_t id, const int w, const int h, const std::string& params) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s/%ld_%dx%d_%s.bmp", PHOTO_DIR, static_cast<long>(id), w, h, params.c_str());
  return buf;
}

// Which picture to show now: the same one for the whole period (each opening, hour or day).
size_t photoIndex(const PhotoOpt& o, const size_t n, const Item& it, const Data& d) {
  if (n <= 1 || o.mode == 'f') return 0;
  uint32_t period = d.showSeq;
  if (o.every != 'v' && d.timeValid) {
    const uint32_t day = static_cast<uint32_t>(lunar::julianDay(d.now.tm_mday, d.now.tm_mon + 1, d.now.tm_year + 1900));
    period = o.every == 'd' ? day : day * 24 + static_cast<uint32_t>(d.now.tm_hour);
  }
  if (o.mode == 's') return period % n;
  auto pick = [&](const uint32_t p) {
    uint32_t v = p * 2654435761u ^ (static_cast<uint32_t>(it.x) * 7u + static_cast<uint32_t>(it.y) * 13u + 1u) * 40503u;
    v ^= v >> 15;
    v *= 2246822519u;
    v ^= v >> 13;
    return v % n;
  };
  size_t i = pick(period);
  if (i == pick(period - 1)) i = (i + 1) % n;  // never the same picture twice in a row
  return i;
}

void drawPhoto(GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  const PhotoOpt o = photoOpt(it.opt);
  if (!o.ids.empty()) {
    const size_t first = photoIndex(o, o.ids.size(), it, d);
    // The chosen picture, or the next one already on the card.
    for (size_t k = 0; k < o.ids.size(); k++) {
      const std::string path = photoPath(o.ids[(first + k) % o.ids.size()], b.w, b.h, o.params);
      HalFile f;
      if (!Storage.openFileForRead("WGT", path, f)) continue;
      Bitmap bmp(f);
      const bool ok = bmp.parseHeaders() == BmpReaderError::Ok && r.drawBitmap(bmp, b.x, b.y, b.w, b.h);
      f.close();
      if (ok) {
        gImages.push_back(b);
        return;
      }
    }
  }
  const char* msg = o.ids.empty() ? tr(STR_IK_W_PHOTO_EMPTY) : tr(STR_IK_W_PHOTO_WAIT);
  iu::wrapIn(r, SMALL_FONT, b.x + P + 6, b.y + P + 4, b.w - 2 * P - 12, b.h - 2 * P, msg, true, kBold);
}

// Overview: the time, the date (solar and lunar) and the weather in one row; a tall box adds the next event
// and the battery under a rule.
void drawOverview(const GfxRenderer& r, const Item&, const Box& b, const Data& d) {
  char t[12], buf[96];
  timeText(d, t, sizeof(t));
  const int capsH = r.getLineHeight(CAPS_FONT);
  const bool tall = b.h >= H_LOW;
  const int top = tall ? b.y + P : b.y + 8;
  const int topH = tall ? (b.h - 2 * P) * 58 / 100 : b.h - 16;
  const int right = b.x + b.w - P;
  int x = b.x + P;
  int f = digitsFontOr0(r, t, (b.w - 2 * P) * 40 / 100, topH);
  if (f == 0) f = INK_30_FONT_ID;
  drawDigits(r, f, x, top + (topH - digitHeight(f)) / 2, t);
  x += r.getTextWidth(f, t, kBold) + 14;
  r.fillRect(x - 2, top + 6, 2, topH - 12, true);
  x += 12;
  int wx = right;
  if (d.hasWeather) {
    // The weather at the right edge: icon and temperature.
    char temp[16];
    snprintf(temp, sizeof(temp), "%d°", d.temp);
    const int s = std::min(topH, 52);
    const int tf = digitHeight(INK_30_FONT_ID) <= topH ? INK_30_FONT_ID : 0;
    const int tw = tf ? r.getTextWidth(tf, temp, kBold) : r.getTextWidth(NOTOSANS_18_FONT_ID, temp, kBold);
    const int left = right - s - 6 - tw;
    if (left - x >= 110) {
      drawIcon(r, d.icon.c_str(), left + s / 2, top + topH / 2, s);
      if (tf) {
        drawDigits(r, tf, left + s + 6, top + (topH - digitHeight(tf)) / 2, temp);
      } else {
        r.drawText(NOTOSANS_18_FONT_ID, left + s + 6, top + (topH - r.getLineHeight(NOTOSANS_18_FONT_ID)) / 2, temp, true, kBold);
      }
      wx = left - 12;
    }
  }
  dateColumn(r, d, x, top, wx - x, topH);
  if (!tall) return;
  const int ry = top + topH + 6;
  r.fillRect(b.x + P, ry, b.w - 2 * P, 2, true);
  const int by = ry + 2;
  const int bh = b.y + b.h - P / 2 - by;
  // The battery at the right.
  snprintf(buf, sizeof(buf), "%d%%", d.battery);
  const int pw = r.getTextWidth(CAPS_FONT, buf, kBold);
  const int iconW = 30, iconH = 15;
  const int bx = right - pw - 8 - iconW - 3;
  const int bcy = by + bh / 2;
  r.drawRect(bx, bcy - iconH / 2, iconW, iconH, 2, true);
  r.fillRect(bx + iconW, bcy - iconH / 2 + 4, 3, iconH - 8, true);
  const int fill = (iconW - 6) * std::min(100, std::max(0, d.battery)) / 100;
  if (fill > 0) r.fillRect(bx + 3, bcy - iconH / 2 + 3, fill, iconH - 6, true);
  r.drawText(CAPS_FONT, right - pw, bcy - capsH / 2, buf, true, kBold);
  // The next event of today, or none left.
  const int ex = b.x + P;
  const int ew = bx - 16 - ex;
  int next = -1;
  if (d.timeValid) {
    const int nowMin = d.now.tm_hour * 60 + d.now.tm_min;
    for (size_t i = 0; i < d.events.size(); i++) {
      if (minutesOfDay(d.events[i].time.c_str(), -1) >= nowMin) {
        next = static_cast<int>(i);
        break;
      }
    }
  }
  const int ef = BODY_FONT;
  const int ey = by + (bh - r.getLineHeight(ef)) / 2;
  if (next >= 0) {
    const auto& e = d.events[next];
    const int tw = r.getTextWidth(ef, e.time.c_str(), kBold);
    r.drawText(ef, ex, ey, e.time.c_str(), true, kBold);
    drawFit(r, ef, ex + tw + 12, ey, e.title.c_str(), ew - tw - 12, kReg);
  } else {
    drawFit(r, SMALL_FONT, ex, by + (bh - r.getLineHeight(SMALL_FONT)) / 2, tr(STR_IK_W_NO_MORE_EVENTS), ew, kBold);
  }
}

void drawSetup(const GfxRenderer& r, const Box& b, const bool focused) {
  if (focused) r.drawRoundedRect(b.x - 1, b.y - 1, b.w + 2, b.h + 2, 3, 14, true);
  r.fillRoundedRect(b.x + 4, b.y + 4, b.w - 8, b.h - 8, RADIUS, Color::Black);
  const int x = b.x + 24;
  int ty = b.y + 20;
  r.drawText(NOTOSANS_14_FONT_ID, x, ty, tr(STR_IK_SETUP_TITLE), false, kBold);
  ty += r.getLineHeight(NOTOSANS_14_FONT_ID) + 2;
  iu::wrap(r, iu::fontSmall(), x, ty, b.w - 48, tr(STR_IK_SETUP_BODY), 4, false);
  const char* cta = tr(STR_IK_SETUP_CTA);
  const int cw = r.getTextWidth(iu::fontSmall(), cta, kBold) + 20;
  const int ch = r.getLineHeight(iu::fontSmall()) + 8;
  const int cy = b.y + b.h - ch - 18;
  r.fillRoundedRect(x, cy, cw, ch, 8, Color::White);
  r.drawText(iu::fontSmall(), x + 10, cy + 4, cta, true, kBold);
}

void readDashboard(Data& d) {
  std::string raw;
  if (!Storage.exists(DASH_FILE) || !Storage.readFileToString("WGT", DASH_FILE, DASH_CAP, raw)) return;
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return;
  raw.clear();
  raw.shrink_to_fit();
  d.lunar = doc["lunar"] | "";
  d.lunarYear = doc["lunar_year"] | "";
  JsonObjectConst w = doc["weather"];
  if (!w.isNull()) {
    d.hasWeather = true;
    d.city = w["city"] | "";
    d.desc = w["desc"] | "";
    d.icon = w["icon"] | "cloud";
    d.temp = w["temp"] | 0;
    d.tmin = w["min"] | 0;
    d.tmax = w["max"] | 0;
    d.rain = w["rain"] | 0;
    for (JsonObjectConst h : w["hours"].as<JsonArrayConst>()) {
      if (d.hours.size() >= 4) break;
      d.hours.push_back({h["h"] | "", h["icon"] | "cloud", h["t"] | 0});
    }
    for (JsonObjectConst x : w["days"].as<JsonArrayConst>()) {
      if (d.days.size() >= 3) break;
      Data::Day day;
      day.d = x["d"] | "";
      day.icon = x["icon"] | "cloud";
      day.min = x["min"] | 0;
      day.max = x["max"] | 0;
      d.days.push_back(std::move(day));
    }
  }
  // Events are the server's "today" at download time: drop them once that day is over.
  const int64_t dayMs = 24LL * 60 * 60 * 1000;
  const int64_t tzMs = static_cast<int64_t>(INKDESK.tzMin) * 60 * 1000;
  d.eventsOld = InkDeskStore::clockValid() && INKDESK.dashFetchedMs > 0 &&
                (InkDeskStore::nowMs() + tzMs) / dayMs != (INKDESK.dashFetchedMs + tzMs) / dayMs;
  JsonArrayConst events = doc["events"];
  if (!d.eventsOld && !events.isNull()) {
    d.eventCount = static_cast<int>(events.size());
    for (JsonObjectConst e : events) {
      if (d.events.size() >= 10) break;
      d.events.push_back({e["time"] | "", e["title"] | ""});
    }
  }
  for (JsonObjectConst n : doc["notes"].as<JsonArrayConst>()) {
    if (d.notes.size() >= 4) break;
    d.notes.push_back({n["title"] | "", n["time"] | ""});
  }
  JsonObjectConst news = doc["news"];
  if (!news.isNull()) {
    d.newsTitle = news["title"] | "";
    d.newsCount = news["count"] | 0;
    for (JsonVariantConst t : news["top"].as<JsonArrayConst>()) {
      if (d.newsTop.size() >= 3) break;
      if (t.as<const char*>()) d.newsTop.emplace_back(t.as<const char*>());
    }
  }
  JsonObjectConst gold = doc["gold"];
  d.goldAt = gold["at"] | "";
  d.goldSrc = gold["src"] | "";
  for (JsonObjectConst g : gold["items"].as<JsonArrayConst>()) {
    if (d.gold.size() >= 8) break;
    d.gold.push_back({g["c"] | "", g["n"] | "", g["b"] | 0.0, g["s"] | 0.0, g["d"] | 0.0});
  }
  JsonObjectConst st = doc["stocks"];
  d.stocksAt = st["at"] | "";
  for (JsonObjectConst t : st["items"].as<JsonArrayConst>()) {
    if (d.stocks.size() >= 8) break;
    d.stocks.push_back({t["s"] | "", t["p"] | 0.0, t["c"] | 0.0, t["pc"] | 0.0});
  }
  JsonObjectConst cr = doc["crypto"];
  d.cryptoAt = cr["at"] | "";
  for (JsonObjectConst c : cr["items"].as<JsonArrayConst>()) {
    if (d.crypto.size() >= 6) break;
    d.crypto.push_back({c["s"] | "", c["p"] | 0.0, c["v"] | 0.0, c["pc"] | 0.0});
  }
  JsonObjectConst fx = doc["fx"];
  d.fxAt = fx["at"] | "";
  for (JsonObjectConst f : fx["items"].as<JsonArrayConst>()) {
    if (d.fx.size() >= 10) break;
    d.fx.push_back({f["c"] | "", f["b"] | 0.0, f["s"] | 0.0});
  }
}

}  // namespace

bool parse(const std::string& json, const Screen screen, Layout& out) {
  JsonDocument doc;
  if (json.empty() || deserializeJson(doc, json)) return false;
  out = Layout{};
  out.screen = screen;
  gridOf(screen, out.cols, out.rows);
  out.footer = (doc["f"] | 1) != 0;
  const char* skin = doc["k"] | "card";
  out.skin = !strcmp(skin, "line") ? Skin::Line : !strcmp(skin, "bare") ? Skin::Bare : !strcmp(skin, "bold") ? Skin::Bold
           : !strcmp(skin, "shadow") ? Skin::Shadow : Skin::Card;
  std::vector<uint8_t> used(out.cols * out.rows, 0);
  for (JsonArrayConst a : doc["i"].as<JsonArrayConst>()) {
    if (out.items.size() >= MAX_ITEMS) break;
    Item it;
    it.kind = kindOf(a[0] | "");
    const int x = a[1] | -1, y = a[2] | -1, w = a[3] | 0, h = a[4] | 0;
    if (it.kind == Kind::Unknown || x < 0 || y < 0 || w < 1 || h < 1 || x + w > out.cols || y + h > out.rows) continue;
    // The sleep screens are only looked at: no shortcut buttons there. Removed shortcuts are skipped.
    if (it.kind == Kind::App && (screen != Screen::Home || !findApp(a[5] | ""))) continue;
    bool free = true;
    for (int yy = y; yy < y + h && free; yy++) {
      for (int xx = x; xx < x + w; xx++) free = free && !used[yy * out.cols + xx];
    }
    if (!free) continue;
    for (int yy = y; yy < y + h; yy++) {
      for (int xx = x; xx < x + w; xx++) used[yy * out.cols + xx] = 1;
    }
    it.x = x;
    it.y = y;
    it.w = w;
    it.h = h;
    it.style = a[5] | "";
    it.opt = a[6] | "";
    it.inverted = !strcmp(a[7] | "", "i");
    if (it.style.size() > 16) it.style.resize(16);
    if (it.opt.size() > 160) it.opt.resize(160);
    out.items.push_back(std::move(it));
  }
  return !out.items.empty();
}

Layout builtin(const Screen screen, const bool linked) {
  Layout l;
  l.screen = screen;
  gridOf(screen, l.cols, l.rows);
  auto& v = l.items;
  switch (screen) {
    case Screen::SleepLandscape:
      v = {item(Kind::Clock, 0, 0, 4, 3, "flip"), item(Kind::Date, 4, 0, 2, 2, "sheet"),
           item(Kind::Weather, 4, 2, 2, 2, "now"), item(Kind::Countdown, 0, 3, 4, 1, "tet")};
      break;
    case Screen::SleepPortrait:
      v = {item(Kind::Clock, 0, 0, 4, 2, "lunar"), item(Kind::Weather, 0, 2, 4, 2, "hourly"),
           item(Kind::Events, 0, 4, 4, 3, "")};
      break;
    default:
      if (linked) {
        // Like the Home InkDesk had before widgets: today, the book, today's calendar, the apps.
        v = {item(Kind::Weather, 0, 0, 4, 1, "now"), item(Kind::Reading, 0, 1, 4, 2, "cover"),
             item(Kind::Events, 0, 3, 4, 2, "")};
      } else {
        v = {item(Kind::Clock, 0, 0, 4, 2, "lunar"), item(Kind::Setup, 0, 2, 4, 3, "")};
      }
      for (int i = 0; i < 8; i++) {
        static const char* const kApps[8] = {"library", "news", "stats", "sync", "transfer", "update", "apps", "settings"};
        v.push_back(item(Kind::App, i % 4, 5 + i / 4, 1, 1, kApps[i]));
      }
      break;
  }
  return l;
}

Layout load(const Screen screen, const bool linked) {
  if (screen == Screen::Home && !linked) return builtin(screen, false);
  Layout l;
  if (parse(INKDESK.get(settingKey(screen), ""), screen, l)) return l;
  return builtin(screen, linked);
}

void loadData(Data& d, const int coverH) {
  d.linked = INKDESK.isLinked();
  d.timeValid = iu::localTime(d.now);
  readDashboard(d);
  if (d.timeValid) {
    // The device's own lunar date wins over the cached server copy (it may be from yesterday).
    const lunar::Date l = lunar::fromSolar(d.now.tm_mday, d.now.tm_mon + 1, d.now.tm_year + 1900, LUNAR_TZ);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d%s", l.day, l.month, l.leap ? "N" : "");
    d.lunar = buf;
    d.lunarYear = lunar::yearName(l.year);
  }
  const auto& books = RECENT_BOOKS.getBooks();
  if (!books.empty()) {
    const auto& b = books.front();
    d.hasBook = true;
    d.bookPath = b.path;
    d.bookTitle = b.title.empty() ? FsHelpers::getFileNameWithoutExtension(b.path) : b.title;
    d.bookAuthor = b.author;
    d.coverPath = b.coverBmpPath.empty() ? std::string() : UITheme::getCoverThumbPath(b.coverBmpPath, coverH);
    d.bookPercent = loadBookProgress(b.path);
  }
  for (const auto& b : books) {
    if (d.library.size() >= 8) break;
    Data::Book bk;
    bk.path = b.path;
    bk.title = b.title.empty() ? FsHelpers::getFileNameWithoutExtension(b.path) : b.title;
    bk.author = b.author;
    bk.coverBmp = b.coverBmpPath;
    bk.percent = loadBookProgress(b.path);
    d.library.push_back(std::move(bk));
  }
  d.stats = stats::summary();
  d.newsPath = INKDESK.latestNewsPath(&d.newsDay);
  d.battery = powerManager.getBatteryPercentage();
  d.lastSyncMs = INKDESK.lastSyncMs;
}

std::vector<Box> place(const Layout& l, const int left, const int top, const int right, const int bottom, const int gap) {
  std::vector<Box> out;
  out.reserve(l.items.size());
  const double cw = static_cast<double>(right - left - gap * (l.cols - 1)) / l.cols;
  const double rh = static_cast<double>(bottom - top - gap * (l.rows - 1)) / l.rows;
  for (const auto& it : l.items) {
    const int x0 = left + static_cast<int>(it.x * (cw + gap));
    const int y0 = top + static_cast<int>(it.y * (rh + gap));
    const int x1 = left + static_cast<int>((it.x + it.w) * (cw + gap) - gap);
    const int y1 = top + static_cast<int>((it.y + it.h) * (rh + gap) - gap);
    const int sh = l.skin == Skin::Shadow ? SHADOW : 0;
    out.push_back({x0, y0, x1 - x0 - sh, y1 - y0 - sh});
  }
  return out;
}

std::vector<Box> boxesFor(const Layout& l) {
  switch (l.screen) {
    case Screen::SleepLandscape:
      return place(l, SLEEP_PAD, l.footer ? SLEEP_TOP : SLEEP_PAD, 800 - SLEEP_PAD,
                   l.footer ? 480 - SLEEP_FOOTER_H - SLEEP_GAP : 480 - SLEEP_PAD, SLEEP_GAP);
    case Screen::SleepPortrait:
      return place(l, SLEEP_PAD, l.footer ? SLEEP_TOP : SLEEP_PAD, 480 - SLEEP_PAD,
                   l.footer ? 800 - SLEEP_FOOTER_H - SLEEP_GAP : 800 - SLEEP_PAD, SLEEP_GAP);
    default:
      return place(l, HOME_SIDE, HOME_TOP, 480 - HOME_SIDE, 800 - HOME_BOTTOM, HOME_GAP);
  }
}

namespace {
RTC_DATA_ATTR uint32_t gShowSeq = 0;
}  // namespace

uint32_t showSeq(const bool next) {
  if (next) gShowSeq++;
  return gShowSeq;
}

std::vector<PhotoFile> photoFiles(const Layout& l, const std::vector<Box>& boxes) {
  std::vector<PhotoFile> out;
  for (size_t i = 0; i < l.items.size() && i < boxes.size(); i++) {
    if (l.items[i].kind != Kind::Photo) continue;
    const PhotoOpt o = photoOpt(l.items[i].opt);
    char dim[16];
    snprintf(dim, sizeof(dim), "%dx%d", boxes[i].w, boxes[i].h);
    for (const int32_t id : o.ids) out.push_back({id, dim, o.params, photoPath(id, boxes[i].w, boxes[i].h, o.params)});
  }
  return out;
}

namespace {
// The thumbnail of an EPUB at `height`, made when missing; "" for other books or on failure.
std::string thumbFor(const std::string& bookPath, const int height, const bool dryRun, bool& work) {
  if (height <= 0 || !FsHelpers::hasReflowableBookExtension(bookPath)) return "";
  Epub epub(bookPath, "/.crosspoint");
  const std::string path = epub.getThumbBmpPath(height);
  if (Storage.exists(path.c_str())) return path;
  work = true;
  if (dryRun) return "";
  const bool cacheLoaded = epub.load(false, true);
  const bool ok = cacheLoaded ? epub.generateThumbBmp(height) : epub.generateThumbBmpFromSource(height);
  return ok ? epub.getThumbBmpPath(height) : std::string();
}
}  // namespace

bool ensureCovers(const Layout& l, const std::vector<Box>& boxes, Data& d, const bool dryRun) {
  bool work = false;
  for (size_t i = 0; i < l.items.size() && i < boxes.size(); i++) {
    const Item& it = l.items[i];
    if (it.kind == Kind::Reading && d.hasBook) {
      const std::string p = thumbFor(d.bookPath, readingCoverH(it, boxes[i]), dryRun, work);
      if (!p.empty()) d.coverPath = p;
    } else if (it.kind == Kind::Books && it.style != "list") {
      const auto list = booksOf(it, d);
      const int count = static_cast<int>(list.size());
      const Shelf sh = shelfOf(boxes[i], count);
      const int ch = sh.ch;
      const int per = sh.feature || sh.card ? count : std::max(1, (boxes[i].w - 2 * P - 12 + 10) / (ch * 2 / 3 + 10));
      for (int k = 0; k < per && k < count; k++) {
        auto* bk = const_cast<Data::Book*>(list[k]);
        bk->thumb = thumbFor(bk->path, ch, dryRun, work);
      }
    }
  }
  return work;
}

namespace {
void drawContent(GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  switch (it.kind) {
    case Kind::Clock:
      drawClock(r, it, b, d);
      break;
    case Kind::Date:
      drawDate(r, it, b, d);
      break;
    case Kind::Month:
      drawMonth(r, it, b, d);
      break;
    case Kind::Events:
      drawEvents(r, it, b, d);
      break;
    case Kind::Countdown:
      drawCountdown(r, it, b, d);
      break;
    case Kind::Weather:
      drawWeather(r, it, b, d);
      break;
    case Kind::Reading:
      drawReading(r, it, b, d);
      break;
    case Kind::Stats:
      drawStats(r, it, b, d);
      break;
    case Kind::News:
      drawNews(r, it, b, d);
      break;
    case Kind::Notify:
      drawNotify(r, it, b, d);
      break;
    case Kind::Battery:
      drawBattery(r, it, b, d);
      break;
    case Kind::Gold:
      drawGold(r, it, b, d);
      break;
    case Kind::Stocks:
      drawStocks(r, it, b, d);
      break;
    case Kind::Fx:
      drawFx(r, it, b, d);
      break;
    case Kind::Crypto:
      drawCrypto(r, it, b, d);
      break;
    case Kind::Books:
      drawBooks(r, it, b, d);
      break;
    case Kind::Photo:
      drawPhoto(r, it, b, d);
      break;
    case Kind::Overview:
      drawOverview(r, it, b, d);
      break;
    case Kind::Pkg:
      inkdesk::decl::drawWidget(r, it.opt, b, d);
      break;
    default:
      break;
  }
}
}  // namespace

void draw(GfxRenderer& r, const Item& it, const Box& b, const Data& d, const bool focused, const Skin skin) {
  gSkin = skin;
  gImages.clear();
  if (skin == Skin::Shadow) shadowUnder(r, b);
  switch (it.kind) {
    case Kind::App:
      drawApp(r, it, b, d, focused);
      return;
    case Kind::Setup:
      drawSetup(r, b, focused);
      return;
    default:
      break;
  }
  const bool sheet = it.kind == Kind::Date && it.style != "compact" && b.h >= H_LOW;  // draws its own frame
  if (it.inverted) {
    // White on black: the widget drawn plain, then flipped; pictures are flipped back.
    drawContent(r, it, b, d);
    r.invertRect(b.x, b.y, b.w, b.h);
    for (const Box& im : gImages) r.invertRect(im.x, im.y, im.w, im.h);
    if (rounded(skin) || skin == Skin::Bare) clearCorners(r, b, RADIUS);
    if (focused) {
      r.drawRoundedRect(b.x - 4, b.y - 4, b.w + 8, b.h + 8, 2, RADIUS + 4, true);
    }
    return;
  }
  if (it.kind == Kind::Photo) {
    // The picture fills the box; the frame goes on top of it.
    drawContent(r, it, b, d);
    if (rounded(skin)) clearCorners(r, b, RADIUS);
    frame(r, b, focused);
    return;
  }
  if (!sheet) frame(r, b, focused);
  drawContent(r, it, b, d);
}

int bookCount(GfxRenderer& r, const Item& it, const Box& b, const Data& d) {
  // A dry run of the drawing: only measures.
  return it.kind == Kind::Books ? drawBooks(r, it, b, d, true) : 0;
}

std::string bookPath(const Item& it, const Data& d, const int index) {
  const auto list = booksOf(it, d);
  return index >= 0 && index < static_cast<int>(list.size()) ? list[index]->path : std::string();
}

Action action(const Item& it) {
  switch (it.kind) {
    case Kind::Reading:
      return Action::Reader;
    case Kind::Stats:
      return Action::Stats;
    case Kind::News:
      return Action::News;
    case Kind::Books:
      return Action::Library;
    case Kind::Setup:
      return Action::Setup;
    case Kind::App:
      return appOf(it.style)->action;
    default:
      return Action::None;
  }
}

}  // namespace inkdesk::widgets
