#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Clock (dark): alarms through the speaker, a timer, a stopwatch and world clocks.
#include <Arduino.h>

#include <algorithm>
#include <ctime>
#include <vector>

#include "alarm.h"
#include "apps.h"
#include "art.h"
#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "keyboard.h"
#include "os.h"
#include "uikit.h"

namespace app_clock {
namespace {

using namespace kit;

const uint32_t BG = 0x0d1018, CELL = 0x171c26, FG = 0xeef1f5, DIM = 0x8a93a3, ORANGE = 0xff9f0a;

lv_obj_t *scr_, *content;
int tab = 0;

// Landscape: alarms two a line, the timer's dials beside its quick picks (content flows in rows then).
int halfW() { return (os::W() - 24 - 8) / 2; }

lv_obj_t* darkCell(lv_obj_t* parent, int h) {
  lv_obj_t* c = box(parent, CELL, 16);
  lv_obj_set_size(c, lv_pct(100), h);
  lv_obj_set_style_pad_hor(c, 14, 0);
  return c;
}

lv_obj_t* darkChips(lv_obj_t* parent, const char* const* names, int n, int sel, void (*cb)(int)) {
  lv_obj_t* bar = chips(parent, names, n, sel, cb);
  for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++) {
    lv_obj_t* c = lv_obj_get_child(bar, i);
    const bool on = (int)i == sel;
    lv_obj_set_style_bg_color(c, C(on ? 0xffffff : 0x1f2430), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(c, 0), C(on ? BG : 0xc9d0d8), 0);
  }
  return bar;
}

void fill();

// ---------------------------------------------------------------- alarms
struct Edit {
  int index;  // -1 = new
  alarms::Alarm a;
  lv_obj_t *hr, *mr, *days, *ta;
} ed;

String rollerOpts(int n) {
  String s;
  for (int i = 0; i < n; i++) {
    char b[4];
    snprintf(b, sizeof b, "%02d", i);
    s += b;
    if (i < n - 1) s += "\n";
  }
  return s;
}

lv_obj_t* roller(lv_obj_t* parent, int n, int sel) {
  lv_obj_t* r = lv_roller_create(parent);
  const String opts = rollerOpts(n);
  lv_roller_set_options(r, opts.c_str(), LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(r, 3);
  lv_roller_set_selected(r, sel, LV_ANIM_OFF);
  lv_obj_set_width(r, 96);
  lv_obj_set_style_text_font(r, fonts::get(34, true), 0);
  lv_obj_set_style_text_font(r, fonts::get(34, true), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, C(BG), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_text_color(r, C(0x4b5563), 0);
  lv_obj_set_style_text_color(r, C(FG), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, C(0x1f2430), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
  lv_obj_set_style_radius(r, 12, LV_PART_SELECTED);
  lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
  return r;
}

void paintDays() {
  static const int ORDER[] = {1, 2, 3, 4, 5, 6, 0};
  for (int i = 0; i < 7; i++) {
    lv_obj_t* b = lv_obj_get_child(ed.days, i);
    const bool on = ed.a.days.indexOf(char('0' + ORDER[i])) >= 0;
    lv_obj_set_style_bg_color(b, C(on ? ORANGE : 0x1f2430), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C(on ? 0x1b1408 : 0xc9d0d8), 0);
  }
}

void saveEdit(lv_event_t*) {
  kb::close();
  ed.a.h = lv_roller_get_selected(ed.hr);
  ed.a.m = lv_roller_get_selected(ed.mr);
  ed.a.label = lv_textarea_get_text(ed.ta);
  ed.a.on = true;
  auto all = alarms::list();
  if (ed.index >= 0 && ed.index < (int)all.size()) all[ed.index] = ed.a;
  else all.push_back(ed.a);
  alarms::save(all);
  os::back();
  fill();
  String when, day;
  alarms::next(when, day);
  if (when.length()) os::toast(i18n::fmt(N_("Alarm {0} · {1}"), when, day).c_str());
}

void openEditor(int index) {
  ed = {};
  ed.index = index;
  const auto all = alarms::list();
  if (index >= 0 && index < (int)all.size()) ed.a = all[index];
  else ed.a.days = "12345", ed.a.h = 6, ed.a.m = 30;
  lv_obj_t* body;
  lv_obj_t* s = os::screen(index < 0 ? N_("Add alarm") : N_("Edit alarm"), &body, os::DARK);
  os::navAction(s, N_("Save"), saveEdit);
  lv_obj_t* row = clear(body);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  ed.hr = roller(row, 24, ed.a.h);
  label(row, ":", 34, true, FG);
  ed.mr = roller(row, 60, ed.a.m);
  lv_obj_t* cap = label(body, upperVi(T_("Repeat")).c_str(), 12, true, DIM);
  lv_obj_set_style_pad_left(cap, 4, 0);
  ed.days = clear(body);
  lv_obj_set_size(ed.days, lv_pct(100), 36);
  lv_obj_set_flex_flow(ed.days, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ed.days, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  static const int ORDER[] = {1, 2, 3, 4, 5, 6, 0};
  for (int i = 0; i < 7; i++) {
    lv_obj_t* b = box(ed.days, 0x1f2430, 17);
    lv_obj_set_size(b, (os::W() - 24 - 36) / 7, 34);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = label(b, WD_SHORT[ORDER[i]], 13, true, 0xc9d0d8);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      const char c = '0' + (int)(intptr_t)lv_event_get_user_data(e);
      const int at = ed.a.days.indexOf(c);
      if (at >= 0) ed.a.days.remove(at, 1);
      else ed.a.days += c;
      paintDays();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)ORDER[i]);
  }
  paintDays();
  lv_obj_t* hint_ = label(body, "", 12, false, DIM);
  wrapText(hint_, N_("No days chosen: it rings once."), os::W() - 24 - 8, 12);
  lv_obj_set_style_pad_left(hint_, 4, 0);
  lv_obj_t* cap2 = label(body, upperVi(T_("Name")).c_str(), 12, true, DIM);
  lv_obj_set_style_pad_left(cap2, 4, 0);
  ed.ta = lv_textarea_create(body);
  lv_obj_set_width(ed.ta, lv_pct(100));
  lv_textarea_set_one_line(ed.ta, true);
  lv_textarea_set_text(ed.ta, ed.a.label.c_str());
  lv_textarea_set_placeholder_text(ed.ta, T_("For example: Get up for work"));
  lv_obj_set_style_text_font(ed.ta, fonts::get(16), 0);
  lv_obj_set_style_bg_color(ed.ta, C(CELL), 0);
  lv_obj_set_style_text_color(ed.ta, C(FG), 0);
  lv_obj_set_style_border_width(ed.ta, 0, 0);
  lv_obj_set_style_radius(ed.ta, 12, 0);
  lv_obj_set_style_pad_all(ed.ta, 10, 0);
  lv_obj_add_event_cb(ed.ta, [](lv_event_t*) {
    kb::open(ed.ta, kb::Mode::Text, [](lv_obj_t*) { kb::close(); });
  }, LV_EVENT_CLICKED, nullptr);
  if (index >= 0) {
    lv_obj_t* del = button(body, N_("Delete alarm"), 0x2a1416, col::RED, [](lv_event_t*) {
      auto all = alarms::list();
      if (ed.index >= 0 && ed.index < (int)all.size()) all.erase(all.begin() + ed.index);
      alarms::save(all);
      os::back();
      fill();
    });
    lv_obj_set_width(del, lv_pct(100));
  }
  os::open(s);
}

void fillAlarms() {
  const auto all = alarms::list();
  int i = 0;
  for (const auto& a : all) {
    String sub = alarms::daysText(a.days);
    if (a.label.length()) sub += " · " + a.label;
    // The days and the name on one line under the time; too long even at 12 px (a longer language, a half-width card
    // in landscape): two lines, the card taller.
    const int subW = (os::landscape() ? halfW() : os::W() - 24) - 28 - 60;
    int n = 0;
    for (const char* q = sub.c_str(); *q; q++) n += (*q & 0xC0) != 0x80;
    const bool two = textW(sub.c_str(), 12) - (n - 1) > subW;
    lv_obj_t* c = darkCell(content, two ? 102 : 84);
    if (os::landscape()) lv_obj_set_width(c, halfW());
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c, C(0x1f2531), LV_STATE_PRESSED);
    lv_obj_t* t = label(c, hhmm(a.h, a.m).c_str(), 44, true, a.on ? FG : 0x4b5563);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 6);
    lv_obj_t* d = label(c, "", 13, false, a.on ? DIM : 0x4b5563);
    fitText(d, sub.c_str(), subW, two ? 36 : 18, 13, 12);
    lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 0, -8);
    lv_obj_t* sw = toggle(c, a.on, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      auto all = alarms::list();
      if (i < (int)all.size()) all[i].on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
      alarms::save(all);
      lv_async_call([](void*) { fill(); }, nullptr);
    }, (void*)(intptr_t)i);
    lv_obj_set_style_bg_color(sw, C(0x3a4250), LV_PART_MAIN);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(c, [](lv_event_t* e) { openEditor((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_SHORT_CLICKED,
                        (void*)(intptr_t)i);
    i++;
  }
  if (all.empty()) {
    lv_obj_t* c = darkCell(content, 70);
    lv_obj_t* l = label(c, "", 15, false, DIM);
    fitText(l, N_("No alarms yet. Tap + to add one."), os::W() - 24 - 28, 60, 15, 12);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  }
  lv_obj_t* h = label(content, "", 12, false, DIM);
  fitText(h, N_("Alarms ring through the speaker, even when the screen saver has dimmed the screen or Do Not Disturb is on. "
                "You can edit them on the web too."),
          os::W() - 32, 60, 12, 12, false, 3);
  lv_obj_set_style_pad_hor(h, 4, 0);
  lv_obj_set_width(h, lv_pct(100));  // a whole line under the cards
}

// ---------------------------------------------------------------- timer
lv_obj_t *timerBig, *timerBtn;
lv_obj_t *thr, *tmr, *tsr;

String mmss(int s) {
  char b[16];
  if (s >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
  else snprintf(b, sizeof b, "%02d:%02d", s / 60, s % 60);
  return b;
}

void fillTimer() {
  if (alarms::timerOn()) {
    lv_obj_t* c = darkCell(content, 200);
    keep(&timerBig, label(c, mmss(alarms::timerLeft()).c_str(), 64, true, FG));
    lv_obj_align(timerBig, LV_ALIGN_CENTER, 0, -30);
    lv_obj_t* row = clear(c);
    lv_obj_set_size(row, lv_pct(100), 50);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* cancel = button(row, N_("Cancel"), 0x2b3040, FG, [](lv_event_t*) {
      alarms::cancelTimer();
      lv_async_call([](void*) { fill(); }, nullptr);
    });
    lv_obj_set_size(cancel, 110, 46);
    keep(&timerBtn, button(row, alarms::timerPaused() ? N_("Resume") : N_("Pause"), alarms::timerPaused() ? 0x1f7a3a : 0x6b4a10,
                           alarms::timerPaused() ? 0x8af0a8 : 0xffc56b, [](lv_event_t*) {
                             alarms::timerPaused() ? alarms::resumeTimer() : alarms::pauseTimer();
                             lv_async_call([](void*) { fill(); }, nullptr);
                           }));
    lv_obj_set_size(timerBtn, 110, 46);
    return;
  }
  // Landscape: the dials and Start on the left, the quick picks on the right.
  const bool land = os::landscape();
  lv_obj_t *left = content, *right = content;
  if (land) {
    left = stack(content, false, 8), right = stack(content, false, 8);
    lv_obj_set_width(left, halfW()), lv_obj_set_width(right, halfW());
  }
  lv_obj_t* row = clear(left);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  thr = roller(row, 24, 0);
  lv_obj_set_width(thr, land ? 64 : 84);
  label(row, N_("hours"), 13, false, DIM);
  tmr = roller(row, 60, 5);
  lv_obj_set_width(tmr, land ? 64 : 84);
  label(row, N_("min"), 13, false, DIM);
  lv_obj_t* go = button(left, N_("Start"), 0x1f7a3a, 0x8af0a8, [](lv_event_t*) {
    const int s = lv_roller_get_selected(thr) * 3600 + lv_roller_get_selected(tmr) * 60;
    if (s <= 0) return os::toast(N_("Choose a time first"));
    alarms::startTimer(s);
    lv_async_call([](void*) { fill(); }, nullptr);
  });
  lv_obj_set_width(go, lv_pct(100));
  lv_obj_t* cap = label(right, upperVi(T_("Quick")).c_str(), 12, true, DIM);
  lv_obj_set_style_pad_left(cap, 4, 0);
  lv_obj_t* grid = clear(right);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, 8, 0);
  static const int PRESETS[] = {1, 3, 5, 10, 15, 25, 30, 60};
  for (int m : PRESETS) {
    lv_obj_t* b = box(grid, 0x1f2430, 14);
    lv_obj_set_size(b, land ? (halfW() - 16) / 3 : (os::W() - 24 - 24) / 4, land ? 42 : 48);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, C(0x2b3040), LV_STATE_PRESSED);
    // i18n: a timer preset in minutes; the long form has room (vi "{0} phút", the short one "{0} ph")
    lv_obj_t* l = label(b, i18n::fmt(m == 60 ? TC_("long", "{0} min") : TC_("short", "{0} min"), String(m)).c_str(), 15, true, FG);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      alarms::startTimer((int)(intptr_t)lv_event_get_user_data(e) * 60);
      lv_async_call([](void*) { fill(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)m);
  }
}

// ---------------------------------------------------------------- stopwatch
uint32_t swStart = 0, swAcc = 0;
bool swRun = false;
std::vector<uint32_t> laps;
lv_obj_t *swBig, *swLaps;

uint32_t swNow() { return swAcc + (swRun ? millis() - swStart : 0); }

String swText(uint32_t ms) {
  char b[16];
  snprintf(b, sizeof b, "%02lu:%02lu%s%02lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60), i18n::decSep(),
           (unsigned long)(ms / 10 % 100));
  return b;
}

void fillLaps() {
  if (!swLaps) return;
  lv_obj_clean(swLaps);
  for (int i = laps.size() - 1; i >= 0 && (int)laps.size() - i <= 6; i--) {
    lv_obj_t* r = darkCell(swLaps, 38);
    lv_obj_t* a = label(r, i18n::fmt(N_("Lap {0}"), String(i + 1)).c_str(), 14, false, DIM);
    lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
    const uint32_t lap = laps[i] - (i ? laps[i - 1] : 0);
    lv_obj_t* b = label(r, swText(lap).c_str(), 15, true, FG);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

void fillStopwatch() {
  const bool land = os::landscape();  // the time and its buttons side by side, the laps under them
  lv_obj_t* c = darkCell(content, 120);
  if (land) lv_obj_set_width(c, halfW());
  keep(&swBig, label(c, swText(swNow()).c_str(), land ? 44 : 56, true, FG));
  lv_obj_center(swBig);
  lv_obj_t* row = clear(content);
  lv_obj_set_size(row, land ? halfW() : lv_pct(100), land ? 120 : 50);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* a = button(row, swRun ? N_("Lap") : N_("Reset"), 0x2b3040, FG, [](lv_event_t*) {
    if (swRun) laps.push_back(swNow());
    else swAcc = 0, laps.clear();
    lv_async_call([](void*) { fill(); }, nullptr);
  });
  lv_obj_set_size(a, land ? 100 : 120, 46);
  lv_obj_t* b = button(row, swRun ? N_("Stop") : N_("Start"), swRun ? 0x4a1a1a : 0x1f7a3a, swRun ? 0xff8a8a : 0x8af0a8, [](lv_event_t*) {
    if (swRun) swAcc = swNow(), swRun = false;
    else swStart = millis(), swRun = true;
    lv_async_call([](void*) { fill(); }, nullptr);
  });
  lv_obj_set_size(b, land ? 100 : 120, 46);
  keep(&swLaps, stack(content, false, 6));
  fillLaps();
}

// ---------------------------------------------------------------- world clocks
struct City {
  const char* id;    // what s3.cities keeps (the web's list): never translated
  const char* name;  // shown
  int off;           // minutes from UTC
};
const City CITIES[] = {{"Hà Nội", N_("Hanoi"), 420},         {"Tokyo", N_("Tokyo"), 540},
                       {"Seoul", N_("Seoul"), 540},          {"Bắc Kinh", N_("Beijing"), 480},
                       {"Singapore", N_("Singapore"), 480},  {"Bangkok", N_("Bangkok"), 420},
                       {"Sydney", N_("Sydney"), 660},        {"Dubai", N_("Dubai"), 240},
                       {"Moskva", N_("Moscow"), 180},        {"Paris", N_("Paris"), 60},
                       {"Berlin", N_("Berlin"), 60},         {"London", N_("London"), 0},
                       {"New York", N_("New York"), -300},   {"Los Angeles", N_("Los Angeles"), -480}};

std::vector<String> chosen() { return data::codesOf(desk::setting("s3.cities", ""), "Tokyo,Paris,New York,Sydney"); }

void saveCities(const std::vector<String>& c) {
  String s;
  for (const String& x : c) s += (s.length() ? "," : "") + x;
  desk::setSetting("s3.cities", s);
}

void fillWorld() {
  const time_t now = time(nullptr);
  int i = 0;
  for (const String& n : chosen()) {
    const City* ct = nullptr;
    for (const City& x : CITIES)
      if (n == x.id) ct = &x;
    if (!ct) continue;
    const time_t t = now + ct->off * 60;
    tm g;
    gmtime_r(&t, &g);
    // Hours from the device's own time zone (sys.tz_min, Vietnam's by default).
    const int diff = (ct->off - desk::settingInt("sys.tz_min", 420)) / 60;
    lv_obj_t* c = darkCell(content, 64);
    lv_obj_t* a = label(c, ct->name, 17, true, FG);
    lv_obj_align(a, LV_ALIGN_TOP_LEFT, 0, 10);
    // i18n: the city is in the device's own time zone (vi "Cùng giờ Việt Nam": the owner's)
    String rel = diff == 0 ? String(T_("Same time as here"))
                 : diff > 0 ? i18n::plural(diff, NP_("{n} hour ahead|{n} hours ahead"))
                            : i18n::plural(-diff, NP_("{n} hour behind|{n} hours behind"));
    lv_obj_t* b = label(c, rel.c_str(), 12, false, DIM);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, -10);
    lv_obj_t* tl = label(c, hhmm(g.tm_hour, g.tm_min).c_str(), 34, true, FG);
    // i18n: takes a city off the world clocks (vi "Bỏ")
    const char* rm = TC_("city", "Remove");
    lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -std::max(36, textW(rm, 13, true) + 12), 0);
    lv_obj_t* x = label(c, rm, 13, true, col::RED);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(x, 8);
    lv_obj_add_event_cb(x, [](lv_event_t* e) {
      auto c = chosen();
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      if (i < (int)c.size()) c.erase(c.begin() + i);
      saveCities(c);
      lv_async_call([](void*) { fill(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    i++;
  }
  lv_obj_t* cap = label(content, upperVi(T_("Add a city")).c_str(), 12, true, DIM);
  lv_obj_set_style_pad_left(cap, 4, 0);
  lv_obj_t* grid = clear(content);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, 6, 0);
  const auto have = chosen();
  for (const City& x : CITIES) {
    bool in = false;
    for (const String& h : have) in |= h == x.id;
    if (in) continue;
    lv_obj_t* b = box(grid, 0x1f2430, 14);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = label(b, x.name, 13, true, 0xc9d0d8);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      auto c = chosen();
      if (c.size() >= 8) return os::toast(N_("At most 8 cities"));
      c.push_back((const char*)lv_event_get_user_data(e));
      saveCities(c);
      lv_async_call([](void*) { fill(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)x.id);
  }
}

void fill() {
  if (!content) return;
  lv_obj_clean(content);
  timerBig = timerBtn = swBig = swLaps = nullptr;
  if (tab == 0) fillAlarms();
  else if (tab == 1) fillTimer();
  else if (tab == 2) fillStopwatch();
  else fillWorld();
}

const char* const TABS[] = {N_("Alarm"), N_("Timer"), N_("Stopwatch"), N_("World clock")};

}  // namespace

void open(const char* arg) {
  const String a = arg ? arg : "";
  tab = a == "timer" ? 1 : a == "stopwatch" ? 2 : a == "world" ? 3 : 0;
  lv_obj_t* body;
  scr_ = os::screen(N_("Clock"), &body, os::ROOT | os::DARK);
  os::navAction(scr_, "+", [](lv_event_t*) {
    if (tab == 0) openEditor(-1);
    else if (tab == 3) os::toast(N_("Pick a city at the bottom of the list"));
  });
  darkChips(body, TABS, 4, tab, [](int i) {
    tab = i;
    lv_obj_t* bar = lv_obj_get_child(lv_obj_get_parent(content), 0);
    for (uint32_t k = 0; k < lv_obj_get_child_count(bar); k++) {
      lv_obj_t* c = lv_obj_get_child(bar, k);
      lv_obj_set_style_bg_color(c, C((int)k == i ? 0xffffff : 0x1f2430), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(c, 0), C((int)k == i ? BG : 0xc9d0d8), 0);
    }
    fill();
  });
  content = stack(body, false, 8);
  if (os::landscape()) lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);  // halves where a tab has them
  lv_obj_add_event_cb(scr_, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == scr_) content = scr_ = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fill();
  every(scr_, 100, [](lv_timer_t*) {
    if (tab == 1 && timerBig) {
      setText(timerBig, mmss(alarms::timerLeft()).c_str());
      if (!alarms::timerOn()) fill();
    }
    if (tab == 2 && swBig && swRun) setText(swBig, swText(swNow()).c_str());
  });
  os::open(scr_);
}

}  // namespace app_clock
