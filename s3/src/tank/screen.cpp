// Hồ cá cảnh: the app screen and the saver's background (tank.h). One water (tank_int.h sc) and one game, shared:
// while the app is in front its timer draws; while the saver shows a tank page, the saver's image draws (slower).
//
// A frame: the water moves (step, real time), the rules tick, then prepare() works out every thing's drawing once
// and the picture goes to the panel in pieces (lcd::native: the 80-row bands of the 3.5" board, or its columns when
// held on its side; the 2.8" board's own 213 x 320). The water is never drawn under the shop sheet. When something
// of the OS lies over the game (a notification, the Control Center) or a panel has no native path (the simulator),
// the same picture is drawn into a buffer that LVGL shows (on the 2.8" board grown to 320 x 480 for LVGL's 2/3).
#pragma GCC optimize("Os")
#include "tank.h"

#include <Arduino.h>
#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <new>

#include "desk.h"
#include "hal/board.h"
#include "home.h"
#include "i18n.h"
#include "lcd.h"
#include "os.h"
#include "pkg/pkg.h"
#include "tank_int.h"
#include "trace.h"

#ifdef SIM
#include <chrono>
namespace {
uint32_t nowUs() {  // the simulator's micros() is its simulated time: the bench wants the computer's
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace
namespace lcd {  // the simulator's panel has no native path unless its driver gives one
__attribute__((weak)) bool native(NativeFill, void*, bool) { return false; }
}  // namespace lcd
#else
namespace {
inline uint32_t nowUs() { return micros(); }
}  // namespace
#endif

namespace tank {
namespace {

struct Stats {
  uint32_t at = 0, pics = 0, late = 0;
  uint64_t move = 0, prep = 0, comp = 0, push = 0;
  uint32_t maxFrame = 0;
  long px = 0;
  int via = 0;  // pictures through LVGL
};

struct App {
  lv_obj_t *scr = nullptr, *img = nullptr;
  lv_image_dsc_t dsc;
  uint16_t* buf = nullptr;  // the screen for LVGL (320 x 480 or 480 x 320)
  lv_timer_t* timer = nullptr;
  uint32_t last = 0, lastPic = 0;
  bool wasOn = false;
  Stats win;
  // the bench: phases of N fish, each 1 s to settle and 5 s measured
  int bench = 0, benchN[3] = {5, 12, 20}, benchCount = 3;
  uint32_t benchAt = 0;
  Stats bw;
};
App* app = nullptr;

struct SaverBg {
  lv_obj_t* img = nullptr;
  lv_image_dsc_t dsc;
  uint16_t* buf = nullptr;
  lv_timer_t* timer = nullptr;
  uint32_t last = 0, lastPic = 0;
};
SaverBg* sv = nullptr;

int users = 0, fps = 30, saverFps = 12;
bool tear = false, freeWhenIdle = false;
uint16_t* nat = nullptr;  // the picture at the panel's size, for the LVGL path on a scaled panel
lv_timer_t* freeTimer = nullptr;
uint32_t compUs = 0;  // time inside the pieces' drawing this picture

float boardScale() {
  const hal::UiMap& u = hal::board().display.ui;
  return (float)u.num / u.den;
}

// ---------------------------------------------------------------- the water and the game, made when first wanted
void freeAll() {
  if (users > 0) return;
  game::stop();
  freeScene();
  heap_caps_free(nat), nat = nullptr;
  gfx::end();
  sprTrim(false);
  trace::add("tank: water freed");
}
bool ensure(const char* id) {
  if (freeTimer) lv_timer_delete(freeTimer), freeTimer = nullptr;
  String err;
  if (!load(id, err)) {
    trace::add("tank: cannot load %s: %s", id, err.c_str());
    return false;
  }
  const bool fresh = !sc;
  if (!sc) {
    void* m = heap_caps_calloc(1, sizeof(Scene), MALLOC_CAP_SPIRAM);
    if (!m) return false;
    sc = new (m) Scene();
  }
  const float s = boardScale();
  if (fresh || sc->land != os::landscape() || fabsf(sc->SC - s) > 0.001f || !sc->bgRaw) sc->setView(os::landscape(), s);
  if (!sc->bgRaw || !sc->bgc) {
    freeScene();
    return false;
  }
  gfx::begin(s);
  if (fresh) game::start();
  return true;
}
void release() {
  if (--users > 0) return;
  users = 0;
  game::save();
  // freed a moment later: a turn of the board closes and opens the app again
  if (!freeTimer) freeTimer = lv_timer_create([](lv_timer_t*) {
      lv_timer_delete(freeTimer), freeTimer = nullptr;
      freeAll();
    }, 1500, nullptr);
}

// ---------------------------------------------------------------- a picture
void stepAll(float dt) {
  for (float left = dt; left > 0.0001f;) {
    const float s = std::min(left, 1 / 30.0f);
    sc->step(s);
    left -= s;
  }
  game::tick(dt);
  ui::tick(dt);
}

void piece(uint16_t* dst, int dx, int dy, int x0, int y0, int x1, int y1, void*) {
  const uint32_t t0 = nowUs();
  const Tgt t = {dst, dx, dy, x0, y0, x1, y1};
  float cx0, cy0, cx1, cy1;
  ui::cover(cx0, cy0, cx1, cy1);
  if (cx1 > cx0) {  // the sheet covers the rest: the water only where it shows
    Tgt w = t;
    if (cy0 > 0) w.y1 = std::min(t.y1, (int)(cy0 * sc->SC));
    else w.x1 = std::min(t.x1, (int)(cx0 * sc->SC));
    if (w.x1 > w.x0 && w.y1 > w.y0) sc->draw(w);
  } else sc->draw(t);
  const uint32_t t1 = nowUs();
  ui::draw(t);
  sc->prof[7] += nowUs() - t1;
  compUs += nowUs() - t0;
}

// The panel's picture grown to the UI's size for LVGL (2.8": 213 x 320 -> 320 x 480), nearest pixel: the driver's
// 2/3 area shrink (scale::down32) gives the picture back nearly as drawn. Rows that repeat are copied.
void grow(const uint16_t* s, int sw, int sh, uint16_t* d, int dw, int dh) {
  static int16_t* xs = (int16_t*)heap_caps_malloc(480 * 2, MALLOC_CAP_SPIRAM);  // (not internal RAM)
  if (!xs) return;
  for (int x = 0; x < dw && x < 480; x++) xs[x] = (int16_t)std::min(sw - 1, x * sw / dw);
  int last = -1;
  for (int y = 0; y < dh; y++) {
    const int sy = std::min(sh - 1, y * sh / dh);
    uint16_t* o = d + y * dw;
    if (sy == last) memcpy(o, o - dw, dw * 2);
    else {
      const uint16_t* r = s + sy * sw;
      for (int x = 0; x < dw; x++) o[x] = r[xs[x]];
    }
    last = sy;
  }
}

// the whole picture into a buffer LVGL shows (UI size)
void intoBuffer(uint16_t* buf, int w, int h) {
  if (sc->FW == w && sc->FH == h) return piece(buf, 1, w, 0, 0, w, h, nullptr);
  if (!nat) nat = (uint16_t*)heap_caps_malloc(320 * 480 * 2, MALLOC_CAP_SPIRAM);
  if (!nat) return;
  piece(nat, 1, sc->FW, 0, 0, sc->FW, sc->FH, nullptr);
  grow(nat, sc->FW, sc->FH, buf, w, h);
}

bool overlays() {  // anything of the OS over the game: then LVGL draws the picture (src/run/run.cpp's rule)
  for (lv_obj_t* layer : {lv_layer_top(), lv_layer_sys()})
    for (uint32_t i = 0; i < lv_obj_get_child_count(layer); i++) {
      lv_obj_t* o = lv_obj_get_child(layer, i);
      if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
      lv_area_t a;
      lv_obj_get_coords(o, &a);
      const bool clear = lv_obj_get_style_bg_opa(o, 0) == LV_OPA_TRANSP;
      if (a.y2 < os::SB_H) continue;                                                        // the status bar
      if (clear && a.x2 < 24 && !lv_obj_get_child_count(o)) continue;                       // the back strip
      if (clear && a.y1 >= os::H() - 30 && a.x1 <= 0 && a.x2 >= os::W() - 1) continue;  // the home line (drawn by us)
      return true;
    }
  return false;
}

bool onScreen() { return app && app->scr && lv_screen_active() == app->scr && !os::saverOn(); }

void addStats(Stats& s, uint32_t move, uint32_t prep, uint32_t total, bool lvgl) {
  s.pics++, s.move += move, s.prep += prep, s.comp += compUs, s.push += total - move - prep - compUs;
  s.maxFrame = std::max(s.maxFrame, total), s.via += lvgl;
}
void report(const char* tag, Stats& s, int nfish) {
  const uint32_t ms = millis() - s.at;
  if (!s.pics || !ms) return;
  const float n = s.pics;
  char b[200];
  snprintf(b, sizeof b, "%s %d fish %s%dx%d: %.1f pics/s, move %.2f ms, prepare %.2f, draw %.2f, push %.2f, worst %.1f ms, %ld px, %d via LVGL",
           tag, nfish, sc->land ? "side " : "", sc->FW, sc->FH, n * 1000 / ms, s.move / n / 1000, s.prep / n / 1000, s.comp / n / 1000,
           s.push / n / 1000, s.maxFrame / 1000.0f, (long)(s.px / n), s.via);
  trace::add("%s", b);
  char q[160];
  const uint32_t* P = sc->prof;
  snprintf(q, sizeof q, "  draw parts ms: bg %.2f, plants %.2f, fish %.2f, food %.2f, specks %.2f, beams %.2f, algae %.2f, screens %.2f",
           P[0] / n / 1000, P[1] / n / 1000, P[2] / n / 1000, P[3] / n / 1000, P[4] / n / 1000, P[5] / n / 1000, P[6] / n / 1000, P[7] / n / 1000);
  memset(sc->prof, 0, sizeof sc->prof);
#ifdef SIM
  fprintf(stderr, "%s\n%s\n", b, q);
#else
  Serial.println(b), Serial.println(q);
#endif
}

// one picture of the app: moved, prepared, sent
void appFrame(float dt, bool viaLvgl) {
  const uint32_t t0 = nowUs();
  ui::saver = false;
  stepAll(dt);
  const uint32_t t1 = nowUs();
  gfx::frameStart();
  sc->touched = 0;
  sc->prepare();
  const uint32_t t2 = nowUs();
  compUs = 0;
  bool sent = false;
  if (!viaLvgl && !overlays()) ui::homeLine = true, sent = lcd::native(piece, nullptr, tear);
  ui::homeLine = false;
  if (!sent) {
    intoBuffer(app->buf, os::W(), os::H());
    lv_obj_invalidate(app->img);
  }
  const uint32_t t3 = nowUs();
  for (Stats* s : {&app->win, &app->bw}) addStats(*s, t1 - t0, t2 - t1, t3 - t0, !sent), s->px += sc->touched;
}

void benchPhase(int k) {
  static const char* const LIST = "angelfish*2,discus*2,goldfish*2,betta,gourami*2,rainbow*3,molly*3,swordtail*3,cory*2";
  // the first N of the list: big fish, as the hand-off's bench ("20 large fish")
  String l, s = LIST;
  int left = app->benchN[k];
  while (left > 0 && s.length()) {
    int c = s.indexOf(',');
    String part = c < 0 ? s : s.substring(0, c);
    s = c < 0 ? "" : s.substring(c + 1);
    const int star = part.indexOf('*');
    int m = star < 0 ? 1 : part.substring(star + 1).toInt();
    if (star >= 0) part = part.substring(0, star);
    m = std::min(m, left), left -= m;
    l += (l.length() ? "," : "") + part + "*" + String(m);
  }
  game::fillFree("planted", l.c_str());
  app->benchAt = millis(), app->bw = Stats();
}
void benchStep() {
  if (!app->bench) return;
  const uint32_t t = millis() - app->benchAt;
  if (t < 1000) app->bw = Stats(), app->bw.at = millis(), memset(sc->prof, 0, sizeof sc->prof);  // settling
  else if (t > 6000) {
    report("TANKBENCH", app->bw, sc->nfish);
    if (app->bench < app->benchCount) benchPhase(app->bench++);
    else {
      app->bench = 0;
      trace::add("TANKBENCH done");
#ifdef SIM
      fprintf(stderr, "TANKBENCH done\n");
#else
      Serial.println("TANKBENCH done");
#endif
      game::start();  // back to the game (its save)
    }
  }
}

void onTimer(lv_timer_t*) {
  if (!app || !sc) return;
  const uint32_t now = millis();
  if (!onScreen()) {
    if (app->wasOn && !os::saverOn()) intoBuffer(app->buf, os::W(), os::H()), lv_obj_invalidate(app->img);
    app->wasOn = false, app->last = now;
    return;
  }
  const bool back = !app->wasOn;  // back in front: one picture through LVGL first (it redraws the whole screen)
  app->wasOn = true;
  if (!back && now - app->lastPic + 1 < 1000u / fps) return;
  if (now - app->lastPic > 1000u / fps + 8 && !back) app->win.late++;
  const float dt = std::min(0.1f, (now - app->last) / 1000.0f);
  app->last = now, app->lastPic = now;
  appFrame(dt, back);
  benchStep();
  if (now - app->win.at > 10000) {
    if (!app->bench) report("tank:", app->win, sc->nfish);
    app->win = Stats(), app->win.at = now;
  }
}

void onTouch(lv_event_t* e) {
  if (!app || !sc) return;
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) ui::down(p.x, p.y);
  else if (code == LV_EVENT_PRESSING) ui::move(p.x, p.y);
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) ui::up(p.x, p.y);
}

void teardown(App* a) {
  if (!a) return;
  if (a->timer) lv_timer_delete(a->timer);
  if (a->img) lv_image_cache_drop(&a->dsc);
  heap_caps_free(a->buf);
  if (app == a) app = nullptr;
  delete a;
  ui::reset();
  release();
  trace::add("tank: closed");
}
void onDelete(lv_event_t* e) {
  App* a = (App*)lv_event_get_user_data(e);
  a->scr = a->img = nullptr;
  teardown(a);
}

// ---------------------------------------------------------------- the saver's background
void onSaver(lv_timer_t*) {
  if (!sv || !sc) return;
  const uint32_t now = millis();
  if (now - sv->lastPic + 1 < 1000u / saverFps) return;
  if (onScreen()) return;  // the app draws
  const float dt = std::min(0.25f, (now - sv->last) / 1000.0f);
  sv->last = now, sv->lastPic = now;
  ui::saver = true;
  const uint32_t t0 = nowUs();
  stepAll(dt);
  gfx::frameStart();
  sc->prepare();
  intoBuffer(sv->buf, sv->dsc.header.w, sv->dsc.header.h);
  lv_obj_invalidate(sv->img);
  static uint32_t n = 0, us = 0, at = 0;
  n++, us += nowUs() - t0;
  if (now - at > 10000) {  // what the saver's pictures cost (LVGL's own drawing of the page comes on top)
    if (at) trace::add("tank: saver %.1f pics/s, %.1f ms a picture", n * 1000.0f / (now - at), us / 1000.0f / n);
    n = us = 0, at = now;
  }
}
void onSaverDelete(lv_event_t* e) {
  SaverBg* s = (SaverBg*)lv_event_get_user_data(e);
  if (s->timer) lv_timer_delete(s->timer);
  lv_image_cache_drop(&s->dsc);
  heap_caps_free(s->buf);
  if (sv == s) sv = nullptr;
  delete s;
  ui::saver = false;
  release();
}

#if TANK_QA
void savePpm(const char* path) {  // the panel's picture: the water and the screens, black bars where the panel has them
  const int fw = sc->FW, fh = sc->FH;
  uint16_t* b = (uint16_t*)heap_caps_malloc(fw * fh * 2, MALLOC_CAP_SPIRAM);
  if (!b) return;
  gfx::frameStart();
  sc->prepare();
  piece(b, 1, fw, 0, 0, fw, fh, nullptr);
  const hal::PanelDesc& pd = hal::board().display.panel;
  const bool scaled = fw != 320 && fw != 480;
  const int pw = scaled ? (sc->land ? pd.h : pd.w) : fw, ph = scaled ? (sc->land ? pd.w : pd.h) : fh;
  const int ox = (pw - fw) / 2, oy = sc->land ? hal::board().display.ui.x : 0;
  FILE* f = fopen(path, "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", pw, ph);
    for (int y = 0; y < ph; y++)
      for (int x = 0; x < pw; x++) {
        const int sx = x - (sc->land ? 0 : ox), sy = y - (sc->land ? oy : 0);
        const uint16_t c = sx >= 0 && sy >= 0 && sx < fw && sy < fh ? b[sy * fw + sx] : 0;
        const uint8_t p[3] = {(uint8_t)(((c >> 11) * 255 + 15) / 31), (uint8_t)((((c >> 5) & 63) * 255 + 31) / 63), (uint8_t)(((c & 31) * 255 + 15) / 31)};
        fwrite(p, 1, 3, f);
      }
    fclose(f);
  }
  heap_caps_free(b);
}
#endif

}  // namespace

