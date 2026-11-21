#include "mic.h"

#include <Arduino.h>

#include <cmath>

#include "driver/i2s_std.h"
#include "sound.h"
#include "trace.h"

namespace mic {
namespace {

constexpr int N = 160;  // 10 ms at 16 kHz
TaskHandle_t task = nullptr;
volatile Sink sink = nullptr;
volatile bool want = false, isOn = false;
volatile uint32_t lostBlocks = 0, readBlocks = 0;
int slot = -1;  // -1: not decided yet (the first second's levels decide)
double accL = 0, accR = 0;
int accN = 0;
float lastL = -96, lastR = -96;

bool IRAM_ATTR onOverflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
  lostBlocks = lostBlocks + 1;
  return false;
}

float dbOf(double sumSq, int n) { return sumSq <= 0 || n <= 0 ? -96.0f : 10.0f * log10f((float)(sumSq / n) / (32768.0f * 32768.0f)); }

#ifdef ESPOS_QEMU
// QEMU has no codec: a microphone that hears silence (and a faint hiss) at the codec's pace, so the wake word's
// listener, a call's recording (WAKEFIRE) and the talk button run there as on the board (memory tests).
void run(void*) {
  int16_t mono[N];
  uint32_t seed = 1;
  TickType_t t = xTaskGetTickCount();
  for (;;) {
    if (!want) {
      if (isOn) isOn = false, trace::add("mic: off (%u blocks, QEMU)", (unsigned)readBlocks);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      t = xTaskGetTickCount();
      continue;
    }
    if (!isOn) isOn = true, trace::add("mic: on (QEMU: silence)");
    for (int i = 0; i < N; i++) seed = seed * 1103515245u + 12345u, mono[i] = (int16_t)((int)(seed >> 16 & 31) - 16);
    readBlocks = readBlocks + 1;
    const Sink s = sink;
    if (s) s(mono, N);
    vTaskDelayUntil(&t, pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
  }
}
#else
void run(void*) {
  i2s_chan_handle_t rx = (i2s_chan_handle_t)sound::micChannel();
  i2s_event_callbacks_t cb = {};
  cb.on_recv_q_ovf = onOverflow;
  i2s_channel_register_event_callback(rx, &cb, nullptr);
  int16_t st[2 * N], mono[N];
  float dcX = 0, dcY = 0;
  int settle = 0;
  for (;;) {
    if (!want) {
      if (isOn) {
        i2s_channel_disable(rx);
        isOn = false;
        trace::add("mic: off (%u blocks, %u lost)", (unsigned)readBlocks, (unsigned)lostBlocks);
      }
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (!isOn) {
      if (i2s_channel_enable(rx) != ESP_OK) {
        want = false;
        trace::add("mic: could not start");
        continue;
      }
      isOn = true;
      dcX = dcY = 0;
      settle = 3;  // the codec's first blocks after a start are not settled
      trace::add("mic: on");
    }
    size_t got = 0;
    if (i2s_channel_read(rx, st, sizeof st, &got, 100) != ESP_OK || got != sizeof st) continue;
    if (settle > 0) {
      settle--;
      continue;
    }
    readBlocks = readBlocks + 1;
    double sL = 0, sR = 0;
    for (int i = 0; i < N; i++) sL += (double)st[2 * i] * st[2 * i], sR += (double)st[2 * i + 1] * st[2 * i + 1];
    accL += sL, accR += sR, accN += N;
    if (accN >= 16000) {  // a second
      lastL = dbOf(accL, accN), lastR = dbOf(accR, accN);
      if (slot < 0) slot = lastR < lastL - 13 ? 1 : lastL < lastR - 13 ? 2 : 0;  // a silent slot: use the other
      accL = accR = 0, accN = 0;
    }
    const int sl = slot < 0 ? 0 : slot;
    for (int i = 0; i < N; i++) {
      const float x = sl == 1 ? st[2 * i] : sl == 2 ? st[2 * i + 1] : 0.5f * (st[2 * i] + st[2 * i + 1]);
      dcY = x - dcX + 0.995f * dcY;  // DC blocker
      dcX = x;
      const float y = dcY < -32768.0f ? -32768.0f : dcY > 32767.0f ? 32767.0f : dcY;
      mono[i] = (int16_t)lrintf(y);
    }
    const Sink s = sink;
    if (s) s(mono, N);
  }
}
#endif

}  // namespace

#ifdef ESPOS_QEMU
bool available() { return true; }
#else
bool available() { return sound::micChannel() != nullptr; }
#endif

bool open(Sink s) {
  if (!available()) return false;
  sink = s;
  want = true;
  if (!task) {
    // Core 0 beside the sound task, above it and the network worker: a block every 10 ms, 40 ms of DMA to spare.
    xTaskCreatePinnedToCoreWithCaps(run, "mic", 4096, nullptr, 6, &task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  } else {
    xTaskNotifyGive(task);
  }
  return true;
}

void close() {
  want = false;
  sink = nullptr;
}

bool isOpen() { return want && isOn; }

Stats stats() { return {slot, lastL, lastR, readBlocks, lostBlocks}; }

}  // namespace mic
