#include "talk.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <string>

#include "../lic.h"
#include "../trace.h"
#include "ear.h"
#include "link.h"
#include "spk.h"
#include "store.h"
#include "ui.h"

namespace bot {
namespace talk {
namespace {

enum class St : uint8_t { Idle, Listen, Send, Show, After };
St st = St::Idle;
uint32_t stAt = 0, shownAt = 0;
bool byWake = false, follow = false, heardSpeech = false, quiet = false;
std::string cid;
uint32_t cidAt = 0;
constexpr uint32_t QUESTION_MS = 6000, END_QUIET_MS = 2000, MAX_MS = 9000, SEND_MS = 50000, AFTER_MS = 20000;
constexpr uint32_t SPEECH_MS = 200;  // in-order speech score before it counts as the question

void set(St s) {
  st = s;
  stAt = millis();
}

void end() {
  spk::stop();
  ear::captureFree();
  ear::hold(false);
  link::cancelAsk();
  set(St::Idle);
  ui::standby();
}

void listen(bool isFollow) {
  follow = isFollow, heardSpeech = false;
  ear::hold(true);
  // the wake word's own sound is not the question: from now; BOOT: a little before the press
  if (!ear::captureStart(isFollow || byWake ? 0 : 300, MAX_MS)) return end();
  ear::vadRestart();
  link::warm();
  if (!quiet) spk::cue(cues::LISTEN);  // "I'm listening" (the mic does not count the speaker's own sound)
  ui::listen();
  set(St::Listen);
}

ui::Mood moodOf(const char* m) {
  if (!strcmp(m, "sad") || !strcmp(m, "sorry") || !strcmp(m, "no_tired")) return ui::Mood::SAD;
  if (!strcmp(m, "happy") || !strcmp(m, "excited") || !strcmp(m, "love") || !strcmp(m, "laugh")) return ui::Mood::HAPPY;
  return ui::Mood::TALK;
}

void show(const std::string& words, ui::Mood m) {
  ear::hold(false);  // "Gâu gâu" may start a new turn over a reply
  ui::talk(words, m);
  shownAt = millis();
  set(St::Show);
}

void answer(int status, const std::string& body) {
  ear::captureFree();
  JsonDocument d;
  deserializeJson(d, body);
  trace::add("talk: answer %d (%s)", status, d["silent"] == true ? "nothing made out" : d["error"] | "");
  if (status == 200 && d["silent"] == true) {
    if (follow) return end();  // a follow-up nobody used
    ear::hold(false);
    ui::notHeard();
    shownAt = millis();
    return set(St::Show);
  }
  if (status == 200) {
    lic::usedTurn();
    if (d["cid"].is<const char*>()) cid = d["cid"].as<const char*>(), cidAt = millis();
    if ((d["done"].is<JsonArray>() && d["done"].size()) || d["saved"] == true) link::syncNow();
    const std::string reply = d["reply"] | "", heard = d["heard"] | "";
    trace::add("talk: heard \"%.80s\" -> \"%.120s\"", heard.c_str(), reply.c_str());
    if (reply.empty()) {
      ear::hold(false);
      ui::notHeard();
      shownAt = millis();
      return set(St::Show);
    }
    follow = d["asks"] == true || store::setting("bot.follow") == "1";
    show(reply, moodOf(d["mood"] | "stand"));
    return;
  }
  follow = false;
  if (status == 402) {  // the trial is over, or no AI on this account
    lic::locked(d["locked"] | "no_ai", d["buy_url"] | "");
    return show(ui::str(ui::S_TRIAL_OVER), ui::Mood::SAD);
  }
  return show(ui::str(ui::S_NO_SERVER), ui::Mood::SAD);
}

void send() {
  uint32_t bytes = 0;
  const uint8_t* w = ear::wav(bytes, 300);
  std::string q;
  if (byWake) q += "&wake=1";
  if (!cid.empty()) q += "&cid=" + cid;
  const std::string dog = store::setting("bot.dog");
  if (!dog.empty()) q += "&dog=" + dog;
  trace::add("talk: question %u ms%s", (unsigned)((bytes - 44) / 32), follow ? " (follow-up)" : "");
  if (!link::ask(w, bytes, q, store::setting("bot.voice") != "0" && !quiet)) return answer(-1, "");
  if (!quiet) spk::cue(cues::SENT);  // "got it"
  ui::think();
  set(St::Send);
}

}  // namespace

void start(bool wake) {
  if (st == St::Listen || st == St::Send) return;
  spk::stop();  // a call over a reply: its voice stops
  byWake = wake;
  if (millis() - cidAt > 5 * 60000) cid.clear();  // a new conversation after 5 quiet minutes
#ifdef BOT_PAIR_PAUSED
  if (link::state() == link::State::None) return show(ui::str(ui::S_PAIR_SOON), ui::Mood::TALK);
#endif
  if (link::state() == link::State::None) return show(ui::str(ui::S_SET_UP_USB), ui::Mood::SAD);
  if (link::state() != link::State::Linked) return show(ui::str(ui::S_NO_SERVER), ui::Mood::SAD);
  if (lic::aiLocked()) return show(ui::str(ui::S_TRIAL_OVER), ui::Mood::SAD);
  listen(false);
}

bool active() { return st != St::Idle; }
void setQuiet(bool on) { quiet = on; }
bool busy() { return st == St::Listen || st == St::Send; }

void press() {
  if (st == St::Listen) {
    if (heardSpeech) return send();
    return end();  // nothing said yet: BOOT closes it
  }
  if (st == St::Show || st == St::After) return end();
  if (st == St::Send) return end();  // not wanted any more
}

void loop() {
  const uint32_t now = millis(), in = now - stAt;
  switch (st) {
    case St::Idle:
      break;
    case St::Listen: {
      const uint32_t sp = ear::speechMs(), quiet = ear::quietMs();
      if (!heardSpeech && sp >= SPEECH_MS) heardSpeech = true;
      if (!heardSpeech && in > QUESTION_MS) {
        trace::add("talk: no question (level %.1f, floor %.1f)", ear::level(), ear::floorDb());
        ear::captureFree();
        if (follow) return end();
        ear::hold(false);
        ui::notHeard();
        shownAt = now;
        follow = false;
        return set(St::Show);
      }
      if (heardSpeech && (quiet >= END_QUIET_MS || ear::captureFull())) return send();
      break;
    }
    case St::Send: {
      link::Answer a;
      if (link::takeAnswer(a)) return answer(a.status, a.body);
      if (in > SEND_MS) {
        link::cancelAsk();
        return answer(-3, "");
      }
      break;
    }
    case St::Show:
      if (!ui::talkDone() || spk::outputSince(400)) break;  // the words read, the voice and its echo over
      if (follow && ear::ok()) return listen(true);
      set(St::After);
      break;
    case St::After:
      if (in > AFTER_MS) end();
      break;
  }
}

void fake(int status, const char* json) {
  if (st == St::Idle) byWake = false;
  ear::captureFree();
  link::cancelAsk();
  answer(status, json ? json : "");
}

}  // namespace talk
}  // namespace bot