const char* installedId() {
  for (const pkg::Package& p : pkg::list())
    if (p.engine == "tank" && p.available && p.type != "pack") return p.id.c_str();
  return nullptr;
}

bool isOpen() { return app != nullptr; }

void open(const char* pkgId, const char* arg) {
  if (app && app->scr && lv_obj_is_valid(app->scr) && lv_screen_active() == app->scr) {  // already in front
    if (arg && *arg) dev(arg);
    return;
  }
  if (app) {  // its screen is on its way out (apps::open -> os::closeApps deletes it later)
    App* a = app;
    if (a->scr) lv_obj_remove_event_cb_with_user_data(a->scr, onDelete, a);
    a->img = nullptr;
    teardown(a);
  }
#ifndef SIM
  desk::releaseConnection();
#endif
  home::releasePictures();  // PSRAM for the water's pictures
  users++;
  const char* id = pkgId && *pkgId ? pkgId : installedId();
  const int W = os::W(), H = os::H();
  uint16_t* buf = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  if (!buf || !id || !ensure(id)) {
    heap_caps_free(buf);
    release();
    os::toast(N_("Not enough memory to open the aquarium"));
    return;
  }
  app = new App();
  app->buf = buf;
  lv_obj_t* body;
  app->scr = os::screen(N_("Aquarium"), &body, os::BARE);
  lv_obj_set_style_bg_color(app->scr, lv_color_black(), 0);
  app->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  app->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  app->dsc.header.w = W, app->dsc.header.h = H, app->dsc.header.stride = W * 2;
  app->dsc.data_size = W * H * 2, app->dsc.data = (const uint8_t*)buf;
  intoBuffer(buf, W, H);
  app->img = lv_image_create(app->scr);
  lv_image_set_src(app->img, &app->dsc);
  lv_obj_set_pos(app->img, 0, 0);
  lv_obj_add_flag(app->img, LV_OBJ_FLAG_CLICKABLE);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(app->img, onTouch, c, nullptr);
  lv_obj_add_event_cb(app->scr, onDelete, LV_EVENT_DELETE, app);
  os::fullScreenFor(app->scr, true);  // the tank is the picture: no status bar
  app->last = app->lastPic = millis(), app->win.at = millis();
  app->timer = lv_timer_create(onTimer, 4, nullptr);
  os::open(app->scr);
  trace::add("tank: open %s %s, %s %dx%d at %.2f", id, arg ? arg : "", sc->land ? "side" : "upright", sc->FW, sc->FH, sc->SC);
  if (arg && *arg) dev(arg);
}

