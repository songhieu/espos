#pragma GCC optimize("Os")  // widgets/JS glue built once per change: size over speed (0.5.7 flash diet)
// The weather family (docs/widgets.md §3): now, hourly, days, detail (feels, humidity, wind, UV, air) and sun,
// each a small piece of app_weather on its sky. web/app/src/os/wid/weather.ts draws the same.
#include <ArduinoJson.h>

#include <cmath>

#include "../art.h"
#include "../data.h"
#include "kit.h"

namespace wid {

using namespace kit;

const std::vector<Style> WEATHER_STYLES = {{"now", N_("Current")},   {"hourly", N_("Hourly")}, {"days", N_("Daily")},
                                           {"detail", N_("Details")}, {"sun", N_("Sun")},       {"big", N_("Big temperature")}};

namespace {

// Marked: the callers translate (T_).
const char* aqiText(int a) {
  return a <= 50    ? N_("Good")
         : a <= 100 ? N_("Moderate")
         : a <= 150 ? N_("Poor")
         : a <= 200 ? N_("Unhealthy")
         : a <= 300 ? N_("Very unhealthy")
                    : N_("Hazardous");
}
const char* uvText(int uv) { return uv <= 2 ? N_("Low") : uv <= 5 ? N_("Moderate") : uv <= 7 ? N_("High") : N_("Very high"); }
// "05:52" -> minutes of the day (-1 when unreadable).
int minutesOf(const String& hm) {
  if (hm.length() < 4 || hm.indexOf(':') < 1) return -1;
  return hm.substring(0, hm.indexOf(':')).toInt() * 60 + hm.substring(hm.indexOf(':') + 1).toInt();
}
String span(int mins) {  // "11 h 29 min"
  if (mins % 60) return i18n::fmt(N_("{0} h {1} min"), String(mins / 60), String(mins % 60));
  return i18n::fmt(N_("{0} h"), String(mins / 60));
}

// The city of the looks with the big icon at the top right: on one line beside the icon (cap() closing its letters
// up); too long for that, on the whole row with the icon moved down out of its way (dy), wrapping only past the whole
// row. Returns the heading's height past one line: what is under it moves down by that much.
int cityOverIcon(Card c, const String& city, uint32_t dim, int& dy) {
  const bool beside = capFits(city.c_str(), c.w - 58);  // the 56 px icon stands at c.w - 54
  const int ex = heightOf(cap(c.o, city.c_str(), dim, 0, 0, beside ? c.w - 58 : 0)) - lineH(12, true);
  dy = beside ? 0 : 16 + ex;
  return ex;
}

// The heading row of the small and medium looks: the city on the left and, on the right, the first of vals (each
// shorter than the one before; px bold at y) that leaves the city one line; none does, the city alone on the row; too
// long for that too, the city wrapped beside the last value. Returns the heading's height past one line (0 mostly):
// what is under it moves down by that much and leaves out what then no longer fits.
int cityRow(Card c, const String& city, std::initializer_list<String> vals, int y, int px, uint32_t fg, uint32_t dim) {
  const String* last = nullptr;
  for (const String& v : vals) {
    last = &v;
    const int room = c.w - textW(v.c_str(), px, true) - 4;
    if (!capFits(city.c_str(), room)) continue;
    cap(c.o, city.c_str(), dim, 0, 0, room);
    right(c.o, v, y, px, true, fg);
    return 0;
  }
  if (capFits(city.c_str(), c.w) || !last) return heightOf(cap(c.o, city.c_str(), dim)) - lineH(12, true);
  right(c.o, *last, y, px, true, fg);
  return heightOf(cap(c.o, city.c_str(), dim, 0, 0, c.w - textW(last->c_str(), px, true) - 4)) - lineH(12, true);
}

// The header of the large looks: city, temperature, conditions and the big icon (as "now" draws it). Returns how
// far a wrapped city moved it down (0 mostly).
int header(Card c, JsonObjectConst w, bool atNight, uint32_t fg, uint32_t dim) {
  int dy;
  const int ex = cityOverIcon(c, data::cityShort(w["city"] | ""), dim, dy);
  at(c.o, String((int)(w["temp"] | 0)) + "°", -2, 16 + ex, 56, true, fg);
  const String line = String(w["desc"] | "") + " · " + (int)(w["min"] | 0) + "–" + (int)(w["max"] | 0) + "°";
  fit(c.o, line, 0, 76 + ex, c.w - 70, 18, 13, 12, false, dim);
  lv_obj_t* big = img(c.o, data::wxIcon(w["icon"] | "cloud", 56, atNight));
  lv_obj_align(big, LV_ALIGN_TOP_RIGHT, 2, 6 + dy);
  return ex;
}

// One stat: glyph, name, value, note (app_weather's tiles), in a translucent box at (x, y) of w x h.
void tile(Card c, int x, int y, int w, int h, const lv_image_dsc_t* g, const char* name, const String& value,
          const String& note, uint32_t fg, uint32_t dim) {
  lv_obj_t* b = box(c.o, SKIN.own ? 0xffffff : fg, 12);
  lv_obj_set_style_bg_opa(b, SKIN.own ? 46 : 26, 0);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  lv_obj_t* i = glyph(c.o, g, dim);
  lv_obj_set_pos(i, x + 7, y + 6);
  if (w < 120) {  // narrow: the value beside the glyph, the name under it
    at(c.o, value, x + 27, y + 3, 18, true, fg);
    fit(c.o, name, x + 8, y + 25, w - 16, 16, 12, 12, false, dim);
    return;
  }
  lv_obj_t* n = label(c.o, "", 12, true, dim);
  fitText(n, upperVi(i18n::trl(name)).c_str(), w - 30, 16, 12, 12, true);
  lv_obj_set_pos(n, x + 27, y + 7);
  at(c.o, value, x + 8, y + 24, h >= 54 ? 20 : 18, true, fg);
  if (note.length()) {
    lv_obj_t* t = label(c.o, note.c_str(), 12, false, dim);
    lv_obj_align(t, LV_ALIGN_TOP_RIGHT, x + w - c.w - 8, y + h - 20);
  }
}

// The stats a detail look lists, in order (those the dashboard has).
struct Stat {
  const lv_image_dsc_t* g;
  const char* name;  // English (marked): compared untranslated, shown through helpers or T_
  String value, note;
};
std::vector<Stat> stats(JsonObjectConst w) {
  std::vector<Stat> out;
  if (!w["feels"].isNull()) out.push_back({&gl_sun_16, N_("Feels like"), String((int)(w["feels"] | 0)) + "°", ""});
  if (!w["hum"].isNull()) out.push_back({&gl_drop_16, N_("Humidity"), String((int)(w["hum"] | 0)) + "%", ""});
  if (!w["wind"].isNull()) out.push_back({&gl_wind_16, N_("Wind"), String((int)(w["wind"] | 0)), "km/h"});
  if (!w["uv"].isNull()) out.push_back({&gl_uv_16, N_("UV index"), String((int)(w["uv"] | 0)), T_(uvText(w["uv"] | 0))});
  if (!w["aqi"].isNull())
    out.push_back({&gl_leaf_16, N_("Air quality"), String((int)(w["aqi"] | 0)), T_(aqiText(w["aqi"] | 0))});
  if (!w["rain"].isNull()) out.push_back({&gl_drop_16, N_("Rain"), String((int)(w["rain"] | 0)) + "%", T_("today")});
  return out;
}

// The sun's path from rise to set: a half circle of radius r around (cx, cy), the part already walked in
// yellow, the sun on it (below the line at night: before rise or after set).
void sunArc(Card c, int cx, int cy, int r, int rise, int set, int now, uint32_t track) {
  const int wd = r >= 90 ? 8 : 6;
  lv_obj_t* a = lv_arc_create(c.o);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(a, 2 * r, 2 * r);
  lv_obj_set_pos(a, cx - r, cy - r);
  lv_arc_set_bg_angles(a, 180, 360);
  const float k = set > rise ? (float)(now - rise) / (set - rise) : 0;
  const float f = k < 0 ? 0 : k > 1 ? 1 : k;
  lv_arc_set_angles(a, 180, 180 + (int)(f * 180));
  lv_obj_set_style_arc_width(a, wd, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, wd, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C(track), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, 90, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, C(0xfbbf24), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, 0);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
  if (k < 0 || k > 1) return;  // night: no sun on the path
  const float rad = (180 + f * 180) * PI / 180, rc = r - wd / 2.0f;
  lv_obj_t* sun = box(c.o, 0xfbbf24, 9);
  lv_obj_set_size(sun, 18, 18);
  lv_obj_set_style_border_width(sun, 3, 0);
  lv_obj_set_style_border_color(sun, C(0xffffff), 0);
  lv_obj_set_pos(sun, cx + (int)lroundf(cosf(rad) * rc) - 9, cy + (int)lroundf(sinf(rad) * rc) - 9);
}

}  // namespace

// ---------------------------------------------------------------- weather
void wWeather(lv_obj_t* parent, const Item& it, Card c) {
  JsonObjectConst w = data::weather();
  const bool atNight = data::night();
  uint32_t top, bottom;
  data::sky(w["icon"] | "cloud", atNight, top, bottom);
  if (SKIN.own) grad(c.o, top, bottom);
  const uint32_t fg = onOwn(WHITE), dim = onOwnDim(0xdfe6ff);
  if (!data::hasWeather()) {
    cap(c.o, N_("Weather"), dim);
    empty(c, noData(), dim, 22);
    return;
  }
  const String st = styleOf(it);
  const String city = data::cityShort(w["city"] | "");
  const int temp = w["temp"] | 0;
  const String rng = String((int)(w["min"] | 0)) + "–" + (int)(w["max"] | 0) + "°";
  JsonArrayConst hours = w["hours"];
  JsonArrayConst days = w["days"];
  if (st == "big") {
    // A poster: the picture, the temperature as big as fits, the sky (and the place, the day's range).
    const bool sm = it.size == S, md = it.size == M;
    const int ip = sm ? 40 : 80;
    lv_obj_t* ic = img(c.o, data::wxIcon(w["icon"] | "cloud", ip, atNight));
    lv_obj_set_pos(ic, md ? 0 : (c.w - ip) / 2, md ? (c.h - ip) / 2 : sm ? -4 : 0);
    const String t = String(temp) + "°", where = city + " · " + rng;
    const PLine lines[3] = {{t.c_str(), 96, true, fg}, {w["desc"] | "", sm ? 14 : 18, true, fg}, {where.c_str(), 13, false, dim}};
    if (md) poster(c.o, lines, 3, ip + 8, 0, c.w - ip - 8, c.h);
    else poster(c.o, lines, sm ? 2 : 3, 0, sm ? ip - 6 : ip, c.w, c.h - (sm ? ip - 6 : ip));
    return;
  }

  // ---- by the hour
  if (st == "hourly") {
    if (it.size == S) {
      const int ex = cityRow(c, city, {String(temp) + "°"}, -2, 14, fg, dim);
      for (int i = 0; i < (int)hours.size() && i < 3; i++) {
        JsonObjectConst h = hours[i];
        const int y = 26 + ex + i * 31;
        if (y + 20 > c.h) break;
        // i18n: the first hour of an hourly forecast (vi "Giờ tới"), at most ~55 px at 13 px bold
        fit(c.o, i == 0 ? TC_("hourly", "Next") : (h["h"] | ""), 0, y + 2, 56, 18, 13, 12, true, fg);
        lv_obj_t* ic = img(c.o, data::wxIcon(h["icon"] | "cloud", 20, data::hourIsNight(h["h"] | "12")));
        lv_obj_set_pos(ic, 58, y);
        right(c.o, String((int)(h["t"] | 0)) + "°", y + 2, 14, true, fg);
      }
      return;
    }
    if (it.size == M) {
      const int ex = cityRow(c, city, {String(temp) + "° · " + (const char*)(w["desc"] | ""), String(temp) + "°"}, -1, 13, fg,
                             dim);
      const int n = min<int>(hours.size(), 6);
      for (int i = 0; i < n; i++) {
        JsonObjectConst h = hours[i];
        lv_obj_t* col_ = clear(c.o);
        lv_obj_set_size(col_, c.w / n, 98);
        lv_obj_set_pos(col_, i * (c.w / n), 22 + ex);
        lv_obj_t* a = label(col_, i == 0 ? N_("Now") : (h["h"] | ""), 12, true, fg);
        lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_t* ic = img(col_, data::wxIcon(h["icon"] | "cloud", 28, data::hourIsNight(h["h"] | "12")));
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);
        lv_obj_t* b = label(col_, (String((int)(h["t"] | 0)) + "°").c_str(), 14, true, fg);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 50);
        if (!h["rain"].isNull() && (int)(h["rain"] | 0) >= 30 && 22 + ex + 70 + lineH(12, true) <= c.h) {
          lv_obj_t* r = label(col_, (String((int)(h["rain"] | 0)) + "%").c_str(), 12, true, SKIN.own ? 0x93c5fd : dim);
          lv_obj_align(r, LV_ALIGN_TOP_MID, 0, 70);
        }
      }
      return;
    }
    const int ex = header(c, w, atNight, fg, dim);
    const int n = min<int>(hours.size(), min(8, (c.h + 2 - 100 - ex) / 21));  // the last row 2 px into the padding
    for (int i = 0; i < n; i++) {
      JsonObjectConst h = hours[i];
      const int y = 100 + ex + i * 21;
      at(c.o, i == 0 ? N_("Now") : (h["h"] | ""), 0, y, 13, true, fg);
      lv_obj_t* ic = img(c.o, data::wxIcon(h["icon"] | "cloud", 20, data::hourIsNight(h["h"] | "12")));
      lv_obj_set_pos(ic, 64, y - 2);
      if (!h["rain"].isNull() && (int)(h["rain"] | 0) >= 30)
        at(c.o, i18n::fmt(N_("rain {0}%"), String((int)(h["rain"] | 0))), 96, y + 1, 12, false, SKIN.own ? 0x93c5fd : dim);
      right(c.o, String((int)(h["t"] | 0)) + "°", y, 14, true, fg);
    }
    return;
  }

  // ---- the next days
  if (st == "days") {
    if (it.size != L) {
      const int ex = it.size == M ? cityRow(c, city, {String(temp) + "° · " + rng, String(temp) + "°"}, -1, 13, fg, dim)
                                  : heightOf(cap(c.o, city.c_str(), dim)) - lineH(12, true);
      const int n = min<int>(days.size(), it.size == S ? 3 : 5);
      for (int i = 0; i < n; i++) {
        JsonObjectConst d = days[i];
        lv_obj_t* col_ = clear(c.o);
        lv_obj_set_size(col_, c.w / n, 98);
        lv_obj_set_pos(col_, i * (c.w / n), 22 + ex);
        // i18n: short (vi "Nay")
        lv_obj_t* a = label(col_, data::dayName(d, it.size == S).c_str(), 12, true, fg);
        lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_t* ic = img(col_, data::wxIcon(d["icon"] | "cloud", 28));
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);
        if (22 + ex + 50 + lineH(14, true) > c.h) continue;  // under a wrapped city: what still fits
        lv_obj_t* b = label(col_, (String((int)(d["max"] | 0)) + "°").c_str(), 14, true, fg);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 50);
        if (22 + ex + 70 + lineH(12, false) > c.h) continue;
        lv_obj_t* m = label(col_, (String((int)(d["min"] | 0)) + "°").c_str(), 12, false, dim);
        lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 70);
      }
      return;
    }
    const int ex = header(c, w, atNight, fg, dim);
    int lo = 99, hi = -99;
    for (JsonObjectConst d : days) lo = min(lo, (int)(d["min"] | 0)), hi = max(hi, (int)(d["max"] | 0));
    const int rows_ = min<int>(days.size(), min(6, (c.h - 120 - ex) / 28 + 1));
    for (int i = 0; i < rows_; i++) {
      JsonObjectConst d = days[i];
      const int ry = 100 + ex + i * 28;
      at(c.o, data::dayName(d), 0, ry + 2, 13, true, fg);
      lv_obj_t* ic = img(c.o, data::wxIcon(d["icon"] | "cloud", 20));
      lv_obj_set_pos(ic, 72, ry);
      const int mn = d["min"] | 0, mx = d["max"] | 0;
      at(c.o, String(mn) + "°", 102, ry + 2, 13, false, dim);
      lv_obj_t* tr = box(c.o, SKIN.own ? 0xffffff : fg, 2);
      lv_obj_set_style_bg_opa(tr, 50, 0);
      const int bx = 136, bw = c.w - bx - 36;
      lv_obj_set_size(tr, bw, 4);
      lv_obj_set_pos(tr, bx, ry + 9);
      lv_obj_t* fill = box(c.o, 0xffd65a, 2);
      const int sp = max(1, hi - lo);
      grad(fill, 0x7dd3fc, 0xfbbf24, LV_GRAD_DIR_HOR);
      lv_obj_set_size(fill, max(6, (mx - mn) * bw / sp), 4);
      lv_obj_set_pos(fill, bx + (mn - lo) * bw / sp, ry + 9);
      right(c.o, String(mx) + "°", ry + 2, 13, true, fg);
    }
    return;
  }

  // ---- feels, humidity, wind, UV, air
  if (st == "detail") {
    const auto sts = stats(w);
    if (it.size == S) {
      const int ex = cityRow(c, city, {String(temp) + "°"}, -2, 14, fg, dim);
      for (int i = 0; i < (int)sts.size() && i < 4; i++) {
        const int y = 24 + ex + i * 24;
        if (y + 18 > c.h) break;
        lv_obj_t* g = glyph(c.o, sts[i].g, dim);
        lv_obj_set_pos(g, 0, y + 1);
        String v = String(T_(sts[i].name)) + " " + sts[i].value;
        if (sts[i].note.length() && sts[i].note != T_("today")) v += " " + sts[i].note;
        fit(c.o, v, 22, y, c.w - 22, 18, 13, 12, false, fg);
      }
      return;
    }
    const bool lg = it.size == L;
    int ex = 0;
    if (lg) ex = header(c, w, atNight, fg, dim);
    else ex = cityRow(c, city, {String(temp) + "° · " + (const char*)(w["desc"] | ""), String(temp) + "°"}, -1, 13, fg, dim);
    const int cols_ = lg ? 2 : 3, y0 = (lg ? 100 : 22) + ex, gap = 8;
    const int tw = (c.w - gap * (cols_ - 1)) / cols_, th = lg ? 50 : 44;
    for (int i = 0; i < (int)sts.size() && i < 6 && y0 + (i / cols_) * (th + gap) + th <= c.h; i++)
      tile(c, (i % cols_) * (tw + gap), y0 + (i / cols_) * (th + gap), tw, th, sts[i].g, sts[i].name, sts[i].value,
           lg ? sts[i].note : String(""), fg, dim);
    return;
  }

  // ---- the sun's day
  if (st == "sun") {
    const String sr = w["sunrise"] | "", ss = w["sunset"] | "";
    const int rise = minutesOf(sr), set = minutesOf(ss), now = data::nowMinutes();
    cap(c.o, N_("Sun"), dim);
    if (rise < 0 || set < 0) return empty(c, noData(), dim, 22);
    const String len = i18n::fmt(N_("Day length {0}"), span(set - rise));
    const String left = now < rise ? i18n::fmt(N_("Sunrise in {0}"), span(rise - now))
                        : now < set ? i18n::fmt(N_("Sunset in {0}"), span(set - now))
                                    : String(T_("The sun has set"));
    if (it.size == S) {
      lv_obj_t* g = glyph(c.o, &gl_sunrise_16, dim);
      lv_obj_set_pos(g, 0, 26);
      at(c.o, sr, 22, 20, 24, true, fg);
      at(c.o, i18n::fmt(N_("Sets {0}"), ss), 0, 54, 15, true, fg);
      fit(c.o, now < set && now >= rise ? left : len, 0, 78, c.w, 40, 13, 12, false, dim);
      return;
    }
    const bool lg = it.size == L;
    const int r = lg ? 90 : 76, cx = c.w / 2, cy = lg ? 132 : c.h - 6;  // the path clear of the times under its ends
    sunArc(c, cx, cy, r, rise, set, now, SKIN.own ? 0xffffff : fg);
    at(c.o, NC_("sun", "Sunrise"), 0, cy - 34, 12, false, dim);
    at(c.o, sr, 0, cy - 18, 16, true, fg);
    right(c.o, NC_("sun", "Sunset"), cy - 34, 12, false, dim);
    right(c.o, ss, cy - 18, 16, true, fg);
    lv_obj_t* mid = label(c.o, "", 13, false, fg);
    fitText(mid, left.c_str(), 2 * r - 60, 36, 13, 12);
    lv_obj_set_style_text_align(mid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(mid, LV_ALIGN_TOP_MID, 0, cy - (lg ? 36 : 40));  // low in the arch, clear of the sun
    if (!lg) return;
    int y = cy + 14;
    at(c.o, len, 0, y, 14, true, fg);
    y += 26;
    for (const Stat& s : stats(w)) {
      if (y + 20 > c.h) break;
      if (strcmp(s.name, "UV index") && strcmp(s.name, "Air quality")) continue;  // the English names (untranslated)
      lv_obj_t* g = glyph(c.o, s.g, dim);
      lv_obj_set_pos(g, 0, y + 1);
      at(c.o, String(T_(s.name)) + " " + s.value + (s.note.length() ? " · " + s.note : String("")), 22, y, 13, false, fg);
      y += 24;
    }
    return;
  }

  // ---- now (the default)
  if (it.size == S) {
    // The city on one line beside the icon (cap() closing its letters up); too long, on the whole row with the icon
    // moved down beside the temperature; longer still, wrapped on the whole row with the temperature under it, as big
    // as clears the conditions (then without the high/low line, then without the conditions). Never over each other.
    // The conditions take one line over the high/low, or (too long) two lines in their place.
    const bool beside = capFits(city.c_str(), c.w - 40);
    const int ty = heightOf(cap(c.o, city.c_str(), dim, 0, 0, beside ? c.w - 40 : c.w)) - 1;  // 18 under one line
    const String desc = w["desc"] | "";
    const int dh = measure(c.o, desc, c.w, 21, 14, 12, true) >= 0 ? 20 : 38;  // the conditions' box: one line or two
    int px = 0, foot = dh == 20 ? 2 : 1;  // foot: what stays under the temperature (2 the conditions and the high/low)
    while (!px) {
      const int top = foot == 2 ? c.h - 38 : foot == 1 ? c.h - dh : c.h;
      for (int p : {44, 34, 28})
        if (!px && ty + lineH(p, true) <= top) px = p;
      if (!px && !foot) px = 28;  // nothing clears it (a tiny card): the temperature alone
      else if (!px) foot--;
    }
    at(c.o, String(temp) + "°", -2, ty, px, true, fg);
    lv_obj_t* i = img(c.o, data::wxIcon(w["icon"] | "cloud", 40, atNight));
    lv_obj_align(i, LV_ALIGN_TOP_RIGHT, 4, beside ? -4 : ty + (lineH(px, true) - 40) / 2);
    if (foot) fit(c.o, desc, 0, foot == 2 ? c.h - 38 : c.h - dh, c.w, dh, 14, 12, true, fg);
    // i18n: the day's high and low (vi "C {0}° T {1}°")
    if (foot == 2)
      at(c.o, i18n::fmt(N_("H {0}° L {1}°"), String((int)(w["max"] | 0)), String((int)(w["min"] | 0))), 0, c.h - 17, 13,
         false, dim);
    return;
  }
  // Now: city, temperature, conditions; big icon on the right. Under a wrapped city the medium look keeps the
  // temperature and the hours (no room for the conditions line and the icon).
  int idy;
  const int ex = cityOverIcon(c, city, dim, idy);
  at(c.o, String(temp) + "°", -2, 16 + ex, it.size == L ? 56 : 34, true, fg);
  if (it.size == L || !ex) {
    fit(c.o, String(w["desc"] | "") + " · " + rng, 0, (it.size == L ? 76 : 54) + ex, c.w - 70, 18, 13, 12, false, dim);
    lv_obj_t* big = img(c.o, data::wxIcon(w["icon"] | "cloud", 56, atNight));
    lv_obj_align(big, LV_ALIGN_TOP_RIGHT, 2, (it.size == L ? 6 : -4) + idy);
  }
  // Hours.
  const int n = min<int>(hours.size(), 5);
  const int hy = it.size == L ? 104 + ex : c.h - 46;
  if (n) {
    const int colW = c.w / n;
    for (int i = 0; i < n; i++) {
      JsonObjectConst h = hours[i];
      lv_obj_t* col_ = clear(c.o);
      lv_obj_set_size(col_, colW, 50);
      lv_obj_set_pos(col_, i * colW, hy);
      lv_obj_t* a = label(col_, i == 0 && it.size == L ? N_("Now") : (h["h"] | ""), 12, false, dim);
      lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 0);
      lv_obj_t* ic = img(col_, data::wxIcon(h["icon"] | "cloud", 20, data::hourIsNight(h["h"] | "12")));
      lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 15);
      lv_obj_t* b = label(col_, (String((int)(h["t"] | 0)) + "°").c_str(), 13, true, fg);
      lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 35);
    }
  }
  if (it.size != L) return;
  // Days.
  int y = hy + 60;
  if (days.isNull() || !days.size()) {
    if (!w["hum"].isNull()) at(c.o, i18n::fmt(N_("Humidity {0}%"), String((int)(w["hum"] | 0))), 0, y, 13, false, dim);
    return;
  }
  int lo = 99, hi = -99;
  for (JsonObjectConst d : days) lo = min(lo, (int)(d["min"] | 0)), hi = max(hi, (int)(d["max"] | 0));
  const int rows_ = min<int>(days.size(), (c.h - y) / 26);
  for (int i = 0; i < rows_; i++) {
    JsonObjectConst d = days[i];
    const int ry = y + i * 26;
    at(c.o, data::dayName(d), 0, ry + 2, 13, true, fg);
    lv_obj_t* ic = img(c.o, data::wxIcon(d["icon"] | "cloud", 20));
    lv_obj_set_pos(ic, 72, ry);
    const int mn = d["min"] | 0, mx = d["max"] | 0;
    at(c.o, String(mn) + "°", 102, ry + 2, 13, false, dim);
    lv_obj_t* tr = box(c.o, SKIN.own ? 0xffffff : fg, 2);
    lv_obj_set_style_bg_opa(tr, 50, 0);
    const int bx = 136, bw = c.w - bx - 36;
    lv_obj_set_size(tr, bw, 4);
    lv_obj_set_pos(tr, bx, ry + 9);
    lv_obj_t* fill = box(c.o, 0xffd65a, 2);
    const int sp = max(1, hi - lo);
    const int fx = bx + (mn - lo) * bw / sp, fw = max(6, (mx - mn) * bw / sp);
    grad(fill, 0x7dd3fc, 0xfbbf24, LV_GRAD_DIR_HOR);
    lv_obj_set_size(fill, fw, 4);
    lv_obj_set_pos(fill, fx, ry + 9);
    right(c.o, String(mx) + "°", ry + 2, 13, true, fg);
  }
}

}  // namespace wid
