// Small built-in apps: Photos, Lunar (the lunar calendar), Calculator and Store (the app store: packages from the
// catalog, mockups/s3-store.html).
#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>

#include "apps.h"
#include "art.h"
#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "os.h"
#include "photo.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "pkg/ui.h"
#include <ArduinoJson.h>
#include "uikit.h"

// ====================================================================== Photos
namespace app_photos {
namespace {

using namespace kit;

struct Thumb {
  lv_image_dsc_t dsc;
  uint16_t* px;
};

// A w x h copy of a full-screen picture, averaged down (PSRAM).
Thumb* shrink(const lv_image_dsc_t* src, int w, int h) {
  Thumb* t = new Thumb();
  t->px = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  if (!t->px) {
    delete t;
    return nullptr;
  }
  const int sw = src->header.w, sh = src->header.h;
  const uint16_t* s = (const uint16_t*)src->data;
  const float k = min((float)sw / w, (float)sh / h);
  const float x0 = (sw - w * k) / 2, y0 = (sh - h * k) / 2;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint32_t r = 0, g = 0, b = 0, n = 0;
      const int sx0 = x0 + x * k, sy0 = y0 + y * k, sx1 = max(sx0 + 1, (int)(x0 + (x + 1) * k)),
                sy1 = max(sy0 + 1, (int)(y0 + (y + 1) * k));
      for (int yy = sy0; yy < sy1; yy += 2)
        for (int xx = sx0; xx < sx1; xx += 2) {
          const uint16_t v = s[yy * sw + xx];
          r += v >> 11, g += (v >> 5) & 63, b += v & 31, n++;
        }
      t->px[y * w + x] = ((r / n) << 11) | ((g / n) << 5) | (b / n);
    }
  t->dsc = {};
  t->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  t->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  t->dsc.header.w = w;
  t->dsc.header.h = h;
  t->dsc.header.stride = w * 2;
  t->dsc.data_size = w * h * 2;
  t->dsc.data = (const uint8_t*)t->px;
  return t;
}

void viewer(int start) {
  lv_obj_t* body;
  lv_obj_t* s = os::screen("", &body, os::BARE);
  lv_obj_set_style_bg_color(s, C(0x000000), 0);
  const int n = photo::screenCount(os::W(), os::H());
  lv_obj_t* tv = lv_tileview_create(s);
  lv_obj_set_size(tv, os::W(), os::H());
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < n; i++) {
    lv_obj_t* t = lv_tileview_add_tile(tv, i, 0, (lv_dir_t)((i ? LV_DIR_LEFT : 0) | (i < n - 1 ? LV_DIR_RIGHT : 0)));
    lv_obj_set_user_data(t, (void*)(intptr_t)i);
  }
  // Decoded pictures are big: only the one in view is attached.
  auto show = [](lv_obj_t* tv) {
    lv_obj_t* act = lv_tileview_get_tile_active(tv);
    for (uint32_t i = 0; i < lv_obj_get_child_count(tv); i++) {
      lv_obj_t* t = lv_obj_get_child(tv, i);
      if (t == act && !lv_obj_get_child_count(t)) {
        const lv_image_dsc_t* p = photo::screen((int)(intptr_t)lv_obj_get_user_data(t), os::W(), os::H());
        if (p) lv_obj_center(img(t, p));
      } else if (t != act) {
        lv_obj_clean(t);
      }
    }
  };
  lv_obj_add_event_cb(tv, [](lv_event_t* e) {
    lv_obj_t* tv = (lv_obj_t*)lv_event_get_current_target(e);
    lv_obj_t* act = lv_tileview_get_tile_active(tv);
    for (uint32_t i = 0; i < lv_obj_get_child_count(tv); i++) {
      lv_obj_t* t = lv_obj_get_child(tv, i);
      if (t == act && !lv_obj_get_child_count(t)) {
        const lv_image_dsc_t* p = photo::screen((int)(intptr_t)lv_obj_get_user_data(t), os::W(), os::H());
        if (p) lv_obj_center(img(t, p));
      } else if (t != act) {
        lv_obj_clean(t);
      }
    }
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_tileview_set_tile_by_index(tv, start, 0, LV_ANIM_OFF);
  show(tv);
  lv_obj_t* bk = clear(s);
  lv_obj_set_size(bk, 44, 40);
  lv_obj_set_pos(bk, 0, os::SB_H);
  lv_obj_add_flag(bk, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(glyph(bk, &gl_back_22, 0xffffff));
  lv_obj_add_event_cb(bk, [](lv_event_t*) { os::back(); }, LV_EVENT_CLICKED, nullptr);
  os::open(s);
}

}  // namespace

void open(const char*) {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Photos"), &body, os::ROOT);
  const int n = photo::screenCount(os::W(), os::H());
  if (!n) {
    hint(body, N_("No photos on the device yet. Add them on the EspOS web (Devices, pick the S3, Photos); the device "
                  "downloads them when it syncs. Photos are used by this app, the Photos widget, the \"Your photos\" "
                  "wallpaper and the slideshow saver."));
    os::open(s);
    return;
  }
  lv_obj_t* grid = clear(body);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, 4, 0);
  // Portrait: 3 a row, cut from the portrait pictures. Landscape: 4 a row, from the landscape ones.
  const bool land = os::landscape();
  const int per = land ? 4 : 3, tw = (os::W() - 24 - 4 * (per - 1)) / per, th = land ? tw * 2 / 3 : tw * 3 / 2;
  for (int i = 0; i < n; i++) {
    lv_obj_t* c = box(grid, pal().cell2, 6);
    lv_obj_set_size(c, tw, th);
    lv_obj_set_style_clip_corner(c, true, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, [](lv_event_t* e) { viewer((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
  }
  // Thumbnails one by one, so the screen opens at once.
  struct Job {
    lv_obj_t* grid;
    int next, tw, th;
  };
  Job* j = new Job{grid, 0, tw, th};
  own(grid, j);
  every(grid, 30, [](lv_timer_t* t) {
    Job* j = (Job*)lv_timer_get_user_data(t);
    if (j->next >= (int)lv_obj_get_child_count(j->grid)) return;
    const lv_image_dsc_t* p = photo::screen(j->next, os::W(), os::H());
    lv_obj_t* c = lv_obj_get_child(j->grid, j->next++);
    if (!p) return;
    Thumb* th = shrink(p, j->tw, j->th);
    if (!th) return;
    lv_obj_t* i = img(c, &th->dsc);
    lv_obj_center(i);
    lv_obj_add_event_cb(i, [](lv_event_t* e) {
      Thumb* th = (Thumb*)lv_event_get_user_data(e);
      heap_caps_free(th->px);
      delete th;
    }, LV_EVENT_DELETE, th);
  }, j);
  os::open(s);
}

}  // namespace app_photos

// ====================================================================== Lunar
namespace app_lunar {
namespace {

using namespace kit;

data::Day picked;
lv_obj_t *card, *list;

void fillCard() {
  lv_obj_clean(card);
  const data::Day d = picked;
  const data::Lunar l = data::lunar(d);
  // Lines that fit the card in any language: smaller, then wrapped (never cut).
  lv_obj_update_layout(card);
  const int cw = lv_obj_get_content_width(card);
  lv_obj_t* a = label(card, "", 13, true, 0xffd979);
  fitText(a, upperVi(i18n::date(N_("{W}, {M} {d}, {y}"), d.d, d.m, d.y, d.wday)).c_str(), cw, 18, 13, 12, true, 0);
  char b[12];
  snprintf(b, sizeof b, "%d/%d", l.d, l.m);
  lv_obj_t* big = label(card, b, 64, true, 0xffe9a8);
  lv_obj_set_pos(big, -2, 20);
  // i18n: {0} the lunar year's name ("Fire Horse"), {1} " · leap month" or nothing
  lv_obj_t* y = label(card, "", 16, true, 0xffffff);
  fitText(y, i18n::fmt(N_("Year of the {0}{1}"), data::canChiYear(l.y), l.leap ? T_(" · leap month") : "").c_str(), cw, 22,
          16, 12, true, 0);
  lv_obj_set_pos(y, 0, 96);
  // i18n: the day's and the month's stem-branch names ("Wood Rat day · Fire Horse month")
  lv_obj_t* cc = label(card, "", 14, false, 0xffe2c2);
  fitText(cc, i18n::fmt(N_("{0} day · {1} month"), data::canChiDay(d), data::canChiMonth(l)).c_str(), cw, 36, 14, 12, false,
          0);
  lv_obj_set_pos(cc, 0, 122);
  const String hol = data::holiday(d, l);
  if (hol.length()) {
    lv_obj_update_layout(cc);
    lv_obj_t* h = label(card, "", 14, true, 0xffffff);
    fitText(h, hol.c_str(), cw, 20, 14, 12, true, 0);
    lv_obj_set_pos(h, 0, 122 + lv_obj_get_height(cc) + 4);
  }
  // Steppers to look at other days.
  lv_obj_t* row = clear(card);
  lv_obj_set_size(row, lv_pct(100), 36);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  static const int STEPS[] = {-30, -1, 0, 1, 30};
  static const char* NAMES[] = {"−30", "−1", N_("Today"), "+1", "+30"};
  for (int i = 0; i < 5; i++) {
    lv_obj_t* bt = button(row, NAMES[i], 0x8f1016, 0xffe2c2, [](lv_event_t* e) {
      const int s = (int)(intptr_t)lv_event_get_user_data(e);
      picked = s ? data::addDays(picked, s) : data::today();
      lv_async_call([](void*) { fillCard(); }, nullptr);
    }, (void*)(intptr_t)STEPS[i]);
    lv_obj_set_height(bt, 34);
    lv_obj_set_style_pad_hor(bt, 10, 0);
  }
}

void fillList() {
  const data::Day t = data::today();
  int shown = 0;
  for (int k = 0; k < 120 && shown < 8; k++) {
    const data::Day d = data::addDays(t, k);
    const data::Lunar l = data::lunar(d);
    String what = data::holiday(d, l);
    if (!what.length() && (l.d == 1 || l.d == 15)) what = data::lunarText(l);  // "New moon, month 10" / "Full moon..."
    if (!what.length()) continue;
    const String when = k == 0   ? String(TC_("lower", "today"))
                        : k == 1 ? String(TC_("lower", "tomorrow"))
                                 : i18n::date(N_("{M} {d}"), d.d, d.m) + " · " + i18n::plural(k, NP_("{n} day|{n} days"));
    lv_obj_t* r = listRow(list, what.c_str(), when.c_str(), nullptr);
    (void)r;
    shown++;
  }
  const int tet = data::daysToTet(t);
  // i18n: {0} the coming lunar year's name ("Fire Horse")
  hint(list, (tet ? i18n::fmt(i18n::plural(tet, NP_("Lunar New Year ({0}) in {n} day.|Lunar New Year ({0}) in {n} days.")).c_str(),
                              data::canChiYear(data::lunar(t).y + 1))
                  : String(T_("Today is Lunar New Year's Day."))).c_str());
}

}  // namespace

void open(const char*) {
  picked = data::today();
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Lunar"), &body, os::ROOT);
  // Landscape: the card on the left, the coming dates beside it.
  const bool land = os::landscape();
  if (land) lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  card = box(body, 0xc81e1e, 20);
  grad(card, 0xd7261e, 0x9f1217);
  lv_obj_set_size(card, land ? 236 : lv_pct(100), 220);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_t* right = body;
  if (land) {
    right = stack(body, false, 6);
    lv_obj_set_width(right, os::W() - 24 - 236 - 8);
    lv_obj_set_style_pad_column(body, 8, 0);
  }
  caption(right, N_("Coming up"));
  list = stack(right, false, 6);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (card && lv_obj_get_screen(card) == lv_event_get_current_target(e)) card = list = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillCard();
  fillList();
  os::open(s);
}

}  // namespace app_lunar

// ====================================================================== Calculator
namespace app_calc {
namespace {

using namespace kit;

lv_obj_t* display;
double acc = 0, cur = 0;
char op = 0;
bool typing = false, hasDot = false;
String entry = "0";

String fmt(double v) {
  if (std::isnan(v) || std::isinf(v)) return T_("Error");
  char b[32];
  snprintf(b, sizeof b, "%.10g", v);
  String s = b;
  s.replace(".", ",");
  return s;
}

void show() {
  String shown = entry;  // ',' inside, the language's decimal mark on screen
  if (strcmp(i18n::decSep(), ",")) shown.replace(",", i18n::decSep());
  setText(display, shown.c_str());
  const int w = textW(shown.c_str(), 64, false);
  lv_obj_set_style_text_font(display, fonts::get(w > lv_obj_get_width(lv_obj_get_parent(display)) - 16 ? 40 : 64, false), 0);
}

double apply(double a, double b, char o) {
  switch (o) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0 ? NAN : a / b;
  }
  return b;
}

void key(const char* k) {
  const String s = k;
  if (s == "C") {
    acc = cur = 0, op = 0, typing = false, entry = "0";
  } else if (s == "±") {
    if (entry.startsWith("−")) entry = entry.substring(strlen("−"));
    else if (entry != "0") entry = "−" + entry;
  } else if (s == "%") {
    String e = entry;
    e.replace(",", ".");
    e.replace("−", "-");
    entry = fmt(e.toDouble() / 100);
  } else if (s == "+" || s == "−" || s == "×" || s == "÷" || s == "=") {
    String e = entry;
    e.replace(",", ".");
    e.replace("−", "-");
    const double v = e.toDouble();
    acc = op ? apply(acc, v, op) : v;
    op = s == "+" ? '+' : s == "−" ? '-' : s == "×" ? '*' : s == "÷" ? '/' : 0;
    entry = fmt(acc);
    typing = false;
  } else {
    if (!typing) entry = "", typing = true;
    if (s == ",") {
      if (entry.indexOf(',') >= 0) return;
      if (!entry.length()) entry = "0";
    }
    if (entry == "0" && s != ",") entry = "";
    if (entry.length() < 14) entry += s;
  }
  show();
}

}  // namespace

void open(const char*) {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Calculator"), &body, os::DARK | os::NOSCROLL);
  lv_obj_set_style_bg_color(s, C(0x000000), 0);
  entry = "0", acc = 0, op = 0, typing = false;
  // Portrait: the display over the keys. Landscape: the display on the left, the keys on the right (240 wide).
  const bool land = os::landscape();
  const int bodyH = os::H() - os::SB_H - os::NAV_H - 4 - 24, gap = land ? 6 : 8, gw = land ? 240 : os::W() - 24;
  const int kw = (gw - 3 * gap) / 4, kh = land ? (bodyH - 4 * gap) / 5 : 54;
  if (land) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 8, 0);
  }
  lv_obj_t* dispBox = clear(body);
  lv_obj_set_size(dispBox, land ? os::W() - 24 - gw - 8 : lv_pct(100),
                  land ? bodyH : os::H() - os::SB_H - os::NAV_H - 5 * 62 - 30);
  display = label(dispBox, "0", 64, false, 0xffffff);
  lv_obj_align(display, LV_ALIGN_BOTTOM_RIGHT, -6, 0);
  static const char* KEYS[] = {"C", "±", "%", "÷", "7", "8", "9", "×", "4", "5", "6", "−", "1", "2", "3", "+", "0", ",", "="};
  lv_obj_t* grid = clear(body);
  lv_obj_set_size(grid, land ? gw : lv_pct(100), land ? bodyH : 5 * 62);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, gap, 0);
  for (const char* k : KEYS) {
    const bool opk = !strcmp(k, "÷") || !strcmp(k, "×") || !strcmp(k, "−") || !strcmp(k, "+") || !strcmp(k, "=");
    const bool fn = !strcmp(k, "C") || !strcmp(k, "±") || !strcmp(k, "%");
    lv_obj_t* b = box(grid, opk ? 0xff9f0a : fn ? 0xa5a5a5 : 0x333333, kh / 2);
    lv_obj_set_size(b, !strcmp(k, "0") ? kw * 2 + gap : kw, kh);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_t* l = label(b, strcmp(k, ",") ? k : i18n::decSep(), 22, true, fn ? 0x000000 : 0xffffff);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { key((const char*)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED, (void*)k);
  }
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (display && lv_obj_get_screen(display) == lv_event_get_current_target(e)) display = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  os::open(s);
}

}  // namespace app_calc

// ====================================================================== Store
#pragma GCC optimize("Os")  // screens built once per change: size over speed (the app slot is full)
// Apps and games as packages (pkg/store.h): the catalog's "packages" by category, a details screen (parts,
// install place, permissions, install / open / update / move / remove with progress), and the storage strip that
// opens the Storage page. Drawn after ~/projects/xt/mockups/s3-store.html.
namespace app_store {
namespace {

using namespace kit;
using pkg::store::Entry;
using pkg::store::State;
namespace st = pkg::store;

struct Cat {
  const char* id;
  const char* name;
};
const Cat CATS[] = {{"", N_("All")},         {"game", N_("Games")},   {"tool", N_("Tools")},
                    {"study", N_("Study")}, {"health", N_("Health")}, {"widget", N_("Widgets")}};
constexpr int NCATS = sizeof CATS / sizeof CATS[0];

constexpr uint32_t BLUE = col::IOS_BLUE;

// ---------------------------------------------------------------- the store's front page
lv_obj_t *scr_ = nullptr, *listBox = nullptr;
int catSel = 0;
struct RowRef {
  String id;
  lv_obj_t* arc;
  State shown;
};
std::vector<RowRef> rows;
std::vector<String> ids;  // ids behind the buttons of the current list
uint32_t seenPkg = 0, seenCat = 0;

void fill();
void openDetails(const String& id);

void refillSoon() {
  lv_async_call([](void*) { fill(); }, nullptr);
}

int idIndex(const String& id) {
  for (size_t i = 0; i < ids.size(); i++)
    if (ids[i] == id) return i;
  ids.push_back(id);
  return ids.size() - 1;
}
const String* idAt(lv_event_t* e) {
  const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  return i < ids.size() ? &ids[i] : nullptr;
}

// Pictures of packages not installed yet: the catalog's icon_url, fetched one at a time and kept for the run.
struct NetIcon {
  uint8_t* jpg = nullptr;
  size_t len = 0;
  bool loading = false, failed = false;
};
std::map<String, NetIcon> netIcons;  // by package id
String fetchingIcon;
void buildDetails();

void iconLoaded(void*, int status, JsonDocument*, const uint8_t* data, size_t len) {
  NetIcon& n = netIcons[fetchingIcon];
  n.loading = false;
  if (status == 200 && data && len && len < 65536 && (n.jpg = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM))) {
    memcpy(n.jpg, data, len);
    n.len = len;
  } else {
    n.failed = true;
  }
  fetchingIcon = "";
  if (listBox) refillSoon();
  lv_async_call([](void*) { buildDetails(); }, nullptr);
}

const lv_image_dsc_t* netIcon(const Entry* e, int size) {
  if (!e || !e->iconUrl.length()) return nullptr;
  NetIcon& n = netIcons[e->id];
  if (n.jpg) return apps::jpegIcon("url:" + e->id + "#" + e->code, n.jpg, n.len, size);
  if (!n.failed && !n.loading && !fetchingIcon.length()) {
    n.loading = true;
    fetchingIcon = e->id;
    desk::fetch(e->iconUrl, true, iconLoaded, &netIcons, false);
  }
  return nullptr;
}

lv_obj_t* entryIcon(lv_obj_t* parent, const String& id, const Entry* e, int size) {
  if (pkg::find(id.c_str()) || id == "vn.pawboop.pet") return apps::packageIcon(parent, id.c_str(), size);
  if (const lv_image_dsc_t* d = netIcon(e, size)) return img(parent, d);
  return apps::drawnIcon(parent, e ? e->bg1 : 0x64748b, e ? e->bg2 : 0x334155, e ? e->glyph.c_str() : "grid", size);
}

// Installs at once when there is nothing to choose (no part choice, no card, room on Máy); else the details.
void quickInstall(const String& id) {
  const Entry* e = st::entry(id.c_str());
  if (!e) return;
  if (e->addonOf.length()) return openDetails(id);
  bool choice = false;
  for (const st::Part& p : e->parts) choice |= p.group.length() > 0;
  const std::vector<String> parts = st::defaultParts(*e);
  if (choice || pkg::volume(pkg::SD).present || !e->supported() ||
      !st::room(st::partsBytes(*e, parts), pkg::FLASH, st::onLfs(e->kind, e->engine, st::partsBytes(*e, parts))).ok)
    return openDetails(id);
  String why;
  if (!st::install(*e, parts, pkg::FLASH, why)) os::toast(why.c_str());
  refillSoon();
}

void onAction(lv_event_t* ev) {
  const String* id = idAt(ev);
  if (!id) return;
  const String i = *id;
  const pkg::Package* p = pkg::find(i.c_str());
  String why;
  if (p && st::hasUpdate(i.c_str())) {
    if (!st::update(i.c_str(), why)) os::toast(why.c_str());
    refillSoon();
  } else if (p) {
    if (const char* a = pkg::ui::appOf(i)) apps::open(a);
  } else {
    quickInstall(i);
  }
}

void onRow(lv_event_t* ev) {
  if (const String* id = idAt(ev)) openDetails(*id);
}

lv_obj_t* progressArc(lv_obj_t* parent, int pct) {
  lv_obj_t* a = lv_arc_create(parent);
  lv_obj_set_size(a, 28, 28);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, pct);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C(pal().line), LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, C(BLUE), LV_PART_INDICATOR);
  lv_obj_t* sq = box(a, BLUE, 1);
  lv_obj_set_size(sq, 8, 8);
  lv_obj_center(sq);
  return a;
}

