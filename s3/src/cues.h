#pragma once
// The dog assistant's two little sounds (sound::blip / sound::blipEnd), the same on the board and in the simulator
// (and wake/tools render them: scratchpad cues/render.py mirrors this). Owner, 2026-11-20: the old ones "nghe như
// tiếng lỗi" (the falling two notes are the classic error/cancel shape). Now, as the smart speakers do:
//   LISTEN "I'm listening": two rising warm notes, G5 then C6, 190 ms (when the dog's voice ack is off or missing);
//   SENT   "got it": one soft note bending up a fourth (A5 to D6), 110 ms, quieter. Never falling.
// Each note: a sine with a touch of its octave, a raised-cosine attack, an exponential fall, the last 15 ms faded.
#include <math.h>
#include <stdint.h>

namespace cues {

enum Cue : uint8_t { LISTEN, SENT };

struct Note {
  float startMs, durMs, f0, f1, glideMs, attackMs, tauMs, amp;
};

inline int notes(Cue c, const Note*& out) {
  static const Note listen[2] = {{0, 75, 784.0f, 784.0f, 0, 6, 40, 7000}, {55, 135, 1046.5f, 1046.5f, 0, 6, 60, 7000}};
  static const Note sent[1] = {{0, 110, 880.0f, 1174.7f, 60, 8, 45, 5500}};
  out = c == LISTEN ? listen : sent;
  return c == LISTEN ? 2 : 1;
}

// The cue's length in samples at `rate`.
inline int length(Cue c, int rate) {
  const Note* n;
  const int k = notes(c, n);
  float end = 0;
  for (int i = 0; i < k; i++) end = fmaxf(end, n[i].startMs + n[i].durMs);
  return (int)(end * rate / 1000);
}

// The whole cue into out (length(c, rate) samples, mono). Phases are accumulated, so the glide stays smooth.
inline void render(Cue c, int rate, int16_t* out) {
  const Note* n;
  const int k = notes(c, n);
  const int len = length(c, rate);
  for (int i = 0; i < len; i++) out[i] = 0;
  for (int j = 0; j < k; j++) {
    const Note& t = n[j];
    const int k0 = (int)(t.startMs * rate / 1000), kn = (int)(t.durMs * rate / 1000);
    float ph = 0;
    for (int s = 0; s < kn && k0 + s < len; s++) {
      const float ms = s * 1000.0f / rate;
      const float f = t.glideMs > 0 ? t.f0 + (t.f1 - t.f0) * fminf(1.0f, ms / t.glideMs) : t.f0;
      ph += 2 * (float)M_PI * f / rate;
      const float a = ms < t.attackMs ? 0.5f - 0.5f * cosf((float)M_PI * ms / t.attackMs) : expf(-(ms - t.attackMs) / t.tauMs);
      const float tail = fminf(1.0f, (t.durMs - ms) / 15.0f);
      const float v = out[k0 + s] + (sinf(ph) + 0.12f * sinf(2 * ph)) * a * tail * t.amp;
      out[k0 + s] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, v));
    }
  }
}

}  // namespace cues
