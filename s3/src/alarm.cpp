#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "alarm.h"

#include <ArduinoJson.h>
#include <lvgl.h>

#include <ctime>

#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "lcd.h"
#include "os.h"
#include "sound.h"
#include "trace.h"
#include "uikit.h"

namespace alarms {
namespace {

using namespace kit;

lv_obj_t *ring, *ringTime, *ringLabel;
bool isRinging = false;
uint32_t ringStarted = 0, lastBeep = 0;
int64_t snoozeUntil = 0;  // epoch ms
String snoozeLabel;
int lastFiredMin = -1;
const Hook* hook = nullptr;
int rings = 0;  // the alarm ringing now: its 1st, 2nd... time (again after each snooze); 0 = none or the timer

void tell(int ev, int h = 0, int m = 0) {
  if (hook && hook->event && (rings || ev)) hook->event(ev, rings, h, m, ring);
}

// Timer state (millis based).
uint32_t timerEnd = 0, timerPausedLeft = 0;
int timerTot = 0;
bool tOn = false, tPaused = false;

void stopRing() {
  isRinging = false;
  if (ring) lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
  trace::add("alarm stopped");
}

void startRing(const String& label) {
  os::wake();
  isRinging = true;
  ringStarted = millis();
  lastBeep = 0;
  if (!ring) buildLayer();
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  setText(ringTime, hhmm(l.tm_hour, l.tm_min).c_str());
  setText(ringLabel, label.length() ? label.c_str() : N_("Alarm"));
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ring);
  lcd::setBacklight(max(os::brightness(), 40));
  trace::add("alarm ringing: %s", label.c_str());
}

bool dueToday(const Alarm& a, int wday) { return !a.days.length() || a.days.indexOf(char('0' + wday)) >= 0; }

}  // namespace

std::vector<Alarm> list() {
  std::vector<Alarm> out;
  JsonDocument doc;
  if (!data::loadJson("s3.alarms", doc)) return out;
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    Alarm a;
    a.h = o["h"] | 7;
    a.m = o["m"] | 0;
    a.days = o["days"] | "";
    a.on = (o["on"] | 1) != 0;
    a.label = o["label"] | "";
    out.push_back(a);
  }
  return out;
}

void save(const std::vector<Alarm>& all) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Alarm& a : all) {
    JsonObject o = arr.add<JsonObject>();
    o["h"] = a.h;
    o["m"] = a.m;
    o["days"] = a.days;
    o["on"] = a.on ? 1 : 0;
    if (a.label.length()) o["label"] = a.label;
  }
  data::saveJson("s3.alarms", doc);
}

String daysText(const String& d) {
  if (!d.length()) return T_("Once");
  if (d.length() == 7) return T_("Every day");
  if (d == "12345") return T_("Weekdays");  // i18n: Monday to Friday (vi "T2 đến T6")
  if (d == "06" || d == "60") return T_("Weekends");
  String s;
  for (int i : {1, 2, 3, 4, 5, 6, 0})
    if (d.indexOf(char('0' + i)) >= 0) s += String(s.length() ? ", " : "") + WD_SHORT[i];
  return s;
}

void next(String& when, String& day) {
  when = day = "";
  const auto all = list();
  const time_t now = time(nullptr);
  tm l;
  localtime_r(&now, &l);
  const int nowMin = l.tm_hour * 60 + l.tm_min;
  int best = 1 << 30;
  const Alarm* pick = nullptr;
  int pickDays = 0;
  for (const Alarm& a : all) {
    if (!a.on) continue;
    for (int add = 0; add < 8; add++) {
      const int wd = (l.tm_wday + add) % 7;
      const int at = add * 1440 + a.h * 60 + a.m;
      if (add == 0 && a.h * 60 + a.m <= nowMin) continue;
      if (!dueToday(a, wd)) continue;
      if (at < best) best = at, pick = &a, pickDays = add;
      break;
    }
  }
  if (snoozeUntil > 0) {
    const int64_t mins = (snoozeUntil / 1000 - now) / 60;
    if (mins >= 0 && nowMin + mins < best) {
      const time_t st = snoozeUntil / 1000;
      tm s;
      localtime_r(&st, &s);
      when = hhmm(s.tm_hour, s.tm_min);
      day = i18n::fmt(N_("Snoozed · {0}"), snoozeLabel);
      return;
    }
  }
  if (!pick) return;
  when = hhmm(pick->h, pick->m);
  const int wd = (l.tm_wday + pickDays) % 7;
  day = pickDays == 0 ? T_("Today") : pickDays == 1 ? T_("Tomorrow") : WEEKDAY[wd];
  if (pick->label.length()) day += " · " + pick->label;
}

void begin() {}

