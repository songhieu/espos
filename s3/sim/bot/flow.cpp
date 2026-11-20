// EspOS Bot's turn on the computer: src/bot/talk.cpp (and the real ui) with the mic, the network, the speaker, the
// store and the licence played by this file on a fake clock. Every way a turn can go, checked step by step.
// sim/bot/run.sh builds it with sim/shim's Arduino stand-in.
#include <Arduino.h>
#include <stdio.h>

#include <deque>
#include <map>
#include <string>

#include "../../src/bot/ear.h"
#include "../../src/bot/link.h"
#include "../../src/bot/logic.h"
#include "../../src/bot/spk.h"
#include "../../src/bot/store.h"
#include "../../src/bot/talk.h"
#include "../../src/bot/ui.h"
#include "../../src/lic.h"
#include "../../src/trace.h"

using namespace bot;

// ---------------------------------------------------------------- the clock
static uint32_t now_ = 100000;
uint32_t millis() { return now_; }
uint32_t micros() { return now_ * 1000; }
void delay(uint32_t ms) { now_ += ms; }
long random(long m) { return m ? rand() % m : 0; }
long random(long a, long b) { return a + random(b - a); }

// ---------------------------------------------------------------- the pieces talk uses
static bool verbose = getenv("V") != nullptr;
namespace trace {
void begin() {}
void add(const char* f, ...) {
  if (!verbose) return;
  va_list a;
  va_start(a, f);
  printf("  [%6u] ", (unsigned)now_);
  vprintf(f, a);
  printf("\n");
  va_end(a);
}
void dump() {}
void setEcho(bool) {}
}  // namespace trace

// the room: speech from..to (ms on the clock)
static uint32_t speakFrom = 0, speakTo = 0, vadAt = 0;
static bool capOn = false, capFull = false, heldNow = false;
static int captures = 0;
namespace bot {
namespace ear {
bool begin() { return true; }
bool ok() { return true; }
void setWake(int) {}
bool takeWake() { return false; }
void hold(bool on) { heldNow = on; }
float level() { return -40; }
float floorDb() { return -60; }
void vadRestart() { vadAt = now_; }
static bool speaking(uint32_t t) { return t >= speakFrom && t < speakTo; }
uint32_t speechMs() {
  const uint32_t a = speakFrom > vadAt ? speakFrom : vadAt, b = speakTo < now_ ? speakTo : now_;
  return speakTo > vadAt && b > a ? b - a : 0;
}
uint32_t quietMs() {
  if (speaking(now_)) return 0;
  const uint32_t last = speakTo > vadAt && speakTo <= now_ ? speakTo : vadAt;
  return now_ - last;
}
bool captureStart(uint32_t, uint32_t) {
  if (capOn) return false;
  capOn = true, capFull = false, captures++;
  return true;
}
void captureStop() {}
bool captureFull() { return capFull; }
uint32_t captureMs() { return 0; }
static uint8_t wavBuf[64];
const uint8_t* wav(uint32_t& bytes, uint32_t) { return bytes = 44 + 3200, wavBuf; }
void captureFree() { capOn = false; }
std::string stats() { return ""; }
void setGain(int) {}
int gain() { return 4096; }
}  // namespace ear

static link::State linkState = link::State::Linked;
static int asks = 0, cancels = 0, warms = 0;
static std::string lastQuery;
static bool lastVoice = false;
static std::deque<std::pair<uint32_t, link::Answer>> answers;  // when it comes, what
namespace link {
void begin() {}
void loop() {}
State state() { return linkState; }
bool blocked() { return false; }
void syncNow() {}
bool synced() { return true; }
uint32_t lastSyncAgoS() { return 0; }
std::string hwid() { return ""; }
std::string mac4() { return "7e38"; }
bool provision(const char*, std::string&) { return false; }
bool weather(ui::Weather&) { return false; }
std::string lunar() { return ""; }
bool ask(const uint8_t*, uint32_t, const std::string& q, bool voice) { return asks++, lastQuery = q, lastVoice = voice, true; }
void cancelAsk() { cancels++; }
bool takeAnswer(Answer& a) {
  if (answers.empty() || answers.front().first > now_) return false;
  a = answers.front().second;
  answers.pop_front();
  return true;
}
void warm() { warms++; }
void install() {}
bool updating() { return false; }
int updatePct() { return -1; }
std::string offerVersion() { return ""; }
}  // namespace link

static uint32_t playUntil = 0;
static int cueN = 0;
namespace spk {
bool begin() { return true; }
void volume(int) {}
bool pushPcm(const uint8_t*, uint32_t) { return true; }
void endStream() {}
void stop() { playUntil = 0; }
bool playing() { return now_ < playUntil; }
bool outputSince(uint32_t ms) { return now_ < playUntil + ms && playUntil; }
void cue(cues::Cue) { cueN++; }
void alarmTone() {}
}  // namespace spk

static std::map<std::string, std::string> sets;
namespace store {
std::string setting(const std::string& k) { return logic::get(sets, k); }
}  // namespace store
}  // namespace bot

