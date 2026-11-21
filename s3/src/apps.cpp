#pragma GCC optimize("Os")  // cold code: built small (0.5.7 flash diet; the app slot is full)
#include "apps.h"

#include <ctime>
#include <deque>

#include "art.h"
#include "fonts.h"
#include "lock.h"
#include "lunar.h"
#include "notif.h"
#include "os.h"
#if !defined(NO_PET) && !ESPOS_ONBOARD
#include "pet/pet_icon.h"
#endif
#include "jpeg.h"
#include "pkg/pkg.h"
#include "trace.h"
#include "uikit.h"

#include <map>

namespace artz {
const char* report();
}

namespace apps {
namespace {

using namespace kit;

// Names marked N_: shown through the kit helpers, which translate them (installed apps keep their own names).
std::vector<App> builtin = {
    {"news", N_("News"), &ic_news_52, &ic_news_40, &ic_news_28, app_news::open, app_news::badge},
    {"weather", N_("Weather"), &ic_weather_52, &ic_weather_40, &ic_weather_28, app_weather::open, nullptr},
    {"cal", N_("Calendar"), &ic_cal_52, &ic_cal_40, &ic_cal_28, app_calendar::open, nullptr},
    {"photos", N_("Photos"), &ic_photos_52, &ic_photos_40, &ic_photos_28, app_photos::open, nullptr},
    {"clock", N_("Clock"), &ic_clock_52, &ic_clock_40, &ic_clock_28, app_clock::open, nullptr},
    {"notes", N_("Notes"), &ic_notes_52, &ic_notes_40, &ic_notes_28, app_notes::open, nullptr},
    {"markets", N_("Markets"), &ic_markets_52, &ic_markets_40, &ic_markets_28, app_markets::open, nullptr},
    {"remind", N_("Reminders"), &ic_remind_52, &ic_remind_40, &ic_remind_28, app_remind::open, app_remind::badge},
    {"lunar", N_("Lunar"), &ic_lunar_52, &ic_lunar_40, &ic_lunar_28, app_lunar::open, nullptr},
    {"calc", N_("Calculator"), &ic_calc_52, &ic_calc_40, &ic_calc_28, app_calc::open, nullptr},
    {"store", N_("Store"), &ic_store_52, &ic_store_40, &ic_store_28, app_store::open, nullptr},
    {"settings", N_("Settings"), &ic_settings_52, &ic_settings_40, &ic_settings_28, app_settings::open, nullptr},
};

std::vector<App> list;          // built-in apps, then installed ones
std::deque<String> pool;        // installed apps' strings (pointers stay valid for good)
uint32_t listVer = 0;
uint32_t shownSig = 0, shownVer = 0;  // what Home draws of the list (version())

const char* intern(const String& s) {
  for (const String& p : pool)
    if (p == s) return p.c_str();
  pool.push_back(s);
  return pool.back().c_str();
}

constexpr const char* PET_PKG = "vn.pawboop.pet";

void rebuildList() {
  static bool logged = false;
  if (!logged) {
    logged = true;
    trace::add("%s", artz::report());  // the built-in art was inflated before setup(): say how it went
  }
  const uint32_t v = pkg::version();
  if (v == listVer && !list.empty()) return;
  listVer = v;
  list = builtin;
  for (const pkg::Package& p : pkg::list()) {
    if (p.addonOf.length()) continue;  // an add-on opens from its main app, not from Home
    if (p.type == "pack") continue;    // data for an engine (e.g. the Java runtime): nothing to open
#ifndef NO_PET
    if (p.id == PET_PKG) {
#if ESPOS_ONBOARD
      // PawBoop keeps its app id "pet" (widgets, notifications, Home); it sits before the store. Its icon is the
      // package's icon.jpg (the firmware's own copies went, 18 KB, for the first-boot setup: docs/onboarding-plan.md).
      App a = {"pet", "PawBoop", nullptr, nullptr, nullptr, nullptr, nullptr};
      a.js = true, a.bg1 = 0xffcf7a, a.bg2 = 0xe8892c, a.glyph = "play";
#else
      // PawBoop keeps its app id "pet" (widgets, notifications, Home) and its pictures; it sits before the store.
      App a = {"pet", "PawBoop", &ic_pet_52, &ic_pet_40, &ic_pet_28, nullptr, nullptr};
#endif
      a.pkg = intern(p.id);
      a.off = !p.available && (p.vol != pkg::SD || pkg::cardChecked());
      size_t at = list.size();
      for (size_t i = 0; i < list.size(); i++)
        if (!strcmp(list[i].id, "store")) at = i;
      list.insert(list.begin() + at, a);
      continue;
    }
#endif
    App a = {intern(p.id), intern(p.name), nullptr, nullptr, nullptr, nullptr, nullptr};
    a.js = true;
    a.bg1 = p.bg1, a.bg2 = p.bg2;
    a.glyph = intern(p.glyph);
    a.pkg = a.id;
    a.off = !p.available && (p.vol != pkg::SD || pkg::cardChecked());
    // Keep the built-ins first; an installed app with a built-in's id is ignored.
    bool clash = false;
    for (const App& b : builtin) clash |= !strcmp(b.id, a.id);
    if (!clash) list.push_back(a);
  }
  // What Home shows of it: ids, names, pictures, greyed or not, versions (a new icon.jpg).
  uint32_t h = 2166136261u;
  auto mix = [&h](const char* s) {
    for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    h = (h ^ 0xffu) * 16777619u;
  };
  for (const App& a : list) {
    mix(a.id), mix(a.name), mix(a.glyph);
    h = (h ^ a.bg1 ^ (a.bg2 << 1) ^ (a.off ? 0x55u : 0xaau)) * 16777619u;
  }
  for (const pkg::Package& p : pkg::list()) mix(p.id.c_str()), mix(p.version.c_str()), h = (h ^ p.icon) * 16777619u;
  if (h != shownSig) shownSig = h, shownVer++;
}

// ---- package icons: icon.jpg decoded once per size, corners rounded into an alpha channel (no clipping layer).
struct Pic {
  lv_image_dsc_t dsc;
  uint8_t* px;
};
std::map<String, Pic*> pics;

Pic* makePic(const uint8_t* jpg, size_t len, int s) {
  uint16_t* rgb = jpeg::cover(jpg, len, s, s);
  if (!rgb) return nullptr;
  Pic* pc = new Pic();
  pc->px = (uint8_t*)heap_caps_malloc(s * s * 3, MALLOC_CAP_SPIRAM);
  if (!pc->px) {
    heap_caps_free(rgb);
    delete pc;
    return nullptr;
  }
  memcpy(pc->px, rgb, s * s * 2);
  heap_caps_free(rgb);
  // Alpha: a rounded square (radius 27% like the drawn icons), 4x4 samples per edge pixel.
  uint8_t* a = pc->px + s * s * 2;
  const float r = s * 0.27f;
  for (int y = 0; y < s; y++)
    for (int x = 0; x < s; x++) {
      int in = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          const float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
          const float cx = px < r ? r : px > s - r ? s - r : px, cy = py < r ? r : py > s - r ? s - r : py;
          in += (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
        }
      a[y * s + x] = in * 255 / 16;
    }
  pc->dsc = {};
  pc->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  pc->dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
  pc->dsc.header.w = s;
  pc->dsc.header.h = s;
  pc->dsc.header.stride = s * 2;
  pc->dsc.data_size = s * s * 3;
  pc->dsc.data = pc->px;
  return pc;
}

const lv_image_dsc_t* jpgIcon(const pkg::Package& p, int s) {
  const String key = p.id + "#" + p.code + "#" + s;
  auto it = pics.find(key);
  if (it != pics.end()) return it->second ? &it->second->dsc : nullptr;
  pics[key] = nullptr;  // tried once
  pkg::File f;
  if (!p.icon || !pkg::open(p.id.c_str(), "icon.jpg", f) || f.size() > 200000) return nullptr;
  uint8_t* jpg = pkg::loadAll(f);
  Pic* pc = jpg ? makePic(jpg, f.size(), s) : nullptr;
  heap_caps_free(jpg);
  pics[key] = pc;
  return pc ? &pc->dsc : nullptr;
}

const lv_image_dsc_t* glyphOf(const char* name, bool small) {
  // Image numbers, not pointers: the table stays in flash (art.h).
  struct G {
    const char* n;
    uint16_t big, sm;
  };
  static const G gs[] = {{"timer", ART_gl_timer_22, ART_gl_timer_16}, {"alarm", ART_gl_alarm_22, ART_gl_alarm_16},
                         {"bell", ART_gl_bell_22, ART_gl_bell_16},    {"list", ART_gl_list_22, ART_gl_list_16},
                         {"drop", ART_gl_drop_22, ART_gl_drop_16},    {"flag", ART_gl_flag_22, ART_gl_flag_16},
                         {"globe", ART_gl_globe_22, ART_gl_globe_16}, {"sun", ART_gl_sun_22, ART_gl_sun_16},
                         {"moon", ART_gl_moon_22, ART_gl_moon_16},    {"photo", ART_gl_photo_22, ART_gl_photo_16},
                         {"edit", ART_gl_edit_22, ART_gl_edit_16},    {"font", ART_gl_font_22, ART_gl_font_16},
                         {"home", ART_gl_home_22, ART_gl_home_16},    {"search", ART_gl_search_22, ART_gl_search_16},
                         {"speaker", ART_gl_speaker_22, ART_gl_speaker_16}, {"play", ART_gl_play_22, ART_gl_play_16},
                         {"lap", ART_gl_lap_22, ART_gl_lap_16},       {"pin", ART_gl_pin_22, ART_gl_pin_16},
                         {"leaf", ART_gl_leaf_22, ART_gl_leaf_16},    {"power", ART_gl_power_22, ART_gl_power_16},
                         {"grid", ART_gl_grid_22, ART_gl_grid_16}};
  for (const G& g : gs)
    if (name && !strcmp(name, g.n)) return art_dsc(small ? g.sm : g.big);
  return small ? &gl_grid_16 : &gl_grid_22;
}

struct Live {
  lv_obj_t* obj;
  const char* id;
  int size;
};
std::vector<Live> live;  // calendar and lunar icons on screen

void drawDate(lv_obj_t* box, const char* id, int size) {
  lv_obj_clean(box);
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  const int s = size == 52 ? 52 : size;  // drawn area (the 52 image has a 4 px shadow margin)
  if (!strcmp(id, "cal")) {
    if (s >= 40) {
      lv_obj_t* wd = label(box, upperVi(WEEKDAY[t.tm_wday]).c_str(), 12, true, col::RED);
      if (textW(lv_label_get_text(wd), 12, true) > s - 6) lv_label_set_text(wd, WD_SHORT[t.tm_wday]);
      lv_obj_set_style_text_font(wd, fonts::get(s >= 52 ? 12 : 12, true), 0);
      lv_obj_align(wd, LV_ALIGN_TOP_MID, 0, s >= 52 ? 2 : 1);
    }
    char d[4];
    snprintf(d, sizeof d, "%d", t.tm_mday);
    lv_obj_t* n = label(box, d, s >= 52 ? 28 : s >= 40 ? 22 : 16, s < 52, 0x16181d);
    if (s >= 52) lv_obj_set_style_text_font(n, fonts::get(28, true), 0);
    lv_obj_align(n, LV_ALIGN_CENTER, 0, s >= 40 ? 6 : 0);
  } else {
    if (s >= 40) {  // the icon's heading in the language shown (the art has none since 2026-11-18)
      lv_obj_t* hd = label(box, "", 12, true, 0xffd979);
      fitText(hd, upperVi(TC_("icon", "Lunar")).c_str(), s - 2, 18, 12, 12, true, 0);  // i18n: on the icon, ~7 letters
      lv_obj_align(hd, LV_ALIGN_TOP_MID, 0, s >= 52 ? 2 : 1);
    }
    const auto l = espos::lunar::fromSolar(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    char d[8];
    snprintf(d, sizeof d, "%d/%d", l.day, l.month);
    lv_obj_t* n = label(box, d, s >= 52 ? 20 : s >= 40 ? 16 : 13, true, 0xffd979);
    lv_obj_align(n, LV_ALIGN_CENTER, 0, s >= 40 ? 6 : 3);
  }
}

}  // namespace

const std::vector<App>& all() {
  rebuildList();
  return list;
}

uint32_t version() {
  rebuildList();
  return shownVer;
}

const App* find(const char* id) {
  if (!id) return nullptr;
  rebuildList();
  for (const App& a : list)
    if (!strcmp(a.id, id)) return &a;
  return nullptr;
}

void open(const char* id, const char* arg) {
  const App* a = find(id);
  trace::add("app open %s %s", id ? id : "?", arg ? arg : "");
  if (a && a->off) {
    os::toast(N_("Needs the memory card"));
    return;
  }
  if (a && a->pkg && lock::locked(a->pkg)) return lock::sheet(a->name, lock::Look::Os);  // paid, locked now (lock.h)
  // Like a phone: opening an app replaces the one on screen (no stacks of the same app).
  os::closeApps();
  setRoute(id, arg);
  if (a) notif::seen(a->id);  // its notifications stop counting on the icon
  if (a && a->pkg) pkg::run(a->pkg, arg);
  else if (a && a->open) a->open(arg);
}

namespace {
String routeId, routeArg;
}  // namespace
bool route(String& id, String& arg) {
  id = routeId, arg = routeArg;
  return routeId.length() > 0;
}
void setRoute(const char* id, const char* arg) { routeId = id ? id : "", routeArg = arg ? arg : ""; }
void clearRoute() { routeId = routeArg = ""; }

int badgeOf(const char* id) {
  // The app's own count and its unread notifications (notif.h calls App::badge itself, never this).
  return id ? notif::badge(id) : 0;
}

const lv_image_dsc_t* jpegIcon(const String& key, const uint8_t* jpg, size_t len, int s) {
  const String k = key + "#" + s;
  auto it = pics.find(k);
  if (it != pics.end()) return it->second ? &it->second->dsc : nullptr;
  Pic* pc = jpg && len ? makePic(jpg, len, s) : nullptr;
  pics[k] = pc;
  return pc ? &pc->dsc : nullptr;
}

lv_obj_t* drawnIcon(lv_obj_t* parent, uint32_t bg1, uint32_t bg2, const char* g, int s) {
  lv_obj_t* sq = box(parent, bg1, s * 27 / 100);
  grad(sq, bg1, bg2);
  lv_obj_set_size(sq, s, s);
  lv_obj_t* gl = glyph(sq, glyphOf(g, s < 40), 0xffffff);
  if (s >= 60) lv_image_set_scale(gl, 256 * s / 52);
  lv_obj_center(gl);
  return sq;
}

lv_obj_t* packageIcon(lv_obj_t* parent, const char* pkgId, int s) {
  const pkg::Package* p = pkg::find(pkgId);
#ifndef NO_PET
#if ESPOS_ONBOARD
  if (!(p && p->icon) && pkgId && !strcmp(pkgId, PET_PKG)) return drawnIcon(parent, 0xffcf7a, 0xe8892c, "play", s);
#else
  if ((p && p->id == PET_PKG && !p->icon) || (!p && pkgId && !strcmp(pkgId, PET_PKG))) {
    // PawBoop's own pictures (the 52 one has a shadow margin): scaled to the size asked.
    const lv_image_dsc_t* src = s > 40 ? &ic_pet_52 : s > 28 ? &ic_pet_40 : &ic_pet_28;
    lv_obj_t* i = img(parent, src);
    const int drawn = s > 40 ? 52 : s > 28 ? 40 : 28;
    if (drawn != s) lv_image_set_scale(i, 256 * s / drawn);
    if (s > 40) lv_obj_set_size(i, s, s), lv_image_set_inner_align(i, LV_IMAGE_ALIGN_CENTER);
    return i;
  }
#endif
#endif
  if (p && p->icon)
    if (const lv_image_dsc_t* d = jpgIcon(*p, s)) return img(parent, d);
  return drawnIcon(parent, p ? p->bg1 : 0x64748b, p ? p->bg2 : 0x334155, p ? p->glyph.c_str() : "grid", s);
}

lv_obj_t* icon(lv_obj_t* parent, const char* id, int size) {
  const App* a = find(id);
  if (a && a->js) {
    // An installed app: its picture or a gradient squircle with its glyph, in the same box as the images.
    const int s = size == 52 ? 52 : size;
    lv_obj_t* boxObj = clear(parent);
    lv_obj_set_size(boxObj, size == 52 ? 60 : s, size == 52 ? 60 : s);
    lv_obj_t* sq = packageIcon(boxObj, a->pkg ? a->pkg : a->id, s);
    lv_obj_align(sq, LV_ALIGN_CENTER, 0, size == 52 ? -1 : 0);
    if (a->off) lv_obj_set_style_opa(boxObj, LV_OPA_40, 0);
    return boxObj;
  }
  const lv_image_dsc_t* src = !a ? (size == 52 ? &ic_app_52 : size == 40 ? &ic_app_40 : &ic_app_28)
                              : size == 52 ? a->icon52 : size == 40 ? a->icon40 : a->icon28;
  lv_obj_t* i = img(parent, src);
  if (a && a->off) lv_obj_set_style_image_opa(i, LV_OPA_40, 0);
  if (a && (!strcmp(id, "cal") || !strcmp(id, "lunar"))) {
    // The live date sits in a box the size of the drawn icon (inside the shadow margin).
    lv_obj_t* box = clear(i);
    const int s = size == 52 ? 52 : size;
    lv_obj_set_size(box, s, s);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, size == 52 ? -1 : 0);
    drawDate(box, id, size);
    live.push_back({box, a->id, size});
    lv_obj_add_event_cb(box, [](lv_event_t* e) {
      lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
      for (auto it = live.begin(); it != live.end(); ++it)
        if (it->obj == o) {
          live.erase(it);
          break;
        }
    }, LV_EVENT_DELETE, nullptr);
  }
  return i;
}

void refreshIcons() {
  for (const Live& l : live) drawDate(l.obj, l.id, l.size);
}

}  // namespace apps
