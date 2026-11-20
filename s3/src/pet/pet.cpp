#pragma GCC optimize("Os")  // cold code (ticks of 0.5 s, dog switches, notifications): size first (s3-perf, 2026-11-20)
// PawBoop's entry points and its background work (pet.h).
#include "pet.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include <ctime>

#include "alarm.h"
#include "app.h"
#include "apps.h"
#include "assist.h"
#include "book.h"
#include "chooser.h"
#include "data.h"
#include "desk.h"
#include "dog.h"
#include "dogs.h"
#include "game.h"
#include "i18n.h"
#include "notif.h"
#include "os.h"
#include "packs.h"
#include "trace.h"
#include "voice.h"

namespace pet {

void summonTick();   // summon.cpp
void widgetsTick();  // widget.cpp
void widgetsReload();

namespace {

uint32_t lastTick = 0, lastWidgets = 0, seenSettings = 0;
int doneTodos = -1;
uint32_t notedAt[3] = {0, 0, 0};  // hungry, sleepy, bored: millis() of the last notification
bool noted[3] = {false, false, false};

int minutesOf(const String& hm) { return hm.substring(0, 2).toInt() * 60 + hm.substring(3, 5).toInt(); }

// The dog needs something: one notification per mood, at most every 3 hours, with its own line as the text.
void notify() {
  if (millis() < 60000) return;  // not right after power-on
  const char* mood = game::moodRole();
  static const char* MOODS[3] = {"hungry", "sleepy", "bored"};
  static const char* TITLE[3] = {N_("{0} is hungry"), N_("{0} is sleepy"), N_("{0} is bored, wants to play")};
  static const char* LINE[3] = {"sulk", "lie_down", "plead"};
  for (int k = 0; k < 3; k++) {
    if (strcmp(mood, MOODS[k])) continue;
    if (noted[k] && millis() - notedAt[k] < 3 * 3600 * 1000UL) return;  // told its need not long ago
    // Quiet hours and Do Not Disturb are notif's job: no banner, no sound, but the need waits in the Center.
    if (!game::s().notify || app::active() || summoned()) return;
    noted[k] = true, notedAt[k] = millis();
    const pba_clip_t* l = voice::lineFor(LINE[k]);
    notif::Opts o;
    o.arg = MOODS[k];  // tapped: the game (pet::open treats a mood as "open the game")
    o.tag = MOODS[k];  // a new "hungry" replaces the old one
    notif::post("pet", i18n::fmt(TITLE[k], dogs::name()).c_str(), pk::shown(l).c_str(), o);
    trace::add("pet: told the user (%s)", mood);
  }
}

// A reminder checked off on the device: the dog celebrates (only when someone is at the device right now).
void watchTodos() {
  if (desk::settingsVersion() == seenSettings) return;
  seenSettings = desk::settingsVersion();
  JsonDocument doc;
  int done = 0;
  if (data::loadJson("s3.todo", doc))
    for (JsonObjectConst t : doc.as<JsonArrayConst>()) done += (t["done"] | false) ? 1 : 0;
  if (doneTodos >= 0 && done > doneTodos && os::idleMs() < 5000 && !os::saverOn() && !quietNow()) celebrate("remind");
  doneTodos = done;
}

// The dog to use changed (the web's s3.pet_dog, a dog installed or removed, its card out): its packs are loaded
// once nothing shows the old one (not while the game, its book or the pop-up is open).
uint32_t dogSeen = 0;
void watchDog() {
  if (dogSeen == dogs::version()) return;
  const dogs::Info* d = dogs::chosen();
  // Nothing loaded yet: pk::open() takes the chosen dog by itself. The dog loaded is the one: nothing to do.
  if (!*pk::dogKey() || (d && !strcmp(d->key, pk::dogKey()))) {
    dogSeen = dogs::version();
    return;
  }
  if (app::isOpen() || summoned() || book::isOpen()) return;
  dogSeen = dogs::version();
  switchDog(d ? d->key : "", false);
}

bool reloading = false;
bool reopen = false;

// The game again with the new dog, once the old game screen is gone (the OS deletes closed screens a little later).
void reopenGame(lv_timer_t* t) {
  static int tries = 0;
  if (app::isOpen() && ++tries < 40) return;
  tries = 0;
  lv_timer_delete(t);
  if (!app::isOpen()) apps::open("pet");
}

void finishSwitch(void*) {
  voice::release();
  dog::release();
  assist::release();
  pk::reload();
  pk::open();
  widgetsReload();
  reloading = false;
  trace::add("pet: now %s", pk::dogKey());
  if (reopen) lv_timer_create(reopenGame, 50, nullptr);
}

}  // namespace

bool quietNow() {  // Do Not Disturb, or the quiet hours of the dashboard settings (as the OS's saver)
  if (desk::settingInt("s3.dnd", 0)) return true;
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  tm t;
  localtime_r(&now, &t);
  const int m = t.tm_hour * 60 + t.tm_min;
  const int from = minutesOf(desk::setting("dash.quiet_from", "23:00")), to = minutesOf(desk::setting("dash.quiet_to", "06:00"));
  return from <= to ? m >= from && m < to : m >= from || m < to;
}

void switchDog(const char* key, bool open) {
  if (key && *key) dogs::choose(key);
  dismiss();
  app::stop();  // the game draws no more pictures of the old dog
  if (app::isOpen() || book::isOpen() || !os::atHome()) os::closeApps();
  reopen = open;
  if (reloading) return;
  reloading = true;
  lv_async_call(finishSwitch, nullptr);  // after the closed screens are gone
}

// tick() also runs from its own LVGL timer once any part of PawBoop was used, so the needs and the widgets
// move even before the OS loop calls it (both together are fine: tick() ignores calls closer than 400 ms).
void ensureTicker() {
  static lv_timer_t* t = nullptr;
  if (!t) t = lv_timer_create([](lv_timer_t*) { tick(); }, 500, nullptr);
}

bool ready() { return pk::ready(); }
const char* name() { return dogs::name(); }

void open(const char* arg) {
  ensureTicker();
  if (arg && !strcmp(arg, "summon")) return summon("app");
  if (arg && !strcmp(arg, "book")) return book::open();
  if (arg && !strcmp(arg, "dogs")) return chooser::open();
  if (arg && !strncmp(arg, "clip:", 5)) return app::devClip(arg + 5);  // tests (the two beats)
  if (arg && !strncmp(arg, "say:", 4)) return app::devSay(arg + 4);     // tests (a bubble against the page's)
  app::open();
  if (pk::ready() && !game::hasMoment(0, 0)) game::addMoment(0, 0, "nudge", 12);
}

void celebrate(const char* what) { summon(what ? what : "remind"); }

void tick() {
  const uint32_t now = millis();
  if (lastTick && now - lastTick < 400) return;  // safe to call often
  const uint32_t dt = lastTick ? now - lastTick : 0;
  lastTick = now;
  static const alarms::Hook HOOK = {assist::alarmEvent, assist::talking};  // the dog speaks at the alarm
  static bool hooked = false;
  if (!hooked) alarms::setHook(&HOOK), hooked = true;
  if (reloading) return;
  watchDog();
  if (!pk::ready()) return;
  if (!app::isOpen()) game::advance(dt, true);  // the game screen's own clock does it while it is open
  notify();
  watchTodos();
  assist::tick();
  summonTick();
  if (now - lastWidgets > 30000) {
    lastWidgets = now;
    widgetsTick();
  }
}

}  // namespace pet

namespace app_pet {
void open(const char* arg) { pet::open(arg); }
}  // namespace app_pet
