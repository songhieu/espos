#pragma GCC optimize("Os")  // the hot loops are in wake_fe.c / wake_rt.c; this is glue (the firmware must fit 3.75 MB)
#include "wake.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <sys/stat.h>

#include <cmath>

#include "../desk.h"
#include "../mic.h"
#include "../pet/assist.h"
#include "../pkg/pkg.h"
#include "../power.h"
#include "../sound.h"
#include "../trace.h"
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
float THR2[3] = {0.98f, 0.70f, 0.50f}, THR1[3] = {0.99f, 0.85f, 0.75f}, THR1G[3] = {2.0f, 0.90f, 0.80f};

// ---- shared with the mic task
int16_t* ring = nullptr;
volatile uint32_t clock_ = 0;  // samples seen
volatile float lvl = -96, flr = -60;
volatile bool levelsReset = true;  // the mic (re)opened: the levels start afresh
// The detector (state touched by the mic task only while `det` is true; changed by the UI with det false).
volatile bool det = false, held = false, paused_ = false;
wfe_t* fe = nullptr;
ewm_t net;
void* netMem = nullptr;
const uint8_t* model = nullptr;
uint8_t* modelCopy = nullptr;
float thr[EWM_MAX_OUT] = {2, 2, 2, 2};
volatile bool reset = true, fired = false;
volatile uint32_t firedAt = 0, fires = 0, frames = 0, usSum = 0, usMax = 0;
int warm = 0;
float lastP[2] = {0, 0}, peakP[2] = {0, 0};
volatile float secP[2] = {0, 0};
volatile float firedP[2] = {0, 0};  // the best of the current second (logged when it came near: the near misses)
// The capture: 0 idle, 1 wanted (the mic task copies the ring from capFrom, then records), 2 recording, 3 stopped.
volatile int capState = 0;
int16_t* cap = nullptr;  // 22 samples of room for the WAV header, then the sound
volatile uint32_t capLen = 0;
uint32_t capCap = 0, capFrom = 0;

// ---- UI loop only
bool micWanted = false;
uint8_t mode_ = OFF, word = 0, sens = 2;
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
  if (capLen >= capCap) capState = 3;
}

void detect(const int16_t* pcm, int n) {
  if (held || paused_ || sound::outputSince(GATE_MS)) {
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
  const int r = ewm_push(&net, f, thr);
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
  if (warm > 0) {
    warm -= net.h->stride;
    return;
  }
  if (r > 0 && !fired && capState == 0) {
    firedP[0] = net.prob[0], firedP[1] = net.h->nout > 1 ? net.prob[1] : 0;  // the scores that fired (for the log)
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
  thr[0] = word == 1 ? THR1[k] : THR1G[k];
  thr[1] = THR2[k];
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
      }
    }
    heap_caps_free(b);
  }
  model = m;
  trace::add("wake: model %u B, state %u B, %u channels", (unsigned)f.size(), (unsigned)st, (unsigned)net.h->ch);
  return true;
}

void readPrefs() {
  if (prefsVer == desk::settingsVersion() && prefsVer) return;
  prefsVer = desk::settingsVersion();
  const pet::assist::Prefs p = pet::assist::prefs();
  mode_ = p.wake, word = p.wakeWord, sens = p.wakeSens;
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

void tick() {
  const uint32_t ms = millis();
  if (ms - lastCheck >= 500) {
    lastCheck = ms;
    readPrefs();
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
    if (secP[0] >= 0.2f || secP[1] >= 0.2f) trace::add("wake: best gau %.3f gau2 %.3f", secP[0], secP[1]);
    secP[0] = secP[1] = 0;
  }
  if (fired) {
    fired = false;
    trace::add("wake: heard (gau %.3f, gau2 %.3f; bars %.2f / %.2f)", firedP[0], firedP[1], thr[0], thr[1]);
    if (cb) cb(firedAt);
  }
  if (recSecs && capState == 3) dumpRec();
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
  capCap = (uint32_t)maxMs * (RATE / 1000);
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
  if (capState == 1 || capState == 2) capState = 3;
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

bool console(const char* cmd) {
  if (!strcmp(cmd, "WAKE")) {
    const mic::Stats m = mic::stats();
    Serial.printf("WAKE mode %d word %s sens %d paused %d model %s listening %d thr %.3f/%.3f\n", mode_, word ? "gau" : "gau2",
                  sens, paused_, model ? "loaded" : modelInstalled() ? "installed" : "missing", listening(), thr[0], thr[1]);
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
  // wake model before it is in the store). The package shows after RESTART (pkg scans LittleFS at boot).
  if (!strncmp(cmd, "PKGPUT ", 7)) {
    char id[64] = "", name[52] = "";
    unsigned n = 0;
    if (sscanf(cmd + 7, "%63s %51s %u", id, name, &n) != 3 || strncmp(id, "dev.espos.", 10) || n > 512 * 1024 ||
        strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789.-_") != strlen(id) ||
        strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789.-_") != strlen(name)) {
      Serial.println("PKGPUT bad");
      return true;
    }
    String dir = String("/littlefs/pkgs/") + id;
    mkdir("/littlefs/pkgs", 0777);
    mkdir(dir.c_str(), 0777);
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
