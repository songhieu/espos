// The dog assistant (assist.h): the chosen dog's assistant lines and number kit, the moments that call it.
#pragma GCC optimize("Os")
#include "assist.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include <ctime>

#include "alarm.h"
#include "data.h"
#include "desk.h"
#include "dogs.h"
#include "os.h"
#include "packs.h"
#include "pet.h"
#include "text.h"
#include "trace.h"
#include "uikit.h"
#include "view.h"

namespace pet {
bool quietNow();                                                      // pet.cpp
void showSpeech(const assist::Speech& s, const char* clip, bool wake);  // summon.cpp

namespace assist {
namespace {

using namespace kit;

pk::Pba ap;   // the dog's assist.pba
String apKey;  // whose
uint32_t apTried = 0;

Prefs cached;
uint32_t prefsVer = 0;

// What was said when (millis); the day's flags.
uint32_t lastPop = 0, lastWater = 0, lastMove = 0, lastEyes = 0, lastRain = 0, lastAlarmSay = 0;
uint32_t lastTouchAt = 0, presentSince = 0;
uint32_t greetDay = 0, weatherDay = 0, mealDay[2] = {0, 0};
int greetPart = -1;
int ringNow = 0;  // the alarm ringing now: 1st, 2nd, 3rd... time (0: none)
bool loaded = false;

uint32_t today(const tm& t) { return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday; }
long dayNum(uint32_t ymd) {  // days since 1970 of a yyyymmdd
  tm g = {};
  g.tm_year = ymd / 10000 - 1900, g.tm_mon = ymd / 100 % 100 - 1, g.tm_mday = ymd % 100, g.tm_hour = 12;
  return (long)(mktime(&g) / 86400);
}
bool clock(tm& t) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &t);
  return true;
}

bool load() {
  const char* k = pk::dogKey();
  if (apKey == k && ap.ok()) return true;
  if (apKey == k && apTried == pkg::version()) return false;
  apKey = k, apTried = pkg::version();
  ap.free();
  if (!*k) return false;
  const bool ok = ap.load(dogs::pkgId(k).c_str(), "assist.pba");
  trace::add("assist: %s's file %s (%d clips)", k, ok ? "in" : "missing", ap.count());
  return ok;
}

void loadDays() {
  if (loaded) return;
  loaded = true;
  Preferences p;
  p.begin("pet", true);
  greetDay = (uint32_t)p.getInt("agd", 0), weatherDay = (uint32_t)p.getInt("awd", 0);
  p.end();
}
void saveDays() {
  Preferences p;
  p.begin("pet", false);
  p.putInt("agd", (int)greetDay), p.putInt("awd", (int)weatherDay);
  p.end();
}

// A kit clip's word as the page shows it: its text without the final dot, in lower case (the words start with
// an ASCII capital or Đ).
String word(const char* name) {
  const pba_clip_t* c = ap.find(name);
  if (!c) return "";
  String s = ap.text(c);
  while (s.endsWith(".")) s.remove(s.length() - 1);
  if (s.length() > 1 && (uint8_t)s[0] == 0xC4 && (uint8_t)s[1] == 0x90) s.setCharAt(1, (char)0x91);  // U+0110 -> U+0111 (capital D with stroke to small)
  else if (s.length()) s.setCharAt(0, tolower(s[0]));
  return s;
}

void kit(Speech& s, const char* name) {
  const pba_clip_t* c = ap.find(name);
  if (!c) return;
  if (s.items.size() < 12) s.items.push_back({&ap, c, 20});
  s.kit += String(s.kit.length() ? " " : "") + word(name);
}

const char* partOf(int h) {
  return h >= 4 && h <= 10 ? "p_sang" : h >= 11 && h <= 12 ? "p_trua" : h >= 13 && h <= 17 ? "p_chieu" : h >= 18 && h <= 21 ? "p_toi" : "p_dem";
}

void pop(Speech& s, bool wake = false) {
  if (s.items.empty()) return;
  if (s.kit.length()) s.kit += ".";
  lastPop = millis();
  showSpeech(s, clipFor(s.moment), wake);
}

bool popMoment(const char* moment, bool wake = false) {
  Speech s;
  if (!line(moment, s)) return false;
  pop(s, wake);
  return true;
}

// ---- the alarm's screen: the dog in a card under the time, its words beside it
lv_obj_t *faceCard = nullptr, *faceSaid = nullptr, *faceKit = nullptr;
View* faceView = nullptr;

void face(lv_obj_t* layer, const Speech& s) {
  if (!layer || os::landscape()) return;  // landscape: no room under the time (the voice only)
  if (!faceCard || lv_obj_get_parent(faceCard) != layer) {
    keep(&faceCard, box(layer, 0xfff3df, 22));
    lv_obj_set_size(faceCard, os::W() - 24, 128);
    lv_obj_align(faceCard, LV_ALIGN_TOP_MID, 0, 248);
    lv_obj_set_style_clip_corner(faceCard, true, 0);
    faceView = View::create(faceCard, 112, 128, 1, 0xfff3df, 24, 22);
    lv_obj_set_pos(faceView->obj(), 0, 0);
    lv_obj_add_event_cb(faceView->obj(), [](lv_event_t*) { faceView = nullptr; }, LV_EVENT_DELETE, nullptr);
    keep(&faceSaid, label(faceCard, "", 14, true, 0x2b2f3a));
    lv_obj_set_style_text_font(faceSaid, pk::font(1), 0);
    lv_obj_set_pos(faceSaid, 112, 12);
    keep(&faceKit, label(faceCard, "", 13, true, 0xe07b12));
    lv_obj_set_style_text_font(faceKit, pk::font(0), 0);
  }
  lv_obj_remove_flag(faceCard, LV_OBJ_FLAG_HIDDEN);
  const int w = os::W() - 24 - 112 - 12;
  // The dog's own words and the kit's, broken into lines to the column (Lexend, every letter; smaller when long).
  const int h1 = text::words(faceSaid, s.said, w, 3);
  const int h2 = s.kit.length() ? text::words(faceKit, s.kit, w, 1) : 0;
  const int hh = h1 + (h2 ? 4 + h2 : 0), ch = constrain(hh + 24, 128, 132);
  lv_obj_set_height(faceCard, ch);
  const int top = (ch - hh) / 2;  // the words in the middle of the card, beside the dog
  lv_obj_set_pos(faceSaid, 112, top);
  lv_obj_set_pos(faceKit, 112, top + h1 + 4);
  if (h2) lv_obj_remove_flag(faceKit, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(faceKit, LV_OBJ_FLAG_HIDDEN);
  if (faceView) faceView->play(clipFor(s.moment));
}

void sayRing(lv_obj_t* layer) {
  char m[12];
  snprintf(m, sizeof m, "alarm_%d", constrain(ringNow, 1, 3));
  Speech s;
  if (!line(m, s)) return;
  tm t;
  if (clock(t)) addTime(s, t.tm_hour, t.tm_min, false);
  if (s.kit.length()) s.kit += ".";
  face(layer, s);
  play(s);
  lastAlarmSay = millis();
}

// ---- greetings: the first touch in a new part of the day, or after three hours away; three days away: long_time
void greet(uint32_t gapMs, const Prefs& p) {
  tm t;
  if (!clock(t)) return;
  loadDays();
  const int h = t.tm_hour;
  const int part = h >= 4 && h <= 10 ? 0 : h <= 12 && h >= 11 ? 1 : h >= 13 && h <= 17 ? 2 : h >= 18 && h <= 21 ? 3 : h >= 22 ? 4 : 5;
  const uint32_t day = today(t);
  const bool fresh = day != greetDay || part != greetPart || gapMs > 3 * 3600 * 1000UL;
  if (!fresh) return;
  const bool night = part >= 4;
  if (desk::settingInt("s3.dnd", 0) || (!night && quietNow())) return;  // a late touch still gets "go to bed"
  static const char* const MOMENT[6] = {"hi_morning", "hi_noon", "hi_afternoon", "hi_evening", "good_night", "late_night"};
  Speech s;
  const long days = greetDay ? dayNum(day) - dayNum(greetDay) : 0;
  if (!line(days >= 3 ? "long_time" : MOMENT[part], s)) return;
  greetDay = day, greetPart = part;
  // The weather once a day with a daytime greeting: its line and the temperature after the greeting.
  const char* wm;
  int temp;
  if (p.weather && !night && weatherDay != day && weatherNow(wm, temp)) {
    Speech w;
    if (line(wm, w)) {
      s.items.back().gapMs = 600;
      for (const voice::Item& it : w.items) s.items.push_back(it);
      s.said += " " + w.said;
      if (strcmp(wm, "w_air")) addTemp(s, temp);
      weatherDay = day;
    }
  }
  saveDays();
  trace::add("assist: %s (away %lu min)", s.moment, (unsigned long)(gapMs / 60000));
  pop(s);
}

// ---- habits, while someone is at the device (touched in the last half hour) and it shows Home or the saver
void habits(const Prefs& p, uint32_t now) {
  tm t;
  if (!clock(t) || quietNow() || now - lastPop < 20 * 60000UL) return;
  const int hm = t.tm_hour * 60 + t.tm_min;
  const uint32_t day = today(t);
  if (p.meal) {
    static const int AT[2] = {11 * 60 + 30, 18 * 60 + 30};
    for (int k = 0; k < 2; k++)
      if (hm >= AT[k] && hm < AT[k] + 45 && mealDay[k] != day) {
        mealDay[k] = day;
        if (popMoment("h_meal", true)) return;
      }
  }
  const uint32_t here = now - presentSince;  // minutes at the device count for moving and the eyes
  auto due = [&](int every, uint32_t& last, uint32_t since) {
    return every > 0 && now - max(last, since) >= (uint32_t)every * 60000UL;
  };
  if (hm >= 8 * 60 && hm < 21 * 60 && due(p.water, lastWater, 0)) {
    lastWater = now;
    if (popMoment("h_water", true)) return;
  }
  if (here > 10 * 60000UL && due(p.move, lastMove, presentSince)) {
    lastMove = now;
    if (popMoment("h_move", true)) return;
  }
  if (here > 10 * 60000UL && due(p.eyes, lastEyes, presentSince)) {
    lastEyes = now;
    popMoment("h_eyes", true);
  }
}

}  // namespace

Prefs prefs() {
  if (prefsVer == desk::settingsVersion() && prefsVer) return cached;
  prefsVer = desk::settingsVersion();
  Prefs p;
  JsonDocument d;
  if (!deserializeJson(d, desk::setting("s3.assist", "")) && d.is<JsonObjectConst>()) {
    p.on = d["on"] | 1, p.greet = d["greet"] | 1, p.weather = d["weather"] | 1, p.alarm = d["alarm"] | 1;
    p.meal = d["meal"] | 1;
    p.water = constrain(d["water"] | 120, 0, 600), p.move = constrain(d["move"] | 90, 0, 600);
    p.eyes = constrain(d["eyes"] | 60, 0, 600);
    p.wake = constrain(d["wake"] | 0, 0, 2), p.wakeWord = constrain(d["wake_word"] | 0, 0, 1);
    p.wakeSens = constrain(d["wake_sens"] | 2, 1, 3);
    p.led = constrain(d["led"] | 2, 0, 2), p.ledBright = constrain(d["led_bright"] | 2, 1, 3);
    p.follow = (d["follow"] | 1) != 0;
    p.ack = constrain(d["ack"] | 2, 0, 2);
  }
  return cached = p;
}

void setPrefs(const Prefs& p) {
  JsonDocument d;
  d["on"] = p.on ? 1 : 0, d["greet"] = p.greet ? 1 : 0, d["weather"] = p.weather ? 1 : 0, d["alarm"] = p.alarm ? 1 : 0;
  d["water"] = p.water, d["move"] = p.move, d["eyes"] = p.eyes, d["meal"] = p.meal ? 1 : 0;
  if (p.wake || p.wakeWord || p.wakeSens != 2) {  // only when changed: a server from before them refuses the keys
    d["wake"] = p.wake, d["wake_word"] = p.wakeWord, d["wake_sens"] = p.wakeSens;
  }
  if (p.led != 2 || p.ledBright != 2) d["led"] = p.led, d["led_bright"] = p.ledBright;
  if (!p.follow) d["follow"] = 0;  // only when off (the default is on; an older server refuses the key)
  if (p.ack != 2) d["ack"] = p.ack;
  String s;
  serializeJson(d, s);
  desk::setSetting("s3.assist", s);
  cached = p;
}

bool ready() { return pk::ready() && load(); }

void release() {
  ap.free();
  apKey = "", apTried = 0;
}

bool line(const char* moment, Speech& s) {
  if (!ready()) return false;
  char on[20];
  snprintf(on, sizeof on, "@%s", moment);
  const pba_clip_t* l = voice::of(ap, on);
  if (!l) l = ap.find(moment);  // its only line was the one said last (of() skips that): say it again
  if (!l) return false;
  s.items.push_back({&ap, l, 260});
  s.said = ap.text(l);
  s.moment = moment;
  return true;
}

void addTime(Speech& s, int h, int m, bool alarm) {
  char n[8];
  kit(s, alarm ? "t_alarm" : "t_now");
  snprintf(n, sizeof n, "n_%d", h % 12 == 0 ? 12 : h % 12);
  kit(s, n);
  kit(s, "u_gio");
  if (m == 30) {
    kit(s, "u_ruoi");
  } else if (m == 0) {
    kit(s, "u_dung");
  } else {
    snprintf(n, sizeof n, "n_%d", m);
    kit(s, n);
    kit(s, "u_phut");
  }
  kit(s, partOf(h));
}

void addTemp(Speech& s, int degrees) {
  char n[8];
  kit(s, "t_out");
  snprintf(n, sizeof n, "n_%d", constrain(degrees, 0, 59));
  kit(s, n);
  kit(s, "u_do");
}

bool play(const Speech& s) {
  if (s.items.empty()) return false;
  return voice::say(s.items.data(), (int)s.items.size());
}

bool weatherNow(const char*& moment, int& temp) {
  if (!data::hasWeather()) return false;
  JsonObjectConst w = data::weather();
  temp = (int)lroundf(w["temp"] | 0.0f);
  const int aqi = w["aqi"] | 0;
  bool rain = false;
  const String icon = w["icon"] | "";
  rain |= icon == "rain" || icon == "storm";
  int k = 0;
  for (JsonObjectConst hr : w["hours"].as<JsonArrayConst>()) {  // the next two slots (about six hours)
    if (k++ >= 2) break;
    const String i = hr["icon"] | "";
    rain |= (hr["rain"] | 0) >= 60 || i == "rain" || i == "storm";
  }
  moment = aqi > 150 ? "w_air" : rain ? "w_rain" : temp >= 34 ? "w_hot" : temp <= 17 ? "w_cold" : "w_nice";
  return true;
}

const char* clipFor(const char* m) {
  static const char* const MAP[][2] = {
      {"hi_morning", "nudge"},     {"hi_noon", "happy"},         {"hi_afternoon", "look_up"}, {"hi_evening", "nudge"},
      {"good_night", "lie_down"},  {"late_night", "lie_down"},   {"long_time", "look_up"},    {"alarm_1", "alert"},
      {"alarm_2", "plead"},        {"alarm_3", "pounce"},        {"alarm_snooze", "lie_down"}, {"alarm_off", "high_five"},
      {"w_rain", "shake"},         {"w_hot", "lick"},            {"w_cold", "shake"},         {"w_nice", "sit_spin"},
      {"w_air", "sick"},           {"h_water", "lick"},          {"h_move", "sit_spin"},      {"h_eyes", "lie_down"},
      {"h_meal", "beg"},           {"time", "look_up"}};
  for (const auto& e : MAP)
    if (m && !strcmp(m, e[0])) return pk::clip(e[1]) ? e[1] : "happy";
  return "happy";
}

void tick() {
  const Prefs p = prefs();
  if (!p.on || !ready()) return;
  const uint32_t now = millis();
  // The alarm rings on: the dog says it again (its other line) every 45 s once it has finished.
  if (ringNow && !alarms::ringing()) ringNow = 0;
  if (ringNow && p.alarm && !voice::busy() && now - lastAlarmSay > 45000) sayRing(faceCard ? lv_obj_get_parent(faceCard) : nullptr);
  if (ringNow) return;
  // A touch: someone came to the device.
  const uint32_t touchAt = now - os::idleMs();
  if (touchAt > lastTouchAt + 1000) {
    const uint32_t gap = lastTouchAt ? touchAt - lastTouchAt : 0xFFFFFFFF;
    if (gap > 30 * 60000UL) presentSince = touchAt;  // back after half an hour: a new sitting
    lastTouchAt = touchAt;
    if (p.greet && os::atHome() && !os::saverOn() && !summoned() && now > 20000) greet(gap, p);
  }
  if (summoned() || voice::busy()) return;
  const bool present = lastTouchAt && os::idleMs() < 30 * 60000UL;
  if (!present || !(os::atHome() || os::saverOn())) return;
  // Rain coming: once in six hours.
  const char* wm;
  int temp;
  if (p.weather && now - lastRain > 6 * 3600000UL && now - lastPop > 20 * 60000UL && !quietNow() &&
      weatherNow(wm, temp) && !strcmp(wm, "w_rain")) {
    lastRain = now;
    Speech s;
    if (line("w_rain", s)) {
      addTemp(s, temp);
      pop(s, true);
      return;
    }
  }
  habits(p, now);
}

void alarmEvent(int ev, int ring, int h, int m, lv_obj_t* layer) {
  const Prefs p = prefs();
  if (!p.on || !p.alarm || !ready()) return;
  if (ev == 0) {
    ringNow = max(1, ring);
    sayRing(layer);
    return;
  }
  ringNow = 0;
  if (faceCard) lv_obj_add_flag(faceCard, LV_OBJ_FLAG_HIDDEN);
  Speech s;
  if (!line(ev == 1 ? "alarm_snooze" : "alarm_off", s)) return;
  if (ev == 1) addTime(s, h, m, true);  // "báo thức lúc sáu giờ ba mươi chín phút sáng"
  pop(s);
}

bool talking() { return voice::busy(); }

}  // namespace assist
}  // namespace pet
