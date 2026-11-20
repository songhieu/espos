// The widget families' shared pieces (kit.h): card, labels, colours, live parts and their tick.
#include "kit.h"

#include "../apps.h"
#include "../data.h"
#include "../desk.h"
#include "../fonts.h"
#include "../net.h"
#include "../theme.h"

namespace wid {

using namespace kit;

uint32_t INK = 0x16181d, GREY = 0x6b7280, PAPER = 0xffffff;

Card frame(lv_obj_t* parent, int w, int h, uint32_t bg, int padH, int padV) {
  lv_obj_t* c = box(parent, bg, 20);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_pad_hor(c, padH, 0);
  lv_obj_set_style_pad_ver(c, padV, 0);
  lv_obj_set_style_clip_corner(c, false, 0);
  return {c, w - 2 * padH, h - 2 * padV};
}

void glass(lv_obj_t* c) {
  const theme::Home& h = theme::home();
  lv_obj_set_style_bg_color(c, C(h.glass), 0);
  lv_obj_set_style_bg_opa(c, h.glassOpa, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, C(0xffffff), 0);
  lv_obj_set_style_border_opa(c, h.light ? LV_OPA_80 : 34, 0);
}

lv_obj_t* cap(lv_obj_t* p, const char* text, uint32_t color, int x, int y, int w) {
  const String t = upperVi(i18n::trl(text));
  lv_obj_t* l = label(p, t.c_str(), 12, true, color);
  lv_obj_set_pos(l, x, y);
  // Wider than its room (a longer language, a long city): the letters close up, then the heading wraps (never cut).
  if (!w) lv_obj_update_layout(p);
  const int room = w ? w : lv_obj_get_content_width(p) - x;
  const lv_font_t* f = fonts::get(12, true);
  int ls = 1;
  while (ls > -1 && lv_text_get_width(t.c_str(), t.length(), f, ls) > room) ls--;
  lv_obj_set_style_text_letter_space(l, ls, 0);
  if (room > 20 && lv_text_get_width(t.c_str(), t.length(), f, ls) > room) wrapText(l, t.c_str(), room, 12, true, 0);
  return l;
}

bool capFits(const char* text, int w) {
  const String t = upperVi(i18n::trl(text));
  return lv_text_get_width(t.c_str(), t.length(), fonts::get(12, true), -1) <= w;
}

lv_obj_t* fit(lv_obj_t* p, const char* text, int x, int y, int w, int h, int maxPx, int minPx, bool bold, uint32_t color,
              int ls) {
  lv_obj_t* l = label(p, "", maxPx, bold, color);
  fitText(l, text, w, h, maxPx, minPx, bold, ls);
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t* at(lv_obj_t* p, const char* text, int x, int y, int px, bool bold, uint32_t color) {
  lv_obj_t* l = label(p, text, px, bold, color);
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t* right(lv_obj_t* p, const char* text, int y, int px, bool bold, uint32_t color, int xo) {
  lv_obj_t* l = label(p, text, px, bold, color);
  lv_obj_align(l, LV_ALIGN_TOP_RIGHT, xo, y);
  return l;
}

int heightOf(lv_obj_t* l) {
  int lines = 1;
  for (const char* q = lv_label_get_text(l); *q; q++) lines += *q == '\n';
  return lines * lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)) +
         (lines - 1) * lv_obj_get_style_text_line_space(l, 0);
}

lv_obj_t* fitIn(lv_obj_t* p, const char* text, int x, int y, int w, int h, int maxPx, int minPx, bool bold,
                uint32_t color, int ls) {
  lv_obj_t* l = fit(p, text, x, y, w, h, maxPx, minPx, bold, color, ls);
  if (heightOf(l) <= h) return l;
  lv_obj_delete(l);
  return nullptr;
}

int measure(lv_obj_t* p, const char* text, int w, int h, int maxPx, int minPx, bool bold, int ls) {
  lv_obj_t* l = fit(p, text, 0, 0, w, h, maxPx, minPx, bold, 0, ls);
  const int used = heightOf(l);
  lv_obj_delete(l);
  return used <= h ? used : -1;
}

#pragma GCC push_options
#pragma GCC optimize("Os")  // posters are built when a page is made
namespace {
const uint8_t BOLD_SZ[] = {96, 64, 56, 44, 34, 28, 24, 22, 20, 18, 17, 16, 15, 14, 13, 12};
const uint8_t REG_SZ[] = {96, 84, 64, 40, 20, 18, 16, 15, 14, 13, 12};
}  // namespace

int posterPx(const char* s, int px, bool bold, int w) {
  s = i18n::trl(s);
  if (!*s) return 0;  // nothing to show: no room taken
  bool dg = true;  // only what the digit fonts have: space % + , - . / 0-9 : ° −
  for (const char* q = s; *q && dg; q++) {
    const uint8_t c = *q;
    if (c == 0xc2 && (uint8_t)q[1] == 0xb0) q++;                                    // °
    else if (c == 0xe2 && (uint8_t)q[1] == 0x88 && (uint8_t)q[2] == 0x92) q += 2;  // −
    else dg = c == ' ' || c == '%' || c == '+' || (c >= ',' && c <= ':');
  }
  const uint8_t* sz = bold ? BOLD_SZ : REG_SZ;
  const int n = bold ? sizeof BOLD_SZ : sizeof REG_SZ;
  for (int i = 0; i < n; i++)
    if (sz[i] <= px && (dg || sz[i] <= 24) && textW(s, sz[i], bold) <= w) return sz[i];
  return 0;
}

void poster(lv_obj_t* p, const PLine* lines, int n, int x, int y, int w, int h, int gap) {
  int px[8];
  n = min(n, 8);
  for (int i = 0; i < n; i++) px[i] = posterPx(lines[i].s, lines[i].px, lines[i].bold, w);
  auto total = [&] {
    int t = -gap;
    for (int i = 0; i < n; i++) t += px[i] ? lineH(px[i], lines[i].bold) + gap : 0;
    return t;
  };
  while (total() > h) {
    int bi = -1;
    for (int i = 0; i < n; i++)
      if (px[i] > 12 && (bi < 0 || px[i] > px[bi])) bi = i;
    if (bi < 0) break;
    px[bi] = posterPx(lines[bi].s, px[bi] - 1, lines[bi].bold, w);
  }
  int yy = y + (h - total()) / 2;
  for (int i = 0; i < n; i++) {
    if (!px[i]) continue;
    at(p, lines[i].s, x + (w - textW(lines[i].s, px[i], lines[i].bold)) / 2, yy, px[i], lines[i].bold, lines[i].color);
    yy += lineH(px[i], lines[i].bold) + gap;
  }
}
#pragma GCC pop_options

const char* noData() {
  return desk::link() == desk::Link::Linked ? N_("Waiting for the next sync") : N_("Link EspOS to get data");
}

bool firstDataComing() {
  return desk::link() == desk::Link::Linked && desk::lastSyncMs() == 0 && net::savedCount() > 0 && millis() < 120000;
}

// No dashboard yet and the first sync after power-on on its way (linked, a network saved, none done yet): grey bars
// stand where the data will come, instead of words about waiting or "nothing today" (0.5.7). Two minutes without it:
// the words again (os.cpp rebuilds Home then).
bool skeleton(lv_obj_t* p, int x, int y, int w, int h, uint32_t color) {
  if (!desk::dash().isNull() || !firstDataComing()) return false;
  static const uint8_t PCT[] = {45, 85, 65};
  for (int i = 0, yy = y; i < 3 && yy + 10 <= y + h; i++) {
    const int bh = i || h < 40 ? 10 : 16;
    lv_obj_t* b = lv_obj_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, yy);
    lv_obj_set_size(b, w * PCT[i] / 100, bh);
    lv_obj_set_style_radius(b, bh / 2, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, i ? LV_OPA_20 : LV_OPA_30, 0);
    yy += bh + 10;
  }
  return true;
}

