#pragma GCC optimize("Os")  // a few hundred bytes of animation; the firmware must fit 3.75 MB
#include "led.h"

#include <Arduino.h>

#include <cmath>

#include "hal/board.h"
#ifndef NO_PET
#include "pet/assist.h"
#include "pet/pet.h"
#endif
#ifdef SIM
#include <chrono>
#include <thread>
void simLed(uint8_t r, uint8_t g, uint8_t b);  // sim/live.cpp draws it (a no-op without a window)
#endif

namespace led {
namespace {

volatile Mode cur = Mode::Idle;
volatile uint32_t modeAt = 0, recvAt = 0, fadeAt = 0, fadeMs = 250;
volatile float vlevel = 0;
volatile uint8_t onSetting = 2, bright = 2;
volatile bool night = false, started = false;
int pin = -1;
float env = 0, from[3], last[3];
uint8_t shown[3] = {1, 1, 1};
uint32_t prefsAt = 0;

float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float ease(float x) {
  x = clamp01(x);
  return x * x * (3 - 2 * x);
}
float wave(uint32_t t, uint32_t period) { return 0.5f - 0.5f * cosf(6.2831853f * (float)(t % period) / period); }
void set(float* c, float r, float g, float b, float k) { c[0] = r * k, c[1] = g * k, c[2] = b * k; }

void frame() {
  const uint32_t now = millis(), t = now - modeAt;
  Mode m = cur;
  if (m == Mode::Wake && t > 420) m = Mode::Listen;  // the flash settles into listening
  if (m == Mode::Error && t > 800) m = Mode::Idle;
  float c[3] = {0, 0, 0};
  const float v = vlevel;
  env += (v > env ? 0.5f : 0.12f) * (v - env);  // the voice's loudness: quick up, slow down
  switch (onSetting ? m : Mode::Idle) {
    case Mode::Wake:  // up in 70 ms, then down into the listening blue
      if (t < 70) set(c, 0.75f, 1, 1, ease(t / 70.0f));
      else {
        const float k = ease((t - 70) / 350.0f);
        c[0] = 0.75f + (0.2f - 0.75f) * k, c[1] = 1 + (0.8f - 1) * k, c[2] = 1;
        for (float& x : c) x *= 1 - 0.6f * k;
      }
      break;
    case Mode::Listen:
      set(c, 0.2f, 0.8f, 1, 0.25f + 0.75f * wave(t, 1600));
      break;
    case Mode::Think: {
      const float k = 0.5f + 0.5f * sinf(6.2831853f * (float)(t % 3000) / 3000);  // blue -> violet -> blue
      set(c, 0.15f + 0.45f * k, 0.35f - 0.15f * k, 1, 0.3f + 0.7f * wave(t, 800));
      break;
    }
    case Mode::Speak:
      set(c, 1, 0.72f, 0.22f, 0.12f + 0.88f * clamp01(env));  // warm amber (after the gamma)
      break;
    case Mode::Error: {  // two soft blinks
      const uint32_t p = t % 350;
      set(c, 1, 0.5f, 0.08f, t < 700 ? (p < 175 ? ease(p / 175.0f) : ease((350 - p) / 175.0f)) : 0);
      break;
    }
    default: {  // idle / done: dark, or the green glow of something that arrived
      const uint32_t r = recvAt ? now - recvAt : 0;
      if (recvAt && onSetting == 2 && !night) {
        if (r < 200) set(c, 0.2f, 1, 0.45f, ease(r / 200.0f));
        else if (r < 1000) set(c, 0.2f, 1, 0.45f, ease((1000 - r) / 800.0f));
        else recvAt = 0;
      }
      break;
    }
  }
  const uint32_t f = now - fadeAt;  // the crossfade from the colour shown when the mode changed
  if (f < fadeMs) {
    const float k = ease((float)f / fadeMs);
    for (int i = 0; i < 3; i++) c[i] = from[i] + (c[i] - from[i]) * k;
  }
  for (int i = 0; i < 3; i++) last[i] = c[i];
  static const float CAP[3] = {0.08f, 0.20f, 0.40f};
  const float cap = CAP[(bright < 1 ? 1 : bright > 3 ? 3 : bright) - 1] * (night ? 0.25f : 1);
  uint8_t o[3];
  for (int i = 0; i < 3; i++) o[i] = (uint8_t)lrintf(255 * powf(clamp01(c[i]), 2.2f) * cap);  // gamma
  if (o[0] == shown[0] && o[1] == shown[1] && o[2] == shown[2]) return;
  for (int i = 0; i < 3; i++) shown[i] = o[i];
#ifdef SIM
  simLed(o[0], o[1], o[2]);
#else
  rgbLedWrite(pin, o[0], o[1], o[2]);
#endif
}

#ifndef SIM
void run(void*) {
  for (;;) {
    frame();
    delay(20);
  }
}
#endif

}  // namespace

void begin() {
  if (!hal::has(hal::CAP_RGB_LED) || hal::board().misc.rgbLed < 0) return;
  pin = hal::board().misc.rgbLed;
  started = true;
#ifdef SIM
  std::thread([] {
    for (;;) {
      frame();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }).detach();
#else
  xTaskCreatePinnedToCoreWithCaps(run, "led", 2048, nullptr, 1, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

void tick() {
  if (!started || millis() - prefsAt < 1000) return;
  prefsAt = millis();
#ifndef NO_PET
  const pet::assist::Prefs p = pet::assist::prefs();
  onSetting = p.led, bright = p.ledBright;
  night = pet::quietNow();
#endif
}

void mode(Mode m) {
  if (m == cur && m != Mode::Wake && m != Mode::Error) return;
  for (int i = 0; i < 3; i++) from[i] = last[i];
  fadeMs = m == Mode::Done ? 400 : m == Mode::Wake ? 1 : 250;
  fadeAt = millis();
  modeAt = millis();
  cur = m;
}

void voice(float level) { vlevel = level; }
void receive() { recvAt = millis() | 1; }
void bump() { prefsAt = 0; }

}  // namespace led
