#include "ear.h"

#include <Arduino.h>
#include <Preferences.h>
#ifndef BOT_SIM
#include <driver/i2s_std.h>
#endif
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include <atomic>

#include "../trace.h"
#include "../wake/wake_fe.h"
#include "../wake/wake_rt.h"
#include "config.h"
#include "spk.h"

#ifndef BOT_SIM
// The wake model of the S3's package (wake/pkg/dev.espos.wake.gau/gau.ewm), built into the firmware (env bot128,
// tools/bot/model.py: empty when the package's file is not there, as in the public repository).
extern const uint8_t gau_ewm_start[] asm("_binary__pio_bot_gau_ewm_start");
extern const uint8_t gau_ewm_end[] asm("_binary__pio_bot_gau_ewm_end");
#else
// The simulator reads the package's file (sim/bot/sim.cpp: BOT_SIM_MODEL or wake/pkg/...): bot::simModel().
namespace bot {
const uint8_t* simModel(uint32_t& size);
}
#endif

namespace bot {
namespace ear {
namespace {

constexpr int RATE = 16000, HOP = WFE_HOP;  // 160 samples = 10 ms
constexpr uint32_t RING = 2 * RATE;
// The package's thresholds (wake.json, 2026-11-19: normal = the easy mode), per sensitivity low, normal, high.
// Listener 1.1.0 (model c6c, wake/reports/c6c.md), the same as the S3 package's wake.json.
const float THR2[3] = {0.99f, 0.80f, 0.60f}, THR1G[3] = {2.0f, 0.93f, 0.85f};
// The other calls (2026-11-21, the owner: "mấy tiếng chó kia cũng nên có cho nó"): "Ki ki", "Lu lu", "Cún ơi", "Milu",
// each an output of the built-in model (when it has it), with the package's thresholds; all on, as on the S3.
constexpr const char* CALLS[] = {"kiki", "lulu", "cun", "milu"};
const float THRC[4][3] = {{0.98f, 0.90f, 0.80f}, {0.97f, 0.85f, 0.75f}, {0.97f, 0.90f, 0.80f}, {0.97f, 0.90f, 0.80f}};
int callOut[4] = {-1, -1, -1, -1};

#ifndef BOT_SIM
i2s_chan_handle_t rx = nullptr;
#endif
bool micOk = false;
int16_t* ring = nullptr;
volatile uint32_t clock_ = 0, lastLoud = 0;
volatile float lvl = -96, flr = -60;
volatile int gainQ8 = 4096;
std::atomic<uint32_t> vadGen{0}, vadBest{0};
uint32_t micGen = 0;
int32_t vadScore = 0, vadMax = 0;
volatile uint32_t vadFrom = 0;  // the clock at vadRestart

wfe_t* fe = nullptr;
ewm_t net;
void* netMem = nullptr;
bool modelOk = false;
float thr[EWM_MAX_OUT] = {2, 2, 2, 2, 2, 2, 2, 2};  // 2: off
volatile bool det = false, held = false, reset = true, fired = false;
int warm = 0;
volatile uint32_t frames = 0, usMax = 0, fires = 0;
volatile float firedP[2] = {0, 0};
volatile int firedMask = 0;  // the outputs that fired (bit per output), for the log

uint8_t* fireWav = nullptr;  // the 2 s before the last "Gâu gâu" (44 bytes of WAV header room first)

volatile int capState = 0;  // 0 idle, 1 wanted, 2 recording, 3 full or stopped
int16_t* cap = nullptr;     // 22 samples of room for the WAV header, then the sound
volatile uint32_t capLen = 0, capLastLoud = 0;
uint32_t capCap = 0, capFrom = 0;

void push(const int16_t* pcm, int n) {
  const uint32_t c = clock_;
  for (int i = 0; i < n; i++) ring[(c + i) % RING] = pcm[i];
  clock_ = c + n;
  static float pw = 0, hist[200];
  static int hi = 0;
  static bool first = true;
  double s = 0;
  for (int i = 0; i < n; i++) s += (double)pcm[i] * pcm[i];
  pw = first ? (float)(s / n) : 0.6f * pw + 0.4f * (float)(s / n);
  const float db = pw <= 1 ? -90.3f : 10.0f * log10f(pw) - 90.309f;
  if (first) {
    for (float& h : hist) h = db;
    first = false;
  }
  hist[hi] = db;
  hi = hi + 1 == 200 ? 0 : hi + 1;
  float m = hist[0];
  for (float h : hist) m = h < m ? h : m;
  lvl = db;
  flr = m;
  // The bot's own sound (a cue, the dog's voice) and its echo in the room are never speech.
  const bool loud = db > m + 9 && db > -60 && !spk::outputSince(400);
  if (loud) lastLoud = clock_;
  const uint32_t g = vadGen.load(std::memory_order_relaxed) & 0xffff;
  if (g != micGen) micGen = g, vadScore = vadMax = 0;
  vadScore = loud ? vadScore + 10 : (vadScore > 5 ? vadScore - 5 : 0);
  if (vadScore > vadMax) vadMax = vadScore;
  vadBest.store(micGen << 16 | (uint32_t)(vadMax < 0xffff ? vadMax : 0xffff), std::memory_order_relaxed);
}

void captureBlock(const int16_t* pcm, int n) {
  if (capState == 1) {
    const uint32_t now = clock_;
    uint32_t from = capFrom;
    if ((int32_t)(now - from) > (int32_t)(RING - n) || (int32_t)(now - from) < 0) from = now - (RING - n);
    uint32_t len = 0;
    for (uint32_t c = from; c != now && len < capCap; c++) cap[22 + len++] = ring[c % RING];
    capLen = len;
    capState = 2;
    return;
  }
  if (capState != 2) return;
  const uint32_t len = capLen;
  const uint32_t m = (uint32_t)n < capCap - len ? (uint32_t)n : capCap - len;
  memcpy(cap + 22 + len, pcm, m * 2);
  capLen = len + m;
  if (lastLoud == clock_) capLastLoud = capLen;
  if (capLen >= capCap) capState = 3;
}

void detect(const int16_t* pcm) {
  if (held || !modelOk || spk::outputSince(300)) {  // its own "gâu" must never call it
    reset = true;
    return;
  }
  if (reset) {
    wfe_reset(fe);
    ewm_reset(&net);
    warm = 60;
    reset = false;
  }
  const uint32_t t0 = micros();
  int8_t f[WFE_BANDS];
  wfe_frame(fe, pcm, f);
  const int r = ewm_push(&net, f, thr);
  const uint32_t us = micros() - t0;
  frames = frames + 1;
  if (us > usMax) usMax = us;
  if (r < 0) return;
  if (warm > 0) {
    warm -= net.h->stride;
    return;
  }
  if (r > 0 && !fired && capState == 0) {
    if (fireWav) {  // the last 2 s, kept for the console's WAKELAST (was it a call, or the TV?)
      const uint32_t c = clock_;
      int16_t* d = (int16_t*)(fireWav + 44);
      for (uint32_t i = 0; i < RING; i++) d[i] = ring[(c + i) % RING];
    }
    firedP[0] = net.prob[0];
    firedP[1] = net.h->nout > 1 ? net.prob[1] : 0;
    firedMask = r;
    fires = fires + 1;
    fired = true;
  }
}

// A 2nd-order Butterworth high-pass at 100 Hz: the MEMS mic's DC and slow drift, and room rumble, out (on the owner's
// desk, 2026-11-20, 97 % of the raw signal's power was under 100 Hz: drift and a ~70 Hz hum). The wake word's mel
// bands start at 80 Hz; the S3's codec mic, which the model learned from, has little below 100 Hz either.
struct Hpf {
  float b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  Hpf(float fc) {
    const float w = 2 * (float)M_PI * fc / RATE, c = cosf(w), al = sinf(w) / (2 * 0.70710678f), a0 = 1 + al;
    b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, b2 = b0, a1 = -2 * c / a0, a2 = (1 - al) / a0;
  }
  float step(float x) {
    const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1, x1 = x, y2 = y1, y1 = y;
    return y;
  }
};

void process(const int16_t* pcm) {
  push(pcm, HOP);
  captureBlock(pcm, HOP);
  if (det) detect(pcm);
}

#ifndef BOT_SIM
void task(void*) {
  static int32_t raw[HOP];
  static int16_t pcm[HOP];
  Hpf hpf(100);
  for (;;) {
    size_t got = 0;
    if (i2s_channel_read(rx, raw, sizeof raw, &got, 200) != ESP_OK || got != sizeof raw) continue;
    const int g = gainQ8;
    for (int i = 0; i < HOP; i++) {
      const float y = hpf.step((float)(raw[i] >> 8) * g / 65536.0f);  // the 24 bits as 16 (/256), the gain (q8)
      pcm[i] = (int16_t)(y > 32767 ? 32767 : y < -32767 ? -32767 : y);
    }
    process(pcm);
  }
}
#endif

}  // namespace

bool begin() {
  Preferences p;
  if (p.begin("bot", true)) gainQ8 = p.getInt("micgain", 4096), p.end();
  ring = (int16_t*)heap_caps_calloc(RING, 2, MALLOC_CAP_SPIRAM);
  if (!ring) return false;
#ifndef BOT_SIM
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 6, cc.dma_frame_num = 240;
  if (i2s_new_channel(&cc, nullptr, &rx) != ESP_OK) return false;
  i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)pins::MIC_SCK, .ws = (gpio_num_t)pins::MIC_WS,
                   .dout = I2S_GPIO_UNUSED, .din = (gpio_num_t)pins::MIC_DIN,
                   .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
  };
  sc.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  if (i2s_channel_init_std_mode(rx, &sc) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) return false;
  // the model
  const uint8_t* m = gau_ewm_start;
  const uint32_t size = gau_ewm_end - gau_ewm_start;
#else
  uint32_t size = 0;
  const uint8_t* m = simModel(size);
  if (!m) m = (const uint8_t*)"", size = 0;
#endif
  const uint32_t st = size >= sizeof(ewm_hdr_t) ? ewm_state_size(m) : 0;  // empty: built without the model
  netMem = heap_caps_calloc(1, st, MALLOC_CAP_SPIRAM);
  fe = (wfe_t*)heap_caps_calloc(1, sizeof(wfe_t), MALLOC_CAP_SPIRAM);
  const int e = st && netMem && fe ? ewm_init(&net, m, size, netMem) : -9;
  if (!e) {
    wfe_init(fe, net.h->fe_floor, net.h->fe_off, net.h->fe_scale);
    modelOk = true;
    for (int c = 0; c < 4; c++) callOut[c] = ewm_find(&net, CALLS[c]);
    trace::add("ear: model %u B, state %u B, %u channels, %u outputs", (unsigned)size, (unsigned)st, (unsigned)net.h->ch,
               (unsigned)net.h->nout);
  } else {
    trace::add("ear: model refused (%d)", e);
  }
  fireWav = (uint8_t*)heap_caps_calloc(1, 44 + RING * 2, MALLOC_CAP_SPIRAM);
  micOk = true;
#ifndef BOT_SIM
  xTaskCreatePinnedToCore(task, "bot-ear", 6144, nullptr, 5, nullptr, 0);
#endif
  return true;
}

