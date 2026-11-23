#pragma GCC optimize("Os")  // the hot loops are in wake_fe.c / wake_rt.c; this is glue (the firmware must fit 3.75 MB)
#include <algorithm>
#include "wake.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <sys/stat.h>

#include <atomic>
#include <cmath>

#include "../desk.h"
#include "../i18n.h"
#include "../mic.h"
#include "../assistant.h"
#include "../pet/assist.h"
#include "../pet/sfx.h"  // bedOnly(): a tune alone does not stop the wake word
#include "../notif.h"
#include "../pkg/pkg.h"
#include "../pkg/store.h"
#include "../power.h"
#include "../sound.h"
#include "../trace.h"
#ifdef SIM
void simWakeEvent(const char* kind, float score);  // sim/live.cpp keeps ~10 s of the live microphone around it
#endif
#include "wake_fe.h"
#include "wake_rt.h"

namespace wake {
namespace {

constexpr int RATE = 16000;
constexpr uint32_t RING = 2 * RATE;  // the last 2 s
constexpr uint32_t GATE_MS = 300;    // after the speaker stops, before listening again
// Thresholds per sensitivity (low, normal, high); normal is the easy mode (two syllables with the call's pitch):
// THR2 on "gâu gâu"; THR1G on the any-"gâu" output in the "gâu gâu" setting (a quick "gâu gâu" the network
// takes for one); THR1 on it when "gâu" alone is allowed.
// A package's wake.json {"thr": {"gau2": [3], "gau": [3], "gau_in2": [3]}} replaces them.
// (listener 1.1.0, model c6c, 2026-11-21: gau2 0.99 / 0.8 / 0.6, gau 0.995 / 0.9 / 0.8, gau_in2 off / 0.93 / 0.85 come
// from its wake.json; these are the 1.0.x values, kept for an older listener.)
float THR2[3] = {0.98f, 0.70f, 0.50f}, THR1[3] = {0.99f, 0.85f, 0.75f}, THR1G[3] = {2.0f, 0.90f, 0.80f};
// The calls after "gâu" (2026-11-21): one output each in a model that has them (named in the .ewm), thresholds from
// the package's wake.json {"thr": {"kiki": [3], ...}} only: a model without them, or no thresholds, leaves them off.
constexpr const char* CALL_OUT[] = {"kiki", "lulu", "cun", "milu"};  // calls bits 2, 4, 8, 16
constexpr int NCALL = sizeof(CALL_OUT) / sizeof(CALL_OUT[0]);
float THRC[NCALL][3] = {{2, 2, 2}, {2, 2, 2}, {2, 2, 2}, {2, 2, 2}};
int callOut[NCALL] = {-1, -1, -1, -1};  // the output of each call in the loaded model

// ---- shared with the mic task
int16_t* ring = nullptr;
volatile uint32_t clock_ = 0;  // samples seen
volatile float lvl = -96, flr = -60;
volatile bool levelsReset = true;  // the mic (re)opened: the levels start afresh
// vadTake's counts (the mic task adds, the UI loop takes them with a swap): blocks above the floor + vadRel, blocks,
// the loudest level in centi-dB.
volatile float vadRel = 9, vadMin = -60;
volatile uint32_t vadMuteUntil = 0;  // in mic samples (clock_)
volatile uint32_t vadLoudAt = 0, vadNoisyAt = 0;  // mic clock of the last loud block, and of the last not silent one
volatile float vadQPeak = -90;  // the question's loudest block so far (vadQuestion): the room's hum stays far under it
volatile bool vadQOn = false;
constexpr uint32_t VAD_ECHO_MS = 400;  // the speaker's sound and its echo in the room, after the last sample written
std::atomic<uint32_t> vadLoudN{0}, vadN{0}, vadOursN{0};
std::atomic<int32_t> vadPeakC{-9000};
// The speech-start score, block by block in order (loud +10 ms, quiet -5 ms, never under 0): its highest value since
// vadRestart, packed with the restart's number (16 bits each) so a take never sees a score from before the restart.
std::atomic<uint32_t> vadGen{0}, vadBest{0};
uint32_t micGen = 0;
// The mic while the speaker plays (the speaker-to-mic level, for the game music's level and the wake word's bar):
// 10 ms blocks while anything played, their summed dB and loudest; the same for a tune alone. Logged once a minute.
volatile uint32_t spkN = 0, bedN = 0;
volatile float spkSum = 0, spkMax = -90, bedSum = 0, bedMax = -90;
int32_t vadScore = 0, vadMax = 0;
// The detector (state touched by the mic task only while `det` is true; changed by the UI with det false).
volatile bool det = false, held = false, paused_ = false;
wfe_t* fe = nullptr;
ewm_t net;
void* netMem = nullptr;
const uint8_t* model = nullptr;
uint8_t* modelCopy = nullptr;
float thr[EWM_MAX_OUT] = {2, 2, 2, 2, 2, 2, 2, 2};  // 2: above any probability (off)
volatile bool reset = true, fired = false;
volatile uint32_t firedAt = 0, fires = 0, frames = 0, usSum = 0, usMax = 0;
int warm = 0;
float lastP[2] = {0, 0}, peakP[2] = {0, 0};
volatile float secP[2] = {0, 0}, secC = 0;  // secC: the best of the other calls this second
volatile float firedP[2] = {0, 0};
volatile int firedMask = 0;  // the outputs that fired (bit per output)
volatile bool firedBed = false;  // it fired over a game's tune (the bar BED_UP higher)  // the best of the current second (logged when it came near: the near misses)
// The capture: 0 idle, 1 wanted (the mic task copies the ring from capFrom, then records), 2 recording, 3 stopped.
volatile int capState = 0;
volatile uint32_t capDoneAt = 0;  // when a capture filled up or was stopped (state 3: waiting for its owner)
int16_t* cap = nullptr;  // 22 samples of room for the WAV header, then the sound
volatile uint32_t capLen = 0;
uint32_t capCap = 0, capFrom = 0;

// ---- UI loop only
bool micWanted = false;
uint8_t mode_ = OFF, word = 0, sens = 2, calls = 31;
uint32_t prefsVer = 0, lastCheck = 0;
void (*cb)(uint32_t) = nullptr;
bool modelTried = false;
uint32_t modelTriedAt = 0;
// WAKEREC: a pending dump of a capture to the console.
int recSecs = 0;

void push(const int16_t* pcm, int n) {
  const uint32_t c = clock_;
  for (int i = 0; i < n; i++) ring[(c + i) % RING] = pcm[i];
  clock_ = c + n;
  // The level (power smoothed over ~25 ms, dBFS), and the noise floor: the lowest level of the last 2 s (speech
  // always has dips inside 2 s; a floor carried over from an earlier opening of the mic is never used).
  static float pw = 0, hist[200];
  static int hi = 0;
  double s = 0;
  for (int i = 0; i < n; i++) s += (double)pcm[i] * pcm[i];
  pw = levelsReset ? (float)(s / n) : 0.6f * pw + 0.4f * (float)(s / n);
  const float db = pw <= 1 ? -90.3f : 10.0f * log10f(pw) - 90.309f;
  if (levelsReset) {
    for (float& h : hist) h = db;
    levelsReset = false;
  }
  hist[hi] = db;
  hi = hi + 1 == 200 ? 0 : hi + 1;
  float m = hist[0];
  for (float h : hist) m = h < m ? h : m;
  lvl = db;
  flr = m;
  if (sound::outputSince(20)) {
    spkN = spkN + 1, spkSum = spkSum + db, spkMax = db > spkMax ? db : spkMax;
    if (pet::sfx::bedOnly(300)) bedN = bedN + 1, bedSum = bedSum + db, bedMax = db > bedMax ? db : bedMax;
  }
  vadN.fetch_add(1, std::memory_order_relaxed);
  // Our own sound is never speech: talk.cpp's mute (the ack, on the mic clock), and whatever the speaker played in
  // the last VAD_ECHO_MS but a game's tune (the speaker sits next to the mic: on the board the dog's voice comes back
  // at -30..-35 dBFS, peaks -12, and the room carries it ~0.3 s more; the I2S buffer plays ~40 ms after the write).
  // A dry run on the board (2026-11-20, build 7) took the ack's tail for a question; the sim with SIM_ECHO did too.
  const bool ours = (int32_t)(c - vadMuteUntil) < 0 || (sound::outputSince(VAD_ECHO_MS) && !pet::sfx::bedOnly(VAD_ECHO_MS));
  const bool loud = db > m + vadRel && db > vadMin && !ours;
  if (ours) vadOursN.fetch_add(1, std::memory_order_relaxed);
  if (loud) vadLoudN.fetch_add(1, std::memory_order_relaxed);
  // The end of a question counts only clear speech: +9 dB over the rolling floor (the bot's bar) and within 18 dB of
  // the question's own loudest moment (a fan or the TV 20-30 dB under the voice is not speech; on the board a steady
  // -50 dBFS room ran a -21 dBFS question to the 8 s cap, 2026-11-21).
  // ...and 6 dB over the room's usual level (the 30th percentile of the last 2 s; the minimum alone sits far under a
  // rumbling room: a bus-like hum crossed the minimum + 9 all the time and ran a soft question to the 8 s cap).
  if (vadQOn && !ours && db > vadQPeak) vadQPeak = db;
  if (loud && db > m + 9) {
    static float tmp[200];
    memcpy(tmp, hist, sizeof tmp);
    std::nth_element(tmp, tmp + 60, tmp + 200);
    if (db > tmp[60] + 6 && (!vadQOn || db > vadQPeak - 18)) vadLoudAt = c;
  }
  if (!ours && db >= m + 3) vadNoisyAt = c;
  const uint32_t g = vadGen.load(std::memory_order_relaxed) & 0xffff;
  if (g != micGen) micGen = g, vadScore = vadMax = 0;
  const int32_t blockMs = n * 1000 / 16000;
  vadScore = loud ? vadScore + blockMs : max<int32_t>(0, vadScore - blockMs / 2);
  vadMax = max(vadMax, vadScore);
  vadBest.store(micGen << 16 | (uint32_t)min<int32_t>(vadMax, 0xffff), std::memory_order_relaxed);
  const int32_t cdb = (int32_t)(db * 100);
  int32_t p = vadPeakC.load(std::memory_order_relaxed);
  while (!ours && cdb > p && !vadPeakC.compare_exchange_weak(p, cdb, std::memory_order_relaxed)) {
  }
}

void captureBlock(const int16_t* pcm, int n) {
  if (capState == 1) {
    // From capFrom (at most what the ring still holds) to now.
    const uint32_t now = clock_;
    uint32_t from = capFrom;
    if ((int32_t)(now - from) > (int32_t)(RING - n) || (int32_t)(now - from) < 0) from = now - (RING - n);
    uint32_t len = 0;
    for (uint32_t c = from; c != now && len < capCap; c++) cap[22 + len++] = ring[c % RING];
    capLen = len;
    capState = 2;
    return;  // this block is in the ring already (push() ran first)
  }
  if (capState != 2) return;
  uint32_t len = capLen;
  const int m = (int)min<uint32_t>(n, capCap - len);
  memcpy(cap + 22 + len, pcm, m * 2);
  capLen = len + m;
  if (capLen >= capCap) capState = 3, capDoneAt = millis();
}

// A game's tune or ambience alone (pet::sfx::bedOnly: no voice, no effect for GATE_MS) does not stop the wake word
// (PawBoop plays music most of the time); the bar is BED_UP higher then. Measured 2026-11-20 with the board's
// detector on the 9 tunes (10 min) under the room's floor: 1-2 false calls per 10 min at the normal bar, 0-1 with
// +0.05, calls heard about the same. Anything else the speaker plays (the dog's lines, effects, the AI's voice)
// still stops it (its own "gâu" must never call it).
constexpr float BED_UP = 0.05f;

void detect(const int16_t* pcm, int n) {
  const bool out = sound::outputSince(GATE_MS), bed = out && pet::sfx::bedOnly(GATE_MS);
  if (held || paused_ || (out && !bed)) {
    reset = true;  // the device's own sound, a turn or a pause: start afresh after it
    return;
  }
  if (reset) {
    wfe_reset(fe);
    ewm_reset(&net);
    warm = 60;  // 0.6 s of fresh sound before it may fire (a call said right after the dog spoke still counts)
    reset = false;
  }
  const uint32_t t0 = micros();
  int8_t f[WFE_BANDS];
  wfe_frame(fe, pcm, f);
  float bar[EWM_MAX_OUT];
  for (int o = 0; o < EWM_MAX_OUT; o++) bar[o] = bed && thr[o] < 1 ? min(thr[o] + BED_UP, 0.99f) : thr[o];  // 2 = off
  const int r = ewm_push(&net, f, bed ? bar : thr);
  const uint32_t us = micros() - t0;
  frames = frames + 1;
  usSum = usSum + us;
  if (us > usMax) usMax = us;
  if (r < 0) return;
  for (int o = 0; o < 2 && o < net.h->nout; o++) {
    lastP[o] = net.prob[o];
    if (net.prob[o] > peakP[o]) peakP[o] = net.prob[o];
    if (warm <= 0 && net.prob[o] > secP[o]) secP[o] = net.prob[o];
  }
  for (int c = 0; c < NCALL; c++)
    if (warm <= 0 && callOut[c] >= 0 && thr[callOut[c]] < 1 && net.prob[callOut[c]] > secC) secC = net.prob[callOut[c]];
  if (warm > 0) {
    warm -= net.h->stride;
    return;
  }
  if (r > 0 && !fired && capState == 0) {
    firedP[0] = net.prob[0], firedP[1] = net.h->nout > 1 ? net.prob[1] : 0;  // the scores that fired (for the log)
    firedMask = r;
    firedBed = bed;
    firedAt = clock_;
    fires = fires + 1;
    fired = true;
  }
}

void sink(const int16_t* pcm, int n) {
  if (!ring || n != WFE_HOP) return;
  push(pcm, n);
  captureBlock(pcm, n);
  if (det) detect(pcm, n);
}

void applyThr() {
  const int k = constrain(sens, 1, 3) - 1;
  const bool gau = calls & 1;
  thr[0] = !gau ? 2.0f : word == 1 ? THR1[k] : THR1G[k];
  thr[1] = gau ? THR2[k] : 2.0f;
  for (int c = 0; c < NCALL; c++)
    if (callOut[c] >= 0) thr[callOut[c]] = (calls >> (c + 1)) & 1 ? THRC[c][k] : 2.0f;
}

// The output a fired mask names, for the log: "gau2", "kiki" ...
const char* firedName(int mask) {
  for (int o = 0; o < net.h->nout; o++)
    if (mask & (1 << o)) return ewm_name(&net, o);
  return "?";
}

bool loadModel() {
  if (model) return true;
  if (modelTried && millis() - modelTriedAt < 30000) return false;
  modelTried = true, modelTriedAt = millis();
  pkg::File f;
  if (!pkg::find(PKG) || !pkg::open(PKG, "gau.ewm", f)) return false;
  const uint8_t* m = f.data();
  if (!m) m = modelCopy = pkg::loadAll(f);
  if (!m) return false;
  const uint32_t st = ewm_state_size(m);
  netMem = heap_caps_calloc(1, st, MALLOC_CAP_SPIRAM);
  fe = (wfe_t*)heap_caps_calloc(1, sizeof(wfe_t), MALLOC_CAP_SPIRAM);
  const int e = netMem && fe ? ewm_init(&net, m, f.size(), netMem) : -9;
  if (e) {
    trace::add("wake: model refused (%d)", e);
    heap_caps_free(netMem), heap_caps_free(fe), heap_caps_free(modelCopy);
    netMem = nullptr, fe = nullptr, modelCopy = nullptr;
    return false;
  }
  wfe_init(fe, net.h->fe_floor, net.h->fe_off, net.h->fe_scale);
  // Optional thresholds from the package.
  pkg::File j;
  if (pkg::open(PKG, "wake.json", j) && j.size() < 2048) {
    uint8_t* b = pkg::loadAll(j);
    JsonDocument d;
    if (b && !deserializeJson(d, (const char*)b, j.size())) {
      for (int k = 0; k < 3; k++) {
        THR2[k] = d["thr"]["gau2"][k] | THR2[k];
        THR1[k] = d["thr"]["gau"][k] | THR1[k];
        THR1G[k] = d["thr"]["gau_in2"][k] | THR1G[k];
        for (int c = 0; c < NCALL; c++) THRC[c][k] = d["thr"][CALL_OUT[c]][k] | THRC[c][k];
      }
    }
    heap_caps_free(b);
  }
  for (int c = 0; c < NCALL; c++) callOut[c] = ewm_find(&net, CALL_OUT[c]);
  model = m;
  trace::add("wake: model %u B, state %u B, %u channels, %u outputs", (unsigned)f.size(), (unsigned)st,
             (unsigned)net.h->ch, (unsigned)net.h->nout);
  return true;
}

void readPrefs() {
  static uint32_t pkgVer = 0;
  if (prefsVer == desk::settingsVersion() && prefsVer && pkgVer == pkg::version()) return;
  prefsVer = desk::settingsVersion(), pkgVer = pkg::version();
  const pet::assist::Prefs p = pet::assist::prefs();
  mode_ = p.wake, word = p.wakeWord, sens = p.wakeSens, calls = p.calls;
  // It listens only for the assistant slot's app (app-slots.md §16): none, or one whose turn the device lacks, and
  // the wake word is off whatever s3.assist says (kept for when an assistant is back).
  if (!assistant::listens()) mode_ = OFF;
}

bool wantDetector() {
  return mode_ != OFF && !paused_ && (mode_ == ALWAYS || power::usb()) && mic::available() && modelInstalled();
}

void setMic(bool on) {
  if (on == micWanted) return;
  micWanted = on;
  if (on) {
    levelsReset = true;
    if (!ring) ring = (int16_t*)heap_caps_calloc(RING, 2, MALLOC_CAP_SPIRAM);
    if (!ring || !mic::open(sink)) micWanted = false;
  } else {
    mic::close();
  }
}

void dumpRec() {
  uint32_t bytes = 0;
  uint8_t* w = wav(bytes);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(200);
#endif
  Serial.printf("WAKEREC %u\n", (unsigned)bytes);
  for (uint32_t o = 0; w && o < bytes; o += 4096) Serial.write(w + o, min<uint32_t>(4096, bytes - o));
  Serial.print("\nENDWAKEREC\n");
  Serial.flush();
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  heap_caps_free(w);
  recSecs = 0;
}

}  // namespace

void begin() {
  readPrefs();
  applyThr();
}

// The wake word turned on (on the web or here) while the listener is not on the device: it is fetched from the
// store by itself, with a notification, once the catalog has it (the person said yes when turning it on; 0.5.6
// waited for a second tap on "Download" in the dog's book). Tried again every 10 minutes while it is missing.
void fetchModel() {
  static uint32_t triedAt = 0;
  static bool told = false;
  if (mode_ == OFF || modelInstalled() || desk::link() != desk::Link::Linked) return;
  if (triedAt && millis() - triedAt < 10 * 60 * 1000UL) return;
  const pkg::store::Entry* e = pkg::store::entry(PKG);
  if (!e) {  // the catalog is not here yet (or old): ask for it, look again in a minute
    pkg::store::refresh(false);
    triedAt = millis() - 9 * 60 * 1000UL;
    return;
  }
  triedAt = millis();
  String why;
  const bool ok = pkg::store::install(*e, pkg::store::defaultParts(*e), pkg::FLASH, why);
  trace::add("wake: the listener is missing: %s%s", ok ? "downloading it" : "could not start: ", ok ? "" : why.c_str());
  if (ok && !told) {
    told = true;
    // i18n: a notification while the device downloads the small "Gâu" wake-word listener by itself
    notif::Opts o;
    o.tag = "wake", o.sound = false, o.badge = false;
    notif::post("pet", T_("Getting the \"Gâu\" listener"), T_("The dog will hear its name in a moment."), o);
  }
}

void tick() {
  const uint32_t ms = millis();
  static uint32_t spkAt = 0;
  if (ms - spkAt >= 60000) {
    spkAt = ms;
    if (spkN >= 100) {  // a second or more of the speaker in the last minute
      const uint32_t n = spkN, b = bedN;
      trace::add("wake: mic while the speaker played %.1f s: avg %.0f, loudest %.0f dBFS (floor %.0f; a tune alone %.1f s: avg %.0f, loudest %.0f)",
                 n / 100.0f, spkSum / n, spkMax, flr, b / 100.0f, b ? bedSum / b : -90.0f, bedMax);
    }
    spkN = bedN = 0, spkSum = bedSum = 0, spkMax = bedMax = -90;
  }
  if (ms - lastCheck >= 500) {
    lastCheck = ms;
    readPrefs();
    fetchModel();
    const bool want = wantDetector() && loadModel();
    if (want != det) {
      if (want) {
        applyThr();
        reset = true;
      }
      det = want;
      trace::add("wake: %s", want ? "listening" : "not listening");
    } else if (det) {
      applyThr();
    }
  }
  setMic(det || capState != 0);
  static uint32_t lastSec = 0;
  if (det && ms - lastSec >= 1000) {  // the scores that came near in the last second (0.2 and up)
    lastSec = ms;
    if (secP[0] >= 0.2f || secP[1] >= 0.2f || secC >= 0.2f) {
      trace::add("wake: best gau %.3f gau2 %.3f calls %.3f", secP[0], secP[1], secC);
#ifdef SIM
      simWakeEvent("near", fmaxf(fmaxf(secP[0], secP[1]), secC));
#endif
    }
    secP[0] = secP[1] = 0, secC = 0;
  }
  if (fired) {
    fired = false;
    trace::add("wake: heard %s (gau %.3f, gau2 %.3f; bars %.2f / %.2f%s)", firedName(firedMask), firedP[0], firedP[1],
               thr[0], thr[1], firedBed ? ", over a tune: +0.05" : "");
#ifdef SIM
    simWakeEvent("call", (firedMask & ~3) ? 0.99f : fmaxf(firedP[0], firedP[1]));
#endif
    if (cb) cb(firedAt);
  }
  if (recSecs && capState == 3) dumpRec();
  // A finished recording nobody took for 30 s (a bug elsewhere, as 0.5.6's closed pop-up): the wake word waits for
  // no recording, so it would never fire again. Freed, and said in the trace.
  if (!recSecs && capState == 3 && millis() - capDoneAt > 30000) {
    trace::add("wake: a recording nobody took for 30 s, freed");
    captureFree();
  }
}

bool modelInstalled() {
  const pkg::Package* p = pkg::find(PKG);
  return p && p->available;
}
Mode mode() { return (Mode)mode_; }
bool listening() { return det && mic::isOpen(); }
bool micOn() { return mic::isOpen(); }
void setPaused(bool p) {
  paused_ = p;
  lastCheck = 0;  // act at the next tick
}
bool paused() { return paused_; }
void onWake(void (*c)(uint32_t)) { cb = c; }
void hold(bool h) {
  if (held && !h && det) trace::add("wake: listening (resumed)");
  held = h;
}

uint32_t now() { return clock_; }

bool captureStart(uint32_t fromSample, int maxMs) {
  if (!mic::available()) return false;
  captureFree();
  // As long as PSRAM's largest free block allows (48 KB kept for the rest), at least 6 s: a call asked one block
  // of 544 KB with 375 KB the largest left (the 2.8", 2026-11-21: "still no room to record after 4 s"); 10 s of it
  // holds the call, the ack and the question all the same (the question's end is heard long before the cap).
  constexpr uint32_t MARGIN = 48 * 1024, MIN_MS = 6000;
  const uint32_t want = (uint32_t)maxMs * (RATE / 1000);
  const size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const uint32_t room = big > MARGIN + 44 ? (uint32_t)((big - MARGIN) / 2) - 22 : 0;
  capCap = want < room ? want : room;
  if (capCap < MIN_MS * (RATE / 1000) && capCap < want) return false;
  if (capCap < want) trace::add("wake: recording room %u ms of %d (largest PSRAM block %u KB)", (unsigned)(capCap / (RATE / 1000)), maxMs,
                                (unsigned)(big / 1024));
  cap = (int16_t*)heap_caps_malloc((capCap + 22) * 2, MALLOC_CAP_SPIRAM);
  if (!cap) return false;
  capLen = 0;
  capFrom = micWanted ? fromSample : clock_;  // a mic that was off has nothing before now (the ring is old)
  __sync_synchronize();  // the buffer before the state: the mic task reads them in that order
  capState = 1;
  setMic(true);
  return micWanted;
}

void captureStop() {
  if (capState == 1 || capState == 2) capState = 3, capDoneAt = millis();
}

bool capturing() { return capState == 1 || capState == 2; }

int captureMs() { return (int)(capLen / (RATE / 1000)); }

uint8_t* wav(uint32_t& bytes) {
  captureStop();
  if (!cap) return bytes = 0, nullptr;
  if (mic::isOpen()) delay(12);  // a block being written right now
  const uint32_t data = capLen * 2;
  uint8_t* h = (uint8_t*)cap;
  auto u32 = [](uint8_t* p, uint32_t v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; };
  memcpy(h, "RIFF", 4), u32(h + 4, 36 + data), memcpy(h + 8, "WAVEfmt ", 8), u32(h + 16, 16);
  h[20] = 1, h[21] = 0, h[22] = 1, h[23] = 0;  // PCM, mono
  u32(h + 24, RATE), u32(h + 28, RATE * 2);
  h[32] = 2, h[33] = 0, h[34] = 16, h[35] = 0;
  memcpy(h + 36, "data", 4), u32(h + 40, data);
  bytes = 44 + data;
  capState = 0;
  cap = nullptr;  // the caller has it now
  capLen = 0;
  return h;
}

void captureFree() {
  capState = 0;
  delay(0);
  // The mic task may be in captureBlock right now: it only writes while capState is 1 or 2, so wait a block.
  if (mic::isOpen()) delay(12);
  heap_caps_free(cap);
  cap = nullptr;
  capLen = 0;
}

float levelDb() { return lvl; }
float floorDb() { return flr; }
void vadSet(float relDb, float minDb) { vadRel = relDb, vadMin = minDb; }
void vadRestart() { vadGen.fetch_add(1); }
void vadMute(uint32_t forMs) { vadMuteUntil = clock_ + forMs * (RATE / 1000); }
uint32_t vadQuietMs() { return (clock_ - vadLoudAt) / (RATE / 1000); }
void vadQuestion(bool on, float seedDb) {
  vadQPeak = seedDb;
  vadQOn = on;
}
uint32_t vadSilentMs() { return (clock_ - vadNoisyAt) / (RATE / 1000); }
uint32_t vadSpeechMs() {
  const uint32_t b = vadBest.load(), g = vadGen.load() & 0xffff;
  return (b >> 16) == g ? (b & 0xffff) : 0;
}
Vad vadTake() {
  Vad v;
  const uint32_t blockMs = WFE_HOP * 1000 / RATE;
  v.ms = vadN.exchange(0) * blockMs;
  v.loudMs = vadLoudN.exchange(0) * blockMs;
  v.oursMs = vadOursN.exchange(0) * blockMs;
  v.peakDb = vadPeakC.exchange(-9000) / 100.0f;
  return v;
}

bool console(const char* cmd) {
#if defined(ESPOS_QA) || defined(SIM)
  // QA: PKGINSTALL <id> / PKGREMOVE <id>: a store package installed to internal flash or removed, as the store page
  // does (board stress tests: a flash write while something else reads; answers in the trace's store: lines).
  if (!strncmp(cmd, "PKGINSTALL ", 11) || !strncmp(cmd, "PKGREMOVE ", 10)) {
    const bool add = cmd[3] == 'I';
    const char* id = cmd + (add ? 11 : 10);
    String why;
    bool ok = false;
    if (add) {
      const pkg::store::Entry* e = pkg::store::entry(id);
      ok = e && pkg::store::install(*e, pkg::store::defaultParts(*e), pkg::FLASH, why);
      if (!e) {  // the catalog is not here yet (the board loads it when the store needs it): ask for it now
        pkg::store::refresh(false);
        why = "not in the catalog yet: asked for it, send again in ~10 s";
      }
    } else {
      ok = pkg::store::remove(id, why);
    }
    Serial.printf("%s %s %s %s\n", add ? "PKGINSTALL" : "PKGREMOVE", id, ok ? "ok:" : "refused:", why.length() ? why.c_str() : "queued");
    trace::add("qa: %s %s %s %s", add ? "PKGINSTALL" : "PKGREMOVE", id, ok ? "ok:" : "refused:", why.length() ? why.c_str() : "queued");
    return true;
  }
  if (!strncmp(cmd, "PKGSTATUS ", 10)) {  // QA: PKGSTATUS <id>: installed (version, volume) or not, a job for it or not
    const pkg::Package* p = pkg::find(cmd + 10);
    const pkg::store::Status s = pkg::store::status();
    const String line = String(cmd + 10) + (p ? " installed " + p->version + (p->vol == pkg::SD ? " card" : " flash") : " not installed") +
                        ", store job: " + (pkg::store::busy() ? s.id + " " + String((int)s.state) : String("none"));
    Serial.printf("PKGSTATUS %s\n", line.c_str());
    trace::add("qa: PKGSTATUS %s", line.c_str());
    return true;
  }
#endif
  if (!strcmp(cmd, "WAKE")) {
    const mic::Stats m = mic::stats();
    Serial.printf("WAKE mode %d word %s calls %d sens %d paused %d model %s listening %d thr %.3f/%.3f\n", mode_,
                  word ? "gau" : "gau2", calls, sens, paused_, model ? "loaded" : modelInstalled() ? "installed" : "missing",
                  listening(), thr[0], thr[1]);
    for (int c = 0; c < NCALL; c++)
      if (callOut[c] >= 0) Serial.printf("WAKE call %s output %d thr %.3f\n", CALL_OUT[c], callOut[c], thr[callOut[c]]);
    Serial.printf("WAKE fires %u frames %u avg %u us max %u us, last p %.3f/%.3f, peak p %.3f/%.3f, level %.1f floor %.1f dBFS\n",
                  (unsigned)fires, (unsigned)frames, frames ? (unsigned)(usSum / frames) : 0, (unsigned)usMax, lastP[0],
                  lastP[1], peakP[0], peakP[1], lvl, flr);
    Serial.printf("MIC slot %d rms L %.1f R %.1f dBFS, %u blocks, %u lost\n", m.slot, m.rmsL, m.rmsR, (unsigned)m.blocks,
                  (unsigned)m.lost);
    peakP[0] = peakP[1] = 0;
    usMax = 0;
    return true;
  }
  if (!strncmp(cmd, "WAKEREC ", 8)) {
    const int s = constrain(atoi(cmd + 8), 1, 60);
    if (capState != 0 || !captureStart(now(), s * 1000)) {
      Serial.println("WAKEREC 0");
      return true;
    }
    recSecs = s;
    Serial.printf("WAKEREC recording %d s\n", s);
    return true;
  }
  // PKGPUT <id> <file> <bytes>, then the bytes: a file of a small package straight into LittleFS (development: the
  // wake model before it is in the store, the Sessions app on a test board). One folder level ("fonts/m12.bin").
  // Send manifest.json last: a folder without it is removed at boot. The package shows after RESTART (pkg scans
  // LittleFS at boot). wake/tools/s3.py pkg FILE.espk sends a whole package.
  if (!strncmp(cmd, "PKGPUT ", 7)) {
    char id[64] = "", name[52] = "";
    unsigned n = 0;
    const char* slash = nullptr;
    if (sscanf(cmd + 7, "%63s %51s %u", id, name, &n) != 3 || strncmp(id, "dev.espos.", 10) || n > 512 * 1024 ||
        strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789.-_") != strlen(id) ||
        strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_/") != strlen(name) ||
        name[0] == '.' || name[0] == '/' || strstr(name, "..") || strstr(name, "/.") ||
        ((slash = strchr(name, '/')) && (slash != strrchr(name, '/') || !slash[1]))) {
      Serial.println("PKGPUT bad");
      return true;
    }
    String dir = String("/littlefs/pkgs/") + id;
    mkdir("/littlefs/pkgs", 0777);
    mkdir(dir.c_str(), 0777);
    if (slash) mkdir((dir + "/" + String(name).substring(0, slash - name)).c_str(), 0777);
    FILE* f = fopen((dir + "/" + name).c_str(), "wb");
    uint8_t buf[256];
    unsigned got = 0;
    uint32_t last = millis();
    while (got < n && millis() - last < 5000) {
      const int a = Serial.available();
      if (a <= 0) {
        delay(1);
        continue;
      }
      const int r = Serial.readBytes(buf, min<unsigned>(min<unsigned>(a, sizeof buf), n - got));
      if (f && r > 0) fwrite(buf, 1, r, f);
      got += r > 0 ? r : 0;
      last = millis();
    }
    if (f) fclose(f);
    Serial.printf("PKGPUT %s %u of %u\n", f && got == n ? "ok" : "failed", got, n);
    return true;
  }
  // SETTING <key> [value]: read (or set, synced up like a change on the device) one setting: QA snapshots and restores.
  if (!strncmp(cmd, "SETTING ", 8)) {
    String k = cmd + 8, v;
    const int sp = k.indexOf(' ');
    if (sp > 0) {
      v = k.substring(sp + 1);
      k = k.substring(0, sp);
      desk::setSetting(k.c_str(), v == "-" ? String() : v);
    }
    Serial.printf("SETTING %s=%s\n", k.c_str(), desk::setting(k.c_str(), "").c_str());
    return true;
  }
  if (!strcmp(cmd, "RESTART")) {
    Serial.println("RESTART");
    Serial.flush();
    delay(100);
    ESP.restart();
  }
  if (!strncmp(cmd, "WAKEPAUSE ", 10)) {
    setPaused(atoi(cmd + 10) != 0);
    Serial.printf("WAKEPAUSE %d\n", paused_);
    return true;
  }
  return false;
}

}  // namespace wake