void appRow(lv_obj_t* parent, const String& id, const Entry* e) {
  const Pal& p = pal();
  const pkg::Package* inst = pkg::find(id.c_str());
  const int idx = idIndex(id);
  lv_obj_t* c = box(parent, p.cell, 14);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(c, 64, 0);
  lv_obj_set_style_pad_hor(c, 10, 0);
  lv_obj_set_style_pad_ver(c, 9, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(c, C(p.press), LV_STATE_PRESSED);
  lv_obj_add_event_cb(c, onRow, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
  lv_obj_t* ic = entryIcon(c, id, e, 44);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  if (inst && !inst->available) lv_obj_set_style_opa(ic, LV_OPA_40, 0);
  // Name and a line or two under it, shown in full (the row grows).
  const int textW = os::W() - 24 - 20 - 54 - 84;
  lv_obj_t* tx = clear(c);
  lv_obj_set_size(tx, textW, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(tx, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(tx, 2, 0);
  lv_obj_set_pos(tx, 54, 0);
  lv_obj_t* n = label(tx, "", 15, true, p.fg);
  fitText(n, e ? e->name.c_str() : inst->name.c_str(), textW, 20, 15, 12, true);
  const String sub = e && e->desc.length() ? e->desc : inst ? String(pkg::ui::volName(inst->vol)) : String();
  if (sub.length()) {
    lv_obj_t* d = label(tx, "", 12, false, p.faint);
    wrapText(d, sub.c_str(), textW, 12, false, 1);
  }
  lv_obj_update_layout(c);
  lv_obj_align(tx, LV_ALIGN_LEFT_MID, 54, 0);
  // The action on the right, the size under it (on its own line: "7,4 MB" is never split).
  lv_obj_t* right = clear(c);
  lv_obj_set_size(right, 80, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_gap(right, 3, 0);
  const State s = st::stateOf(id.c_str());
  lv_obj_t* arc = nullptr;
  if (s == st::QUEUED) {
    label(right, N_("Waiting"), 12, true, p.faint);
  } else if (s != st::IDLE) {
    const st::Status ss = st::status();
    arc = progressArc(right, ss.id == id ? ss.pct : 0);
    lv_obj_set_style_margin_right(arc, 10, 0);
  } else if (inst && st::hasUpdate(id.c_str())) {
    pkg::ui::pill(right, N_("Update"), BLUE, 0xffffff, onAction, (void*)(intptr_t)idx);
  } else if (inst) {
    pkg::ui::pill(right, N_("Open"), p.chip, inst->available ? p.link : p.faint, onAction, (void*)(intptr_t)idx);
  } else if (e && !e->supported()) {
    lv_obj_t* l = label(right, "", 11, true, p.faint);
    wrapText(l, N_("Needs a software update"), 80, 11, true, 1);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
  } else {
    pkg::ui::pill(right, N_("Install"), p.chip, p.link, onAction, (void*)(intptr_t)idx);
  }
  const uint64_t bytes = inst ? inst->bytes : e ? st::partsBytes(*e, st::defaultParts(*e)) : 0;
  if (bytes && s == st::IDLE) label(right, pkg::sizeText(bytes).c_str(), 11, false, p.faint);
  lv_obj_update_layout(c);
  lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
  rows.push_back({id, arc, s});
}

void featured(lv_obj_t* parent) {
  const Entry* f = nullptr;
  for (const Entry& e : st::catalog())
    if (!pkg::find(e.id.c_str()) && e.supported() && !e.addonOf.length() && e.type != "pack" &&
        (!f || (e.category == "game" && f->category != "game")))
      f = &e;
  if (!f) return;
  lv_obj_t* c = box(parent, f->bg1, 18);
  grad(c, f->bg1, f->bg2);  // vertical: horizontal fills need LV_DRAW_SW_SUPPORT_RGB888 (off in lv_conf.h)
  lv_obj_set_size(c, lv_pct(100), 132);
  lv_obj_set_style_pad_all(c, 14, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c, onRow, LV_EVENT_CLICKED, (void*)(intptr_t)idIndex(f->id));
  // Words on the left in a column (the description wraps in full, the button goes under it), the icon right.
  const int textW = os::W() - 24 - 28 - 86;
  lv_obj_t* col = clear(c);
  lv_obj_set_size(col, textW, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(col, 4, 0);
  const String kicker = upperVi(f->category == "game" ? T_("Games to try") : T_("Worth a try"));
  lv_obj_t* k = label(col, kicker.c_str(), 12, true, 0xffffff);
  lv_obj_set_style_text_letter_space(k, 1, 0);
  lv_obj_t* n = label(col, "", 20, true, 0xffffff);
  fitText(n, f->name.c_str(), textW, 26, 20, 15, true);
  if (f->desc.length()) {
    lv_obj_t* d = label(col, "", 13, false, 0xffffff);
    wrapText(d, f->desc.c_str(), textW, 13, false, 2, 4);
  }
  lv_obj_t* b = pkg::ui::pill(col, N_("View"), 0xffffff, f->bg2, onRow, (void*)(intptr_t)idIndex(f->id));
  lv_obj_set_style_margin_top(b, 4, 0);
  lv_obj_t* ic = box(c, 0xffffff, 20);
  lv_obj_set_style_bg_opa(ic, LV_OPA_30, 0);
  lv_obj_set_size(ic, 76, 76);
  lv_obj_center(entryIcon(ic, f->id, f, 64));
  // As tall as the words need (at least 132).
  lv_obj_update_layout(col);
  lv_obj_set_height(c, max<int32_t>(132, lv_obj_get_height(col) + 28));
  lv_obj_align(ic, LV_ALIGN_RIGHT_MID, 0, 0);
}

void storageStrip(lv_obj_t* parent) {
  const Pal& p = pal();
  lv_obj_t* c = box(parent, p.cell, 14);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, 12, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(c, 7, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(c, C(p.press), LV_STATE_PRESSED);
  lv_obj_add_event_cb(c, [](lv_event_t*) { pkg::openStorage(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* top = clear(c);
  lv_obj_set_size(top, lv_pct(100), 18);
  label(top, N_("Storage"), 13, true, p.fg);
  const pkg::ui::Usage u = pkg::ui::usage(pkg::FLASH);
  String r = i18n::fmt(N_("Device: {0} free"), pkg::sizeText(u.freeB));
  const pkg::ui::Usage s = pkg::ui::usage(pkg::SD);
  if (s.present) r += i18n::fmt(N_(" · card {0}"), pkg::sizeText(s.freeB));
  lv_obj_t* rl = label(top, r.c_str(), 13, false, p.faint);
  lv_obj_align(rl, LV_ALIGN_RIGHT_MID, 0, 0);
  pkg::ui::bar(c, u, 8);
}

void fill() {
  if (!listBox) return;
  const int32_t sy = lv_obj_get_scroll_y(lv_obj_get_parent(listBox));
  lv_obj_clean(listBox);
  rows.clear();
  ids.clear();
  seenPkg = pkg::version();
  seenCat = st::catalogVersion();
  static const char* names[NCATS];
  for (int i = 0; i < NCATS; i++) names[i] = CATS[i].name;
  chips(listBox, names, NCATS, catSel, [](int i) {
    catSel = i;
    refillSoon();
  });
  const auto& cat = st::catalog();
  if (!st::loaded() && cat.empty()) {
    lv_obj_t* r = clear(listBox);
    lv_obj_set_size(r, lv_pct(100), 60);
    lv_obj_center(spinner(r, 24, pal().line, BLUE));
  } else if (st::failed() && cat.empty()) {
    hint(listBox, desk::link() == desk::Link::Linked ? N_("Could not load the list of apps, try again later.")
                                                     : N_("Link to EspOS to install more apps."));
  }
  if (catSel == 0 && cat.size()) featured(listBox);
  for (int k = 1; k < NCATS; k++) {
    if (catSel && catSel != k) continue;
    bool any = false;
    for (const Entry& e : cat) {
      if (e.category != CATS[k].id || e.addonOf.length() || e.type == "pack") continue;  // add-ons: on their app's page
      if (!any) caption(listBox, CATS[k].name), any = true;
      appRow(listBox, e.id, &e);
    }
    if (catSel == k && !any && st::loaded()) hint(listBox, N_("No apps in this category yet."));
  }
  // Installed packages the catalog does not list (offline, or no longer offered).
  bool any = false;
  for (const pkg::Package& p : pkg::list()) {
    if (st::entry(p.id.c_str()) || p.addonOf.length() || p.type == "pack") continue;
    if (catSel && p.category != CATS[catSel].id) continue;
    if (!any) caption(listBox, N_("Installed on the device")), any = true;
    appRow(listBox, p.id, nullptr);
  }
  storageStrip(listBox);
  lv_obj_update_layout(listBox);
  lv_obj_scroll_to_y(lv_obj_get_parent(listBox), sy, LV_ANIM_OFF);
}

String pendingDetail;  // "store <id>" asked before the catalog came

void watch(lv_timer_t*) {
  if (!listBox) return;
  if (pendingDetail.length() && (st::entry(pendingDetail.c_str()) || st::failed())) {
    const String id = pendingDetail;
    pendingDetail = "";
    if (st::entry(id.c_str())) openDetails(id);
  }
  if (seenPkg != pkg::version() || seenCat != st::catalogVersion()) return fill();
  const st::Status ss = st::status();
  for (RowRef& r : rows) {
    const State s = st::stateOf(r.id.c_str());
    if (s != r.shown) return fill();
    if (r.arc && ss.id == r.id && lv_arc_get_value(r.arc) != ss.pct) lv_arc_set_value(r.arc, ss.pct);
  }
}

// ---------------------------------------------------------------- details
// One page per app: the app itself (sels[0]) and, under "Add-ons", its add-ons (sels[1..]), each with its own
// part choice, place, progress and actions. An add-on's page is its main app's page, scrolled to the add-on.
std::vector<String> detIds;  // ids behind the not-enough-room list's buttons
int detIndex(const String& id) {
  for (size_t i = 0; i < detIds.size(); i++)
    if (detIds[i] == id) return i;
  detIds.push_back(id);
  return detIds.size() - 1;
}
const String* detAt(lv_event_t* e) {
  const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  return i < detIds.size() ? &detIds[i] : nullptr;
}

struct Sel {
  String id;
  bool hasEntry = false;
  Entry e;
  std::vector<String> parts;
  pkg::Vol vol = pkg::FLASH;
  bool volPicked = false;  // the user chose the place
  bool confirmRm = false;
  State shown = st::IDLE;
  lv_obj_t *progTitle = nullptr, *progNum = nullptr, *progFill = nullptr;
};

struct Det {
  std::vector<Sel> sels;
  String confirmOther;  // a big app's "Remove" in the not-enough-room list, tapped once
  String scrollTo;      // an add-on to show (opened for it)
  lv_obj_t *scr = nullptr, *body = nullptr;
  uint32_t seenPkg = 0, seenCat = 0;
} det;

void buildDetails();
void rebuildSoon() {
  lv_async_call([](void*) { buildDetails(); }, nullptr);
}
// Callback data: the selection's index and a small value.
void* ud(int si, int v = 0) { return (void*)(intptr_t)(si << 8 | v); }
Sel* selOf(lv_event_t* e, int& v) {
  const intptr_t d = (intptr_t)lv_event_get_user_data(e);
  v = d & 0xff;
  return (size_t)(d >> 8) < det.sels.size() ? &det.sels[d >> 8] : nullptr;
}

bool hasPart(const Sel& s, const String& id) { return std::find(s.parts.begin(), s.parts.end(), id) != s.parts.end(); }
// Files on LittleFS when on Máy (st::onLfs: JavaScript apps, languages, small fonts), the app area otherwise.
bool isJs(const Sel& s, const pkg::Package* inst) {
  return s.hasEntry ? st::onLfs(s.e.kind, s.e.engine, st::partsBytes(s.e, s.parts))
                    : inst && st::onLfs(inst->kind, inst->engine, inst->bytes);
}

// A selection as it starts: its installed parts and place, or the defaults (Máy unless it lacks room and a card is in).
Sel newSel(const String& id) {
  Sel s;
  s.id = id;
  const Entry* e = st::entry(id.c_str());
  const pkg::Package* inst = pkg::find(id.c_str());
  if (e) s.e = *e, s.hasEntry = true;
  if (inst) {
    s.parts = inst->parts;
    s.vol = inst->vol;
  } else if (e) {
    s.parts = st::defaultParts(*e);
    if (pkg::volume(pkg::SD).present && !st::room(st::partsBytes(*e, s.parts), pkg::FLASH, isJs(s, nullptr)).ok) s.vol = pkg::SD;
  }
  return s;
}

lv_obj_t* infoCol(lv_obj_t* row, const String& v, const char* l) {
  lv_obj_t* c = clear(row);
  lv_obj_set_height(c, 40);
  lv_obj_set_flex_grow(c, 1);
  lv_obj_t* a = label(c, "", 15, true, pal().fg);
  fitText(a, v.c_str(), (os::W() - 24) / 3 - 8, 20, 15, 11, true);
  lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t* b = label(c, "", 12, false, pal().faint);
  fitText(b, l, (os::W() - 24) / 3 - 4, 18, 12, 12, false, 0);  // a longer language: letters closer (never cut)
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
  return c;
}

void onPrimary(lv_event_t* ev) {
  int v;
  Sel* s = selOf(ev, v);
  if (!s) return;
  String why;
  const pkg::Package* p = pkg::find(s->id.c_str());
  if (p && st::hasUpdate(s->id.c_str())) {
    if (!st::update(s->id.c_str(), why)) os::toast(why.c_str());
  } else if (p) {
    if (const char* a = pkg::ui::appOf(s->id)) apps::open(a);
    return;
  } else if (!s->hasEntry) {
    return;
  } else if (!s->e.supported()) {
    os::toast(N_("Update the device's software first (Settings, Software update)"));
    return;
  } else if (!st::install(s->e, s->parts, s->vol, why)) {
    os::toast(why.c_str());
  }
  rebuildSoon();
}

// Remove or move one of the biggest apps, from the not-enough-room list.
void onBigAction(lv_event_t* ev) {
  const String* id = detAt(ev);
  if (!id) return;
  const bool rm = lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(ev)) != nullptr;
  String why;
  if (rm && det.confirmOther != *id) {
    det.confirmOther = *id;
    return rebuildSoon();
  }
  const pkg::Package* p = pkg::find(id->c_str());
  const bool ok = rm ? st::remove(id->c_str(), why) : st::move(id->c_str(), p && p->vol == pkg::SD ? pkg::FLASH : pkg::SD, why);
  if (!ok) os::toast(why.c_str());
  det.confirmOther = "";
  rebuildSoon();
}

String nameOf(const Sel& s, const pkg::Package* inst) { return s.hasEntry ? s.e.name : inst ? inst->name : s.id; }
String setOf(const Sel& s) {
  if (s.hasEntry) return s.e.set;
  const pkg::Package* p = pkg::find(s.id.c_str());
  return p ? p->set : String();
}

void progressCell(const Sel& sc, Sel& s, State state, const pkg::Package* inst) {
  const Pal& p = pal();
  const st::Status ss = st::status();
  lv_obj_t* c = cell(det.body, 12);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(c, 8, 0);
  lv_obj_t* top = clear(c);
  lv_obj_set_size(top, lv_pct(100), 18);
  s.progTitle = label(top, "", 14, true, p.fg);
  s.progNum = label(top, "", 13, false, p.faint);
  lv_obj_align(s.progNum, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* track = box(c, p.line, 4);
  lv_obj_set_size(track, lv_pct(100), 8);
  s.progFill = box(track, BLUE, 4);
  lv_obj_set_size(s.progFill, 0, 8);
  String where = state == st::QUEUED ? T_("Waiting for the job before it") : ss.kind == 1 ? T_("Removing from the device") : "";
  if (ss.kind == 2 && inst) where = inst->vol == pkg::SD ? T_("Move to device") : T_("Move to memory card");
  if (ss.kind == 0 && state != st::QUEUED) {
    where = i18n::fmt(N_("Installing on {0}"), pkg::ui::volName(sc.vol));
    if (sc.hasEntry)
      for (const st::Part& pt : sc.e.parts)
        if (pt.group.length() && hasPart(sc, pt.id)) where += " · " + pt.label;
  }
  lv_obj_t* w = label(c, "", 12, false, p.faint);
  fitText(w, where.c_str(), os::W() - 48, 32, 12, 11);
}

// Not enough room: how much, and what to do (which apps to remove, the card).
void roomPanel(const Sel& s, const st::Room& room, bool js, bool sdIn) {
  const Pal& p = pal();
  lv_obj_t* w = box(det.body, p.dark ? 0x3a2c12 : 0xfff4e5, 14);
  lv_obj_set_size(w, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(w, 12, 0);
  // Only apps whose removal frees room where this one goes: native ones in the app area, JavaScript ones in
  // LittleFS, any on the card. One that is enough on its own is named.
  const auto hs = st::helpers(s.vol, js, 3);
  const bool orCard = sdIn && s.vol == pkg::FLASH;
  const bool tooBig = room.capacity && room.need > room.capacity;
  const pkg::Package* main = s.e.addonOf.length() ? pkg::find(s.e.addonOf.c_str()) : nullptr;
  // Two sentences in a row (i18n: no space between them in Chinese and Japanese).
  auto join = [](const String& a, const char* b) { return i18n::fmt(N_("sentences" "\x04" "{0} {1}"), a, b); };
  String t = i18n::fmt(N_("{0} is {1} short."), pkg::ui::volName(s.vol), pkg::ui::glue(pkg::sizeText(room.missing)));
  if (main && main->vol == pkg::FLASH && s.vol == pkg::FLASH && main->bytes * 2 >= room.capacity) {
    // An add-on next to a big main app: the card is the way.
    t = join(i18n::fmt(N_("{0} uses nearly all of the device's room."), main->name),
             (orCard ? i18n::fmt(N_("Install {0} on the memory card."), s.e.name)
                     : i18n::fmt(N_("Insert a memory card to add {0}."), s.e.name)).c_str());
  } else if (tooBig) {
    t = i18n::fmt(N_("{0} has only {1} for apps; this one needs {2}."), pkg::ui::volName(s.vol),
                  pkg::ui::glue(pkg::sizeText(room.capacity)), pkg::ui::glue(pkg::sizeText(room.need)));
    if (orCard) t = join(t, T_("Install on the memory card."));
    else if (s.vol == pkg::FLASH) t = join(t, T_("Insert a memory card to install it."));
  } else if (room.restart) {
    t = join(t, T_("Restart the device to use the room of the apps just removed."));
  } else if (hs.size() && hs[0]->bytes >= room.missing) {
    // i18n: {0} an app, {1} its size
    static const char* const ONE[] = {N_("Remove {0} ({1}) to make room."),
                                      N_("Remove {0} ({1}) to make room, or install on the memory card."),
                                      N_("Remove {0} ({1}) and restart the device to make room."),
                                      N_("Remove {0} ({1}) and restart the device to make room, or install on the memory card.")};
    t = join(t, i18n::fmt(ONE[(st::inUse(hs[0]->id.c_str()) ? 2 : 0) + (orCard ? 1 : 0)], hs[0]->name,
                          pkg::ui::glue(pkg::sizeText(hs[0]->bytes))).c_str());
  } else if (hs.size()) {
    t = join(t, orCard ? T_("Remove some of the apps below, or install on the memory card.") : T_("Remove some of the apps below."));
  } else {
    t = join(t, orCard ? T_("Install on the memory card.") : T_("There are no apps here to remove."));
  }
  lv_obj_t* l = label(w, "", 14, false, p.dark ? 0xffd28a : 0x7a4b00);
  pkg::ui::wrapWords(l, t, os::W() - 48, 14);
  if (room.restart) {
    lv_obj_t* r = button(det.body, N_("Restart"), BLUE, 0xffffff, [](lv_event_t*) { pkg::ui::restartDevice(); });
    lv_obj_set_width(r, lv_pct(100));
  }
  if (room.restart || tooBig || main) return;
  for (const pkg::Package* b : hs) {
    lv_obj_t* r = box(det.body, p.cell, 14);
    lv_obj_set_size(r, lv_pct(100), 52);
    lv_obj_set_style_pad_hor(r, 10, 0);
    lv_obj_t* bi = apps::packageIcon(r, b->id.c_str(), 36);
    lv_obj_align(bi, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* bn = label(r, "", 14, true, p.fg);
    fitText(bn, b->name.c_str(), os::W() - 24 - 20 - 46 - 130, 18, 14, 11, true);
    lv_obj_set_pos(bn, 46, 6);
    lv_obj_t* bs = label(r, pkg::sizeText(b->bytes).c_str(), 12, false, p.faint);
    lv_obj_set_pos(bs, 46, 26);
    const int idx = detIndex(b->id);
    const bool confirm = det.confirmOther == b->id;
    lv_obj_t* rm = pkg::ui::pill(r, confirm ? N_("Really remove?") : N_("Remove"), confirm ? col::RED : p.chip, confirm ? 0xffffff : col::RED,
                                 onBigAction, (void*)(intptr_t)idx);
    lv_obj_set_user_data(rm, (void*)1);
    lv_obj_align(rm, LV_ALIGN_RIGHT_MID, 0, 0);
    if (sdIn) {
      lv_obj_t* mv = pkg::ui::pill(r, N_("Move"), p.chip, p.link, onBigAction, (void*)(intptr_t)idx);
      lv_obj_update_layout(rm);
      lv_obj_align_to(mv, rm, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    }
  }
}

// A choice of parts ("Small / Light / Fine", the catalog's labels), before it is installed.
void partsChoice(Sel& s, int si) {
  const Pal& p = pal();
  std::vector<String> groups;
  for (const st::Part& pt : s.e.parts)
    if (pt.group.length() && std::find(groups.begin(), groups.end(), pt.group) == groups.end()) groups.push_back(pt.group);
  for (const String& g : groups) {
    caption(det.body, groups.size() > 1 ? i18n::fmt(N_("Choose a pack: {0}"), g).c_str() : N_("Choose what to download"));
    lv_obj_t* c = cell(det.body, 0);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    for (size_t k = 0; k < s.e.parts.size() && k < 255; k++) {
      const st::Part& pt = s.e.parts[k];
      if (pt.group != g) continue;
      lv_obj_t* o = clear(c);
      lv_obj_set_size(o, lv_pct(100), 44);
      lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
      const bool on = hasPart(s, pt.id);
      lv_obj_t* rd = box(o, on ? BLUE : p.cell, 10);
      lv_obj_set_size(rd, 20, 20);
      lv_obj_set_style_border_width(rd, on ? 6 : 2, 0);
      lv_obj_set_style_border_color(rd, C(on ? BLUE : 0xc3c7d0), 0);
      if (on) lv_obj_set_style_bg_color(rd, C(0xffffff), 0);
      lv_obj_align(rd, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* l = label(o, "", 14, false, p.fg);
      fitText(l, pt.label.c_str(), os::W() - 24 - 24 - 30 - 70, 36, 14, 11);
      lv_obj_align(l, LV_ALIGN_LEFT_MID, 30, 0);
      lv_obj_t* z = label(o, pkg::sizeText(pt.size).c_str(), 13, false, p.faint);
      lv_obj_align(z, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_add_event_cb(o, [](lv_event_t* ev) {
        int k;
        Sel* s = selOf(ev, k);
        if (!s || k >= (int)s->e.parts.size()) return;
        const st::Part& pick = s->e.parts[k];
        for (auto it = s->parts.begin(); it != s->parts.end();) {
          bool same = false;
          for (const st::Part& q : s->e.parts) same |= q.id == *it && q.group == pick.group;
          it = same ? s->parts.erase(it) : std::next(it);
        }
        s->parts.push_back(pick.id);
        rebuildSoon();
      }, LV_EVENT_CLICKED, ud(si, k));
    }
  }
}

// Where to install: only with a card in.
void volChoice(Sel& s, int si, bool js) {
  const Pal& p = pal();
  caption(det.body, N_("Install on"));
  lv_obj_t* seg = box(det.body, p.cell2, 10);
  lv_obj_set_size(seg, lv_pct(100), 36);
  lv_obj_set_style_pad_all(seg, 2, 0);
  lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(seg, 2, 0);
  for (int k = 0; k < 2; k++) {
    const pkg::Vol v = k ? pkg::SD : pkg::FLASH;
    const bool on = s.vol == v;
    lv_obj_t* o = box(seg, on ? (p.dark ? 0x3a4150 : 0xffffff) : p.cell2, 8);
    lv_obj_set_height(o, lv_pct(100));
    lv_obj_set_flex_grow(o, 1);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    const st::Room r = st::room(0, v, js);
    const String t = i18n::fmt(N_("{0} · {1} free"), pkg::ui::volName(v), pkg::sizeText(r.free));
    lv_obj_t* l = label(o, "", 13, on, on ? p.fg : p.dim);
    fitText(l, t.c_str(), (os::W() - 32) / 2 - 8, 18, 13, 10, on);
    lv_obj_center(l);
    lv_obj_add_event_cb(o, [](lv_event_t* ev) {
      int k;
      if (Sel* s = selOf(ev, k)) s->vol = k ? pkg::SD : pkg::FLASH, s->volPicked = true;
      rebuildSoon();
    }, LV_EVENT_CLICKED, ud(si, k));
  }
}

// Installed: move and remove (an app with add-ons asks whether to remove them too).
void actionsRow(Sel& s, int si, const pkg::Package* inst, bool sdIn) {
  const Pal& p = pal();
  std::vector<String> mine;  // its installed add-ons
  for (size_t k = 1; si == 0 && k < det.sels.size(); k++)
    if (pkg::find(det.sels[k].id.c_str())) mine.push_back(det.sels[k].id);
  lv_obj_t* r = clear(det.body);
  lv_obj_set_size(r, lv_pct(100), 40);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(r, 8, 0);
  if (sdIn && inst->available) {
    lv_obj_t* mv = button(r, inst->vol == pkg::SD ? N_("Move to device") : N_("Move to card"), p.cell2, p.fg, [](lv_event_t* ev) {
      int v;
      Sel* s = selOf(ev, v);
      const pkg::Package* q = s ? pkg::find(s->id.c_str()) : nullptr;
      String why;
      if (q && !st::move(s->id.c_str(), q->vol == pkg::SD ? pkg::FLASH : pkg::SD, why)) os::toast(why.c_str());
      rebuildSoon();
    }, ud(si));
    lv_obj_set_flex_grow(mv, 1);
  }
  // Remove: v 0 = ask, 1 = this one only, 2 = with its add-ons.
  auto onRm = [](lv_event_t* ev) {
    int v;
    Sel* s = selOf(ev, v);
    if (!s) return;
    if (!s->confirmRm) {
      s->confirmRm = true;
      return rebuildSoon();
    }
    s->confirmRm = false;
    String why;
    if (!st::remove(s->id.c_str(), why)) os::toast(why.c_str());
    for (size_t k = 1; v == 2 && k < det.sels.size(); k++)
      if (pkg::find(det.sels[k].id.c_str())) st::remove(det.sels[k].id.c_str(), why);
    rebuildSoon();
  };
  const bool both = s.confirmRm && mine.size();
  const String only = inst->name.length() <= 10 ? i18n::fmt(N_("Remove only {0}"), inst->name) : String(T_("Remove only the app"));
  lv_obj_t* rm = button(r, s.confirmRm ? (both ? only.c_str() : N_("Really remove?")) : N_("Remove"),
                        s.confirmRm ? col::RED : (p.dark ? 0x3a1d1d : 0xffe5e3), s.confirmRm ? 0xffffff : col::RED, onRm,
                        ud(si, 1));
  lv_obj_set_flex_grow(rm, 1);
  if (both) {
    String names;
    for (const String& a : mine) {
      const pkg::Package* q = pkg::find(a.c_str());
      names += String(names.length() ? ", " : "") + (q ? q->name : a);
    }
    lv_obj_t* all = button(det.body, "", col::RED, 0xffffff, onRm, ud(si, 2));
    lv_obj_set_width(all, lv_pct(100));
    lv_obj_set_height(all, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(all, 10, 0);
    lv_obj_t* l = lv_obj_get_child(all, 0);
    pkg::ui::wrapWords(l, i18n::fmt(N_("Also remove {0}"), names), os::W() - 64, 15, true);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  if ((inst->engine == "pet" || inst->kind == "js" || inst->addonOf.length()) && !setOf(s).length())  // not on a shelf
    hint(det.body, N_("Removing an app keeps its data (levels, coins, notes) on the device; install it again to carry on."));
}

void buildDetails() {
  if (!det.body || det.sels.empty()) return;
  const Pal& p = pal();
  const int32_t sy = lv_obj_get_scroll_y(det.body);
  lv_obj_clean(det.body);
  det.seenPkg = pkg::version();
  det.seenCat = st::catalogVersion();
  detIds.clear();
  // Add-ons in the catalog that came after the page opened.
  for (const String& a : st::addons(det.sels[0].id.c_str())) {
    bool have = false;
    for (const Sel& s : det.sels) have |= s.id == a;
    if (!have) det.sels.push_back(newSel(a));
  }
  const pkg::VolInfo sdv = pkg::volume(pkg::SD);
  lv_obj_t* scrollTo = nullptr;
  for (size_t si = 0; si < det.sels.size(); si++) {
    Sel& s = det.sels[si];
    s.progTitle = s.progNum = s.progFill = nullptr;
    if (const Entry* e = st::entry(s.id.c_str())) s.e = *e, s.hasEntry = true;
    const pkg::Package* inst = pkg::find(s.id.c_str());
    const State state = st::stateOf(s.id.c_str());
    s.shown = state;
    const bool js = isJs(s, inst);
    if (!sdv.present && !inst) s.vol = pkg::FLASH;
    // A card came in and Máy lacks room: offer the card (unless the user picked a place).
    if (!inst && s.hasEntry && sdv.present && s.vol == pkg::FLASH && !s.volPicked &&
        !st::room(st::partsBytes(s.e, s.parts), pkg::FLASH, js).ok)
      s.vol = pkg::SD;
    const st::Room room = s.hasEntry && !inst ? st::room(st::partsBytes(s.e, s.parts), s.vol, js) : st::Room();
    const bool addon = si > 0;
    const pkg::Package* main = addon ? pkg::find(det.sels[0].id.c_str()) : nullptr;
    const String name = nameOf(s, inst);
    // Add-ons on shelves (PawBoop: its dogs, its run maps), each with its heading.
    if (addon && (si == 1 || setOf(det.sels[si - 1]) != setOf(s))) {
      const String set = setOf(s);
      lv_obj_t* cap = caption(det.body, set == "dog" ? N_("Dogs") : set == "map" ? N_("Run maps") : N_("Add-ons"));
      if (!scrollTo && det.scrollTo == s.id) scrollTo = cap;  // opened for this add-on: its shelf's heading on top
    }
    // Another add-on it needs first (a run map: the run game).
    const String needs = s.hasEntry ? s.e.needs : String();
    const bool needOther = addon && needs.length() && !pkg::find(needs.c_str());

    // Header: icon, name, author, the main button (no "Open" for an add-on: it opens from its app).
    lv_obj_t* hdr = clear(det.body);
    const int icon = addon ? 52 : 72;
    lv_obj_set_size(hdr, lv_pct(100), addon ? 72 : icon + 4);
    lv_obj_t* ic = entryIcon(hdr, s.id, s.hasEntry ? &s.e : nullptr, icon);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* n = label(hdr, "", addon ? 16 : 20, true, p.fg);
    fitText(n, name.c_str(), os::W() - 24 - icon - 12, 22, addon ? 16 : 20, 13, true);
    lv_obj_set_pos(n, icon + 12, addon ? 0 : 2);
    String who = addon ? pkg::sizeText(inst ? inst->bytes : st::partsBytes(s.e, s.parts))
                       : s.hasEntry && s.e.author.length() ? s.e.author : inst ? inst->author : String();
    if (inst)
      who += String(who.length() ? " · " : "") + i18n::fmt(N_("on {0}"), pkg::ui::volName(inst->vol)) +
             (inst->available ? "" : T_(" (not inserted)"));
    lv_obj_t* a = label(hdr, "", 13, false, p.faint);
    fitText(a, who.c_str(), os::W() - 24 - icon - 12, 18, 13, 11);
    lv_obj_set_pos(a, icon + 12, addon ? 21 : 28);
    const bool needMain = (addon && !main) || needOther;
    if (state == st::IDLE && !(addon && inst && !st::hasUpdate(s.id.c_str()))) {
      const char* t = inst ? (st::hasUpdate(s.id.c_str()) ? N_("Update") : N_("Open"))
                           : !s.hasEntry || s.e.supported() ? N_("Install") : N_("Needs a software update");
      const bool dim = (!inst && s.hasEntry && (!s.e.supported() || !room.ok || needMain)) || (inst && !inst->available);
      lv_obj_t* b = pkg::ui::pill(hdr, t, dim ? p.cell2 : BLUE, dim ? p.faint : 0xffffff, onPrimary, ud(si), addon ? 28 : 30);
      lv_obj_set_style_pad_hor(b, 22, 0);
      lv_obj_set_pos(b, icon + 12, addon ? 42 : 46);
    }
    if (det.scrollTo == s.id && !scrollTo) scrollTo = hdr;

    if (state != st::IDLE) progressCell(s, s, state, inst);

    // Size, version, category (the app itself).
    if (!addon) {
      lv_obj_t* info = cell(det.body, 8);
      lv_obj_set_flex_flow(info, LV_FLEX_FLOW_ROW);
      const uint64_t bytes = inst ? inst->bytes : s.hasEntry ? st::partsBytes(s.e, s.parts) : 0;
      infoCol(info, pkg::sizeText(bytes), NC_("bytes", "Size"));
      const bool newer = inst && st::hasUpdate(s.id.c_str());
      const String newText = newer ? i18n::fmt(N_("{0} available"), s.e.version) : String();
      infoCol(info, inst ? inst->version : s.e.version, newer ? newText.c_str() : N_("Version"));
      infoCol(info, pkg::ui::categoryName(s.hasEntry ? s.e.category : inst->category), N_("Category"));
    }

    if (needMain && !inst && state == st::IDLE) {
      const st::Entry* ne = needOther && main ? st::entry(needs.c_str()) : nullptr;
      hint(det.body, i18n::fmt(N_("Install {0} first."), ne ? ne->name : nameOf(det.sels[0], nullptr)).c_str());
    }
    else if (!inst && s.hasEntry && s.e.supported() && !room.ok && state == st::IDLE)
      roomPanel(s, room, js, sdv.present);

    // What it is.
    const String desc = s.hasEntry ? s.e.desc : inst ? inst->desc : String();
    if (desc.length()) {
      lv_obj_t* d = label(det.body, "", 14, false, p.dim);
      wrapText(d, desc.c_str(), os::W() - 32, 14);
      lv_obj_set_style_pad_hor(d, 4, 0);
    }
    if (!inst && s.hasEntry && state == st::IDLE) partsChoice(s, si);
    if (!inst && s.hasEntry && sdv.present && state == st::IDLE) volChoice(s, si, js);
    if (inst && state == st::IDLE) actionsRow(s, si, inst, sdv.present);

    // Permissions in plain words (the app's; an add-on's own when it asks for more).
    if (!addon || (s.hasEntry && s.e.perms.size())) {
      caption(det.body, addon ? i18n::fmt(N_("Permissions of {0}"), name).c_str() : N_("Permissions"));
      lv_obj_t* pe = label(det.body, "", 14, false, p.dim);
      wrapText(pe, pkg::ui::permsText(s.hasEntry ? s.e.perms : inst->perms).c_str(), os::W() - 32, 14);
      lv_obj_set_style_pad_hor(pe, 4, 0);
    }
  }

  lv_obj_update_layout(det.body);
  if (det.scrollTo.length() && scrollTo) {
    lv_obj_scroll_to_y(det.body, lv_obj_get_y(scrollTo) - 6, LV_ANIM_OFF);  // opened for an add-on: it on top
    det.scrollTo = "";
  } else {
    lv_obj_scroll_to_y(det.body, sy, LV_ANIM_OFF);
  }
}

void detWatch(lv_timer_t*) {
  if (!det.body) return;
  bool changed = det.seenPkg != pkg::version() || det.seenCat != st::catalogVersion();
  for (const Sel& s : det.sels) changed |= st::stateOf(s.id.c_str()) != s.shown;
  if (changed) return buildDetails();
  const st::Status ss = st::status();
  for (Sel& s : det.sels) {
    if (!s.progTitle) continue;
    const bool mine = ss.id == s.id;
    const State showS = mine && ss.state != st::DONE && ss.state != st::ERROR ? ss.state : s.shown;
    String t = st::stateText(showS);
    if (mine && showS != st::QUEUED && showS != st::REMOVE) t += " " + String(ss.pct) + "%";
    setText(s.progTitle, t.c_str());
    setText(s.progNum, mine && ss.total ? (pkg::sizeText(ss.done) + " / " + pkg::sizeText(ss.total)).c_str() : "");
    lv_obj_set_width(s.progFill, lv_pct(mine ? max(2, ss.pct) : 0));
  }
}

void openDetails(const String& asked) {
  // An add-on shows on its main app's page.
  String id = asked;
  const String mainId = st::addonOf(asked.c_str());
  if (mainId.length() && (st::entry(mainId.c_str()) || pkg::find(mainId.c_str()))) id = mainId;
  if (!st::entry(id.c_str()) && !pkg::find(id.c_str())) return;
  det = Det();
  det.sels.push_back(newSel(id));
  for (const String& a : st::addons(id.c_str())) det.sels.push_back(newSel(a));
  // Plain add-ons first, then each shelf (dogs, maps) by name.
  std::stable_sort(det.sels.begin() + 1, det.sels.end(), [](const Sel& a, const Sel& b) {
    const String sa = setOf(a), sb = setOf(b);
    if (sa != sb) return sa < sb;  // "" < "dog" < "map"
    return sa.length() && nameOf(a, pkg::find(a.id.c_str())) < nameOf(b, pkg::find(b.id.c_str()));
  });
  if (id != asked) det.scrollTo = asked;
  lv_obj_t* body;
  det.scr = os::screen(nameOf(det.sels[0], pkg::find(id.c_str())).c_str(), &body, 0);
  det.body = body;
  lv_obj_add_event_cb(det.scr, [](lv_event_t* ev) {
    if (lv_event_get_current_target(ev) != det.scr) return;
    det.scr = det.body = nullptr;
    for (Sel& s : det.sels) s.progTitle = s.progNum = s.progFill = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  every(det.scr, 250, detWatch);
  buildDetails();
  os::open(det.scr);
}

}  // namespace

const lv_image_dsc_t* catalogIcon(const char* id, int size) { return netIcon(st::entry(id), size); }

void open(const char* arg) {
  lv_obj_t* body;
  scr_ = os::screen(N_("Store"), &body, os::ROOT);
  listBox = stack(body, false, 8);
  lv_obj_add_event_cb(scr_, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != scr_) return;
    desk::cancelFetches(nullptr);
    desk::cancelFetches(&netIcons);
    if (fetchingIcon.length()) netIcons[fetchingIcon].loading = false, fetchingIcon = "";
    listBox = scr_ = nullptr;
    rows.clear();
  }, LV_EVENT_DELETE, nullptr);
  st::refresh(true);
  fill();
  every(scr_, 300, watch);
  os::open(scr_);
  // The store opened for one package (a notification, the web, the search).
  pendingDetail = "";
  if (arg && *arg) {
    if (st::entry(arg) || pkg::find(arg)) openDetails(arg);
    else pendingDetail = arg;
  }
}

}  // namespace app_store
