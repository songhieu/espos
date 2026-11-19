#pragma once
// The board's RGB LED (WS2812, hal misc.rgbLed; CAP_RGB_LED): the dog assistant's state, and a soft glow when
// something arrives (a change from the web, a notification). Smooth: ~50 frames a second on its own task (core 0,
// low priority), gamma-corrected, eased crossfades, capped brightness (WS2812s are blinding), very dim in quiet
// hours. Settings in s3.assist: "led" 0 off, 1 only with the dog, 2 the dog and arrivals (default); "led_bright"
// 1-3 (8 / 20 / 40 % of full, default 2). In the simulator the LED is a dot in the window's frame.
#include <stdint.h>

namespace led {

enum class Mode : uint8_t {
  Idle,    // dark (an arrival glow may pass)
  Wake,    // "Gâu gâu" was heard: a quick soft white-cyan flash, then Listen
  Listen,  // listening for the question (and the follow-up): slow cyan-blue breathing
  Think,   // waiting for the server: a faster pulse drifting blue to violet
  Speak,   // the dog talks: warm amber following the loudness of its voice (voice())
  Error,   // didn't catch it, or an error: two soft orange blinks
  Done,    // the turn ended: fades out
};

void begin();               // once, after desk::begin
void tick();                // UI loop: reads the settings (cheap)
void mode(Mode m);          // any task
void voice(float level);    // 0..1, the loudness of the voice playing now (the sound task)
void receive();             // something arrived: one soft green glow (when "led" is 2)
void bump();                // the settings changed: read them again

}  // namespace led