static lic::Ai aiNow = lic::Ai::Trial;
static int used = 0;
namespace lic {
void begin() {}
Ai ai() { return aiNow; }
int aiLeft() { return 10; }
static String empty;
const String& buyUrl() { return empty; }
const char* lockedWhy() { return ""; }
const String& raw() { return empty; }
uint32_t version() { return 1; }
void fromSync(JsonVariantConst) {}
void locked(const char*, const char*) { aiNow = Ai::Off; }
void usedTurn() { used++; }
bool console(const char*) { return false; }
}  // namespace lic

// ---------------------------------------------------------------- driving it
static int fails = 0, checks = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    checks++;                                                            \
    if (!(c)) fails++, printf("FAIL line %d: %s (t=%u)\n", __LINE__, #c, (unsigned)now_); \
  } while (0)

static void run(uint32_t ms) {
  for (uint32_t e = now_ + ms; now_ < e; now_ += 20) {
    talk::loop();
    ui::tick(now_);
  }
}
static link::Answer ans(int status, const char* json, bool voice = false) {
  link::Answer a;
  a.status = status, a.body = json, a.voice = voice;
  return a;
}
static void reset() {
  run(1000);
  if (talk::active()) talk::press(), run(100);
  answers.clear();
  asks = cancels = warms = used = cueN = 0;
  speakFrom = speakTo = 0;
  playUntil = 0;
  aiNow = lic::Ai::Trial;
  linkState = link::State::Linked;
  sets.clear();
}

