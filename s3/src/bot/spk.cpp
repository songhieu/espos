#include "spk.h"

#include <Arduino.h>
#ifndef BOT_SIM
#include <driver/i2s_std.h>
#endif
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "config.h"

namespace bot {
namespace spk {
namespace {

constexpr uint32_t RING = 8 * RATE;  // 8 s: the voice can run ahead of the speaker
#ifndef BOT_SIM
i2s_chan_handle_t tx = nullptr;
#endif
int16_t* ring = nullptr;
volatile uint32_t wpos = 0, rpos = 0, lastOut = 0;
volatile int vol = 60;
volatile bool stopNow = false, enabled = true;
uint8_t carry = 0;
bool hasCarry = false;

void put(const int16_t* s, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    while (wpos - rpos >= RING - 1) {  // full: wait for the speaker (the voice comes faster than it plays)
      if (stopNow) return;
      delay(5);
    }
    ring[wpos % RING] = s[i];
    wpos = wpos + 1;
  }
}

// The next n samples for the speaker (silence when there is nothing), at the volume.
void take(int16_t* buf, int want) {
  const uint32_t have = wpos - rpos;
  const int n = have > (uint32_t)want ? want : (int)have;
  const int v = vol;
  const float g = v <= 0 ? 0 : powf(10.0f, (v - 100) / 40.0f);  // 100 % = 0 dB, 60 % = -20 dB
  for (int i = 0; i < n; i++) buf[i] = (int16_t)(ring[(rpos + i) % RING] * g);
  for (int i = n; i < want; i++) buf[i] = 0;
  if (n) {
    rpos = rpos + n;
    lastOut = millis();
  }
}

#ifndef BOT_SIM
void task(void*) {
  static int16_t buf[256];
  for (;;) {
    take(buf, 256);
    size_t w = 0;
    // silence when there is nothing: the amp stays quiet; a channel switched off (tests) is not written at all
    if (!enabled || i2s_channel_write(tx, buf, sizeof buf, &w, 100) != ESP_OK) delay(16);
  }
}
#endif

}  // namespace

bool begin() {
  ring = (int16_t*)heap_caps_calloc(RING, 2, MALLOC_CAP_SPIRAM);
  if (!ring) return false;
#ifdef BOT_SIM
  return true;  // sim/bot/sim.cpp's audio callback takes the samples (spk::pull)
#else
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4, cc.dma_frame_num = 256;
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) return false;
  i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)pins::SPK_BCLK, .ws = (gpio_num_t)pins::SPK_LRCK,
                   .dout = (gpio_num_t)pins::SPK_DOUT, .din = I2S_GPIO_UNUSED,
                   .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
  };
  if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) return false;
  xTaskCreatePinnedToCore(task, "bot-spk", 4096, nullptr, 6, nullptr, 0);
  return true;
#endif
}

void pull(int16_t* out, int n) { take(out, n); }

void volume(int pct) { vol = pct < 0 ? 0 : pct > 100 ? 100 : pct; }

bool pushPcm(const uint8_t* p, uint32_t n) {
  if (!ring || stopNow) return !stopNow;
  static int16_t tmp[256];
  uint32_t k = 0;
  if (hasCarry && n) {
    tmp[k++] = (int16_t)(carry | p[0] << 8);
    p++, n--, hasCarry = false;
  }
  while (n >= 2) {
    tmp[k++] = (int16_t)(p[0] | p[1] << 8);
    p += 2, n -= 2;
    if (k == 256) put(tmp, k), k = 0;
  }
  if (n) carry = p[0], hasCarry = true;
  if (k) put(tmp, k);
  return !stopNow;
}

void endStream() { hasCarry = false; }

void stop() {
  stopNow = true;
  rpos = wpos;
  delay(10);
  rpos = wpos;
  stopNow = false;
  hasCarry = false;
}

void enable(bool on) {
#ifdef BOT_SIM
  enabled = on;
  return;
#else
  if (!tx || on == enabled) return;
  if (!on) {
    enabled = false;
    delay(50);
    i2s_channel_disable(tx);
  }
  else {
    i2s_channel_enable(tx);
    enabled = true;
  }
#endif
}

bool playing() { return wpos != rpos; }
bool outputSince(uint32_t ms) { return playing() || (lastOut && millis() - lastOut < ms); }

void cue(cues::Cue c) {
  if (!ring) return;
  static int16_t* pcm[2] = {nullptr, nullptr};
  static int len[2] = {0, 0};
  if (!pcm[c]) {
    len[c] = cues::length(c, RATE);
    pcm[c] = (int16_t*)heap_caps_malloc(len[c] * 2, MALLOC_CAP_SPIRAM);
    if (!pcm[c]) return;
    cues::render(c, RATE, pcm[c]);
  }
  put(pcm[c], len[c]);
}

void alarmTone() {
  if (!ring) return;
  // two bright beeps and a rest: the "LISTEN" cue's notes, louder, twice
  cue(cues::LISTEN);
  static int16_t rest[RATE / 4] = {0};
  put(rest, RATE / 8);
  cue(cues::LISTEN);
  put(rest, RATE / 4);
}

}  // namespace spk
}  // namespace bot