void empty(Card c, const char* text, uint32_t color, int y) {
  if (skeleton(c.o, 0, y, c.w, c.h - y, color)) return;
  fit(c.o, text, 0, y, c.w, c.h - y, 14, 12, false, color, 3);
}

// ---------------------------------------------------------------- skins
Skin SKIN;

Skin skinOf(const String& id) {
  Skin s;
  if (!id.length()) return s;
  const theme::Home& th = theme::home();
  s.own = false;
  if (id == "dark") s.bg = 0x1c2029, s.fg = 0xeef1f5, s.dim = 0x9aa3b2;
  else if (id == "light") s.bg = 0xffffff, s.fg = 0x16181d, s.dim = 0x6b7280;
  else if (id == "glass" || id == "none") {
    s.none = id == "none", s.glass = id == "glass";
    s.fg = th.fg, s.dim = th.light ? 0x4b5160 : 0xd9dcea;
  }
  else if (id.startsWith("c:") && id.length() == 8) {
    s.bg = strtoul(id.c_str() + 2, nullptr, 16) & 0xffffff;
    const int r = s.bg >> 16, g = (s.bg >> 8) & 255, b = s.bg & 255;
    const bool light = r * 299 + g * 587 + b * 114 > 160000;  // dark text on a light colour
    s.fg = light ? 0x16181d : 0xffffff;
    s.dim = light ? 0x4b5160 : 0xe8eaef;
  } else s.own = true;  // unknown: the kind's own look
  return s;
}

