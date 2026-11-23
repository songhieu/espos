#pragma GCC optimize("Os")  // not per frame: built small (0.5.7 flash diet)
#include "os.h"

#include <Preferences.h>
#include <sys/time.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <set>
#include <vector>

#include "alarm.h"
#include "apps.h"
#include "assistant.h"
#include "preset.h"
#include "art.h"
#include "desk.h"
#include "desksaver.h"
#include "saveredit.h"
#include "fonts.h"
#include "home.h"
#include "i18n.h"
#include "keyboard.h"
#include "lcd.h"
#include "lock.h"
#include "net.h"
#include "notif.h"
#if ESPOS_ONBOARD
#include "onboard.h"
#endif
#include "photo.h"
#include "pkg/pkg.h"
#include "js/jsapp.h"
#ifndef NO_PET
#include "pet/pet.h"
#endif
#include "wake/wake.h"
#include "power.h"
#include "sound.h"
#include "splash.h"
#include "src/display/lv_display_private.h"
#include "theme.h"
#include "touch.h"
#include "trace.h"
#include "uikit.h"

namespace os {
namespace {

using namespace kit;

lv_display_t* disp;
lv_obj_t* mainScr;
std::vector<lv_obj_t*> stack;         // app screens over Home, top last
std::map<lv_obj_t*, bool> barDarkOf;  // status bar text colour per screen
std::set<lv_obj_t*> fullScreens;       // screens shown without the status bar (immersive: games)
int rot = 0, bright = 70, vol = 45, saverSec = 120;
Preferences prefs;

// The way up and the brightness, also in NVS for the boot screen (splash.cpp: shown before the settings are read).
void keepBootLook() {
  if (prefs.getInt("rot", -1) != rot) prefs.putInt("rot", rot);
  if (prefs.getInt("bl", -1) != bright) prefs.putInt("bl", bright);
}

// ------------------------------------------------------------------ time
tm nowTm() {
  time_t t = time(nullptr);
  tm out;
  localtime_r(&t, &out);
  return out;
}

// Build time as the first guess for the clock; the server or NTP replaces it once online.
void seedClock() {
  setenv("TZ", "ICT-7", 1);
  tzset();
  if (time(nullptr) > 1700000000) return;
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  tm b = {};
  sscanf(__DATE__, "%3s %d %d", mon, &b.tm_mday, &b.tm_year);
  sscanf(__TIME__, "%d:%d:%d", &b.tm_hour, &b.tm_min, &b.tm_sec);
  b.tm_mon = (strstr(months, mon) - months) / 3;
  b.tm_year -= 1900;
  timeval tv = {mktime(&b), 0};
  settimeofday(&tv, nullptr);
}

// ------------------------------------------------------------------ status bar (top layer)
lv_obj_t *sb, *sbTime, *sbWifi, *sbPct, *sbBat, *sbBatFill, *sbBolt, *sbMic;
bool sbDark = false, sbHidden = false;

void paintBar() {
  if (!sb) return;
  const uint32_t c = sbDark ? 0x16181d : 0xffffff;
  lv_obj_set_style_text_color(sbTime, C(c), 0);
  lv_obj_set_style_text_color(sbPct, C(c), 0);
  lv_obj_set_style_image_recolor(sbWifi, C(c), 0);
  lv_obj_set_style_border_color(sbBat, C(c), 0);
  lv_obj_set_style_bg_color(sbBatFill, C(c), 0);
  lv_obj_set_style_text_color(sbBolt, C(c), 0);
}

void onPull(lv_event_t* e);

void buildStatusBar() {
  keep(&sb, clear(lv_layer_top()));
  lv_obj_set_size(sb, W(), SB_H);
  lv_obj_add_flag(sb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(sb, 10);
  keep(&sbTime, label(sb, "", 14, true));
  lv_obj_align(sbTime, LV_ALIGN_LEFT_MID, 14, 1);
  keep(&sbBat, clear(sb));
  lv_obj_set_size(sbBat, 24, 12);
  lv_obj_set_style_border_width(sbBat, 1, 0);
  lv_obj_set_style_border_opa(sbBat, LV_OPA_60, 0);
  lv_obj_set_style_radius(sbBat, 3, 0);
  lv_obj_align(sbBat, LV_ALIGN_RIGHT_MID, -14, 0);
  keep(&sbBatFill, box(sbBat, 0xffffff, 2));
  lv_obj_set_pos(sbBatFill, 2, 2);
  lv_obj_set_size(sbBatFill, 18, 6);
  lv_obj_t* nub = box(sb, 0xffffff, 1);
  lv_obj_set_size(nub, 2, 5);
  lv_obj_align_to(nub, sbBat, LV_ALIGN_OUT_RIGHT_MID, 1, 0);
  lv_obj_set_style_bg_opa(nub, LV_OPA_60, 0);
  keep(&sbBolt, label(sbBat, LV_SYMBOL_CHARGE, 12, false));
  lv_obj_set_style_text_font(sbBolt, &lv_font_montserrat_14, 0);
  lv_obj_center(sbBolt);
  keep(&sbPct, label(sb, "", 13, true));
  lv_obj_align_to(sbPct, sbBat, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  keep(&sbWifi, glyph(sb, &gl_wifi3_16, 0xffffff));
  // The microphone is open (the "Gâu" wake word listening, or a turn being recorded): an orange dot, like a phone's.
  keep(&sbMic, box(sb, 0xff9f0a, 4));
  lv_obj_set_size(sbMic, 8, 8);
  lv_obj_add_flag(sbMic, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(sb, onPull, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(sb, onPull, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(sb, onPull, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(sb, onPull, LV_EVENT_PRESS_LOST, nullptr);
  paintBar();
}

void updateStatusBar(const tm& t) {
  if (!sb) return;
  char hm[8];
  snprintf(hm, sizeof hm, "%02d:%02d", t.tm_hour, t.tm_min);
  setText(sbTime, hm);
  if (wake::micOn()) {
    lv_obj_remove_flag(sbMic, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(sbMic, sbTime, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
  } else {
    lv_obj_add_flag(sbMic, LV_OBJ_FLAG_HIDDEN);
  }
  const int pct = power::percent();
  char p[8];
  snprintf(p, sizeof p, "%d%%", pct);
  setText(sbPct, power::full() ? "" : p);
  lv_obj_set_width(sbBatFill, max(2, 18 * pct / 100));
  const bool chg = power::usb();
  if (chg) lv_obj_remove_flag(sbBolt, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(sbBolt, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(sbBatFill, chg ? LV_OPA_40 : LV_OPA_COVER, 0);
  lv_obj_align_to(sbPct, sbBat, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  // WiFi: bars by strength, faint while a saved network is not joined, hidden with none saved.
  static uint32_t lastRssi = 0;
  static const lv_image_dsc_t* shown = nullptr;
  const lv_image_dsc_t* g = nullptr;
  if (net::state() == net::State::Connected) {
    if (millis() - lastRssi < 3000 && shown && shown != &gl_wifi0_16) g = shown;
    else {
      lastRssi = millis();
      const int b = net::bars(net::rssi());
      g = b >= 3 ? &gl_wifi3_16 : b == 2 ? &gl_wifi2_16 : &gl_wifi1_16;
    }
  } else if (net::savedCount()) {
    g = &gl_wifi0_16;
  }
  if (!g) {
    lv_obj_add_flag(sbWifi, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(sbWifi, LV_OBJ_FLAG_HIDDEN);
    if (g != shown) lv_image_set_src(sbWifi, g);
    lv_obj_align_to(sbWifi, sbPct, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  }
  shown = g;
}

// ------------------------------------------------------------------ gestures (top layer)
lv_obj_t *homeBar, *homeLine;
bool saverShown = false;
bool ccOpen();

void hideBar(bool hidden);

void updateChrome() {
  const bool app = !stack.empty();
  lv_obj_t* active = app ? stack.back() : mainScr;
  hideBar((!app && ::home::editing()) || (app && fullScreens.count(active)));
  auto it = barDarkOf.find(active);
  barDark(it != barDarkOf.end() ? it->second : (!app && theme::home().light));
  if (homeBar) {
#if ESPOS_ONBOARD
    app && !kb::isOpen() && !onboard::running() ? lv_obj_remove_flag(homeBar, LV_OBJ_FLAG_HIDDEN)
                                                : lv_obj_add_flag(homeBar, LV_OBJ_FLAG_HIDDEN);
#else
    app && !kb::isOpen() ? lv_obj_remove_flag(homeBar, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(homeBar, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_set_style_bg_color(homeLine, C(sbDark ? 0x16181d : 0xffffff), 0);
  }
}

// System gestures, as on a phone: they watch every touch sample (touch::setWatcher) instead of covering the
// screen's edges with strips, so a tap on a button by the left edge or at the very bottom still reaches it.
//   back:   a finger that lands within EDGE px of the left edge (in an app) and moves right
//   home:   a finger that lands in the bottom BOTTOM px (in an app) and moves up
//   search: on a Home page, a finger that moves down (home::searchAt says where that may start)
constexpr int EDGE = 22, BOTTOM = 28;
enum GKind : uint8_t { G_NONE, G_BACK, G_HOME, G_SEARCH };
struct Gesture {
  bool down = false;
  GKind kind = G_NONE;
  int32_t x0 = 0, y0 = 0;
} gst;

void fireLater(void (*fn)()) {
  lv_async_call([](void* f) { ((void (*)())f)(); }, (void*)fn);
}

bool watchTouch(bool down, int32_t x, int32_t y) {
  if (!down) {
    gst = Gesture();
    return false;
  }
  if (!gst.down) {  // the finger lands: which gesture could this be?
    gst.down = true, gst.x0 = x, gst.y0 = y;
    const bool app = !stack.empty();
    // With the keyboard up, a finger landing on its keys types (no back swipe from its left column).
    const int kbTop = kb::isOpen() ? H() - kb::height() : H();
    // The assistant's pop-up (or its "No assistant installed" card) has its own gestures (swipe down, tap outside):
    // nothing underneath may take its touches.
    if (saverShown || ccOpen() || ::home::editing() || notif::centerShown() || assistant::open()) gst.kind = G_NONE;
    else if (app && x < EDGE && y > SB_H && y < kbTop) gst.kind = G_BACK;
#if ESPOS_ONBOARD
    else if (onboard::running()) gst.kind = G_NONE;  // the first-boot setup: only back (one step back, onboard.h)
#endif
    else if (app && y >= H() - BOTTOM) gst.kind = G_HOME;
    else if (!app && ::home::searchAt(x, y)) gst.kind = G_SEARCH;
    else gst.kind = G_NONE;
    return false;
  }
  const int dx = x - gst.x0, dy = y - gst.y0;
  switch (gst.kind) {
    case G_BACK:
      if (dx > 36 && dx > 2 * abs(dy)) {
        gst.kind = G_NONE;
        trace::add("gesture: back");
#if ESPOS_ONBOARD
        fireLater([] {
          if (!onboard::back()) back();
        });
#else
        fireLater(back);
#endif
        return true;
      }
      if (abs(dy) > 36 || dx < -12) gst.kind = G_NONE;  // a scroll, not a back swipe
      break;
    case G_HOME:
      if (-dy > 30 && -dy > abs(dx)) {
        gst.kind = G_NONE;
        trace::add("gesture: home");
        fireLater(goHome);
        return true;
      }
      if (dy > 12 || abs(dx) > 48) gst.kind = G_NONE;
      break;
    case G_SEARCH:
      if (dy > 42 && dy > 2 * abs(dx)) {
        gst.kind = G_NONE;
        trace::add("gesture: search");
        fireLater([] { ::home::openSearch(); });
        return true;
      }
      if (abs(dx) > 20 || dy < -12) gst.kind = G_NONE;  // the pages slide sideways instead
      break;
    default:
      break;
  }
  return false;
}

void buildGestures() {
  // The home indicator is only a picture now (not a touch strip).
  keep(&homeBar, clear(lv_layer_top()));
  lv_obj_set_size(homeBar, W(), 12);
  lv_obj_align(homeBar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_remove_flag(homeBar, LV_OBJ_FLAG_CLICKABLE);
  keep(&homeLine, box(homeBar, 0xffffff, 2));
  lv_obj_set_size(homeLine, 110, 4);
  lv_obj_set_style_bg_opa(homeLine, LV_OPA_50, 0);
  lv_obj_align(homeLine, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_remove_flag(homeLine, LV_OBJ_FLAG_CLICKABLE);
  touch::setWatcher(watchTouch);
}

// ------------------------------------------------------------------ toast (notifications: notif.cpp)
lv_obj_t* toastObj;
uint32_t toastAt = 0;

void showToast(const char* text) {
  if (!toastObj) {
    keep(&toastObj, box(lv_layer_top(), 0x1c1f26, 18));
    lv_obj_set_style_bg_opa(toastObj, 235, 0);
    lv_obj_set_style_pad_hor(toastObj, 16, 0);
    lv_obj_set_style_pad_ver(toastObj, 9, 0);
    lv_obj_set_size(toastObj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(toastObj, W() - 32, 0);
    lv_obj_t* l = label(toastObj, "", 14, true, 0xffffff);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_t* l = lv_obj_get_child(toastObj, 0);
  wrapText(l, text, W() - 64, 14, true, 2, 3);
  lv_obj_align(toastObj, LV_ALIGN_TOP_MID, 0, SB_H + 6);
  lv_obj_remove_flag(toastObj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(toastObj);
  toastAt = millis();
}

// ------------------------------------------------------------------ Control Center
constexpr int CC_ANIM = 200;
lv_obj_t *cc, *ccWifi, *ccSync, *ccDnd, *ccBright, *ccVol, *ccAlarm, *ccNotes, *ccMic;
int32_t pullStart = 0;

// While it slides, the Control Center is a picture of itself (one copy from PSRAM a frame
// instead of its tiles, sliders and labels: 119 ms a frame on the board); the live one comes
// back when it stops.
lv_obj_t* ccShot;
uint16_t* ccPx;
lv_image_dsc_t ccDsc;
bool ccShotOk = false, ccSliding = false;
uint32_t ccShotAt = 0;
int ccY = 0;

void updateControl(bool force = false);
int ccPxW = 0, ccPxH = 0;

void snapCc() {
  if (!cc || !ccShot || ccSliding) return;
  const int w = W(), h = H();
  if (!ccPx || ccPxW != w || ccPxH != h) {
    heap_caps_free(ccPx);
    ccPx = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
    ccPxW = w, ccPxH = h;
  }
  if (!ccPx) return;
  updateControl(true);
  lv_draw_buf_t buf;
  lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, w * 2, ccPx, w * h * 2);
  const bool hidden = lv_obj_has_flag(cc, LV_OBJ_FLAG_HIDDEN);
  if (hidden) lv_obj_remove_flag(cc, LV_OBJ_FLAG_HIDDEN);
  ccShotOk = lv_snapshot_take_to_draw_buf(cc, LV_COLOR_FORMAT_RGB565, &buf) == LV_RESULT_OK;
  if (hidden) lv_obj_add_flag(cc, LV_OBJ_FLAG_HIDDEN);
  ccDsc = {};
  ccDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  ccDsc.header.cf = LV_COLOR_FORMAT_RGB565;
  ccDsc.header.w = w;
  ccDsc.header.h = h;
  ccDsc.header.stride = w * 2;
  ccDsc.data_size = w * h * 2;
  ccDsc.data = (const uint8_t*)ccPx;
  lv_image_set_src(ccShot, &ccDsc);
  ccShotAt = millis();
}

void setCcY(int y) {
  y = constrain(y, -H(), 0);
  ccY = y;
  lv_obj_t* o = ccSliding && ccShotOk ? ccShot : cc;
  lv_obj_t* other = o == cc ? ccShot : cc;
  if (other) lv_obj_add_flag(other, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_y(o, y);
  if (o == ccShot) lv_obj_set_y(cc, y);  // the live one follows, hidden
  if (y <= -H()) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void ccSlide(bool on) {
  if (on == ccSliding) return;
  ccSliding = on;
  if (on && ccShotOk) lv_obj_move_foreground(ccShot);
  setCcY(ccY);
}

bool ccOpen() { return cc && ccY > -H(); }

void animCc(bool open) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, cc);
  lv_anim_set_values(&a, ccY, open ? 0 : -H());
  lv_anim_set_duration(&a, CC_ANIM);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void*, int32_t v) { setCcY(v); });
  lv_anim_set_completed_cb(&a, [](lv_anim_t*) {
    ccSlide(false);  // the live Control Center, to be touched
    if (ccOpen()) updateControl();
  });
  ccSlide(true);
  lv_anim_start(&a);
  trace::add("control center %s", open ? "open" : "close");
}

void onPull(lv_event_t* e) {
  if (sbHidden && lv_event_get_current_target(e) == sb) return;
#if ESPOS_ONBOARD
  if (onboard::running()) return;  // the first-boot setup: no Control Center or notifications under it
#endif
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  const lv_event_code_t code = lv_event_get_code(e);
  const bool fromCc = lv_event_get_current_target(e) == cc;
  // As on a phone: pulled down from the left part of the status bar, the Notification Center; from the right
  // part, the Control Center (it follows the finger).
  static bool ncPull = false, ncFired = false;
  if (code == LV_EVENT_PRESSED) {
    pullStart = p.y - (fromCc ? H() : 0);
    ncPull = !fromCc && p.x < W() * 55 / 100, ncFired = false;
    if (!fromCc) kb::close();
  } else if (ncPull) {
    // The Center follows the finger and opens or slides back on release.
    const int dy = p.y - pullStart;
    if (code == LV_EVENT_PRESSING && (ncFired || dy > 4)) {
      if (!ncFired) trace::add("gesture: notifications");
      ncFired = true;
      notif::pullCenter(dy, false);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && ncFired) {
      notif::pullCenter(dy, true);
    }
  } else if (code == LV_EVENT_PRESSING) {
    if (abs(p.y - pullStart - (fromCc ? H() : 0)) > 4) ccSlide(true);
    if (ccSliding) setCcY(-H() + (p.y - pullStart));
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (ccSliding) animCc(ccY > (fromCc ? -H() / 5 : -H() * 4 / 5));
  }
}

// One round toggle of the 2x2 block: glyph and a two-line name under it.
lv_obj_t* ccToggle(lv_obj_t* parent, const lv_image_dsc_t* g, const char* name, lv_event_cb_t cb) {
  lv_obj_t* t = box(parent, 0x2b3050, 18);
  lv_obj_set_size(t, 62, 62);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(t, LV_OPA_70, LV_STATE_PRESSED);
  // Two-line names ("Do Not Disturb") lift the glyph so nothing overlaps.
  const bool two = textW(name, 12, true) > 58;
  lv_obj_t* i = glyph(t, g, 0xffffff);
  lv_obj_align(i, LV_ALIGN_TOP_MID, 0, two ? 2 : 10);
  lv_obj_t* l = label(t, "", 12, true, 0xffffff);
  fitText(l, name, 58, 36, 12, 12, true, -2);  // two bold lines take 36 px ("Không làm phiền", "Nicht stören")
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, two ? -3 : -7);
  if (cb) lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, nullptr);
  return t;
}

lv_obj_t* ccTile(lv_obj_t* parent, int w, int h, const lv_image_dsc_t* g, const char* name, lv_event_cb_t cb) {
  lv_obj_t* t = box(parent, 0x2b3050, 18);
  lv_obj_set_size(t, w, h);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(t, LV_OPA_70, LV_STATE_PRESSED);
  const bool two = textW(name, 12, true) > w - 4;  // a longer translation wraps (never cut): the glyph moves up
  lv_obj_t* i = glyph(t, g, 0xffffff);
  lv_obj_align(i, LV_ALIGN_TOP_MID, 0, two ? h / 2 - 30 : h / 2 - 22);
  lv_obj_t* l = label(t, "", 12, true, 0xffffff);
  fitText(l, name, w - 4, 36, 12, 12, true, -2);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, two ? -(h / 2 - 28) : -(h / 2 - 20));
  lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, nullptr);
  return t;
}

lv_obj_t* ccSlider(lv_obj_t* parent, int w, int h, int value, const lv_image_dsc_t* g, lv_event_cb_t change,
                   lv_event_cb_t release) {
  lv_obj_t* s = lv_slider_create(parent);
  lv_obj_set_size(s, w, h);
  lv_slider_set_range(s, 0, 100);
  lv_slider_set_value(s, value, LV_ANIM_OFF);
  lv_obj_set_style_radius(s, 20, LV_PART_MAIN);
  lv_obj_set_style_radius(s, 20, LV_PART_INDICATOR);  // no clip_corner: it costs a layer a frame
  lv_obj_set_style_bg_color(s, C(0x2b3050), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, C(0xffffff), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 0, LV_PART_MAIN);
  lv_obj_t* i = glyph(s, g, 0x6b7280);
  lv_obj_align(i, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_add_event_cb(s, change, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s, release, LV_EVENT_RELEASED, nullptr);
  return s;
}

void closeCcNow() {
  ccSliding = false;
  setCcY(-H());
}

void fillCcNotes();
String ccAssist;  // what the assistant tile and the mic tile were built for (ccFollowAssistant)
String assistSig() { return assistant::id() + "|" + assistant::name() + (assistant::listens() ? "|m" : ""); }

void buildControl() {
  ccAssist = assistSig();
  keep(&cc, clear(lv_layer_top()));
  lv_obj_set_size(cc, W(), H());
  grad(cc, 0x141833, 0x1d1230);
  lv_obj_add_flag(cc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cc, onPull, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(cc, onPull, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(cc, onPull, LV_EVENT_RELEASED, nullptr);
  const bool land = landscape();
  const int x0 = 12, y0 = SB_H + 6;
  // Block 1: four toggles.
  lv_obj_t* b1 = box(cc, 0x22274a, 22);
  lv_obj_set_size(b1, 146, 146);
  lv_obj_set_pos(b1, x0, y0);
  lv_obj_set_style_pad_all(b1, 8, 0);
  lv_obj_set_flex_flow(b1, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(b1, 6, 0);
  keep(&ccWifi, ccToggle(b1, &gl_wifi3_22, "WiFi", [](lv_event_t*) {
    closeCcNow();
    apps::open("settings", "wifi");
  }));
  keep(&ccSync, ccToggle(b1, &gl_sync_22, NC_("cc", "Sync"), [](lv_event_t*) {  // i18n: a small tile, ~8 letters
    if (desk::link() == desk::Link::Linked) {
      desk::syncNow();
      showToast(N_("Syncing"));
    } else {
      closeCcNow();
      apps::open("settings", "account");
    }
  }));
  keep(&ccDnd, ccToggle(b1, &gl_moon_22, NC_("cc", "Do Not Disturb"), [](lv_event_t*) {  // i18n: a tile, 2 short lines
    desk::setSetting("s3.dnd", desk::settingInt("s3.dnd", 0) ? "0" : "1");
  }));
  // Rotate: between the user's own portrait way and landscape way (the last one used of each, so never upside
  // down by surprise); touch and hold: the other way of the same shape, turned 180 degrees (owner, 2026-11-20: "lật
  // màn 180°"). Settings > Display has all four.
  lv_obj_t* rt = ccToggle(b1, &gl_rotate_22, N_("Rotate"), nullptr);
  lv_obj_add_event_cb(rt, [](lv_event_t*) { lv_async_call([](void*) { turn(); }, nullptr); }, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(rt, [](lv_event_t*) {
    lv_async_call([](void*) {
      applyRotation(rot ^ 2);
      toast(N_("Screen flipped"));  // after the rebuild (it clears the top layer)
    }, nullptr);
  }, LV_EVENT_LONG_PRESSED, nullptr);
  // Block 2: the next alarm.
  keep(&ccAlarm, box(cc, 0x22274a, 22));
  lv_obj_set_size(ccAlarm, land ? 146 : W() - 2 * x0 - 146 - 8, 146);
  lv_obj_set_pos(ccAlarm, x0 + 146 + 8, y0);
  lv_obj_set_style_pad_all(ccAlarm, 12, 0);
  lv_obj_add_flag(ccAlarm, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ccAlarm, [](lv_event_t*) {
    closeCcNow();
    apps::open("clock", "alarm");
  }, LV_EVENT_CLICKED, nullptr);
  label(ccAlarm, N_("Alarm"), 13, true, 0xc9cde0);
  lv_obj_t* at = label(ccAlarm, "", 34, true, 0xffffff);
  lv_obj_set_pos(at, 0, 26);
  lv_obj_t* ad = label(ccAlarm, "", 13, false, 0xc9cde0);
  lv_obj_set_pos(ad, 0, 72);
  // Row 2: brightness and volume, four small tiles.
  const int y1 = land ? y0 : y0 + 146 + 10;
  const int xs = land ? x0 + 2 * (146 + 8) : x0;
  keep(&ccBright, ccSlider(cc, 68, 146, bright, &gl_sun_22, [](lv_event_t* e) {
    bright = max(5, (int)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)));
    lcd::setBacklight(bright);
  }, [](lv_event_t*) { setBrightness(bright, true); }));
  lv_obj_set_pos(ccBright, xs, y1);
  keep(&ccVol, ccSlider(cc, 68, 146, vol, &gl_speaker_22, [](lv_event_t* e) {
    vol = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    sound::setVolume(vol);
  }, [](lv_event_t*) {
    setVolume(vol, true);
    sound::click();
  }));
  lv_obj_set_pos(ccVol, xs + 68 + 10, y1);
  {
    // Four tiles: a 2 x 2 block beside the sliders in portrait, one row under the first in landscape.
    lv_obj_t* grid = clear(cc);
    lv_obj_set_size(grid, land ? W() - 2 * x0 : W() - 2 * x0 - 2 * 68 - 20, land ? 60 : 146);
    lv_obj_set_pos(grid, land ? x0 : x0 + 2 * 68 + 20, land ? y0 + 146 + 10 : y1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(grid, 8, 0);
    const int tw = land ? (W() - 2 * x0 - 3 * 8) / 4 : (W() - 2 * x0 - 2 * 68 - 20 - 8) / 2, th = land ? 60 : 69;
    ccTile(grid, tw, th, &gl_moon_22, N_("Saver"), [](lv_event_t*) {  // i18n: the screen saver (a short tile name)
      closeCcNow();
      showSaver();
    });
    ccTile(grid, tw, th, &gl_alarm_22, N_("Alarm"), [](lv_event_t*) {
      closeCcNow();
      apps::open("clock", "alarm");
    });
#ifndef NO_PET
    // The assistant slot's app (app-slots.md §16): the dog's name, or "No assistant" (a tap says how to get one).
    const String an = assistant::name();
    ccTile(grid, tw, th, &gl_home_22, an.length() ? an.c_str() : NC_("cc", "No assistant"), [](lv_event_t*) {
      closeCcNow();
      assistant::summon("cc");  // its pop-up over any screen, or the "No assistant installed" card
    });
#else
    ccTile(grid, tw, th, &gl_timer_22, N_("Timer"), [](lv_event_t*) {
      closeCcNow();
      apps::open("clock", "timer");
    });
#endif
    ccTile(grid, tw, th, &gl_gear_22, NC_("cc", "Settings"), [](lv_event_t*) {  // i18n: a tile ~9 letters wide
      closeCcNow();
      apps::open("settings");
    });
  }
  // Notifications: like a phone, not listed here; one tile opens the Notification Center.
  const int ny = land ? y0 + 146 + 10 + 60 + 8 : y1 + 146 + 10;
  // The "Gâu" wake word's tile beside it (boards with a mic): listening (orange) / paused; off: opens its settings.
  const int nh = land ? 56 : 64;
  int micW = 0;
#ifndef NO_PET
  if (sound::micChannel() && assistant::listens()) {  // no assistant: nothing listens, no tile for it
    micW = nh + 8;
    keep(&ccMic, box(cc, 0x2b3050, 22));
    lv_obj_set_size(ccMic, nh, nh);
    lv_obj_set_pos(ccMic, W() - x0 - nh, ny);
    lv_obj_add_flag(ccMic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ccMic, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_center(assistant::micIcon(ccMic, 0xffffff));
    lv_obj_add_event_cb(ccMic, [](lv_event_t*) {
      if (wake::mode() == wake::OFF) {
        closeCcNow();
        apps::open("pet", "book");
        return;
      }
      wake::setPaused(!wake::paused());
      showToast(wake::paused() ? N_("Not listening for \"Gâu\"") : N_("Listening for \"Gâu\""));
      updateControl(true);
    }, LV_EVENT_CLICKED, nullptr);
  }
#endif
  keep(&ccNotes, box(cc, 0x22274a, 22));
  lv_obj_set_pos(ccNotes, x0, ny);
  lv_obj_set_size(ccNotes, W() - 2 * x0 - micW, nh);
  lv_obj_add_flag(ccNotes, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(ccNotes, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_add_event_cb(ccNotes, [](lv_event_t*) {
    closeCcNow();
    notif::openCenter();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* handle = box(cc, 0xffffff, 2);
  lv_obj_set_size(handle, 40, 4);
  lv_obj_set_style_bg_opa(handle, LV_OPA_50, 0);
  lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -10);
  fillCcNotes();
  keep(&ccShot, lv_image_create(lv_layer_top()));
  lv_obj_set_pos(ccShot, 0, -H());
  lv_obj_add_flag(ccShot, LV_OBJ_FLAG_HIDDEN);
  ccShotOk = false;
  ccSliding = false;
  ccY = -H();
  setCcY(-H());
}

// The assistant slot changed (its app removed or installed, another dog's name): the closed Control Center is built
// again, so its tile names the new one (or "No assistant") and the mic tile comes or goes (app-slots.md §16).
void ccFollowAssistant() {
  if (!cc || ccOpen() || ccSliding || ccAssist == assistSig()) return;
  if (ccShot) lv_obj_delete(ccShot);
  lv_obj_delete(cc);  // keep() clears cc, ccShot and the tiles' pointers
  buildControl();
  trace::add("control center: the assistant tile is now %s", ccAssist.c_str());
}

// The "Thông báo" tile: a red bell, the name and how many wait in the Notification Center.
void fillCcNotes() {
  if (!ccNotes) return;
  lv_obj_clean(ccNotes);
  lv_obj_t* bell = box(ccNotes, col::RED, 19);
  lv_obj_set_size(bell, 38, 38);
  lv_obj_align(bell, LV_ALIGN_LEFT_MID, 13, 0);
  lv_obj_center(glyph(bell, &gl_bell_22, 0xffffff));
  lv_obj_t* name = label(ccNotes, N_("Notifications"), 15, true, 0xffffff);
  const int dy = lv_obj_get_height(ccNotes) < 60 ? -4 : 0;  // landscape's tile is 56 high
  lv_obj_set_pos(name, 63, 12 + dy);
  const int n = notif::count();
  lv_obj_t* sub = label(ccNotes, n ? i18n::plural(n, NP_("{n} notification|{n} notifications")).c_str() : N_("No new notifications"),
                        13, false, 0xc9cde0);
  lv_obj_set_pos(sub, 63, 34 + dy);
  lv_obj_t* chev = glyph(ccNotes, &gl_chev_16, 0xc9cde0);
  lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);
}

void updateControl(bool force) {
  if (!force && !ccOpen()) return;
  const bool wifiOn = net::state() == net::State::Connected;
  lv_obj_set_style_bg_color(ccWifi, C(wifiOn ? 0x0a84ff : 0x2b3050), 0);
  const bool linked = desk::link() == desk::Link::Linked;
  lv_obj_set_style_bg_color(ccSync, C(desk::syncing() ? 0x34c759 : linked ? 0x248a3d : 0x2b3050), 0);
  const bool dnd = desk::settingInt("s3.dnd", 0);
  lv_obj_set_style_bg_color(ccDnd, C(dnd ? 0x5e5ce6 : 0x2b3050), 0);
  if (ccMic) lv_obj_set_style_bg_color(ccMic, C(wake::mode() != wake::OFF && !wake::paused() ? 0xff9f0a : 0x2b3050), 0);
  String when, day;
  alarms::next(when, day);
  setText(lv_obj_get_child(ccAlarm, 1), when.length() ? when.c_str() : "--:--");
  lv_obj_t* ad = lv_obj_get_child(ccAlarm, 2);  // a long label wraps (never cut)
  fitText(ad, day.length() ? day.c_str() : N_("Not set"), lv_obj_get_content_width(ccAlarm), 40, 13, 12, false, 0);
}

// ------------------------------------------------------------------ screen saver (pages: desksaver.h)
lv_obj_t* saver;
int saverLight = -1;       // backlight the saver set (reset each time it shows)
uint32_t saverPressAt = 0;  // a finger on the saver: a short touch wakes, 1 s opens the editor
lv_point_t saverPressPt;
bool saverHeld = false;

int minutesOf(const String& hm) { return hm.substring(0, 2).toInt() * 60 + hm.substring(3, 5).toInt(); }

bool quietNow(const tm& t) {
  const int from = minutesOf(desk::setting("dash.quiet_from", "23:00")), to = minutesOf(desk::setting("dash.quiet_to", "06:00"));
  const int now = t.tm_hour * 60 + t.tm_min;
  return from == to ? false : from < to ? now >= from && now < to : now >= from || now < to;
}

void onSaverTouch(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (saveredit::isOpen()) return;
  if (code == LV_EVENT_PRESSED) {
    saverHeld = true;
    saverPressAt = millis();
    saverPressPt = p;
    lcd::setBacklight(bright);  // lit at once, as a wake; the saver goes away when the finger lifts
    saverLight = -1;
  } else if (code == LV_EVENT_PRESSING && saverHeld) {
    if (abs(p.x - saverPressPt.x) > 16 || abs(p.y - saverPressPt.y) > 16) {
      saverPressAt = millis() + 3600000;  // a swipe: wakes when lifted, never edits
    } else if ((int32_t)(millis() - saverPressAt) >= 1000) {
      saverHeld = false;
      lv_indev_wait_release(lv_indev_active());
      trace::add("saver: long press, editor");
      saveredit::open();
    }
  } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && saverHeld) {
    saverHeld = false;
    wake();
  }
}

void buildSaver() {
  keep(&saver, box(lv_layer_top(), 0x000000, 0));
  lv_obj_set_size(saver, W(), H());
  lv_obj_add_flag(saver, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(saver, onSaverTouch, c, nullptr);
  desksaver::build(saver);
  if (saverShown) {  // rebuilt while showing (rotation, a new look): show the new one
    lv_obj_remove_flag(saver, LV_OBJ_FLAG_HIDDEN);
    desksaver::show(true);
    saverLight = -1;
  }
}

void updateSaver(const tm& t) {
  if (!saverShown) {
    // Not over a ringing alarm (it woke the screen; nobody has touched it since the night, so idle is long).
    // Nor over the assistant's pop-up (it was called by "Gâu gâu" while the saver was on: talking to it is no touch).
    if (assistant::open()) return;
    // An app's screen keeps the saver away (stack); one with "screen.awake" does even if that rule changes.
    if (saverSec > 0 && stack.empty() && !jsapp::keepsAwake() && !kb::isOpen() && !ccOpen() && !home::editing() && !alarms::ringing() &&
        idleMs() > (uint32_t)saverSec * 1000 && millis() > 5000)
      showSaver();
    return;
  }
  if (saveredit::isOpen()) return saveredit::tick();  // lit and still while the editor or a finger is on it
  if (saverHeld) return;
  const bool q = quietNow(t);
  const int want = desksaver::backlight(q);
  if (want != saverLight) {
    saverLight = want;
    lcd::setBacklight(want);
    trace::add("saver light %d%% (quiet hours %d)", want, q);
  }
  desksaver::update(t, q);
}

// ------------------------------------------------------------------ settings shared with the web
void applyDeskSettings() {
  const int b = constrain(desk::settingInt("s3.bright", bright), 5, 100);
  if (b != bright) {
    bright = b;
    if (!saverShown) lcd::setBacklight(b);
    keepBootLook();
  }
  vol = constrain(desk::settingInt("s3.volume", vol), 0, 100);
  sound::setVolume(vol);
  saverSec = max(0, desk::settingInt("s3.saver", saverSec));
  if (ccBright) lv_slider_set_value(ccBright, bright, LV_ANIM_OFF);
  if (ccVol) lv_slider_set_value(ccVol, vol, LV_ANIM_OFF);
  const int r = desk::settingInt("s3.rot", rot) & 3;
  if (r != rot) applyRotation(r);
}

// ------------------------------------------------------------------ build
void deleteAppScreens() {
  for (lv_obj_t* s : stack)
    if (lv_obj_is_valid(s) && s != lv_screen_active()) lv_obj_delete(s);
  stack.clear();
}

void build() {
  lv_obj_t* old = mainScr;
  kb::close();
  ::home::reset();
  mainScr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(mainScr);
  lv_obj_set_style_bg_color(mainScr, C(0x000000), 0);
  lv_obj_set_style_bg_opa(mainScr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(mainScr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clean(lv_layer_top());
  toastObj = nullptr;
  home::build(mainScr);  // portrait or landscape: home.cpp lays out its own grid
  buildStatusBar();
  buildGestures();
  buildControl();
  buildSaver();
  alarms::buildLayer();
  // App screens were laid out for the old size or look: drop them.
  for (lv_obj_t* s : stack)
    if (lv_obj_is_valid(s)) lv_obj_delete_async(s);
  stack.clear();
  lv_screen_load(mainScr);
  // LVGL 9 keeps its layers (bottom, top, sys) in the same screen list: never touch those.
  for (uint32_t i = 0; i < disp->screen_cnt; i++) {
    lv_obj_t* sc = disp->screens[i];
    if (sc == mainScr || sc == old || sc == lv_layer_top() || sc == lv_layer_sys() || sc == lv_layer_bottom() ||
        sc == disp->prev_scr || sc == disp->scr_to_load)
      continue;
    lv_obj_delete_async(sc);
  }
  if (old) lv_obj_delete(old);
  barDarkOf.clear();
  updateChrome();
  trace::add("os built (rotation %d, theme %s), heap %u KB", rot, theme::kindId(), ESP.getFreeHeap() / 1024);
}

uint32_t seenSettings = 0, seenDash = 0, seenLink = 0, seenPhoto = 0;
// Seeded at begin(): Home was just built from what they say (0 made every boot build Home a second time, 0.5.7).
uint32_t seenApps = 0, seenNotif = 0, seenLock = 0;


}  // namespace

// ================================================================== public
int W() { return lv_display_get_horizontal_resolution(disp); }
int H() { return lv_display_get_vertical_resolution(disp); }
bool landscape() { return W() > H(); }
int colX() { return (W() - (landscape() ? 480 : 320)) / 2; }
int colY() { return (H() - (landscape() ? 320 : 480)) / 2; }
void column(lv_obj_t* b) {  // a BARE body: from SB_H down, the whole width
  lv_obj_set_pos(b, colX(), SB_H + colY());
  lv_obj_set_size(b, W() - 2 * colX(), H() - SB_H - 2 * colY());
}

lv_obj_t* screen(const char* title, lv_obj_t** body, int flags) {
  const Pal& p = pal();
  const bool dark = (flags & DARK) || p.dark;
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s);
  lv_obj_set_style_bg_color(s, C(flags & DARK ? 0x0d1018 : p.bg), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  barDarkOf[s] = !dark && !(flags & BARE);
  lv_obj_add_event_cb(s, [](lv_event_t* e) { barDarkOf.erase((lv_obj_t*)lv_event_get_current_target(e)); },
                      LV_EVENT_DELETE, nullptr);
  const uint32_t fg = flags & DARK ? 0xeef1f5 : p.fg;
  int top = SB_H;
  if (!(flags & BARE)) {
    lv_obj_t* nav = clear(s);
    lv_obj_set_size(nav, W(), NAV_H);
    lv_obj_set_pos(nav, 0, SB_H);
    lv_obj_set_user_data(s, nav);
    if (flags & ROOT) {
      lv_obj_t* hit = clear(nav);
      lv_obj_set_size(hit, W() - 110, NAV_H);
      lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_ext_click_area(hit, 6);
      lv_obj_add_event_cb(hit, [](lv_event_t*) { back(); }, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* chev = glyph(hit, &gl_back_22, fg);
      lv_obj_align(chev, LV_ALIGN_LEFT_MID, 6, 0);
      lv_obj_t* t = label(hit, "", 24, true, fg);
      fitText(t, title, W() - 150, 36, 24, 18, true, 0);
      lv_obj_align(t, LV_ALIGN_LEFT_MID, 30, 0);
    } else {
      lv_obj_t* hit = clear(nav);
      lv_obj_set_size(hit, W() - 90, NAV_H);
      lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(hit, [](lv_event_t*) { back(); }, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* chev = glyph(hit, &gl_back_22, fg);
      lv_obj_align(chev, LV_ALIGN_LEFT_MID, 6, 0);
      lv_obj_t* t = label(hit, "", 20, true, fg);
      fitText(t, title, W() - 130, 36, 20, 15, true, 0);
      lv_obj_align(t, LV_ALIGN_LEFT_MID, 30, 0);
    }
    top += NAV_H;
  }
  lv_obj_t* b = clear(s);
  lv_obj_set_pos(b, 0, top);
  lv_obj_set_size(b, W(), H() - top);
  if (!(flags & BARE)) {
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_top(b, 4, 0);
    lv_obj_set_style_pad_bottom(b, 24, 0);
    lv_obj_set_style_pad_row(b, 8, 0);
    // TWO in landscape: rows half wide, two a line (kit::listRow); captions and hints keep the whole line.
    const bool two = (flags & TWO) && landscape();
    lv_obj_set_flex_flow(b, two ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    if (two) {
      lv_obj_set_style_pad_column(b, 8, 0);
      lv_obj_add_flag(b, LV_OBJ_FLAG_USER_1);
    }
  }
  if (!(flags & NOSCROLL) && !(flags & BARE)) {
    lv_obj_add_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(b, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_OFF);
  }
  *body = b;
  return s;
}

lv_obj_t* navAction(lv_obj_t* scr, const char* text, lv_event_cb_t cb, void* data) {
  lv_obj_t* nav = (lv_obj_t*)lv_obj_get_user_data(scr);
  if (!nav) return nullptr;
  const bool plus = !strcmp(text, "+");
  lv_obj_t* a = clear(nav);
  lv_obj_set_size(a, LV_SIZE_CONTENT, NAV_H);
  lv_obj_set_style_pad_hor(a, 14, 0);
  lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(a, LV_ALIGN_RIGHT_MID, 0, 0);
  const uint32_t c = pal().dark || lv_obj_get_style_bg_color(scr, 0).red < 0x40 ? 0x5aa9ff : pal().link;
  if (plus) {
    lv_obj_t* g = glyph(a, &gl_plus_22, c);
    lv_obj_center(g);
  } else {
    lv_obj_t* l = label(a, text, 15, true, c);
    lv_obj_center(l);
  }
  lv_obj_set_style_opa(a, LV_OPA_50, LV_STATE_PRESSED);
  if (cb) lv_obj_add_event_cb(a, cb, LV_EVENT_CLICKED, data);
  return a;
}

lv_obj_t* navTitle(lv_obj_t* scr) {
  lv_obj_t* nav = (lv_obj_t*)lv_obj_get_user_data(scr);
  if (!nav) return nullptr;
  lv_obj_t* first = lv_obj_get_child(nav, 0);
  return lv_obj_check_type(first, &lv_label_class) ? first : lv_obj_get_child(first, 1);
}

void open(lv_obj_t* scr) {
  kb::close();
  if (ccOpen()) closeCcNow();
  stack.push_back(scr);
  trace::add("screen open (%u deep)", (unsigned)stack.size());
  // No slide: on this board a slide drew both screens at ~12 frames a second (it looked stuck);
  // one clean switch feels faster.
  lv_screen_load(scr);
  updateChrome();
}

std::map<lv_obj_t*, bool (*)()> backHooks;  // setBackHook

void setBackHook(lv_obj_t* scr, bool (*fn)()) {
  if (!backHooks.count(scr))
    lv_obj_add_event_cb(scr, [](lv_event_t* e) { backHooks.erase((lv_obj_t*)lv_event_get_current_target(e)); },
                        LV_EVENT_DELETE, nullptr);
  backHooks[scr] = fn;
}

void back() {
  if (stack.empty()) return;
  auto hook = backHooks.find(stack.back());
  if (hook != backHooks.end() && hook->second && hook->second()) return;  // the app went back inside itself
  kb::close();
  lv_obj_t* top = stack.back();
  stack.pop_back();
  lv_obj_t* under = stack.empty() ? mainScr : stack.back();
  if (!lv_obj_is_valid(under)) under = mainScr;
  trace::add("screen back (%u deep)", (unsigned)stack.size());
  lv_screen_load(under);
  if (lv_obj_is_valid(top)) lv_obj_delete_async(top);  // often called from one of its own buttons
  if (stack.empty()) apps::clearRoute();
  updateChrome();
}

void goHome() {
  if (stack.empty()) {
    ::home::toFirstPage();
    return;
  }
  kb::close();
  lv_obj_t* active = lv_screen_active();
  for (lv_obj_t* s : stack)
    if (s != active && lv_obj_is_valid(s)) lv_obj_delete_async(s);
  const bool fromApp = std::find(stack.begin(), stack.end(), active) != stack.end();
  stack.clear();
  apps::clearRoute();
  trace::add("screen home");
  lv_screen_load(mainScr);
  if (fromApp && lv_obj_is_valid(active)) lv_obj_delete_async(active);
  updateChrome();
}

void closeApps() {
  if (stack.empty()) return;
  kb::close();
  lv_obj_t* active = lv_screen_active();
  if (active != mainScr) lv_screen_load(mainScr);
  for (lv_obj_t* s : stack)
    if (lv_obj_is_valid(s)) lv_obj_delete_async(s);
  stack.clear();
  updateChrome();
}

bool atHome() { return stack.empty(); }
lv_obj_t* homeScreen() { return mainScr; }

void barDark(bool darkText) {
  sbDark = darkText;
  paintBar();
}

void barDarkFor(lv_obj_t* scr, bool darkText) {
  barDarkOf[scr] = darkText;
  if (!stack.empty() && stack.back() == scr) barDark(darkText);
}

void setStatusHidden(bool) { updateChrome(); }

void fullScreenFor(lv_obj_t* scr, bool on) {
  if (on && !fullScreens.count(scr)) {
    fullScreens.insert(scr);
    lv_obj_add_event_cb(scr, [](lv_event_t* e) { fullScreens.erase((lv_obj_t*)lv_event_get_current_target(e)); },
                        LV_EVENT_DELETE, nullptr);
  } else if (!on) {
    fullScreens.erase(scr);
  }
  updateChrome();
}

namespace {
void hideBar(bool hidden) {
  sbHidden = hidden;
  if (!sb) return;
  for (uint32_t i = 0; i < lv_obj_get_child_count(sb); i++)
    hidden ? lv_obj_add_flag(lv_obj_get_child(sb, i), LV_OBJ_FLAG_HIDDEN)
           : lv_obj_remove_flag(lv_obj_get_child(sb, i), LV_OBJ_FLAG_HIDDEN);
  hidden ? lv_obj_add_flag(sb, LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(sb, LV_OBJ_FLAG_HIDDEN);
}
}  // namespace

void toast(const char* text) { showToast(text); }

void post(const char* app, const char* title, const char* body) { notif::post(app, title, body); }

void openControl() {
  lv_obj_move_foreground(cc);
  updateControl();
  animCc(true);
}

void showSaver() {
  if (saverShown) return;
  kb::close();
  saverShown = true;
  saverHeld = false;
  lv_obj_remove_flag(saver, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(saver);
  desksaver::show(true);
  saverLight = -1;
  trace::add("saver on");
  updateSaver(nowTm());
}

void wake() {
  if (!saverShown) return;
  saveredit::close(true);  // an alarm or a notification while editing: keep what was done
  saverShown = false;
  saverHeld = false;
  lv_obj_add_flag(saver, LV_OBJ_FLAG_HIDDEN);
  desksaver::show(false);
  lcd::setBacklight(bright);
  trace::add("saver off");
}

bool saverOn() { return saverShown; }

void rebuild() { build(); }

void applyRotation(int r) {
  kb::close();
  rot = r & 3;
  desk::setSetting("s3.rot", String(rot));
  prefs.putInt(rot & 1 ? "rotL" : "rotP", rot);  // the user's own landscape and portrait ways (Control Center)
  keepBootLook();
  static const lv_display_rotation_t map[] = {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180,
                                              LV_DISPLAY_ROTATION_270};
  // The app in front opens again, laid out for the new shape (every screen is built again).
  String id, arg;
  const bool reopen = !stack.empty() && apps::route(id, arg);
  lcd::setRotation(map[rot]);
  build();
  if (reopen) {
    trace::add("rotation: %s %s again", id.c_str(), arg.c_str());
    apps::open(id.c_str(), arg.length() ? arg.c_str() : nullptr);
  }
}

void turn() { applyRotation(landscape() ? prefs.getInt("rotP", 0) & 2 : prefs.getInt("rotL", 1) | 1); }

int rotation() { return rot; }
int brightness() { return bright; }
void setBrightness(int pct, bool save) {
  bright = constrain(pct, 5, 100);
  lcd::setBacklight(bright);
  if (save) desk::setSetting("s3.bright", String(bright)), keepBootLook();
}
int volume() { return vol; }
void setVolume(int pct, bool save) {
  vol = constrain(pct, 0, 100);
  sound::setVolume(vol);
  if (save) desk::setSetting("s3.volume", String(vol));
}

bool fingerDown() { return lv_indev_get_state(touch::indev()) == LV_INDEV_STATE_PRESSED; }
uint32_t idleMs() { return millis() - touch::lastTouchMs(); }

void begin(lv_display_t* d) {
  disp = d;
  prefs.begin("desk");
  rot = desk::settingInt("s3.rot", prefs.getInt("rot", 0)) & 3;
  bright = constrain(desk::settingInt("s3.bright", prefs.getInt("bl", 70)), 5, 100);
  vol = constrain(desk::settingInt("s3.volume", prefs.getInt("vol", 45)), 0, 100);
  saverSec = desk::settingInt("s3.saver", prefs.getInt("saver", 120));
  pkg::awaitCard(500);  // the card's apps and language font in before Home is built (0.5.7: built twice)
  splash::step(40);
  i18n::begin();  // the language (sys.lang) before anything is built
  splash::step(45);
  seedClock();
  theme::begin();
  alarms::begin();
  splash::step(60);
  applyRotation(rot);
  lcd::setBacklight(bright);
  keepBootLook();
  seenApps = apps::version();
  seenNotif = notif::version();
  seenLock = lock::version();
  seenSettings = desk::settingsVersion();
  seenDash = desk::dashVersion();
  seenLink = (uint32_t)desk::link();
  seenPhoto = photo::version();
}

void tick() {
  static uint32_t last = 0;
  static bool pending = false, lookPending = false;
  if (seenSettings != desk::settingsVersion()) {
    seenSettings = desk::settingsVersion();
    applyDeskSettings();
    if (theme::refresh()) lookPending = true;
    if (home::settingsChanged()) pending = true;
  }
  if (seenApps != apps::version()) {  // an app was installed or removed: new icons
    seenApps = apps::version();
    pending = true;
  }
  static uint32_t ccCheck = 0;
  if (millis() - ccCheck >= 1000) ccCheck = millis(), ccFollowAssistant();  // cheap: two strings, once a second
#if ESPOS_ONBOARD
  onboard::tick();  // the first-boot setup, the claim's welcome, a store demo unit (onboard.h)
#endif
  lock::tick();  // the trial's notices and end card (lock.h)
  preset::tick();  // a factory card's preset, once the card has been looked at (preset.h)
  if (seenLock != lock::version()) {  // a paid app locked or opened again: its icon's lock
    seenLock = lock::version();
    pending = true;
  }
  if (seenDash != desk::dashVersion() || seenLink != (uint32_t)desk::link() || seenPhoto != photo::version()) {
    seenDash = desk::dashVersion();
    seenLink = (uint32_t)desk::link();
    seenPhoto = photo::version();
    pending = true;
  }
  // The widgets' skeletons (wid::empty) last until the first data, or 2 minutes: then their words.
  static bool firstWait = true;
  if (firstWait && millis() > 121000) {
    firstWait = false;
    if (desk::dash().isNull()) pending = true;
  }
  i18n::tick();
  if (i18n::switchReady()) lookPending = true;  // sys.lang from the web, its pack just loaded
  notif::tick();
  if (seenNotif != notif::version()) {  // a badge or the Center's list changed
    seenNotif = notif::version();
    pending = true;
    fillCcNotes();
  }
  // Rebuild only when nobody is touching Home, so nothing changes under a finger.
  const bool calm = !fingerDown() && idleMs() > 1500 && !saverShown && !ccOpen() && !home::editing() && !kb::isOpen() &&
                    !notif::centerShown();
  if (lookPending && calm && stack.empty()) {
    lookPending = pending = false;
    i18n::switchDone();
    build();
  } else if (pending && calm && stack.empty()) {
    pending = false;
    home::refresh();
  }
  if (toastObj && toastAt && millis() - toastAt > 2200) {
    lv_obj_add_flag(toastObj, LV_OBJ_FLAG_HIDDEN);
    toastAt = 0;
  }
  alarms::tick();
  kb::tick();
#ifndef NO_PET
  static uint32_t lastPet = 0;
  if (millis() - lastPet >= 500) {
    lastPet = millis();
    pet::tick();
  }
#endif
  if (millis() - last < 500) return;
  last = millis();
  const tm t = nowTm();
  // Night look for the app screens (auto theme) switches at 19:00 and 06:00.
  static int lastMin = -1;
  if (t.tm_min != lastMin) {
    lastMin = t.tm_min;
    if (theme::refresh()) lookPending = true;
  }
  updateStatusBar(t);
  updateControl();
  // The Control Center picture: renewed while nobody touches, at most every 20 s.
  if (!ccSliding && !fingerDown() && idleMs() > 800 && (!ccShotOk || millis() - ccShotAt > 20000) && !saverShown) snapCc();
  home::tick();
  updateSaver(t);
  if (kb::isOpen() && homeBar && !lv_obj_has_flag(homeBar, LV_OBJ_FLAG_HIDDEN)) updateChrome();
  if (!kb::isOpen() && !stack.empty() && homeBar && lv_obj_has_flag(homeBar, LV_OBJ_FLAG_HIDDEN)) updateChrome();
}

}  // namespace os
