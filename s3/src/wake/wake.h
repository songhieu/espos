#pragma once
// The listener: the one user of the microphone (src/mic.cpp). It keeps the last 2 s of sound in a ring (PSRAM), runs
// the "Gâu" wake word (wake_fe.c + wake_rt.c, the model from the package dev.espos.wake.gau) when the person turned it
// on (s3.assist wake / wake_word / wake_sens, docs: s3/wake/FIRMWARE.md), and records the dog assistant's turns
// (push to talk, or after the wake word with the words just before it). Nothing it hears leaves the device; the
// assistant (src/pet/talk.cpp) sends a turn's recording once the person spoke to the dog.
#include <stdint.h>

namespace wake {

constexpr const char* PKG = "dev.espos.wake.gau";

void begin();
void tick();  // UI loop: settings, the model, the mic on/off, the wake word's events

// The wake word.
enum Mode : uint8_t { OFF = 0, CHARGING = 1, ALWAYS = 2 };
Mode mode();               // the setting (s3.assist wake)
bool modelInstalled();     // the package is in
bool listening();          // the wake word is listening now (the status bar's dot)
bool micOn();              // the mic is open for anything (listening, a turn, a test)
void setPaused(bool p);    // the Control Center's mic tile: not listening until tapped again or a restart
bool paused();
// Detected: the callback runs on the UI loop with the mic clock (sample index) of the moment it fired.
void onWake(void (*cb)(uint32_t atSample));
// Hold the wake word while a turn runs (or the dog speaks): it neither fires nor counts.
void hold(bool h);

// Recording a turn: from `fromSample` (the mic clock; at most 2 s back) for at most `maxMs`. The buffer starts with
// 44 free bytes for a WAV header (wav() fills it). UI loop only.
uint32_t now();                                       // the mic clock (samples since it first opened)
bool captureStart(uint32_t fromSample, int maxMs);    // opens the mic if needed
void captureStop();
bool capturing();                                     // still recording (not stopped, not full)
int captureMs();                                      // recorded so far
uint8_t* wav(uint32_t& bytes);                        // stops it and hands it over as a WAV: heap_caps_free() it
void captureFree();                                   // stops it and drops it

// Levels for a meter and the end of speech (dBFS): the last 10 ms (smoothed), and the noise floor (the lowest of 2 s).
float levelDb();
float floorDb();
// Speech activity counted by the mic task in every 10 ms block (talk.cpp's question: a slow or stalled UI loop, a
// game drawing at 6 pictures a second or a 2 s TLS handshake, loses nothing): since the last take, the ms above the
// floor + relDb (and above minDb), the ms in all, and the loudest level. vadSet applies from the next block.
struct Vad {
  uint32_t loudMs = 0, ms = 0, oursMs = 0;  // oursMs: muted for our own sound (vadMute), not counted
  float peakDb = -90;
};
void vadSet(float relDb, float minDb);
Vad vadTake();
// The speech start, counted in the mic task block by block (loud 10 ms +10, quiet -5, never under 0): the highest
// score since vadRestart (talk.cpp: speech has begun at 240 ms).
void vadRestart();
uint32_t vadSpeechMs();
void vadMute(uint32_t forMs);  // the next forMs of the mic are our own sound (talk.cpp's ack): never speech

// Dev console: WAKE (state and timings), WAKEREC n (n s of the raw mic, sent back as "WAKEREC <bytes>" + PCM).
bool console(const char* cmd);

}  // namespace wake