int main() {
  ui::setLang(ui::Lang::VI);
  ui::standby();
  ui::tick(now_);

  printf("1. BOOT, a question, a reply, back to standby (bot.follow 0)\n");
  reset();
  sets["bot.follow"] = "0";
  talk::start(false);
  CHECK(talk::busy() && ui::state() == ui::State::LISTEN && heldNow && warms == 1 && cueN == 1);
  speakFrom = now_ + 800, speakTo = now_ + 2300;  // 1.5 s of speech
  run(2300 + 1900);
  CHECK(asks == 0);  // still within the 2 s after speech
  run(200);
  CHECK(asks == 1 && ui::state() == ui::State::THINK && lastQuery.find("wake=1") == std::string::npos && lastVoice);
  answers.push_back({now_ + 1500, ans(200, R"({"heard":"mấy giờ rồi","reply":"Chín giờ tối rồi đó.","mood":"happy","cid":"c1"})")});
  run(1600);
  CHECK(ui::state() == ui::State::TALK && used == 1 && !heldNow);
  run(8000);
  CHECK(talk::active() && !talk::busy());  // the reply stays up
  run(20000);
  CHECK(!talk::active() && ui::state() == ui::State::STANDBY);
  // bot.follow 1 (the default): after the reply it listens 6 s without the wake word, then goes back quietly
  sets.clear();
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  answers.push_back({now_ + 300, ans(200, R"({"reply":"Dạ.","mood":"happy"})")});
  run(400);
  run(4000);
  CHECK(ui::state() == ui::State::LISTEN && capOn);
  run(6500);
  CHECK(!talk::active() && ui::state() == ui::State::STANDBY && !capOn && !heldNow);

  printf("2. the wake word, nobody speaks: the sad face, then standby\n");
  reset();
  talk::start(true);
  run(6200);
  CHECK(ui::state() == ui::State::SAD && asks == 0 && !capOn);
  run(30000);
  CHECK(!talk::active());

  printf("3. a reply that asks: it listens again; nothing said: back quietly\n");
  reset();
  talk::start(true);
  speakFrom = now_ + 300, speakTo = now_ + 1500;
  run(3800);
  CHECK(asks == 1 && lastQuery.find("wake=1") != std::string::npos);
  answers.push_back({now_ + 500, ans(200, R"({"reply":"Bạn muốn nghe nhạc không?","mood":"stand","asks":true,"cid":"c2"})")});
  run(600);
  CHECK(ui::state() == ui::State::TALK);
  run(6000);
  CHECK(ui::state() == ui::State::LISTEN && talk::busy());  // the follow-up
  run(6500);
  CHECK(!talk::active() && ui::state() == ui::State::STANDBY);  // no sad face for an unused follow-up
  // the conversation goes on: the next question carries cid c2
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  CHECK(asks == 2 && lastQuery.find("cid=c2") != std::string::npos);

  printf("4. the follow-up is used: a second question without the wake word\n");
  reset();
  sets["bot.follow"] = "1";
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1200;
  run(3500);
  answers.push_back({now_ + 300, ans(200, R"({"reply":"Dạ.","mood":"happy"})")});
  run(400);
  run(4000);
  CHECK(ui::state() == ui::State::LISTEN);
  speakFrom = now_ + 500, speakTo = now_ + 1500;
  run(3600);
  CHECK(asks == 2);

  printf("5. 402: the trial is over; the next press shows it without listening\n");
  reset();
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  answers.push_back({now_ + 300, ans(402, R"({"locked":"trial_ended","buy_url":"https://espos.dev/buy"})")});
  run(400);
  CHECK(ui::state() == ui::State::TALK && aiNow == lic::Ai::Off && used == 0);
  run(30000);
  talk::start(false);
  CHECK(ui::state() == ui::State::TALK && !talk::busy() && asks == 1);

  printf("6. the voice: the reply waits for it (and its echo) before listening again\n");
  reset();
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  answers.push_back({now_ + 300, ans(200, R"({"reply":"Ok.","mood":"happy","asks":true})", true)});
  run(400);
  playUntil = now_ + 9000;  // a long voice
  run(8000);
  CHECK(ui::state() == ui::State::TALK);  // the words are done, the voice is not
  run(1500);
  CHECK(ui::state() == ui::State::LISTEN);

  printf("7. BOOT while it listens: sends at once; BOOT before anything is said: closes\n");
  reset();
  talk::start(false);
  speakFrom = now_ + 200, speakTo = now_ + 5000;
  run(1500);
  talk::press();
  CHECK(asks == 1 && ui::state() == ui::State::THINK);
  reset();
  talk::start(false);
  run(1000);
  talk::press();
  CHECK(!talk::active() && asks == 0 && ui::state() == ui::State::STANDBY);

  printf("8. nothing made out (silent), no answer at all, a network failure\n");
  reset();
  talk::start(true);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  answers.push_back({now_ + 200, ans(200, R"({"silent":true})")});
  run(300);
  CHECK(ui::state() == ui::State::SAD && used == 0);
  reset();
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  run(51000);  // never answered
  CHECK(ui::state() == ui::State::TALK && cancels >= 1);
  reset();
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  answers.push_back({now_ + 100, ans(-1, "")});
  run(200);
  CHECK(ui::state() == ui::State::TALK);

  printf("9. not provisioned, or waiting for the claim\n");
  reset();
  linkState = link::State::None;
  talk::start(false);
  CHECK(ui::state() == ui::State::TALK && !talk::busy());
  reset();
  linkState = link::State::Claim;
  talk::start(true);
  CHECK(ui::state() == ui::State::TALK && !talk::busy());

  printf("10. a long question is cut at 9 s and sent\n");
  reset();
  talk::start(false);
  speakFrom = now_ + 200, speakTo = now_ + 20000;
  run(9000);
  capFull = true;
  run(100);
  CHECK(asks == 1);

  printf("11. night hours: the words only (voice=0), no cues\n");
  reset();
  talk::setQuiet(true);
  talk::start(false);
  CHECK(cueN == 0);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  CHECK(asks == 1 && !lastVoice && cueN == 0);
  talk::setQuiet(false);
  reset();
  sets["bot.voice"] = "0";
  talk::start(false);
  speakFrom = now_ + 300, speakTo = now_ + 1000;
  run(3400);
  CHECK(asks == 1 && !lastVoice && cueN == 2);

  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