void feed(const int16_t* pcm) {  // the simulator's microphone: 10 ms blocks as the board's task makes them
  if (micOk) process(pcm);
}

bool ok() { return micOk; }

void setWake(int sens) {
  if (sens <= 0 || !modelOk) {
    det = false;
    return;
  }
  const int k = (sens > 3 ? 3 : sens) - 1;
  det = false;  // the task never sees half-written bars
  thr[0] = THR1G[k];
  thr[1] = THR2[k];
  for (int c = 0; c < 4; c++)
    if (callOut[c] >= 0) thr[callOut[c]] = THRC[c][k];
  reset = true;
  det = true;
}

bool takeWake() {
  if (!fired) return false;
  fired = false;
  const char* name = "?";
  for (int o = 0; o < net.h->nout; o++)
    if (firedMask & (1 << o)) {
      name = ewm_name(&net, o);
      break;
    }
  trace::add("ear: call %s (%.2f %.2f)", name, firedP[0], firedP[1]);
  return true;
}

void hold(bool on) {
  held = on;
  if (!on) fired = false;
}

float level() { return lvl; }
float floorDb() { return flr; }

void vadRestart() {
  vadGen.fetch_add(1);
  vadFrom = clock_;
}
uint32_t speechMs() {
  const uint32_t b = vadBest.load(std::memory_order_relaxed);
  return (b >> 16) == (vadGen.load() & 0xffff) ? (b & 0xffff) : 0;
}
uint32_t quietMs() {
  const uint32_t c = clock_, l = lastLoud;
  const uint32_t since = (int32_t)(l - vadFrom) > 0 ? l : vadFrom;
  return (c - since) / 16;
}