void buildLayer() {
  keep(&ring, box(lv_layer_top(), 0x0b0d14, 0));
  lv_obj_set_size(ring, os::W(), os::H());
  lv_obj_add_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
  grad(ring, 0x1b1f3a, 0x0b0d14);
  keep(&ringLabel, label(ring, N_("Alarm"), 20, true, 0xffb547));
  lv_obj_align(ringLabel, LV_ALIGN_TOP_MID, 0, os::landscape() ? 30 : 90);
  keep(&ringTime, label(ring, "", 96, false, 0xffffff));
  lv_obj_align(ringTime, LV_ALIGN_TOP_MID, 0, os::landscape() ? 60 : 130);
  lv_obj_t* row = clear(ring);
  lv_obj_set_size(row, os::W() - 32, 56);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* sn = button(row, N_("Snooze 9 min"), 0x2b3050, 0xffffff, [](lv_event_t*) {
    snoozeUntil = (desk::nowMs() ? desk::nowMs() : (int64_t)time(nullptr) * 1000) + 9 * 60000;
    snoozeLabel = lv_label_get_text(ringLabel);
    stopRing();
    os::toast(N_("Snoozed for 9 minutes"));
    if (rings) {
      const time_t at = snoozeUntil / 1000;
      tm s;
      localtime_r(&at, &s);
      tell(1, s.tm_hour, s.tm_min);
    }
  });
  lv_obj_set_size(sn, (os::W() - 32 - 12) / 2, 56);
  lv_obj_t* off = button(row, N_("Turn off"), 0xff9500, 0x1b1408, [](lv_event_t*) {
    snoozeUntil = 0;
    stopRing();
    if (rings) tell(2);
    rings = 0;
  });
  lv_obj_set_size(off, (os::W() - 32 - 12) / 2, 56);
  if (isRinging) {
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
    setText(ringLabel, snoozeLabel.length() ? snoozeLabel.c_str() : N_("Alarm"));
  }
}

void tick() {
  if (isRinging) {
    if (millis() - lastBeep > 1600 && !(rings && hook && hook->talking && hook->talking())) {
      lastBeep = millis();
      sound::alarmBeep();
    }
    if (millis() - ringStarted > 5 * 60000) {  // nobody there: stop after 5 minutes
      stopRing();
      rings = 0;
      os::post("clock", N_("The alarm stopped by itself after 5 minutes"), "");
    }
  }
  // Timer.
  if (tOn && !tPaused && (int32_t)(millis() - timerEnd) >= 0) {
    tOn = false;
    trace::add("timer done");
    rings = 0;
    startRing(T_("Time's up"));
  }
  // Alarms, once a minute.
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();
  const time_t now = time(nullptr);
  if (now < 1700000000) return;  // the clock is not set yet
  tm l;
  localtime_r(&now, &l);
  const int nowMin = l.tm_yday * 1440 + l.tm_hour * 60 + l.tm_min;
  if (snoozeUntil > 0 && (int64_t)now * 1000 >= snoozeUntil) {
    snoozeUntil = 0;
    startRing(snoozeLabel);
    if (rings) rings++, tell(0);
    return;
  }
  if (nowMin == lastFiredMin) return;
  auto all = list();
  bool changed = false;
  for (Alarm& a : all) {
    if (!a.on || a.h != l.tm_hour || a.m != l.tm_min || !dueToday(a, l.tm_wday)) continue;
    lastFiredMin = nowMin;
    snoozeLabel = a.label.length() ? a.label : String(T_("Alarm"));
    startRing(snoozeLabel);
    rings = 1;
    tell(0);
    if (!a.days.length()) a.on = false, changed = true;  // one-off alarms turn off
    break;
  }
  if (changed) save(all);
}

void startTimer(int seconds) {
  timerTot = seconds;
  timerEnd = millis() + seconds * 1000UL;
  tOn = true;
  tPaused = false;
}
void pauseTimer() {
  if (!tOn || tPaused) return;
  timerPausedLeft = timerEnd - millis();
  tPaused = true;
}
void resumeTimer() {
  if (!tOn || !tPaused) return;
  timerEnd = millis() + timerPausedLeft;
  tPaused = false;
}
void cancelTimer() { tOn = tPaused = false; }
bool timerOn() { return tOn; }
bool timerPaused() { return tPaused; }
int timerLeft() {
  if (!tOn) return 0;
  const int32_t ms = tPaused ? (int32_t)timerPausedLeft : (int32_t)(timerEnd - millis());
  return ms > 0 ? (ms + 999) / 1000 : 0;
}
int timerTotal() { return timerTot; }
bool ringing() { return isRinging; }
void setHook(const Hook* h) { hook = h; }

}  // namespace alarms