lv_obj_t* saverBg(lv_obj_t* parent, int w, int h) {
  const char* id = installedId();
  if (!id || sv) return nullptr;
  uint16_t* buf = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  users++;
  if (!buf || !ensure(id)) {
    heap_caps_free(buf);
    release();
    return nullptr;
  }
  sv = new SaverBg();
  sv->buf = buf;
  sv->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  sv->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  sv->dsc.header.w = w, sv->dsc.header.h = h, sv->dsc.header.stride = w * 2;
  sv->dsc.data_size = w * h * 2, sv->dsc.data = (const uint8_t*)buf;
  ui::saver = true;
  sc->prepare();
  intoBuffer(buf, w, h);
  sv->img = lv_image_create(parent);
  lv_image_set_src(sv->img, &sv->dsc);
  lv_obj_set_pos(sv->img, 0, 0);
  lv_obj_add_event_cb(sv->img, onSaverDelete, LV_EVENT_DELETE, sv);
  sv->last = sv->lastPic = millis();
  sv->timer = lv_timer_create(onSaver, 10, nullptr);
  trace::add("tank: saver background %dx%d", w, h);
  return sv->img;
}

bool dev(const char* cmd) {
  if (!cmd) return false;
  String c = cmd;
  c.trim();
  const int sp = c.indexOf(' ');
  const String k = sp < 0 ? c : c.substring(0, sp), v = sp < 0 ? "" : c.substring(sp + 1);
  if (k == "fps") return fps = std::max(5, std::min(60, (int)v.toInt())), true;
  if (k == "saverfps") return saverFps = std::max(2, std::min(30, (int)v.toInt())), true;
  if (k == "tear") return tear = v.toInt() != 0, true;
  if (!sc) return false;
  if (k == "bench" && app) {
    if (v.length()) app->benchN[0] = v.toInt(), app->benchCount = 1;
    else app->benchN[0] = 5, app->benchN[1] = 12, app->benchN[2] = 20, app->benchCount = 3;
    app->bench = 1;
    benchPhase(0);
  } else if (k == "free") {
    const int s2 = v.indexOf(' ');
    game::fillFree(s2 < 0 ? v.c_str() : v.substring(0, s2).c_str(), s2 < 0 ? "" : v.substring(s2 + 1).c_str());
  } else if (k == "game") game::start();
  else if (k == "time") game::debugTime(v.toFloat());
  else if (k == "coins") game::debugCoins(v.toInt());
  else if (k == "xp") game::debugXp(v.toInt());
  else if (k == "look") game::debugLook(v.c_str());
  else if (k == "night") game::nightForce = v == "-" ? -1 : v.toInt();
  else if (k == "speed") game::timeK = v.toFloat();
  else if (k == "reset") game::reset();
  else if (k == "save") game::save();
  else if (k == "stats" && app) report("tank:", app->win, sc->nfish);
  else if (k == "wake") ui::wake();
  else if (k == "close") ui::reset();
#if TANK_QA
  else if (k == "shot") savePpm(v.c_str());
  else if (k == "card") ui::devOpen("card", v == "sick" ? 1 : -1);
  else if (k == "tab" || k == "food" || k == "sponge" || k == "scroll") ui::devOpen(k.c_str(), v.toInt());
  else if (k == "say") ui::say(v);
  else if (k == "add" || k == "sick" || k == "algae" || k == "clean" || k == "fed" || k == "young") game::debugSet(k.c_str(), v.c_str());
#endif
  else return false;
  return true;
}

}  // namespace tank