bool captureStart(uint32_t backMs, uint32_t maxMs) {
  if (capState != 0 || !micOk) return false;
  capCap = maxMs * 16;
  if (!cap) cap = (int16_t*)heap_caps_malloc((capCap + 22) * 2, MALLOC_CAP_SPIRAM);
  if (!cap) return false;
  capLen = 0;
  capLastLoud = 0;
  capFrom = clock_ - backMs * 16;
  capState = 1;
  return true;
}
void captureStop() {
  if (capState == 1 || capState == 2) capState = 3;
}
bool captureFull() { return capState == 3; }
uint32_t captureMs() { return capLen / 16; }

namespace {
void wavHeader(uint8_t* h, uint32_t data) {
  auto le32 = [&](int o, uint32_t v) { h[o] = v, h[o + 1] = v >> 8, h[o + 2] = v >> 16, h[o + 3] = v >> 24; };
  auto le16 = [&](int o, uint16_t v) { h[o] = v, h[o + 1] = v >> 8; };
  memcpy(h, "RIFF", 4), le32(4, 36 + data), memcpy(h + 8, "WAVEfmt ", 8), le32(16, 16), le16(20, 1), le16(22, 1);
  le32(24, RATE), le32(28, RATE * 2), le16(32, 2), le16(34, 16), memcpy(h + 36, "data", 4), le32(40, data);
}
}  // namespace

