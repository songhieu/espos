#pragma once
// The microphone: the ES8311's ADC on I2S0 (full duplex with the speaker, src/sound.cpp), 16 kHz, read by one task
// (core 0) in 10 ms blocks of 160 mono samples (DC removed). Only one listener: src/wake/wake.cpp (the wake word,
// the recordings of the dog assistant's turns and the dev console's WAKEREC all go through it). The mic is read only
// while it is open; the status bar shows a dot then (wake::micOn()).
#include <stdint.h>

namespace mic {

// Runs on the mic task, every 10 ms while open: `pcm` holds `n` (160) samples. Keep it short (no LVGL, no I/O).
using Sink = void (*)(const int16_t* pcm, int n);

bool available();  // the board has a microphone and the codec answered
bool open(Sink sink);  // starts reading (false: no mic)
void close();
bool isOpen();

// For the console's MIC command: which I2S slot carries the mic (0 both averaged, 1 left, 2 right; picked from the
// first second's levels), the last second's RMS of each slot (dBFS), blocks lost to a late read.
struct Stats {
  int slot;
  float rmsL, rmsR;
  uint32_t blocks, lost;
};
Stats stats();

}  // namespace mic
