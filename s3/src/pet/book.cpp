#pragma GCC optimize("Os")  // built once per open: size over speed (s3-perf, the .bin budget)
// The dog's book: who it is now (level, XP, mood, needs), the days in a row it was cared for, the album of
// moments (its firsts, each shown with the picture of the clip it was playing), the game's switches, the dog
// assistant's settings (s3.assist) and the way to another dog.
#include "book.h"

#include <Arduino.h>

#include <ctime>
#include <vector>

#include "assist.h"
#include "chooser.h"
#include "dogs.h"
#include "fonts.h"
#include "i18n.h"
#include "game.h"
#include "os.h"
#include "packs.h"
#include "pet.h"
#include "talk.h"
#include "../pkg/store.h"
#include "../sound.h"
#include "../wake/wake.h"
#include "../led.h"
#include "../hal/board.h"
#include <Preferences.h>
#include "uikit.h"
#include "view.h"
#include "sfx.h"
#include "voice.h"

namespace pet {
namespace book {
namespace {

using namespace kit;

// ---------------------------------------------------------------- calling the dog with "Gâu" (src/wake/wake.h)
void setWake(int mode) {
  assist::Prefs q = assist::prefs();
  q.wake = mode;
  assist::setPrefs(q);
}

// The first time it is turned on: what listening means, then on or not.
void askWake(int mode) {
  lv_obj_t* body;
  lv_obj_t* scr = os::screen(N_("Call with \"Gâu\""), &body);  // i18n: a screen title; "Gâu" is the dog's call, as is
  lv_obj_t* c = cell(body);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 8, 0);
  const String say = i18n::fmt(N_("Say \"Gâu gâu\" and what you need, and {0} answers, like a phone's assistant."), dogs::name());
  lv_obj_t* l = label(c, "", 15, true, pal().fg);
  wrapText(l, say.c_str(), os::W() - 56, 15, true);
  static const char* const PARAS[] = {
      N_("To hear it, the microphone listens all the time while this is on. What it hears stays on the device: it is "
         "checked here for \"Gâu\" and forgotten after 2 seconds."),
      N_("Only after \"Gâu\" does the device send what you said next to the server, so the dog can answer. No sound is "
         "kept there."),
      N_("An orange dot at the top of the screen shows when the microphone is on. The microphone tile in the Control "
         "Center stops it at any time."),
  };
  for (const char* t : PARAS) {
    lv_obj_t* p = label(c, "", 14, false, pal().dim);
    wrapText(p, T_(t), os::W() - 56, 14);
  }
  lv_obj_t* on = button(body, N_("Turn on"), 0xff9f0a, 0x3a2200, [](lv_event_t* e) {
    Preferences pr;
    pr.begin("wake", false);
    pr.putInt("ok", 1);
    pr.end();
    setWake((int)(intptr_t)lv_event_get_user_data(e));
    os::back();
  }, (void*)(intptr_t)mode);
  lv_obj_set_width(on, lv_pct(100));
  lv_obj_t* no = button(body, N_("Not now"), pal().chip, pal().fg, [](lv_event_t*) { os::back(); });
  lv_obj_set_width(no, lv_pct(100));
  os::open(scr);
}

// The calls row: several chips on at once (s3.assist "calls", bits in chip order; at least one stays on).
lv_obj_t* callBar = nullptr;
void paintCalls(uint8_t m) {
  if (!callBar) return;
  for (uint32_t i = 0; i < lv_obj_get_child_count(callBar); i++) {
    lv_obj_t* c = lv_obj_get_child(callBar, i);
    const bool on = (m >> i) & 1;
    lv_obj_set_style_bg_color(c, C(on ? pal().chipOn : pal().chip), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(c, 0), C(on ? pal().chipOnFg : pal().chipFg), 0);
  }
}

}  // namespace

lv_obj_t* callChips(lv_obj_t* parent) {
  static const char* const CALLS[] = {"Gâu gâu", "Ki ki", "Lu lu", "Cún ơi", "Milu"};  // what you say: not translated
  callBar = chips(parent, CALLS, 5, -1, [](int i) {
    assist::Prefs q = assist::prefs();
    const uint8_t m = q.calls ^ (1 << i);
    if (m) {
      q.calls = m;
      assist::setPrefs(q);
    }
    paintCalls(q.calls);
  });
  lv_obj_add_event_cb(callBar, [](lv_event_t*) { callBar = nullptr; }, LV_EVENT_DELETE, nullptr);
  paintCalls(assist::prefs().calls);
  return callBar;
}

namespace {

void wakeRows(lv_obj_t* body) {
  caption(body, N_("CALL WITH \"GÂU\""));  // i18n: a heading in upper case; "GÂU" is the dog's call, as is
  lv_obj_t* c = cell(body);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 6, 0);
  const assist::Prefs p = assist::prefs();
  auto title = [&](const char* t) {
    lv_obj_t* l = label(c, "", 14, true, pal().fg);
    fitText(l, t, os::W() - 48, 20, 14, 12, true, 0);
  };
  title(N_("Listen for \"Gâu\""));
  static const char* const MODES[] = {N_("Off"), N_("On the charger"), N_("Always")};  // i18n: chips, short
  chips(c, MODES, 3, p.wake, [](int i) {
    Preferences pr;
    pr.begin("wake", true);
    const bool ok = pr.getInt("ok", 0);
    pr.end();
    if (i && !ok) return askWake(i);
    setWake(i);
  });
  // i18n: which calls wake the dog (several can be on); the calls themselves are Vietnamese words, never translated
  title(N_("It answers to"));
  callChips(c);
  title(N_("Call it with"));
  static const char* const WORDS[] = {N_("Gâu gâu"), N_("\"Gâu\" too")};  // i18n: "Gâu gâu" is the call itself: keep it as is
  chips(c, WORDS, 2, p.wakeWord, [](int i) {
    assist::Prefs q = assist::prefs();
    q.wakeWord = i;
    assist::setPrefs(q);
  });
  title(N_("How easily it wakes"));
  static const char* const SENS[] = {N_("Less often"), N_("Normal"), N_("More often")};  // i18n: chips, short
  chips(c, SENS, 3, p.wakeSens - 1, [](int i) {
    assist::Prefs q = assist::prefs();
    q.wakeSens = i + 1;
    assist::setPrefs(q);
  });
  title(N_("When it hears you"));  // i18n: what the dog does at once when it hears "gâu gâu" (so you know from afar)
  // i18n: chips: nothing, a short beep, a word in the dog's own voice ("Dạ?")
  static const char* const ACK[] = {NC_("ack", "Nothing"), NC_("ack", "A beep"), NC_("ack", "Its voice")};
  chips(c, ACK, 3, p.ack, [](int i) {
    assist::Prefs q = assist::prefs();
    q.ack = i;
    assist::setPrefs(q);
  });
  hint(body, N_("\"Gâu gâu\" alone is the safest. \"Gâu\" too wakes it more easily, also by mistake."));
  // i18n: under the calls; "Gâu gâu" is a call itself (keep it as it is); the listener is the "Gâu" wake-word package
  hint(body, N_("Every call you turn on is one more way to wake it by mistake. The calls after \"Gâu gâu\" need the "
                "newest listener."));
  // After an answer: listen on for a close question, no call needed (s3.assist "follow", default on).
  lv_obj_t* fc = cell(body, 0);
  lv_obj_t* fr = clear(fc);
  lv_obj_set_size(fr, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(fr, 12, 0);
  lv_obj_set_style_pad_ver(fr, 10, 0);
  lv_obj_t* tg = toggle(fr, p.follow, [](lv_event_t* e) {
    assist::Prefs q = assist::prefs();
    q.follow = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    assist::setPrefs(q);
  });
  lv_obj_align(tg, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* fl = label(fr, "", 15, false, pal().fg);
  // i18n: a switch in the dog's book: after the dog answers, it listens on for the next question
  wrapText(fl, T_("Keep listening after it answers"), os::W() - 48 - 12 - 60, 15);
  lv_obj_align(fl, LV_ALIGN_LEFT_MID, 0, 0);
  // i18n: under that switch: what "listening on" means
  hint(body, N_("Ask the next question within 2 seconds, close to the device, without calling it again."));
  if (hal::has(hal::CAP_RGB_LED)) {  // the board's small RGB LED (src/led.h)
    caption(body, N_("LIGHT"));  // i18n: a heading in upper case: the board's small coloured light
    lv_obj_t* lc = cell(body);
    lv_obj_set_flex_flow(lc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(lc, 6, 0);
    auto ltitle = [&](const char* t) {
      lv_obj_t* l = label(lc, "", 14, true, pal().fg);
      fitText(l, t, os::W() - 48, 20, 14, 12, true, 0);
    };
    ltitle(N_("The light shows"));
    // i18n: chips under "The light shows", short (three must fit in one row): no light / while you talk to the dog /
    // that and when something new arrives (a message, a notification)
    static const char* const LED[] = {NC_("led", "Off"), NC_("led", "Talking"), NC_("led", "Talking + alerts")};
    chips(lc, LED, 3, p.led, [](int i) {
      assist::Prefs q = assist::prefs();
      q.led = i;
      assist::setPrefs(q);
      led::bump();
    });
    ltitle(N_("Light brightness"));
    static const char* const LB[] = {N_("Low"), N_("Normal"), N_("High")};
    chips(lc, LB, 3, p.ledBright - 1, [](int i) {
      assist::Prefs q = assist::prefs();
      q.ledBright = i + 1;
      assist::setPrefs(q);
      led::bump();
    });
  }
  if (!wake::modelInstalled()) {
    lv_obj_t* get = button(body, N_("Download the \"Gâu\" listener (40 KB)"), pal().chip, pal().link, [](lv_event_t*) {
      const pkg::store::Entry* e = pkg::store::entry(wake::PKG);
      String why;
      if (!e) {
        pkg::store::refresh(true);
        return os::toast(N_("Not in the store yet, try again later"));
      }
      if (!pkg::store::install(*e, pkg::store::defaultParts(*e), pkg::FLASH, why)) return os::toast(why.c_str());
      os::toast(N_("Downloading"));
    });
    lv_obj_set_width(get, lv_pct(100));
  }
}

constexpr uint32_t CREAM = 0xfff3df;
const uint32_t GAUGE[4] = {0xf0922b, 0x3b8ee6, 0x7b62dd, 0x22ab9b};
const char* const ICON[4] = {"g_food", "g_fun", "g_energy", "g_clean"};
// i18n: the dog's four needs (gauges): fed, fun, health, clean
const char* const NEED[4] = {N_("Fed"), NC_("need", "Fun"), N_("Health"), N_("Clean")};

// i18n: the names of the dog's "firsts" in its album (short: a small card)
String momentName(const game::Moment& m) {
  switch (m.kind) {
    case 0: return i18n::fmt(N_("First day with {0}"), dogs::name());
    case 1: return i18n::fmt(N_("Reached level {0}"), String(m.arg));
    case 2: return T_("First favourite food");
    case 3: return T_("First bath");
    case 4: return T_("First gold coin");
    case 5: return i18n::plural(m.arg, NP_("Cared for {n} day in a row|Cared for {n} days in a row"));
    case 6: return T_("First high five");
    case 7: return T_("First nap");
  }
  return T_("Moment");
}

String moodText(const char* r) {
  if (!strcmp(r, "hungry")) return T_("Hungry");
  if (!strcmp(r, "sleepy")) return T_("Sleepy");
  if (!strcmp(r, "bored")) return T_("Bored, wants to play");
  return T_("Happy");
}

lv_obj_t* row(lv_obj_t* parent) {
  lv_obj_t* r = clear(parent);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  return r;
}

// The moments' pictures are decoded one at a time, after the screen has opened.
struct Pending {
  lv_obj_t* box;
  game::Moment m;
};
std::vector<Pending> pending;
lv_timer_t* drawer;
lv_obj_t* screen_ = nullptr;

void drawNext(lv_timer_t*) {
  while (!pending.empty() && !lv_obj_is_valid(pending.front().box)) pending.erase(pending.begin());
  if (pending.empty()) {
    lv_timer_delete(drawer);
    drawer = nullptr;
    return;
  }
  Pending p = pending.front();
  pending.erase(pending.begin());
  if (p.m.clip >= pk::clipCount()) return;
  View* v = View::create(p.box, 86, 92, 1, CREAM, 37, 30);
  lv_obj_set_pos(v->obj(), 0, 0);
  lv_obj_move_background(v->obj());
  v->still(pk::clipAt(p.m.clip)->name, p.m.frame);
}

}  // namespace

bool isOpen() { return screen_ != nullptr; }

void open() {
  if (!pk::ready()) return pet::open(nullptr);  // the game screen explains what is missing
  game::State& s = game::s();
  lv_obj_t* body;
  lv_obj_t* scr = os::screen(i18n::fmt(N_("{0}'s book"), dogs::name()).c_str(), &body);
  keep(&screen_, scr);
  const Pal& p = pal();

  // The dog now, moving.
  lv_obj_t* hero = cell(body, 0);
  lv_obj_set_style_bg_color(hero, C(CREAM), 0);
  lv_obj_set_style_clip_corner(hero, true, 0);
  View* v = View::create(hero, os::W() - 24, 180, 0, CREAM, (320 - (os::W() - 24)) / 2, 80);
  const char* mood = game::moodRole();
  v->play(!strcmp(mood, "idle") ? "happy" : !strcmp(mood, "hungry") ? "sulk" : !strcmp(mood, "bored") ? "plead" : "lie_down");
  lv_obj_t* info = clear(hero);
  lv_obj_set_size(info, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(info, 12, 0);
  lv_obj_set_style_pad_top(info, 0, 0);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(info, 4, 0);
  lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
  char b[96];
  label(info, i18n::fmt(N_("{0} · Level {1}"), dogs::name(), String(s.lv)).c_str(), 20, true, 0x2b2f3a);
  const int need = s.lv < 12 ? game::G.xpAt[s.lv] - s.xp : 0;
  // i18n: mood, the XP still needed, the next level, her gold coins
  if (need > 0) snprintf(b, sizeof b, T_("%s · %d XP to level %d · %d gold"), moodText(mood).c_str(), need, s.lv + 1, s.coins);
  else snprintf(b, sizeof b, T_("%s · top level · %d gold"), moodText(mood).c_str(), s.coins);
  lv_obj_t* sub = label(info, "", 14, false, 0x8a6d4a);
  wrapText(sub, b, os::W() - 48, 14);
  lv_obj_t* xpT = box(info, 0xf1dfc2, 4);
  lv_obj_set_size(xpT, lv_pct(100), 8);
  lv_obj_t* xpF = box(xpT, 0xffc933, 4);
  lv_obj_set_size(xpF, lv_pct((int)(game::xpFrac() * 100)), 8);

  // Her needs.
  caption(body, N_("NEEDS"));  // i18n: a heading in upper case
  lv_obj_t* nc = cell(body);
  lv_obj_set_flex_flow(nc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(nc, 10, 0);
  for (int k = 0; k < 4; k++) {
    lv_obj_t* r = row(nc);
    lv_obj_set_height(r, 18);
    const lv_image_dsc_t* ic = pk::image(pk::piece(ICON[k]));
    if (ic) lv_obj_align(img(r, ic), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* l = label(r, NEED[k], 15, false, p.fg);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_t* t = box(r, p.dark ? 0x3a3f4a : 0xe6e8ee, 4);
    lv_obj_set_size(t, 110, 8);
    lv_obj_align(t, LV_ALIGN_RIGHT_MID, -36, 0);
    const bool low = s.need[k] < game::G.low;
    lv_obj_t* f = box(t, low ? 0xe8483e : GAUGE[k], 4);
    lv_obj_set_size(f, max(6, (int)(110 * s.need[k] / 100)), 8);
    lv_obj_t* n = label(r, String((int)s.need[k]).c_str(), 14, true, low ? 0xe8483e : p.dim);
    lv_obj_align(n, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  // Days in a row.
  caption(body, N_("CARE STREAK"));  // i18n: a heading in upper case: days in a row the dog was cared for
  lv_obj_t* sc = cell(body);
  const uint32_t today = game::today();
  const bool live = s.careDay && (s.careDay == today || game::s().streak);
  int streak = s.streak;
  if (today && s.careDay != today) {  // yesterday still counts until today ends
    tm t = {};
    t.tm_year = today / 10000 - 1900, t.tm_mon = today / 100 % 100 - 1, t.tm_mday = today % 100 - 1, t.tm_hour = 12;
    mktime(&t);
    const uint32_t y = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
    if (s.careDay != y) streak = 0;
  }
  (void)live;
  const String days = i18n::plural(streak, NP_("{n} day|{n} days"));
  lv_obj_t* big = label(sc, days.c_str(), 28, true, 0xf08a24);
  lv_obj_set_style_text_font(big, fonts::get(28, true), 0);
  // i18n: %d is the longest streak in days
  const String bs = streak ? i18n::fmt(N_("Best streak: {0}. Feed, play with, bathe or pet {1} once a day to keep it going."),
                                       String(s.best), dogs::name())
                           : i18n::fmt(N_("Best streak: {0}. Care for {1} once today to start a new streak."), String(s.best),
                                       dogs::name());
  lv_obj_t* hint_ = label(sc, "", 13, false, p.dim);
  wrapText(hint_, bs.c_str(), os::W() - 48, 13);
  lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(sc, 4, 0);

  // The album.
  caption(body, N_("MOMENTS"));  // i18n: a heading in upper case: the dog's album of firsts
  game::Moment ms[24];
  const int n = game::moments(ms, 24);
  if (!n) {
    hint(body, i18n::fmt(N_("{0}'s firsts are kept here: a new level, the first bath, a favourite food."), dogs::name()).c_str());
  } else {
    lv_obj_t* grid = clear(body);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    pending.clear();
    // Each name gets up to three lines ("First day with Biscuit" takes two in every language); every card is as tall
    // as the longest name of the album needs, so the row stays even and the dates line up.
    lv_obj_t* cards[24];
    lv_obj_t* dates[24];
    int capH = 0;
    for (int i = 0; i < n; i++) {
      lv_obj_t* c = cards[i] = box(grid, CREAM, 14);
      lv_obj_set_width(c, 86);
      lv_obj_set_style_clip_corner(c, true, 0);
      lv_obj_t* cap = label(c, "", 12, true, 0x2b2f3a);
      fitText(cap, momentName(ms[i]).c_str(), 78, 60, 12, 12, true, 1);
      lv_obj_set_pos(cap, 5, 95);
      int lines = 1;
      for (const char* t = lv_label_get_text(cap); *t; t++) lines += *t == '\n';
      capH = max(capH, lines * (int)lv_font_get_line_height(lv_obj_get_style_text_font(cap, 0)) + (lines - 1));
      dates[i] = nullptr;
      if (ms[i].day) {
        snprintf(b, sizeof b, "%u/%u/%u", (unsigned)(ms[i].day % 100), (unsigned)(ms[i].day / 100 % 100),
                 (unsigned)(ms[i].day / 10000));
        dates[i] = label(c, b, 12, false, 0x8a6d4a);
      }
      pending.push_back({c, ms[i]});
    }
    const int dateY = 95 + capH + 1;
    for (int i = 0; i < n; i++) {
      if (dates[i]) lv_obj_set_pos(dates[i], 5, dateY);
      lv_obj_set_height(cards[i], dateY + (int)lv_font_get_line_height(fonts::text(12, false)) + 5);
    }
    if (!drawer) drawer = lv_timer_create(drawNext, 60, nullptr);
  }

  // Switches.
  caption(body, N_("SETTINGS"));  // i18n: a heading in upper case
  lv_obj_t* set = cell(body, 0);
  lv_obj_set_flex_flow(set, LV_FLEX_FLOW_COLUMN);
  auto sw = [&](const char* name, bool on, lv_event_cb_t cb) {
    lv_obj_t* r = row(set);
    lv_obj_set_height(r, 48);
    lv_obj_set_style_pad_hor(r, 12, 0);
    lv_obj_t* l = label(r, name, 15, false, p.fg);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(toggle(r, on, cb), LV_ALIGN_RIGHT_MID, 0, 0);
  };
  const String voiceName = i18n::fmt(N_("{0}'s voice"), dogs::name());
  const String careName = i18n::fmt(N_("Tell me when {0} needs care"), dogs::name());
  sw(voiceName.c_str(), !s.muted, [](lv_event_t* e) {
    game::s().muted = !lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    voice::setMuted(game::s().muted);
    game::save();
  });
  // The phone game's two switches: its sounds (taps, barks, coins, the world's ambience) and its music.
  sw(N_("Game sounds"), sfx::effectsOn(), [](lv_event_t* e) {
    sfx::setEffects(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
  });
  sw(N_("Background music"), sfx::musicOn(), [](lv_event_t* e) {
    sfx::setMusic(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
  });
  sw(careName.c_str(), s.notify, [](lv_event_t* e) {
    game::s().notify = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    game::save();
  });
  hint(body, N_("No notifications with Do Not Disturb on or in quiet hours."));
  lv_obj_t* other = button(body, N_("Choose another dog"), p.chip, p.link, [](lv_event_t*) { chooser::open(); });
  lv_obj_set_width(other, lv_pct(100));

  // The dog assistant: what it may say by itself (alarms, greetings, the weather, habits).
  caption(body, N_("ASSISTANT"));  // i18n: a heading in upper case: the dog as the device's assistant
  lv_obj_t* as = cell(body, 0);
  lv_obj_set_flex_flow(as, LV_FLEX_FLOW_COLUMN);
  const assist::Prefs ap = assist::prefs();
  auto asw = [&](const char* name, bool on, int field) {
    lv_obj_t* r = row(as);
    lv_obj_set_height(r, 48);
    lv_obj_set_style_pad_hor(r, 12, 0);
    lv_obj_t* l = label(r, "", 15, false, p.fg);
    fitText(l, name, os::W() - 24 - 24 - 60, 40, 15, 12, false, 1);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(toggle(r, on, [](lv_event_t* e) {
      assist::Prefs q = assist::prefs();
      const bool v = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
      switch ((int)(intptr_t)lv_event_get_user_data(e)) {
        case 0: q.on = v; break;
        case 1: q.alarm = v; break;
        case 2: q.greet = v; break;
        case 3: q.weather = v; break;
        case 4: q.water = v ? 120 : 0; break;
        case 5: q.move = v ? 90 : 0; break;
        case 6: q.eyes = v ? 60 : 0; break;
        case 7: q.meal = v; break;
      }
      assist::setPrefs(q);
    }, (void*)(intptr_t)field), LV_ALIGN_RIGHT_MID, 0, 0);
  };
  const String asName = i18n::fmt(N_("{0} is your assistant"), dogs::name());
  asw(asName.c_str(), ap.on, 0);
  asw(N_("Speaks at alarms"), ap.alarm, 1);
  asw(N_("Greets you by the time of day"), ap.greet, 2);
  asw(N_("Tells the weather"), ap.weather, 3);
  asw(N_("Drink water (every 2 hours)"), ap.water > 0, 4);
  asw(N_("Stand up and move (every 90 minutes)"), ap.move > 0, 5);
  asw(N_("Rest your eyes (every hour)"), ap.eyes > 0, 6);
  asw(N_("Meal times (11:30, 18:30)"), ap.meal, 7);
  hint(body, N_("The dog says its own line, then the hour or the temperature. Never in Do Not Disturb or quiet hours, except alarms."));
  if (sound::micChannel()) wakeRows(body);
  lv_obj_t* demo = button(body, i18n::fmt(N_("Call {0}"), dogs::name()).c_str(), 0xffcf5c, 0x5a3a00,
                          [](lv_event_t*) { summon("book"); });
  lv_obj_set_width(demo, lv_pct(100));
  os::open(scr);
}

}  // namespace book
}  // namespace pet