void paintSkin(Card c, const Skin& s) {
  if (s.own) return;
  if (s.none) {
    lv_obj_set_style_bg_opa(c.o, LV_OPA_TRANSP, 0);
    return;
  }
  if (s.glass) {
    glass(c.o);
    return;
  }
  lv_obj_set_style_bg_color(c.o, C(s.bg), 0);
  lv_obj_set_style_bg_opa(c.o, LV_OPA_COVER, 0);
}

// ---------------------------------------------------------------- live parts
namespace {
struct Live {
  lv_obj_t* obj;
  LiveFn fn;
  bool everySecond;
  // The card look and ink of the widget being drawn when the part was made: restored around fn, since later
  // widgets change them (a clock redrawn at the next minute keeps its own skin).
  Skin skin;
  uint32_t ink, grey, paper;
};
std::vector<Live> lives;

void run(const Live& x, const tm& l, bool newDay) {
  const Skin s = SKIN;
  const uint32_t i = INK, g = GREY, p = PAPER;
  SKIN = x.skin, INK = x.ink, GREY = x.grey, PAPER = x.paper;
  x.fn(x.obj, l, newDay);
  SKIN = s, INK = i, GREY = g, PAPER = p;
}
}  // namespace

void live(lv_obj_t* o, LiveFn fn, bool everySecond) {
  lives.push_back({o, fn, everySecond, SKIN, INK, GREY, PAPER});
  lv_obj_add_event_cb(o, [](lv_event_t* e) {
    lv_obj_t* t = (lv_obj_t*)lv_event_get_current_target(e);
    for (auto it = lives.begin(); it != lives.end(); ++it)
      if (it->obj == t) {
        lives.erase(it);
        break;
      }
  }, LV_EVENT_DELETE, nullptr);
}

String hmNow() {
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  return hhmm(l.tm_hour, l.tm_min);
}

String dateLine(bool withLunar) {
  const data::Day d = data::today();
  String s = i18n::date(N_("{w}, {M} {d}"), d.d, d.m, d.y, d.wday);
  if (withLunar) s += " · " + i18n::fmt(N_("{0} (lunar)"), data::lunarText(data::lunar(d)));
  return s;
}

bool tick() {
  static int lastMin = -1, lastDay = -1, lastSec = -1;
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  // The seconds parts (hands, digits) move their own objects: Home is not told, so its idle page pictures are not
  // retaken every second (one a second stale is fine for a swipe). A new minute is a change.
  if (l.tm_sec != lastSec) {
    lastSec = l.tm_sec;
    for (const Live& x : lives)
      if (x.everySecond) run(x, l, false);
  }
  if (l.tm_min == lastMin) return false;
  lastMin = l.tm_min;
  const bool newDay = l.tm_yday != lastDay;
  for (const Live& x : lives)
    if (!x.everySecond) run(x, l, newDay);
  if (newDay) {
    lastDay = l.tm_yday;
    apps::refreshIcons();
  }
  return !lives.empty();
}

}  // namespace wid
