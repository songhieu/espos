#include "app.h"

#include <Arduino.h>

#include <string>

#include "../lic.h"
#include "../trace.h"
#include "config.h"
#include "ear.h"
#include "gfx.h"
#include "link.h"
#include "logic.h"
#include "net.h"
#include "oled.h"
#include "portal.h"
#include "store.h"
#include "spk.h"
#include "talk.h"
#include "ui.h"
#include "usb.h"

namespace bot {
namespace app {
namespace {

uint32_t appliedVer = 0;
logic::Night night;
std::vector<logic::Alarm> alarms;
int bright = 60;
bool test = false;  // the console holds a screen

// alarms
int ringing = -1;
uint32_t ringAt = 0, lastMinute = UINT32_MAX;
constexpr uint32_t RING_MS = 60000;

// the buttons
constexpr uint32_t HOLD_MS = 5000;
uint32_t noteUntil = 0;  // a short note (the volume) holds the screen until then

uint32_t idleSince = 0;  // the last turn or press (an update waits for a quiet minute)

void applySettings() {
  if (appliedVer == store::version()) return;
  appliedVer = store::version();
  ui::setLook(logic::lookOf(store::setting("bot.style")));
  ui::setLang(store::setting("sys.lang") == "en" ? ui::Lang::EN : ui::Lang::VI);
  ui::setScreens(logic::screensOf(store::setting("bot.screens")));
  bright = logic::intOf(store::setting("bot.bright"), 60, 5, 100);
  night = logic::nightOf(store::setting("bot.night"));
  alarms = logic::alarmsOf(store::setting("bot.alarms"));
  ear::setWake(logic::intOf(store::setting("bot.wake"), 2, 0, 3));
  spk::volume(logic::intOf(store::setting("bot.volume"), 60, 0, 100));
  std::string ids;
  for (const ui::Screen& x : logic::screensOf(store::setting("bot.screens"))) ids += std::string(ids.empty() ? "" : ",") + ui::screenId(x.id);
  trace::add("settings: look %d, bright %d, wake %s, %u alarms, tz %s, screens %s", ui::look(), bright,
             store::setting("bot.wake").c_str(), (unsigned)alarms.size(), store::setting("sys.tz_min").c_str(), ids.c_str());
}

ui::Now now() {
  ui::Now n = logic::localTime(net::nowMs() / 1000, logic::intOf(store::setting("sys.tz_min"), 420, -720, 840));
  n.lunar = link::lunar();
  return n;
}

void checkAlarms(const ui::Now& n) {
  if (!n.valid) return;
  const uint32_t minute = (uint32_t)(n.hour * 60 + n.min) + (uint32_t)n.day * 1440;
  if (minute == lastMinute) return;
  lastMinute = minute;
  const int i = logic::due(alarms, n);
  if (i < 0) return;
  const logic::Alarm a = alarms[i];
  trace::add("alarm %02d:%02d \"%s\"", a.h, a.m, a.label.c_str());
  if (!a.days) {  // a one-off: done (the web sees it off after the next sync)
    alarms[i].on = false;
    store::set("bot.alarms", logic::alarmsJson(alarms), true);
    appliedVer = store::version();
  }
  ringing = i, ringAt = millis();
  ui::alarm(a.label, a.h, a.m, false);
}

// What the screen should show when nothing else holds it.
void pickScreen(const ui::Now& n) {
  if (link::updating() || link::updatePct() >= 0) {
    const int p = link::updatePct();
    return ui::note(ui::str(ui::S_UPDATING), link::offerVersion(), p < 0 ? 0 : p);
  }
  const bool both = !store::langChosen();
  // the setup page's join, as the phone sees it
  if (net::portal()) {
    const portal::Join j = net::joinState();
    if (j == portal::Join::Joining) return ui::note(ui::str(ui::S_CONNECTING), net::joinName());
    if (j == portal::Join::Failed && net::joinFailedAgo() < 12000) {
      const portal::Why w = net::joinWhy();
      return ui::note(ui::str(w == portal::WHY_PASSWORD ? ui::S_WRONG_PASSWORD
                              : w == portal::WHY_NOT_FOUND ? ui::S_NO_NETWORK : ui::S_JOIN_FAILED),
                      ui::str(ui::S_TRY_ON_PHONE));
    }
    if (j == portal::Join::Ok && net::online()) {
      static std::string shownFor;
      static uint32_t okSince = 0;
      if (shownFor != net::joinName()) shownFor = net::joinName(), okSince = millis();
      if (millis() - okSince < 3000) return ui::note(ui::str(ui::S_WIFI_OK), net::joinName());
    }
    if (!net::online()) {
      // why the setup WiFi is up (a saved network lost, or refusing its password): said 4 s of every 12
      const std::string bad = net::badPassword();
      if ((!bad.empty() || !store::wifi().empty()) && (millis() / 4000) % 3 == 0) {
        if (!bad.empty())
          return ui::note(ui::str(ui::S_WRONG_PASSWORD), bad + " · " + ui::str(ui::S_FIX_ON_PHONE));
        return ui::note(ui::str(ui::S_LOST_WIFI), net::lostName() + " · " + ui::str(ui::S_NEW_WIFI_ON_PHONE));
      }
      return ui::setupAp(net::apName(), store::apPass(), both);
    }
  }
  // offline more than 20 s (the setup WiFi not up yet): saying so, and how to set a new one
  if (!net::online() && !store::wifi().empty() && net::offlineMs() > 20000)
    return ui::note(ui::str(ui::S_LOST_WIFI), net::lostName() + " · " + ui::str(ui::S_RETRYING));
  if (link::state() == link::State::Claim && !store::claimCode().empty())
    return ui::claim(store::claimCode(), store::claimUrl(), both);
  if (link::state() == link::State::Pairing) {
    if (link::pairCode().empty()) return ui::note(ui::str(ui::S_GETTING_CODE), "espos.dev");
    return ui::claim(link::pairCode(), link::pairUrl(), both);
  }
#ifndef BOT_PAIR_PAUSED  // (a test build without pairing: the standby screens, offline; a press says pairing is coming)
  if (link::state() == link::State::None && net::online()) return ui::note("espos.dev", ui::str(ui::S_SET_UP_USB));
#endif
  if (n.valid && logic::inNight(night, n.hour * 60 + n.min)) {
    if (ui::state() != ui::State::NIGHT) ui::night();
    return;
  }
  ui::standby();
}

void brightness() {
  static int shown = -1;
  const int want = ui::state() == ui::State::NIGHT ? 5 : bright;
  if (want != shown) oled::brightness(want), shown = want;
}

// The buttons (active low). BOOT and Vol+ (the round button on top of the head, "xoa đầu"): a press talks, held
// 5 s forgets the WiFi (a countdown from 2 s); Vol+ twice quickly: volume up by 10. Vol-: volume down by 10.
struct Btn {
  int pin;
  bool down = false, longDone = false;
  uint32_t downAt = 0, upAt = 0;
  bool waitSecond = false;  // Vol+: a press was let go, a second one may come (a double press)
};
Btn bBoot{pins::BOOT}, bUp{pins::VOL_UP}, bDown{pins::VOL_DOWN};
constexpr uint32_t DOUBLE_MS = 350;

void volumeBy(int d) {
  const int v = logic::intOf(store::setting("bot.volume"), 60, 0, 100);
  const int n = v + d < 0 ? 0 : v + d > 100 ? 100 : v + d;
  store::set("bot.volume", std::to_string(n), true);  // goes up with the next sync
  spk::volume(n);
  if (n > 0) spk::cue(cues::SENT);  // what it sounds like now
  ui::note(ui::str(ui::S_VOLUME), std::to_string(n) + "%", n);
  test = true;
  noteUntil = millis() + 1500;
  trace::add("volume %d", n);
}

// A talk button (BOOT or Vol+): true when a short press ended (the caller decides what a press does).
bool talkButton(Btn& b, bool d, uint32_t t) {
  if (d && !b.down) b.down = true, b.downAt = t, b.longDone = false;
  if (d && !b.longDone) {
    const uint32_t held = t - b.downAt;
    if (held >= HOLD_MS) {
      b.longDone = true;
      button(true);
    } else if (held >= 2000) {  // a countdown from 2 s: "keep holding to forget the WiFi 3, 2, 1"
      ui::note(ui::str(ui::S_HOLD_RESET), std::to_string((HOLD_MS - held + 999) / 1000));
      test = true;
    }
  }
  if (!d && b.down) {
    b.down = false;
    if (!b.longDone) {
      if (t - b.downAt >= 2000) test = false;  // let go during the countdown: nothing
      else if (t - b.downAt >= 30) return true;
    }
  }
  return false;
}

void readButton() {
  const uint32_t t = millis();
  if (talkButton(bBoot, digitalRead(pins::BOOT) == LOW, t)) button(false);
  // Vol+: a press waits DOUBLE_MS for a second one (volume up) before it talks
  if (talkButton(bUp, digitalRead(pins::VOL_UP) == LOW, t)) {
    if (bUp.waitSecond && t - bUp.upAt < DOUBLE_MS) bUp.waitSecond = false, volumeBy(10);
    else bUp.waitSecond = true, bUp.upAt = t;
  }
  if (bUp.waitSecond && !bUp.down && t - bUp.upAt >= DOUBLE_MS) bUp.waitSecond = false, button(false);
  const bool dn = digitalRead(pins::VOL_DOWN) == LOW;
  if (dn && !bDown.down) bDown.down = true, bDown.downAt = t;
  if (!dn && bDown.down) {
    bDown.down = false;
    if (t - bDown.downAt >= 30) volumeBy(-10);
  }
  if (noteUntil && (int32_t)(t - noteUntil) >= 0) noteUntil = 0, test = false;
}

}  // namespace

void button(bool longHold) {
  idleSince = millis();
  if (longHold) {
    trace::add("button: held, forgetting the WiFi");
    test = false;
    store::forgetWifi("");
    ui::note(ui::str(ui::S_WIFI_FORGOTTEN));
    net::startPortal();
    return;
  }
  if (ringing >= 0) {  // an alarm: off
    ringing = -1;
    spk::stop();
    ui::standby();
    return;
  }
  if (test) {
    test = false;
    return;
  }
  if (!net::online() && !net::portal() && !talk::active()) {  // offline: a press brings the setup QR at once
    trace::add("button: offline, the setup WiFi opens");
    net::startPortal();
    return;
  }
  if (talk::active()) return talk::press();
  talk::start(false);
}

void uiTest(const char* w) {
  test = true;
  const std::string s = w;
  const std::string arg = s.find(' ') == std::string::npos ? "" : s.substr(s.find(' ') + 1);
  if (s == "auto") test = false, ui::standby();
  else if (s == "listen") ui::listen();
  else if (s == "think") ui::think();
  else if (s == "happy") ui::happy();
  else if (s == "sad") ui::notHeard();
  else if (!s.compare(0, 5, "talk ")) ui::talk(arg);
  else if (s == "claim") ui::claim(store::claimCode().empty() ? "K7QM2XPA" : store::claimCode(), store::claimUrl(), false);
  else if (s == "ap") ui::setupAp(net::apName(), store::apPass(), !store::langChosen());
  else if (!s.compare(0, 5, "note ")) ui::note(arg);
  else if (s == "alarm") ui::alarm("", 6, 30, false);
  else if (s == "night") ui::night();
  else if (!s.compare(0, 7, "screen ")) {  // one standby screen: clock, date, weather, dog
    ui::setScreens(logic::screensOf("[{\"id\":\"" + arg + "\",\"s\":600}]"));
    ui::night();
    ui::standby();
  } else if (!s.compare(0, 5, "look ")) ui::setLook((uint8_t)atoi(arg.c_str()));
  else if (!s.compare(0, 5, "lang ")) ui::setLang(arg == "en" ? ui::Lang::EN : ui::Lang::VI);
}

void begin() {
  Serial.begin(115200);
  pinMode(pins::BOOT, INPUT_PULLUP);
  pinMode(pins::VOL_UP, INPUT_PULLUP);
  pinMode(pins::VOL_DOWN, INPUT_PULLUP);
  trace::begin();
  store::begin();
  if (!oled::begin()) trace::add("oled: no panel");
  applySettings();
  ui::tick(millis());
  oled::pushAll(gfx::fb);
  net::begin();
  link::begin();
  if (!spk::begin()) trace::add("spk: no I2S out");
  if (!ear::begin()) trace::add("ear: no mic");
  appliedVer = 0;
  applySettings();
  trace::add("EspOS Bot %s, heap %u KB, psram %u KB", FW_VERSION, (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getFreePsram() / 1024));
}

void loop() {
  usb::loop();
  net::loop();
  link::loop();
  applySettings();
  readButton();
  const ui::Now n = now();
  ui::setNow(n);
  talk::setQuiet(n.valid && logic::inNight(night, n.hour * 60 + n.min));  // night: the words only, no sound
  ui::Weather w;
  if (link::weather(w)) ui::setWeather(w);
  static uint32_t infoVer = UINT32_MAX;
  if (infoVer != link::dashVersion()) infoVer = link::dashVersion(), ui::setInfo(link::dashInfo());
  checkAlarms(n);
  if (ringing >= 0 && millis() - ringAt > RING_MS) ringing = -1, ui::standby();
  static uint32_t toneAt = 0;
  if (ringing >= 0 && millis() - toneAt > 1000 && !spk::playing()) toneAt = millis(), spk::alarmTone();
  if (talk::active()) idleSince = millis();
  // Lost WiFi (2026-11-20): the setup WiFi (QR + password) opens by itself when no network is saved, a saved one
  // refuses its password, the bot has been offline 2 minutes, or (since boot, never online) two full rounds of the
  // saved networks failed. Never while online or during a turn; the saved ones are still tried in the background.
  static bool everOnline = false;
  if (net::online()) everOnline = true;
  if (!net::online() && !net::portal() && !talk::active() &&
      (store::wifi().empty() || !net::badPassword().empty() || net::offlineMs() > 120000 ||
       (!everOnline && net::failedRounds() >= 2))) {
    trace::add("wifi: offline %lu s: the setup WiFi opens", (unsigned long)(net::offlineMs() / 1000));
    net::startPortal();
  }
  ui::setOffline(!net::online() && !store::wifi().empty());
  if (!test && ringing < 0) {
    talk::loop();
    const bool call = ear::takeWake();
    if (call && !talk::busy() && link::state() == link::State::Linked && net::online()) {
      talk::start(true);  // over standby, or over a reply already shown
    } else if (!talk::active()) {
      static uint32_t greetUntil = 0;
      if (link::justPaired()) {  // the web confirmed the code: the dog says hello
        ui::talk(ui::str(ui::S_PAIRED), ui::Mood::HAPPY);
        greetUntil = millis() + 7000;
      }
      if ((int32_t)(millis() - greetUntil) < 0) {
      } else {
        pickScreen(n);
        // An offered update (bot.ota stable/beta) goes in at night, after a quiet minute (the web says "Tự cài bản
        // mới khi máy rảnh, ban đêm"); 02:00-05:00 when night hours are off. One asked for on the web goes in at
        // the next quiet 10 s, day or night.
        const bool nightNow = n.valid && (night.on ? logic::inNight(night, n.hour * 60 + n.min) : n.hour >= 2 && n.hour < 5);
        if (!link::offerVersion().empty() && !link::updating() &&
            ((link::offerNow() && millis() - idleSince > 10000) || (nightNow && millis() - idleSince > 60000)))
          link::install();
      }
    }
  } else {
    ear::takeWake();  // a call during an alarm or a test: dropped
  }
  brightness();
  static uint32_t drawnAt = 0;
  if (millis() - drawnAt >= 20) {
    drawnAt = millis();
    if (ui::tick(drawnAt)) oled::push(gfx::fb);
  }
  delay(2);
}

}  // namespace app
}  // namespace bot