const uint8_t* wav(uint32_t& bytes, uint32_t keepTailMs) {
  captureStop();
  uint32_t n = capLen;
  const uint32_t keep = capLastLoud + keepTailMs * 16;
  if (capLastLoud && keep < n) n = keep;
  wavHeader((uint8_t*)cap, n * 2);
  bytes = 44 + n * 2;
  return (const uint8_t*)cap;
}

const uint8_t* lastFire(uint32_t& bytes) {
  if (!fireWav || !fires) return bytes = 0, nullptr;
  wavHeader(fireWav, RING * 2);
  bytes = 44 + RING * 2;
  return fireWav;
}

void captureFree() {
  capState = 0;
  capLen = 0;
}

std::string stats() {
  char t[160];
  snprintf(t, sizeof t, "MIC level %.1f dBFS floor %.1f, gain %.1fx, wake %s (frames %u, max %u us, fires %u)", lvl, flr,
           gainQ8 / 256.0f, det ? "on" : "off", (unsigned)frames, (unsigned)usMax, (unsigned)fires);
  return t;
}

void setGain(int q8) {
  gainQ8 = q8 < 64 ? 64 : q8 > 65536 ? 65536 : q8;
  Preferences p;
  if (p.begin("bot", false)) p.putInt("micgain", gainQ8), p.end();
}
int gain() { return gainQ8; }

}  // namespace ear
}  // namespace bot
