#pragma once
// EspOS Bot's speaker: the I2S amp on DOUT 7 / BCLK 15 / LRCK 16 (I2S1), 16 kHz mono 16-bit. The owner, 2026-11-20:
// "cứ auto có loa": it always plays as if a speaker is fitted (bot.voice 1, the default); with none fitted nothing is
// heard and nothing breaks. The dog's streamed voice goes through a ring in PSRAM, played by its own task; cues and
// the alarm's tones are written into the same ring.
#include <stdint.h>

#include "../cues.h"

namespace bot {
namespace spk {

constexpr int RATE = 16000;
bool begin();
void volume(int pct);                  // bot.volume 0..100
// The streamed voice: bytes as they come (16-bit LE mono; an odd byte is kept for the next call). false: stopped.
bool pushPcm(const uint8_t* p, uint32_t n);
void endStream();                      // the stream is over: what is in the ring still plays
void stop();                           // silence now (a press, a new turn)
bool playing();                        // the ring still has sound
bool outputSince(uint32_t ms);         // anything played in the last ms (the mic's echo gate)
void cue(cues::Cue c);
void alarmTone();                      // one second of the alarm's pattern
void enable(bool on);                  // tests: the I2S channel to the amp on/off
void pull(int16_t* out, int n);        // the simulator's audio callback: the next n samples (at the volume)

}  // namespace spk
}  // namespace bot
