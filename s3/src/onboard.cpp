// The first-boot setup (onboard.h, docs/onboarding-plan.md, mockups mockups/s3-onboarding.html).
#include "onboard.h"
#if ESPOS_ONBOARD  // off by default until the owner approves it (onboard.h)

#include <Preferences.h>
#ifndef SIM
#include <LittleFS.h>
#endif

#include "apps.h"
#include "appopts.h"
#include "art.h"
#include "brand.h"
#include "desk.h"
#include "fonts.h"
#include "i18n.h"
#include "keyboard.h"
#include "lock.h"
#include "net.h"
#include "os.h"
#include "pet/assist.h"
#include "pet/book.h"
#include "pet/chooser.h"
#include "pet/dogs.h"
#include "pet/packs.h"
#include "pet/pet.h"
#include "pet/talk.h"
#include "pet/view.h"
#include "pet/voice.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "trace.h"
#include "uikit.h"
#include "wake/wake.h"
#include "src/misc/lv_text_private.h"  // lv_text_encoded_next

#pragma GCC optimize("Os")  // screens made once: size over speed (the firmware's budget)

namespace onboard {
void pinScreen();

namespace {

using namespace kit;
using namespace pet;

enum : uint8_t { LANG, WIFI, DOG, NAME, CALL, CITY, PAIR, TIPS, DONE };
const char* const KEYS[] = {"lang", "wifi", "dog", "name", "call", "city", "pair", "tips"};  // s3.onboard "skip"
constexpr int VERSION = 1;  // this flow (a later one may show only its new steps)
// PawBoop's warm card (the dog's steps), as the dog's pop-up and the mockup.
constexpr uint32_t CREAM = 0xfff4e6, PANEL = 0xffe3c7, BROWN = 0x5a3517, SOFT = 0x7a6554, ORANGE = 0xff9a1f,
                   WARM = 0xffcf5c, WARM_FG = 0x5a3a00, GREEN = 0x1f9d4a, SAY = 0x3b2a20;
#define DEMO_FILE "/demo.json"

// ---------------------------------------------------------------- what NVS "onb" keeps
struct Nv {
  int v = -1;  // the flow version done (0 never); -1: not read yet
  int step = 0, skip = 0, demo = 0, sold = 0;
  String pin;
} nv;

void nvs(bool write) {  // "a" = v | step << 8 | demo << 16 | sold << 17, "skip", "pin"
  Preferences p;
  if (!p.begin("onb", !write)) return;
  if (write) {
    p.putInt("a", nv.v | nv.step << 8 | nv.demo << 16 | nv.sold << 17), p.putInt("skip", nv.skip), p.putString("pin", nv.pin);
  } else {
    const int a = p.getInt("a", 0);
    nv.v = a & 255, nv.step = a >> 8 & 255, nv.demo = a >> 16 & 1, nv.sold = a >> 17 & 1;
    nv.skip = p.getInt("skip", 0), nv.pin = p.getString("pin", "");
  }
  p.end();
}
void load() {
  if (nv.v < 0) nv.v = 0, nvs(false);
}
void save() { nvs(true); }

bool on = false, decided = false;
bool fix = false;  // "Finish setting up": only the steps that waited
int sub = 0;       // step 5: 0 the switches, 1 the test, 2 not heard; step 8: the tip
lv_obj_t* scr = nullptr;
uint32_t bootSet = 0;  // desk::settingsVersion() at boot (a demo unit restarts after visitors changed something)

// The screen's parts and what its step follows (cleared with the screen).
struct Ui {
  lv_obj_t *body, *bot, *next, *a, *b, *c, *d;
  View* view;
  int i, k, n, w;  // w: the text width of the column in use (the body, or its right half when turned)
  uint32_t t;
} u;

void show(int step);
void finish();

bool pairNeeded() { return claim::pending() || desk::link() != desk::Link::Linked; }
bool petTheme(int s) { return s == DOG || s == CALL || s == DONE; }
String dogName() { return dogs::name(); }
String callNames(int from) {  // the calls that are on, from call `from` ("Gâu gâu" is 0): what you say, not translated
  static const char* const CALLS[] = {"Gâu gâu", "Ki ki", "Lu lu", "Cún ơi", "Milu"};
  String s;
  for (int i = from; i < 5; i++)
    if (assist::prefs().calls >> i & 1) s += (s.length() ? ", " : "") + String(CALLS[i]);
  return s;
}
const char* dogFmt(const char* pattern) {  // the pattern with the dog's name for {0} (kept until the next call)
  static String s;
  s = i18n::fmt(pattern, dogs::name());
  return s.c_str();
}

void closed(lv_event_t* e) {
  if (lv_event_get_current_target(e) != scr) return;
  scr = nullptr, u = Ui();
  talk::begin();  // the wake word back to the dog's turns (the test took it)
}

// ---------------------------------------------------------------- the frame of a step
void goBack(lv_event_t*) { back(); }

void advance(bool done) {
  if (done) nv.skip &= ~(1 << nv.step);
  else nv.skip |= 1 << nv.step;
  int s = nv.step + 1;
  sub = 0;
  if (fix) {  // "Finish setting up" (or a claim made later): the steps that waited, then back where it was
    while (s < DONE && !(nv.skip >> s & 1)) s++;
    if (s >= DONE) return finish();
  }
  show(s);
}
void onNext(lv_event_t*) { advance(true); }
void onLater(lv_event_t*) { advance(false); }

lv_obj_t* lbl(lv_obj_t* parent, const char* text, int px, bool bold, uint32_t color, int w = 0) {
  lv_obj_t* l = label(parent, "", px, bold, color);
  if (w) wrapText(l, text, w, px, bold);
  else lv_label_set_text(l, i18n::trl(text));
  return l;
}

// The step's screen: a bar (‹, the steps as dots, "Later"), the title, a body that scrolls, the buttons at the bottom.
// Turned (most units stand sideways): the title sits in the bar and the dots beside the buttons, for a taller body.
void frame(const char* title, lv_event_cb_t later = nullptr) {
  os::closeApps();
  u = Ui();
  const bool pet = petTheme(nv.step) && !(nv.step == DONE && sub);
  const Pal& p = pal();
  const uint32_t fg = pet ? 0x2b2f3a : p.fg;
  lv_obj_t* b;
  scr = os::screen("", &b, os::BARE | os::NOSCROLL);
  lv_obj_add_event_cb(scr, closed, LV_EVENT_DELETE, nullptr);
  lv_obj_set_style_bg_color(scr, C(pet ? CREAM : p.bg), 0);
  os::barDarkFor(scr, pet || !p.dark);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* bar = clear(b);
  lv_obj_set_size(bar, lv_pct(100), 40);
  if (nv.step || sub) {
    lv_obj_t* k = clear(bar);
    lv_obj_set_size(k, 48, 40);
    lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(glyph(k, &gl_back_22, fg), LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_event_cb(k, goBack, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t* dots = stack(bar, true, 5);
  lv_obj_center(dots);
  for (int i = 0; i < DONE; i++) {
    const uint32_t c = pet ? (i < nv.step ? 0xf5b46b : i == nv.step ? ORANGE : 0xecd2b4)
                           : (i < nv.step ? 0x8fb9f2 : i == nv.step ? col::IOS_BLUE : 0xcdd0d7);
    lv_obj_set_size(box(dots, c, 3), i == nv.step ? 18 : 6, 6);
  }
  if (later) {
    lv_obj_t* l = label(bar, N_("Later"), 15, true, pet ? 0xa4520c : p.link);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(l, 12);
    lv_obj_add_event_cb(l, later, LV_EVENT_CLICKED, nullptr);
  }
  const bool land = os::landscape();
  if (title) {
    lv_obj_t* t = label(land ? bar : b, "", 22, true, fg);
    const int x = nv.step || sub ? 44 : 16;
    fitText(t, title, land ? os::W() - x - (later ? 100 : 16) : os::W() - 32, land ? 40 : 56, land ? 20 : 22, 14, true, 0);
    if (land) lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    else lv_obj_set_style_pad_hor(t, 16, 0);
  }
  u.body = clear(b);
  lv_obj_set_width(u.body, lv_pct(100));
  lv_obj_set_flex_grow(u.body, 1);
  lv_obj_set_flex_flow(u.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_hor(u.body, 12, 0);
  lv_obj_set_style_pad_ver(u.body, 8, 0);
  lv_obj_set_style_pad_row(u.body, 8, 0);
  lv_obj_add_flag(u.body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(u.body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(u.body, LV_SCROLLBAR_MODE_OFF);
  u.bot = clear(b);
  lv_obj_set_size(u.bot, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(u.bot, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_hor(u.bot, 16, 0);
  lv_obj_set_style_pad_bottom(u.bot, 14, 0);
  lv_obj_set_style_pad_column(u.bot, 8, 0);
  if (land) lv_obj_set_parent(dots, u.bot), lv_obj_set_flex_align(u.bot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  u.w = os::W() - 32;
  os::open(scr);  // (filled in the same loop: drawn once, complete)
}

lv_obj_t* btn(const char* text, bool warm, lv_event_cb_t cb, lv_obj_t* parent = nullptr) {
  lv_obj_t* k = button(parent ? parent : u.bot, text, warm ? WARM : col::IOS_BLUE, warm ? WARM_FG : 0xffffff, cb);
  lv_obj_set_height(k, 44);
  lv_obj_set_flex_grow(k, 1);
  return k;
}
lv_obj_t* text(const char* t, int px = 14, uint32_t c = 0) {  // a paragraph of the body
  return lbl(u.body, t, px, false, c ? c : petTheme(nv.step) ? SOFT : pal().dim, u.w);
}
// Turned (landscape): what was put in the body so far stays on the left, `leftW` wide; the body becomes the column on
// its right for what comes next. Upright: nothing changes.
void split(int leftW) {
  if (!os::landscape()) return;
  lv_obj_set_flex_flow(u.body, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(u.body, 12, 0);
  lv_obj_t* r = stack(u.body, false, 8);
  lv_obj_set_width(r, os::W() - 24 - leftW - 12);
  u.body = r, u.w = os::W() - 24 - leftW - 16;
}
// A sheet over the step's screen (dim behind): returns its column.
lv_obj_t* sheet() {
  lv_obj_t* d = box(scr, 0x0a0c12, 0);
  lv_obj_set_size(d, os::W(), os::H());
  lv_obj_set_style_bg_opa(d, 115, 0);
  lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* s = box(d, pal().cell, 18);
  lv_obj_set_size(s, os::W(), LV_SIZE_CONTENT);
  lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, 18);
  lv_obj_set_style_pad_all(s, 16, 0);
  lv_obj_set_style_pad_bottom(s, 34, 0);
  lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s, 6, 0);
  return s;
}
void centred(lv_obj_t* l) {  // a label across the body, its lines in the middle
  lv_obj_set_width(l, lv_pct(100));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}
// ---------------------------------------------------------------- 1. hello, the language, which way up
void fillLang(lv_obj_t* list) {
  lv_obj_clean(list);
  const Pal& p = pal();
  for (int i = 0; i < i18n::LANG_COUNT; i++) {
    const i18n::Lang* l = &i18n::LANGS[i];
    if (strcmp(l->tag, "en") && !i18n::installed(l->tag)) continue;
    const bool sel = !strcmp(l->tag, i18n::wanted());
    lv_obj_t* r = box(list, sel ? p.chip : p.cell, 12);
    lv_obj_set_size(r, lv_pct(100), os::landscape() ? 42 : 50);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* n = label(r, "", 16, sel, sel ? p.link : p.fg);
    // In its own language, never translated (anyone finds theirs); its English name while the fonts shown lack its
    // letters (日本語 before the Japanese font pack is in use).
    const lv_font_t* f = lv_obj_get_style_text_font(n, 0);
    bool ok = true;
    for (uint32_t k = 0, cp; ok && (cp = lv_text_encoded_next(l->name, &k));) {
      lv_font_glyph_dsc_t g;
      ok = lv_font_get_glyph_dsc(f, &g, cp, 0);
    }
    lv_label_set_text(n, ok ? l->name : T_(l->english));
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
    if (sel) lv_obj_align(glyph(r, &gl_check_16, p.link), LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      desk::setSetting("sys.lang", i18n::LANGS[(intptr_t)lv_event_get_user_data(e)].tag);
      fillLang(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e)));
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

void stepLang() {
  frame(nullptr);
  const Pal& p = pal();
  lv_obj_t* bar = lv_obj_get_child(lv_obj_get_child(scr, 0), 0);
  lv_obj_t* r = box(bar, p.chip, 18);  // which way up (no tilt sensor; the cat-ear 2.8" case stands it sideways)
  lv_obj_set_size(r, 36, 36);
  lv_obj_align(r, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(glyph(r, &gl_rotate_22, p.fg));
  lv_obj_add_event_cb(r, [](lv_event_t*) { os::applyRotation(os::rotation() + 1); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* h = stack(u.body, false, 4);
  lv_obj_set_width(h, os::landscape() ? 170 : lv_pct(100));
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* m = brand::mark(h, 96, 0x111111);
  lv_image_set_scale(m, 128);
  lv_obj_set_size(m, 48, 48);
  lv_image_set_inner_align(m, LV_IMAGE_ALIGN_CENTER);
  lv_obj_t* hi = label(h, N_("Hello!"), 24, true, p.fg);
  lv_obj_set_style_text_font(hi, fonts::text(30, true), 0);
  lbl(h, "Hello · Xin chào · Hola · Bonjour", 13, false, p.faint, os::landscape() ? 160 : 0);
  split(170);
  lv_obj_t* cap = caption(u.body, upperVi(T_("Language")).c_str());
  lv_obj_set_style_margin_top(cap, 10, 0);
  lv_obj_t* list = stack(u.body, false, 8);
  lv_obj_set_width(list, lv_pct(100));
  fillLang(list);
  btn(N_("Continue"), false, [](lv_event_t*) {
    const char* w = i18n::wanted();
    if (strcmp(w, i18n::lang()) && i18n::installed(w) && i18n::use(w)) os::rebuild();  // every screen in it
    advance(true);
  });
}

// ---------------------------------------------------------------- 2. the WiFi (Settings' own list)
void wifiNext(lv_event_t*) {  // on (with WiFi), or first what works without it
    if (net::state() == net::State::Connected) return advance(true);
    lv_obj_t* s = sheet();  // what works without it, what does not
    lv_obj_t* t = label(s, "", 19, true, pal().fg);
    wrapText(t, N_("Go on without WiFi for now?"), os::W() - 32, 19, true);
    lbl(s, N_("Your dog and the games work. Time, weather, talking and activation need WiFi; you will be reminded."), 14, false, pal().dim, os::W() - 32);
    lv_obj_t* r = stack(s, true, 8);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_style_margin_top(r, 6, 0);
    lv_obj_t* k = btn(N_("Go on anyway"), false, onLater, r);
    lv_obj_set_style_bg_color(k, C(pal().chip), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(k, 0), C(pal().fg), 0);
    btn(N_("Choose WiFi"), false, [](lv_event_t* e) {
      lv_obj_delete_async(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)))));
    }, r);
}

void stepWifi() {
  frame(N_("Connect to WiFi"), wifiNext);
  text(N_("For the right time, the weather and talking with your dog."));
  app_settings::wifiList(u.body);
  btn(N_("Continue"), false, wifiNext);
  u.k = net::state() == net::State::Connected;
  every(u.bot, 500, [](lv_timer_t*) {
    if (net::state() == net::State::Connected && !u.k) u.k = 1, u.t = millis();  // joined here: on by itself soon
    if (u.t && millis() - u.t > 1500) u.t = 0, advance(true);
  });
}

// ---------------------------------------------------------------- 3. the dog, one a page (the chooser's logic)
void dogPage();

void bubble(lv_obj_t* parent, const String& t) {
  // The line with each "…" (U+2026, or the "..." pk::shown spells it with) as one ".": never an ellipsis on screen.
  char s[256];
  int n = 0;
  for (const char* p = t.c_str(); *p && n < 255; p++)
    s[n++] = (*p == '\xE2' && p[1] == '\x80' && p[2] == '\xA6') || (*p == '.' && p[1] == '.' && p[2] == '.') ? (p += 2, '.') : *p;
  s[n] = 0;
  if (u.d) lv_obj_delete(u.d);
  u.d = box(parent, SAY, 13);
  lv_obj_set_size(u.d, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(u.d, 12, 0);
  lv_obj_set_style_pad_ver(u.d, 8, 0);
  lv_obj_t* l = label(u.d, "", 12, true, 0xffffff);
  lv_obj_set_style_text_font(l, pk::font(0), 0);  // the game's own bubble font (Lexend)
  lv_obj_update_layout(parent);
  wrapText(l, s, min((int)lv_obj_get_width(parent) - 40, 256), 12, true);
  lv_obj_set_style_text_font(l, pk::font(0), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(u.d, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_move_foreground(u.d);
}

void flip(int by) {
  u.i = (u.i + by + dogs::COUNT) % dogs::COUNT;
  dogPage();
}

void dogWatch(lv_timer_t*) {
  const dogs::Info& d = dogs::ALL[u.i];
  if (u.k >= 0 && dogs::installed(dogs::ALL[u.k].key) &&
      pkg::store::stateOf(dogs::pkgId(dogs::ALL[u.k].key).c_str()) == pkg::store::IDLE) {
    u.i = u.k, u.k = -1;  // the dog downloaded here: it is the dog now
    return dogPage();
  }
  // A dog on the device: its packs once they are loaded (switchDog is asynchronous), then the moving picture.
  if (!u.view && dogs::installed(d.key) && !strcmp(pk::dogKey(), d.key) && pk::ready()) return dogPage();
  String what;
  uint32_t col;
  int pct;
  chooser::state(u.i, what, col, pct);
  if (!dogs::installed(d.key) && u.next) setText(lv_obj_get_child(u.next, 0), what.c_str());
}

void dogPage() {
  const dogs::Info& d = dogs::ALL[u.i];
  const String id = dogs::pkgId(d.key);
  const bool land = os::landscape(), here = dogs::installed(d.key), live = here && !strcmp(pk::dogKey(), d.key) && pk::ready();
  if (here && !live && strcmp(pk::dogKey(), d.key)) switchDog(d.key, false);  // its packs come; dogWatch draws it then
  lv_obj_clean(u.a);
  if (!land) lv_obj_clean(u.bot);
  u.view = nullptr, u.d = nullptr, u.next = nullptr;
  const int sw = land ? 220 : os::W() - 24, sh = land ? os::H() - 112 : 196;  // (turned: only the dots below)
  lv_obj_t* stage = clear(u.a);
  lv_obj_set_size(stage, sw, sh);
  u.c = stage;
  if (live) {
    u.view = View::create(stage, sw, sh, 0, CREAM, (320 - sw) / 2, 292 - sh);
    u.view->play("stand");
    lv_obj_align(u.view->obj(), LV_ALIGN_BOTTOM_MID, 0, 0);
  } else {
    const lv_image_dsc_t* ic = here ? nullptr : app_store::catalogIcon(id.c_str(), 120);
    lv_obj_t* i = here ? apps::packageIcon(stage, id.c_str(), 120) : ic ? img(stage, ic) : apps::drawnIcon(stage, 0xffcf7a, 0xe8892c, "play", 120);
    lv_obj_center(i);
  }
  for (int k = 0; k < 2; k++) {  // ‹ ›
    lv_obj_t* a = box(stage, 0xffffff, 16);
    lv_obj_set_style_bg_opa(a, 190, 0);
    lv_obj_set_size(a, 32, 32);
    lv_obj_align(a, k ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, k ? -4 : 4, 0);
    lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(a, 10);
    lv_obj_center(glyph(a, k ? &gl_chev_16 : &gl_back_16, BROWN));
    lv_obj_add_event_cb(a, [](lv_event_t* e) { flip(lv_event_get_user_data(e) ? 1 : -1); }, LV_EVENT_CLICKED,
                        (void*)(intptr_t)k);
  }
  lv_obj_t* info = stack(u.a, false, 6);
  lv_obj_set_width(info, land ? os::W() - sw - 32 : lv_pct(100));
  lv_obj_t* row = clear(info);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_t* nm = label(row, d.name, 26, true, 0x2b2f3a);  // a name: as it is
  lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* pills = stack(row, false, 4);
  lv_obj_set_flex_align(pills, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_align(pills, LV_ALIGN_RIGHT_MID, 0, 0);
  String what;
  uint32_t col;
  int pct;
  chooser::state(u.i, what, col, pct);
  auto pill = [&](const String& t, uint32_t bg, uint32_t fg) {
    lv_obj_t* p = box(pills, bg, 10);
    lv_obj_set_size(p, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_pad_hor(p, 8, 0);
    lv_obj_t* l = label(p, "", 12, true, fg);
    fitText(l, t.c_str(), os::W() / 2 - 40, 18, 12, 12, true, 0);
    lv_obj_center(l);
  };
  pill(here ? String(T_("On this device")) : what, here ? 0xe3f5e9 : 0xe8efff, here ? 0x14743a : 0x1d4ed8);
  if (nv.sold) pill(T_("Comes with the device"), ORANGE, 0xffffff);  // the dog picked now is kept for good
  const String ds = chooser::desc(u.i);
  if (ds.length()) {
    lv_obj_t* t = label(info, "", 13, false, SOFT);
    fitText(t, ds.c_str(), land ? os::W() - sw - 32 : os::W() - 24, 40, 13, 11, false, 1);
  }
  lv_obj_t* dots = stack(info, true, 5);  // where it is among the ten
  lv_obj_set_style_margin_top(dots, 4, 0);
  for (int k = 0; k < dogs::COUNT; k++) lv_obj_set_size(box(dots, k == u.i ? ORANGE : 0xecd2b4, 3), k == u.i ? 16 : 6, 6);
  if (!land) lv_obj_set_width(dots, lv_pct(100)), lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  u.b = dots;
  // The buttons: its voice, then choose it (or download it).
  lv_obj_t* bp = land ? stack(info, true, 8) : u.bot;
  if (land) lv_obj_set_width(bp, lv_pct(100)), lv_obj_set_style_margin_top(bp, 6, 0);
  if (live) {
    lv_obj_t* v = btn(N_("Hear its voice"), true, [](lv_event_t*) {
      const pba_clip_t* l = voice::lineFor("@hello");
      if (!l || !u.view) return;
      voice::speak(l, true);
      u.view->play("nudge", 1);
      bubble(u.c, pk::shown(l));
    }, bp);
    lv_obj_set_style_bg_color(v, C(PANEL), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(v, 0), C(BROWN), 0);
    lv_obj_set_flex_grow(v, 0);
  }
  u.next = btn(here ? i18n::fmt(N_("Choose {0}"), d.name).c_str() : what.c_str(), true, [](lv_event_t*) {
    const dogs::Info& d = dogs::ALL[u.i];
    if (dogs::installed(d.key) && !strcmp(pk::dogKey(), d.key)) {
      dogs::choose(d.key);
      return advance(true);
    }
    if (chooser::pick(u.i) == 1) u.k = u.i;  // its download: dogWatch shows the percent, then the dog
  }, bp);
  if (!live && here) lv_obj_set_style_bg_opa(u.next, LV_OPA_50, 0);  // its packs are coming
}

void stepDog() {
  frame(N_("Choose your dog"));
  u.k = -1;
  const dogs::Info* d = dogs::chosen();
  if (!d) d = dogs::byKey(desk::setting("s3.pet_dog", "").c_str());
  u.i = d ? d - dogs::ALL : 0;
  u.a = clear(u.body);
  lv_obj_set_size(u.a, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(u.a, os::landscape() ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(u.a, 8, 0);
  dogPage();
  every(u.body, 300, dogWatch);
}

// ---------------------------------------------------------------- 4. the person's name (the assistant calls them so)
void saveName(lv_obj_t* ta) {
  String n = lv_textarea_get_text(ta);
  n.trim();
  desk::setSetting("s3.call_name", n);
  kb::close();
  advance(true);
}

void stepName() {
  frame(dogFmt(N_("What should {0} call you?")), onLater);
  text(N_("The assistant calls you by this name; change it on the web."));
  lv_obj_t* ta = app_settings::field(u.body, desk::setting("s3.call_name", "").c_str(), false);
  lv_textarea_set_max_length(ta, 40);
  hint(u.body, N_("Touch and hold a vowel to pick an accented letter."));
  lv_obj_add_event_cb(ta, [](lv_event_t* e) {
    kb::open((lv_obj_t*)lv_event_get_target(e), kb::Mode::Text, saveName);
  }, LV_EVENT_CLICKED, nullptr);
  u.a = ta;
  btn(N_("Continue"), false, [](lv_event_t*) { saveName(u.a); });
  kb::open(ta, kb::Mode::Text, saveName);
}

// ---------------------------------------------------------------- 5. the wake word: the switches, then a test
const char* const MODES[] = {N_("Off"), N_("On the charger"), N_("Always")};

// A group's name, then its chips (put in the returned parent): beside them when `beside` (turned, the whole width).
lv_obj_t* group(const char* name, bool beside) {
  lv_obj_t* r = u.body;
  if (beside) {
    r = stack(u.body, true, 10);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  }
  fitText(label(r, "", 14, true, 0x2b2f3a), name, beside ? 116 : u.w, 34, 14, 12, true, 0);
  return r;
}
void grow(lv_obj_t* chipRow, bool beside) {
  if (beside) lv_obj_set_flex_grow(chipRow, 1);
}

void notHeard() {  // the test: no call heard for 12 s
  u.k = 1;  // (once)
  if (u.view) u.view->play("look_up");
  if (os::landscape())  // beside the dog: why and how easily, in place of the call and the meter
    for (int i = lv_obj_get_child_count(u.body) - 1; i >= 0; i--)
      if (lv_obj_get_child(u.body, i) != u.b) lv_obj_add_flag(lv_obj_get_child(u.body, i), LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(u.b, C(BROWN), 0);
  wrapText(u.b, dogFmt(N_("{0} didn't hear it. Speak closer, about half a metre away, saying both words together. "
                          "Loud music or TV makes it harder.")), u.w, 13, true);
  static const char* const SENS[] = {N_("Less often"), N_("Normal"), N_("More often")};
  chips(group(N_("How easily it wakes"), false), SENS, 3, assist::prefs().wakeSens - 1, [](int i) {
    assist::Prefs q = assist::prefs();
    q.wakeSens = i + 1;
    assist::setPrefs(q);
  });
  btn(N_("Try again"), true, [](lv_event_t*) { show(CALL); });
  lv_obj_update_layout(scr);
  lv_obj_scroll_to_y(u.body, LV_COORD_MAX, LV_ANIM_OFF);
}

void testStage(int w, int h) {  // the dog listening (step 5's test)
  u.c = clear(u.body);
  lv_obj_set_size(u.c, w, h);
  if (pk::ready()) {
    u.view = View::create(u.c, w, h, 0, CREAM, (320 - w) / 2, 292 - h);
    u.view->play("alert");
  }
}

void heard(uint32_t) {  // the test took the wake word (talk::begin gives it back)
  if (!u.view) return;
  u.n++, u.t = millis();
  assist::Speech sp;
  if (assist::line("ack", sp)) assist::play(sp), bubble(u.c, sp.said);  // "Dạ?" in its own voice
  u.view->play("nudge", 1);
  lv_obj_set_style_text_color(u.a, C(GREEN), 0);
  fitText(u.b, u.n > 1 ? dogFmt(N_("Heard you! {0} knows your voice now.")) : T_("Heard you! Once more."),
          u.w, 40, 14, 12, true, 0);
  lv_obj_set_style_text_color(u.b, C(GREEN), 0);
  if (u.n > 1 && !u.next) {
    u.next = btn(N_("Continue"), true, onNext);
    lv_obj_update_layout(scr);
    lv_obj_scroll_to_view(u.b, LV_ANIM_OFF);
  }
}

void stepCall() {
  if (sub == 1) {  // the test: call it, it answers
    frame(dogFmt(N_("Try calling {0}")), onLater);
    const bool land = os::landscape();
    if (land) testStage(190, os::H() - 138);
    split(190);
    u.a = label(u.body, "\"Gâu gâu\"", 30, true, 0x2b2f3a);  // the call itself: never translated
    lv_obj_set_style_text_font(u.a, fonts::text(30, true), 0);
    centred(u.a);
    const String more = callNames(1);  // the other calls that are on
    if (more.length()) {
      lv_obj_t* m = lbl(u.body, i18n::fmt(N_("or {0}"), more).c_str(), 13, false, SOFT, u.w);
      centred(m);
    }
    if (!land) testStage(os::W() - 24, 188);
    lv_obj_t* m = stack(u.body, true, 3);  // the microphone's level
    lv_obj_set_size(m, lv_pct(100), 22);
    lv_obj_set_flex_align(m, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (int i = 0; i < 10; i++) lv_obj_set_size(box(m, 0xf1d3b0, 2), 6, 5);
    u.b = label(u.body, "", 13, true, 0xa4520c);
    fitText(u.b, T_("Listening. Say it loud, about a metre away."), u.w, 36, 13, 12, true, 0);
    centred(u.b);
    lv_obj_set_user_data(u.body, m);
    u.n = 0, u.t = millis();
    wake::onWake(heard);
    every(u.body, 100, [](lv_timer_t*) {
      lv_obj_t* m = (lv_obj_t*)lv_obj_get_user_data(u.body);
      const int lv = constrain((int)((wake::levelDb() + 62) / 4), 0, 10);
      for (int i = 0; i < 10; i++) {
        lv_obj_t* b = lv_obj_get_child(m, i);
        lv_obj_set_height(b, i < lv ? 6 + (i * 7 + 3) % 16 : 5);
        lv_obj_set_style_bg_color(b, C(i < lv ? (u.n ? GREEN : ORANGE) : 0xf1d3b0), 0);
      }
      if (u.n < 2 && millis() - u.t > 12000 && !u.k) notHeard();  // 12 s without a call: why, and how easily
    });
    return;
  }
  frame(dogFmt(N_("Call {0} with your voice")), onLater);
  lv_obj_t* c = box(u.body, 0xfffaf2, 14);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, 12, 0);
  lv_obj_t* mic = box(c, ORANGE, 16);
  lv_obj_set_size(mic, 32, 32);
  lv_obj_center(talk::micIcon(mic, 0xffffff));
  lbl(c, N_("To hear it, the microphone listens all the time while this is on. What it hears stays on the device: it is "
            "checked here for \"Gâu\" and forgotten after 2 seconds."), 13, false, 0x5a4636, os::W() - 92);
  lv_obj_set_pos(lv_obj_get_child(c, 1), 44, 0);
  const bool land = os::landscape();
  u.k = 2;  // proposed: Always (owner, 2026-11-21); stored when the person goes on
  grow(chips(group(N_("Listen for \"Gâu\""), land), MODES, 3, 2, [](int i) { u.k = i; }), land);
  grow(book::callChips(group(N_("It answers to"), land)), land);
  hint(u.body, N_("Each call turned on is one more way to wake it by mistake."));
  btn(dogFmt(N_("Try calling {0}")), true, [](lv_event_t*) {
    Preferences pr;  // the consent "Turn on" gives in the dog's book
    if (pr.begin("wake", false)) pr.putInt("ok", 1), pr.end();
    assist::Prefs q = assist::prefs();
    q.wake = u.k;
    assist::setPrefs(q);
    if (!u.k) return advance(true);  // off: no test
    sub = 1, show(CALL);
  });
}

// ---------------------------------------------------------------- 6. the city (and its time zone)
void stepCity() {
  frame(N_("Where are you?"), onLater);
  text(N_("The time and the weather follow where you are."));
  app_weather::cityPicker(u.body, app_settings::setZone);
  btn(N_("Continue"), false, onNext);
}

// ---------------------------------------------------------------- 7. the activation (the claim code), or hello
void stepPair() {
  if (claim::takeWelcome()) {  // the code was entered: this unit is theirs
    frame(nullptr);
    lv_obj_set_flex_align(u.body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* r = box(u.body, 0xe3f5e9, 32);
    lv_obj_set_size(r, 64, 64);
    lv_obj_center(glyph(r, &gl_check_22, GREEN));
    String who = desk::setting("s3.call_name", "");
    if (!who.length() && desk::user().indexOf('@') < 0) who = desk::user();  // a name, never an e-mail address
    lv_obj_t* t = label(u.body, "", 24, true, pal().fg);
    fitText(t, who.length() ? i18n::fmt(N_("Hello {0}!"), who).c_str() : N_("Hello!"), os::W() - 48, 64, 24, 16, true);
    lbl(u.body, N_("This device is yours now."), 15, false, pal().dim, os::W() - 48);
    lock::chip(u.body, false);  // "Trial · 7 days left" (lock.h; nothing when the plan has no trial)
    btn(N_("Continue"), false, onNext);
    return;
  }
  if (!pairNeeded()) return advance(true);  // linked already (a reset kept the account)
  frame(N_("Activate this device"), onLater);
  const bool up = net::state() == net::State::Connected;
  if (!claim::pending()) {  // a unit not prepared in Kho máy: the 6-digit link code of Settings
    text(N_("Link it to your EspOS account to talk with your dog and set it up from your phone."));
    btn(N_("Link to an account"), false, [](lv_event_t*) { app_settings::open("account"); });
  } else {
    const bool land = os::landscape();  // turned: every line beside the QR, the code on one
    const int kw = os::W() - 24 - 116 - 12;
    const char* go = N_("On espos.dev, create an account, choose Add device and enter this code, or scan the QR code with your phone.");
    if (!land) lbl(u.body, go, 13, false, pal().dim, os::W() - 32);
    lv_obj_t* r = stack(u.body, true, 12);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, land ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    qr(r, claim::url().c_str(), 116);
    lv_obj_t* k = stack(r, false, 6);
    lv_obj_set_width(k, kw);
    if (land) lbl(k, go, 13, false, pal().dim, kw);
    const String& cd = claim::code();
    lv_obj_t* c = label(k, (cd.substring(0, 4) + (land ? "-" : "-\n") + cd.substring(4)).c_str(), 26, true, pal().fg);
    lv_obj_set_style_text_font(c, fonts::text(30, true), 0);  // a size with every letter
    lv_obj_set_style_text_letter_space(c, 3, 0);
    lbl(k, N_("The code is also printed on the box label."), 12, false, pal().faint, kw);
    lbl(land ? k : u.body, dogFmt(N_("Once activated, {0} talks with you (a free trial first) and the weather, the news and the "
                                    "phone setup work.")), 13, false, pal().fg, land ? kw : u.w);
  }
  u.a = label(u.bot, "", 13, true, col::ORANGE);
  fitText(u.a, up ? T_("Waiting for you to enter the code") : T_("Activation needs WiFi: it goes on by itself once the device has WiFi."),
          os::W() - (os::landscape() ? 136 : 32), 36, 13, 12, true);
  every(u.body, 1000, [](lv_timer_t*) {
    if (!pairNeeded()) show(PAIR);  // claimed or linked: hello (or on)
  });
}

// ---------------------------------------------------------------- 8. three tips over a picture of Home
lv_image_dsc_t tipPic = {};  // Home at 45 %, made once per setup (freed at the end)
uint16_t* tipPx = nullptr;

void makeTipPic() {
  if (tipPx) return;
  lv_draw_buf_t* s = lv_snapshot_take(os::homeScreen(), LV_COLOR_FORMAT_RGB565);
  if (!s) return;
  const int sw = s->header.w, sh = s->header.h, w = sw * 9 / 20, h = sh * 9 / 20;
  tipPx = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  if (tipPx) {
    for (int y = 0; y < h; y++)  // nearest of four, averaged: smooth enough at this size
      for (int x = 0; x < w; x++) {
        const int sx = x * 20 / 9, sy = y * 20 / 9;
        uint32_t r = 0, g = 0, b = 0;
        for (int k = 0; k < 4; k++) {
          const uint16_t c = ((uint16_t*)(s->data + (sy + (k >> 1)) * s->header.stride))[min(sx + (k & 1), sw - 1)];
          r += c >> 11, g += c >> 5 & 63, b += c & 31;
        }
        tipPx[y * w + x] = (r / 4) << 11 | (g / 4) << 5 | b / 4;
      }
    tipPic.header.magic = LV_IMAGE_HEADER_MAGIC;
    tipPic.header.cf = LV_COLOR_FORMAT_RGB565;
    tipPic.header.w = w, tipPic.header.h = h, tipPic.header.stride = w * 2;
    tipPic.data_size = w * h * 2, tipPic.data = (const uint8_t*)tipPx;
  }
  lv_draw_buf_destroy(s);
}

void finger(lv_obj_t* parent, int x, int y, int dy) {  // a finger moving down (dy > 0) or up, again and again
  lv_obj_t* f = box(parent, 0xffffff, 13);
  lv_obj_set_size(f, 26, 26);
  lv_obj_set_style_bg_opa(f, 180, 0);
  lv_obj_set_style_border_color(f, C(col::IOS_BLUE), 0);
  lv_obj_set_style_border_width(f, 2, 0);
  lv_obj_set_pos(f, x, y);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, f);
  lv_anim_set_values(&a, y, y + dy);
  lv_anim_set_duration(&a, 1200);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_repeat_delay(&a, 500);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_y((lv_obj_t*)o, v); });
  lv_anim_start(&a);
}

void stepTips() {
  static const char* const TITLE[] = {N_("Call your dog any time"), N_("Talk with {0}"), N_("Getting around")};
  frame(dogFmt(TITLE[sub]), onLater);
  makeTipPic();
  const int pw = tipPic.header.w, ph = tipPic.header.h;
  lv_obj_t* p = box(u.body, 0x000000, 12);
  lv_obj_set_size(p, pw, ph);
  lv_obj_set_style_clip_corner(p, true, 0);
  lv_obj_set_style_margin_left(p, (os::W() - 24 - pw) / 2, 0);
  if (tipPx) img(p, &tipPic);
  const String nm = dogName();
  if (os::landscape()) lv_obj_set_style_margin_left(p, 0, 0);
  if (sub == 0) {  // the Control Center comes down from the top right; its dog tile
    lv_obj_t* tl = box(p, 0x3a3f4a, 9);
    lv_obj_set_size(tl, 58, 46);
    lv_obj_align(tl, LV_ALIGN_CENTER, 10, -16);
    lv_obj_set_style_border_color(tl, C(ORANGE), 0);
    lv_obj_set_style_border_width(tl, 3, 0);
    lv_obj_align(glyph(tl, &gl_home_16, 0xffffff), LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t* n = label(tl, "", 11, true, 0xffffff);
    fitText(n, nm.c_str(), 54, 18, 12, 12, true, 0);
    lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, 0);
    finger(p, pw - 34, 0, ph / 3);
  } else if (sub == 1) {  // its pop-up: the dog listening, its mic button
    lv_obj_t* cd = box(p, CREAM, 10);
    lv_obj_set_size(cd, pw - 8, ph * 3 / 5);
    lv_obj_align(cd, LV_ALIGN_BOTTOM_MID, 0, -4);
    if (pk::ready()) View::create(cd, pw - 8, ph * 3 / 5, 1, CREAM, (160 - pw + 8) / 2, 146 - ph * 3 / 5)->play("alert");
    lv_obj_t* m = box(cd, 0xe5484d, 15);
    lv_obj_set_size(m, 30, 30);
    lv_obj_align(m, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    finger(p, pw - 38, ph - 36, 4);
  } else {  // up from the bottom: Home
    finger(p, pw / 2 - 13, ph - 30, -ph / 3);
  }
  split(pw);
  static const char* const TEXT[] = {
      N_("Swipe down from the top right: the Control Center. Tap {0} to call your dog."),
      N_("Say \"Gâu gâu\", then ask. Or hold the mic button, speak, let go to send. Swipe down to close."),
      N_("Swipe up from the bottom: Home. In from the left edge: back. Down at the top left: notifications.")};
  text(dogFmt(TEXT[sub]), 14, pal().fg);
  btn(sub < 2 ? N_("Next") : N_("Done"), false, [](lv_event_t*) {
    if (sub < 2) return sub++, show(TIPS);
    advance(true);
  });
}

// ---------------------------------------------------------------- 9. all set
void doneRow(lv_obj_t* c, const char* name, const char* value, uint32_t color) {  // no value: it waits ("Later")
  lv_obj_t* row = clear(c);
  lv_obj_set_size(row, lv_pct(100), 28);
  lv_obj_align(label(row, name, 14, false, SOFT), LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* v = label(row, "", 14, true, value && *value ? color : 0xc2410c);
  fitText(v, value && *value ? value : T_("Later"), u.w - 110, 22, 14, 12, true, 0);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
}

void stepDone() {
  frame(nullptr);
  lv_obj_t* st = clear(u.body);
  const int sw = os::landscape() ? 200 : os::W() - 24, sh = os::landscape() ? os::H() - 140 : 160;
  lv_obj_set_size(st, sw, sh);
  u.c = st;
  if (pk::ready()) {
    View* v = View::create(st, sw, sh, 0, CREAM, (320 - sw) / 2, 292 - sh);
    lv_obj_align(v->obj(), LV_ALIGN_BOTTOM_MID, 0, 0);
    v->play("high_five", 1);
    if (const pba_clip_t* l = voice::lineFor("high_five"))  // its words over its paws: the face stays in sight
      voice::speak(l, true), bubble(st, pk::shown(l)), lv_obj_align(u.d, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
  split(sw);
  lv_obj_t* t = label(u.body, N_("All set!"), 28, true, 0x2b2f3a);
  lv_obj_set_style_text_font(t, fonts::text(30, true), 0);
  centred(t);
  lv_obj_t* c = box(u.body, 0xfffaf2, 14);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(c, 14, 0);
  lv_obj_set_style_pad_ver(c, 4, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  doneRow(c, N_("Dog"), dogName().c_str(), 0x2b2f3a);
  doneRow(c, N_("Calls"), assist::prefs().wake ? callNames(0).c_str() : nv.skip >> CALL & 1 ? nullptr : T_("Off"), 0x2b2f3a);
  doneRow(c, N_("City"), app_weather::cityNow().c_str(), 0x2b2f3a);
  doneRow(c, N_("Account"), pairNeeded() ? nullptr : T_("Activated"), GREEN);
  btn(N_("Start"), true, [](lv_event_t*) { finish(); });
}

// ---------------------------------------------------------------- the flow
void show(int s) {
  if (s > DONE) s = DONE;
  on = true;
  nv.step = s;
  save();
  trace::add("onboard: step %d.%d", s, sub);
  switch (s) {
    case LANG: stepLang(); break;
    case WIFI: stepWifi(); break;
    case DOG: stepDog(); break;
    case NAME: stepName(); break;
    case CALL: stepCall(); break;
    case CITY: stepCity(); break;
    case PAIR: stepPair(); break;
    case TIPS: stepTips(); break;
    default: stepDone(); break;
  }
}

void finish() {
  on = fix = false, sub = 0;
  nv.v = VERSION, nv.step = 0;
  save();
  char j[160];  // s3.onboard for the web (and the server's grant of the dog picked now): what waited
  int n = snprintf(j, sizeof j, "{\"v\":1,\"done\":%ld,\"dog\":\"%s\",\"skip\":[", (long)(desk::nowMs() / 1000), pk::dogKey());
  for (int i = 0; i < DONE; i++)
    if (nv.skip >> i & 1) n += snprintf(j + n, sizeof j - n, "%s\"%s\"", j[n - 1] == '[' ? "" : ",", KEYS[i]);
  snprintf(j + n, sizeof j - n, "]}");
  desk::setSetting("s3.onboard", j);
  if (tipPx) heap_caps_free(tipPx), tipPx = nullptr;
  os::closeApps();
  lv_timer_t* t = lv_timer_create([](lv_timer_t* t) {  // Home, then the dog says hello for the first time
    lv_timer_delete(t);
    if (os::atHome()) summon("cc");
  }, 1200, nullptr);
  (void)t;
  trace::add("onboard: done %s", j);
}

int left() {  // the steps still waiting (some got done elsewhere: the WiFi joined, the device linked)
  int n = 0;
  for (int i = WIFI; i < DONE; i++)
    n += (nv.skip >> i & 1) && !(i == WIFI && net::state() == net::State::Connected) && !(i == PAIR && !pairNeeded());
  return n;
}

// ---------------------------------------------------------------- a store's demo unit (recorded lines only)
uint32_t dogAt = 0;

void demoWake(uint32_t) {  // "Gâu gâu": the dog comes and says its greeting (no AI turn on a demo unit)
  if (!os::atHome()) os::closeApps();
  if (!summoned()) summon("demo");
}

void demoTick() {
  // A minute without a touch at Home (the saver too): the next dog on the device, then it comes and greets once its
  // pictures are in (its pop-up closes by itself after 20 s).
  static bool due = false;
  if (os::atHome() && !summoned() && os::idleMs() > 60000 && millis() - dogAt > 60000) {
    dogAt = millis(), due = true;
    if (const dogs::Info* d = dogs::chosen())
      for (int i = 1; i < dogs::COUNT; i++)
        if (dogs::installed(dogs::ALL[(d - dogs::ALL + i) % dogs::COUNT].key)) {
          switchDog(dogs::ALL[(d - dogs::ALL + i) % dogs::COUNT].key, false);
          break;
        }
  }
  const dogs::Info* d = dogs::chosen();
  if (due && d && pk::ready() && !strcmp(pk::dogKey(), d->key)) {
    due = false;
    if (os::saverOn()) os::wake();
    summon("demo");
  }
#ifndef SIM
  // Visitors changed something: the snapshot comes back with a restart (early()) after 10 minutes alone.
  if (os::idleMs() > 600000 && desk::settingsVersion() != bootSet) ESP.restart();
#endif
}

void demoBegin() {
  assist::Prefs q = assist::prefs();
  if (!q.wake) q.wake = 2, assist::setPrefs(q);
  wake::onWake(demoWake);
  bootSet = desk::settingsVersion();
  trace::add("onboard: demo unit");
}

void copyFile(const char* from, const char* to) {
#ifndef SIM
  File a = LittleFS.open(from, "r");
  if (!a) return;
  File b = LittleFS.open(to, "w");
  uint8_t buf[256];
  for (int n; (n = a.read(buf, sizeof buf)) > 0;) b.write(buf, n);
  a.close(), b.close();
#endif
}

void setDemo(bool d) {
  load();
  nv.demo = d;
  if (d) {
    copyFile("/settings.json", DEMO_FILE);  // what comes back at every start
    on = false;
    os::closeApps();
    demoBegin();
  } else {
#ifndef SIM
    LittleFS.remove(DEMO_FILE);
#endif
    nv.v = 0, nv.step = 0, nv.skip = 0;  // ready to sell: the setup from step 1
    talk::begin();
    show(LANG);
  }
  save();
}

}  // namespace

// ---------------------------------------------------------------- public
void early() {
  load();
  if (nv.demo) copyFile(DEMO_FILE, "/settings.json");
}

void tick() {
  load();
  if (!decided) {
    decided = true;
    if (nv.demo) return demoBegin();
    const bool again = desk::setting("s3.onboard", "") == "again";
    if (nv.v >= VERSION && !again) return;
    if (!nv.v && !again && !claim::pending() && desk::hadSettings()) {  // a device in use took this firmware
      nv.v = VERSION;
      return save();
    }
    if (again && nv.v) nv.v = 0, nv.step = 0, nv.skip = 0;
    if (claim::pending()) nv.sold = 1;
    on = true;
    trace::add("onboard: setup from step %d", nv.step);
  }
  if (nv.demo) return demoTick();
  if (!on) {
    if (claim::welcomeWaits() && os::atHome() && !os::saverOn() && os::idleMs() > 1000) {  // claimed after "Later"
      fix = true, sub = 0;
      return show(PAIR);  // "Hello Lan!", then on with what still waits
    }
    static uint32_t seen = 0;
    if (seen != desk::settingsVersion() && desk::setting("s3.onboard", "") == "again")
      nv.v = 0, nv.step = 0, nv.skip = 0, show(LANG);  // the web asked for the setup again
    seen = desk::settingsVersion();
    return;
  }
  if (!scr && millis() > 1500) show(nv.step);  // (again after a rotation or a language rebuilt the screens)
}

bool running() { return on && scr && lv_obj_is_valid(scr) && !os::atHome(); }

bool back() {
  if (!on || !scr || lv_screen_active() != scr) return false;
  if (sub) {  // the test and its "not heard" go back to the switches; a tip to the tip before
    sub = nv.step == CALL ? 0 : sub - 1;
    show(nv.step);
    return true;
  }
  if (fix) {  // "Finish setting up": back where it was opened
    on = fix = false;
    os::closeApps();
    return true;
  }
  int s = nv.step - 1;
  while (s > LANG && s == PAIR && !pairNeeded()) s--;
  if (s >= 0) show(s);
  return true;
}

void settingsRow(lv_obj_t* body) {
  load();
  const int n = left();
  if (on || nv.demo || !n) return;
  lv_obj_t* r = listRow(body, N_("Finish setting up"), i18n::plural(n, NP_("{n} step left|{n} steps left")).c_str(),
                        [](lv_event_t*) {
                          fix = true, sub = 0;
                          int s = WIFI;
                          while (s < DONE && !(nv.skip >> s & 1)) s++;
                          show(s);
                        });
  lv_obj_set_style_border_color(r, C(col::ORANGE), 0);
  lv_obj_set_style_border_width(r, 2, 0);
}

void aboutRows(lv_obj_t* body) {
  load();
  lv_obj_t* r = listRow(body, N_("Erase data and set up again"), "", [](lv_event_t*) {
    lv_obj_t* b;
    lv_obj_t* s = os::screen(N_("Erase data and set up again"), &b);
    hint(b, N_("WiFi, settings, notes, photos and the dog's game are erased; apps and the account stay. Then "
               "the first setup starts."));
    lv_obj_t* k = button(b, N_("Hold 3 seconds to erase"), col::RED, 0xffffff, nullptr);
    lv_obj_set_size(k, lv_pct(100), 44);
    lv_obj_add_event_cb(k, [](lv_event_t* e) {
      static uint32_t at = 0;
      const lv_event_code_t c = lv_event_get_code(e);
      if (c == LV_EVENT_PRESSED) at = millis();
      else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) at = 0;
      else if (c == LV_EVENT_PRESSING && at && millis() - at > 3000) {
        at = 0;
        trace::add("onboard: erase from Settings");
#ifdef SIM
        nv.v = nv.step = nv.skip = 0, save(), show(LANG);
#else
        claim::usb("ESPOS:FACTORY_RESET");  // the person's data and WiFi, then a restart (claim.cpp)
#endif
      }
    }, LV_EVENT_ALL, nullptr);
    os::open(s);
  });
  lv_obj_set_style_text_color(lv_obj_get_child(r, 0), C(col::RED), 0);
  if (!nv.demo && !nv.pin.length()) return;
  // A demo unit: the shop's PIN turns the mode off (or on, from the setup's first screen: app_settings "demo").
  listRow(body, nv.demo ? N_("Turn off demo mode") : N_("Turn on demo mode"), "", [](lv_event_t*) { pinScreen(); });
}

// The shop's PIN (the mark held on the first screen, or Settings › About): turns the demo mode on or off.
void pinScreen() {
  load();
  lv_obj_t* b;
  lv_obj_t* s = os::screen(N_("Demo mode"), &b, os::NOSCROLL);
  hint(b, nv.demo ? N_("Type the shop's PIN to end demo mode; the first setup follows.")
                  : N_("Type the shop's PIN for demo mode."));
  lv_obj_t* ta = app_settings::field(b, "", true);
  kb::open(ta, kb::Mode::Password, [](lv_obj_t* ta) {
    if (String(lv_textarea_get_text(ta)) != nv.pin || !nv.pin.length()) return os::toast(N_("Wrong PIN"));
    kb::close();
    os::closeApps();
    setDemo(!nv.demo);
  }, N_("OK"));
  os::open(s);
}

bool usb(const char* line) {
  load();
  if (!strncmp(line, "ESPOS:DEMO ", 11)) {
    const char* a = line + 11;
    if (!strncmp(a, "pin ", 4)) nv.pin = a + 4, save();
    else {
      if (a[0] == '1' && a[1] == ' ') nv.pin = a + 2;
      setDemo(a[0] == '1');
    }
    Serial.println("ESPOS:OK");
    return true;
  }
#if defined(SIM) || defined(ESPOS_QA)  // tests (the simulator, QEMU's QA image): any step at once
  if (strncmp(line, "ESPOS:ONBOARD ", 14)) return false;
  const char* a = line + 14;
  if (!strcmp(a, "reset")) nv.v = 0, nv.skip = 0, nv.sold = claim::pending(), sub = 0, fix = false, show(LANG);
  else if (!strncmp(a, "step ", 5)) sub = atoi(a + 5) >= 0 && strchr(a + 5, '.') ? atoi(strchr(a + 5, '.') + 1) : 0, show(atoi(a + 5));
  else if (!strcmp(a, "done")) finish();
  Serial.printf("ESPOS:ONBOARD {\"v\":%d,\"on\":%d,\"step\":%d,\"skip\":%d,\"demo\":%d}\n", nv.v, on, nv.step, nv.skip, nv.demo);
  return true;
#else
  return false;
#endif
}

int state() {
  load();
  return nv.demo ? -1 : on ? 100 + nv.step : nv.v;
}

}  // namespace onboard
#endif  // ESPOS_ONBOARD
