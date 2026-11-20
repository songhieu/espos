#pragma once
// EspOS Bot's ear: the I2S MEMS mic (WS 4, SCK 5, DIN 6; 24 bits in 32-bit slots, left) read by its own task in
// 10 ms blocks, the "Gâu gâu" wake word (the S3's own front end and network, src/wake/wake_fe.c + wake_rt.c, with the
// model of the package dev.espos.wake.gau built in), the levels for "is someone speaking", and the recording of a
// question (a WAV in PSRAM).
#include <stdint.h>

#include <string>

namespace bot {
namespace ear {

bool begin();               // false: no mic answered (the BOOT button still talks... with nothing to hear)
bool ok();
void setWake(int sens);     // bot.wake: 0 off, 1 low, 2 normal, 3 high
bool takeWake();            // "Gâu gâu" since the last call
void hold(bool on);         // no wake word (a turn is on)

float level();              // dBFS, smoothed
float floorDb();            // the room's floor (the lowest of the last 2 s)
void vadRestart();
uint32_t speechMs();        // speech so far (loud +10 ms, quiet -5 ms) since vadRestart: its highest value
uint32_t quietMs();         // since the last loud block (since vadRestart when none)

// A question: from backMs before now, at most maxMs in all.
bool captureStart(uint32_t backMs, uint32_t maxMs);
void captureStop();
bool captureFull();
uint32_t captureMs();
// The recording as a WAV (16 kHz mono 16-bit), its quiet tail cut to keepTailMs after the last loud block.
const uint8_t* wav(uint32_t& bytes, uint32_t keepTailMs);
void captureFree();
const uint8_t* lastFire(uint32_t& bytes);  // the 2 s before the last "Gâu gâu" as a WAV (none yet: null)

std::string stats();        // the console's MIC line
void feed(const int16_t* pcm);  // the simulator: 160 samples (10 ms) from the computer's microphone
void setGain(int q8);       // the sample as 16 bits (the 24 / 256) * q8 / 256 (default 4096 = 16x: Xiaozhi's >> 12)
int gain();

}  // namespace ear
}  // namespace bot
