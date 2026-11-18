#include "sound.h"

#include <Arduino.h>

#include "desk.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "hal/board.h"
#include "touch.h"
#include "trace.h"

namespace sound {
namespace {

constexpr int RATE = 16000;
i2c_master_dev_handle_t codec;
i2s_chan_handle_t tx;
QueueHandle_t q;
bool ready = false;
int vol = 45;

enum Sound : uint8_t { CLICK, CHIME, TEST, BEEP };

bool wr(uint8_t reg, uint8_t v) {
  const uint8_t b[2] = {reg, v};
  return i2c_master_transmit(codec, b, 2, 50) == ESP_OK;
}

uint8_t rd(uint8_t reg) {
  uint8_t v = 0;
  i2c_master_transmit_receive(codec, &reg, 1, &v, 1, 50);
  return v;
}

// Same sequence as the board self-test (s3test), which played tones fine: MCLK 6.144 MHz from
// the I2S peripheral, 16 kHz, 16-bit I2S slave, DAC only.
bool codecInit() {
  bool ok = wr(0x00, 0x1F);
  delay(20);
  ok &= wr(0x00, 0x00);
  ok &= wr(0x00, 0x80);
  ok &= wr(0x01, 0x3F);
  wr(0x06, rd(0x06) & ~0x20);
  wr(0x02, (rd(0x02) & 0x07) | (2 << 5) | (1 << 3));
  wr(0x03, 0x10);
  wr(0x04, 0x10);
  wr(0x05, 0x00);
  wr(0x06, (rd(0x06) & 0xE0) | 3);
  wr(0x07, rd(0x07) & 0xC0);
  wr(0x08, 0xFF);
  wr(0x00, rd(0x00) & 0xBF);
  wr(0x09, 0x0C);
  wr(0x0A, 0x0C);
  wr(0x0D, 0x01);
  wr(0x0E, 0x02);
  wr(0x12, 0x00);
  wr(0x13, 0x10);
  wr(0x1C, 0x6A);
  wr(0x37, 0x08);
  const uint8_t id1 = rd(0xFD), id2 = rd(0xFE);
  return ok && id1 == 0x83 && id2 == 0x11;
}

void applyVolume() {
  // DAC volume register: 0xBF = 0 dB, 0.5 dB a step. 0 % mutes; 1..100 % = -30 dB..+12 dB (the
  // board's self-test played clearly at +12 dB on the kit speaker).
  wr(0x32, vol <= 0 ? 0 : 0x83 + vol * (0xD7 - 0x83) / 100);
}

void write(const int16_t* stereo, int frames) {
  size_t done;
  i2s_channel_write(tx, stereo, frames * 4, &done, 200);
}

void play(Sound s) {
  static int16_t buf[2 * 512];
  if (s == CLICK) {
    // 9 ms: a damped 2.8 kHz ping with a little noise for the "tick".
    const int n = RATE * 9 / 1000;
    uint32_t seed = 12345;
    for (int i = 0; i < n; i++) {
      const float env = expf(-i / 22.0f);
      seed = seed * 1103515245 + 12345;
      const float noise = ((int)(seed >> 16 & 0x7fff) - 16384) / 16384.0f;
      const int16_t v = (int16_t)((sinf(2 * PI * 2800 * i / RATE) * 0.7f + noise * 0.3f) * env * 14000);
      buf[i * 2] = buf[i * 2 + 1] = v;
    }
    write(buf, n);
  } else if (s == TEST) {
    // C E G C, then G C: about 1.5 s, loud enough to tell the speaker works.
    const float notes[][2] = {{523.3f, 180}, {659.3f, 180}, {784.0f, 180}, {1046.5f, 360}, {784.0f, 180}, {1046.5f, 420}};
    for (auto& nt : notes) {
      const int n = RATE * (int)nt[1] / 1000;
      for (int done = 0; done < n; done += 512) {
        const int m = min(512, n - done);
        for (int i = 0; i < m; i++) {
          const int k = done + i;
          const float env = min(1.0f, k / 120.0f) * min(1.0f, (n - k) / 400.0f);
          const float ph = 2 * PI * nt[0] * k / RATE;
          buf[i * 2] = buf[i * 2 + 1] = (int16_t)((sinf(ph) * 0.8f + sinf(2 * ph) * 0.2f) * env * 18000);
        }
        write(buf, m);
      }
    }
  } else if (s == BEEP) {
    // Alarm: four short 2 kHz beeps, never quieter than 70 %.
    if (vol < 70) wr(0x32, 0x83 + 70 * (0xD7 - 0x83) / 100);
    for (int b = 0; b < 4; b++) {
      const int n = RATE * 90 / 1000, gap = RATE * 70 / 1000;
      for (int done = 0; done < n + gap; done += 512) {
        const int m = min(512, n + gap - done);
        for (int i = 0; i < m; i++) {
          const int k = done + i;
          const float env = k < n ? min(1.0f, k / 40.0f) * min(1.0f, (n - k) / 40.0f) : 0;
          buf[i * 2] = buf[i * 2 + 1] = (int16_t)((sinf(2 * PI * 2000 * k / RATE) * 0.8f + sinf(2 * PI * 4000 * k / RATE) * 0.2f) * env * 20000);
        }
        write(buf, m);
      }
    }
    applyVolume();
  } else {
    for (float hz : {1046.5f, 1568.0f}) {
      const int n = RATE * 120 / 1000;
      for (int done = 0; done < n; done += 512) {
        const int m = min(512, n - done);
        for (int i = 0; i < m; i++) {
          const int k = done + i;
          const float env = min(1.0f, k / 80.0f) * expf(-k / 900.0f);
          buf[i * 2] = buf[i * 2 + 1] = (int16_t)(sinf(2 * PI * hz * k / RATE) * env * 16000);
        }
        write(buf, m);
      }
    }
  }
}

// ------------------------------------------------------------------ streaming (PawBoop's voice lines)
constexpr uint8_t WAKE = 0x10;  // queue item: a stream was started or stopped
portMUX_TYPE sLock = portMUX_INITIALIZER_UNLOCKED;
Pull sPull = nullptr, sNext = nullptr;
void *sCtx = nullptr, *sNextCtx = nullptr;
volatile bool sSwitch = false;  // sNext takes over (nullptr = stop)
int clickAt = -1;               // a click being mixed into the stream (sample index)
int16_t *clickWave, *sMono, *sStereo;  // in PSRAM: internal RAM is scarce

void makeClick() {  // the same 9 ms tick as play(CLICK)
  clickWave = (int16_t*)heap_caps_malloc((RATE * 9 / 1000 + 320 + 640) * 2, MALLOC_CAP_SPIRAM);
  if (!clickWave) return;  // no streams then (stream() checks sMono)
  sMono = clickWave + RATE * 9 / 1000, sStereo = sMono + 320;
  uint32_t seed = 12345;
  for (int i = 0; i < RATE * 9 / 1000; i++) {
    const float env = expf(-i / 22.0f);
    seed = seed * 1103515245 + 12345;
    const float noise = ((int)(seed >> 16 & 0x7fff) - 16384) / 16384.0f;
    clickWave[i] = (int16_t)((sinf(2 * PI * 2800 * i / RATE) * 0.7f + noise * 0.3f) * env * 14000);
  }
}

void streamOut(int n) {
  for (int i = 0; i < n; i++) {
    int v = sMono[i];
    if (clickAt >= 0) {
      v += clickWave[clickAt++];
      if (clickAt >= RATE * 9 / 1000) clickAt = -1;
      v = constrain(v, -32768, 32767);
    }
    sStereo[2 * i] = sStereo[2 * i + 1] = (int16_t)v;
  }
  write(sStereo, n);
}

// One 20 ms piece of the stream. A switch first fades the old stream out over 10 ms.
void pump() {
  portENTER_CRITICAL(&sLock);
  Pull old = sPull;
  void* oldCtx = sCtx;
  const bool sw = sSwitch;
  if (sw) {
    sPull = sNext, sCtx = sNextCtx;
    sSwitch = false;
  }
  Pull p = sPull;
  void* c = sCtx;
  portEXIT_CRITICAL(&sLock);
  if (sw && old) {
    const int n = old(sMono, 160, oldCtx);
    for (int i = 0; i < n; i++) sMono[i] = (int16_t)(sMono[i] * (160 - i) / 160);
    if (n > 0) streamOut(n);
  }
  if (!p) return;
  const int n = p(sMono, 320, c);
  if (n > 0) return streamOut(n);
  portENTER_CRITICAL(&sLock);
  if (sPull == p && !sSwitch) sPull = nullptr;  // it ended
  portEXIT_CRITICAL(&sLock);
}

void task(void*) {
  makeClick();
  for (;;) {
    uint8_t s;
    const bool live = sPull || sSwitch;
    if (xQueueReceive(q, &s, live ? 0 : portMAX_DELAY) == pdTRUE && s != WAKE) {
      if (s == CLICK && live) {
        clickAt = 0;
      } else {
        if (live) {  // a chime, the test or an alarm stops the stream
          portENTER_CRITICAL(&sLock);
          sPull = nullptr;
          sSwitch = false;
          portEXIT_CRITICAL(&sLock);
        }
        play((Sound)s);
        continue;
      }
    }
    if (sPull || sSwitch) pump();
  }
}

bool allowed() {
  return ready && vol > 0 && !desk::settingInt("s3.dnd", 0);
}

}  // namespace

// The board's ES8311 (its descriptor's audio: hal/audio.h) on the board's I2C bus, fed by I2S.
bool begin() {
  if (!hal::has(hal::CAP_AUDIO_OUT) || hal::board().audio.codec != hal::Codec::ES8311) {
    trace::add("sound: no codec");  // QEMU: ready stays false, every sound is skipped
    return false;
  }
  const hal::AudioDesc& a = hal::board().audio;
  if (a.amp >= 0) {
    pinMode(a.amp, OUTPUT);
    digitalWrite(a.amp, a.ampOn);  // the speaker amplifier on
  }
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = a.addr;
  dc.scl_speed_hz = 100000;
  if (i2c_master_bus_add_device(touch::i2cBus(), &dc, &codec) != ESP_OK) return false;
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;  // silence between sounds
  chan.dma_desc_num = 3;   // short sounds: small DMA buffers (internal RAM is scarce)
  chan.dma_frame_num = 160;
  if (i2s_new_channel(&chan, &tx, nullptr) != ESP_OK) return false;
  i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = (gpio_num_t)a.mclk, .bclk = (gpio_num_t)a.bclk, .ws = (gpio_num_t)a.ws,
                   .dout = (gpio_num_t)a.dout, .din = I2S_GPIO_UNUSED, .invert_flags = {false, false, false}},
  };
  cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
  if (i2s_channel_init_std_mode(tx, &cfg) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) return false;
  ready = codecInit();
  applyVolume();
  q = xQueueCreate(4, 1);
  xTaskCreatePinnedToCore(task, "sound", 3072, nullptr, 4, nullptr, 0);
  trace::add("sound: codec %s", ready ? "ok" : "not answering");
  return ready;
}

void setVolume(int percent) {
  vol = constrain(percent, 0, 100);
  if (ready) applyVolume();
}

void click() {
  if (!allowed() || !desk::settingInt("s3.click", 1)) return;
  const uint8_t s = CLICK;
  xQueueSend(q, &s, 0);
}

void test() {
  if (!ready) return;
  trace::add("sound test at volume %d%%", vol);
  const uint8_t s = TEST;
  xQueueSend(q, &s, 0);
}

void alarmBeep() {
  if (!ready) return;
  const uint8_t s = BEEP;
  xQueueSend(q, &s, 0);
}

void chime() {
  if (!allowed()) return;
  const uint8_t s = CHIME;
  xQueueSend(q, &s, 0);
}

void stream(Pull pull, void* ctx) {
  if (!ready || !q || !sMono) return;
  portENTER_CRITICAL(&sLock);
  sNext = pull, sNextCtx = ctx;
  sSwitch = true;
  portEXIT_CRITICAL(&sLock);
  const uint8_t s = WAKE;
  xQueueSend(q, &s, 0);
}

void stopStream() { stream(nullptr, nullptr); }

bool streaming() { return sPull || sSwitch; }

}  // namespace sound
