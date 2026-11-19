#include "wake_fe.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f
#define NC (WFE_FFT / 2)  // complex FFT size: 256

static float hz2mel(float f) { return 2595.0f * log10f(1.0f + f / 700.0f); }
static float mel2hz(float m) { return 700.0f * (powf(10.0f, m / 2595.0f) - 1.0f); }

void wfe_reset(wfe_t* s) { memset(s->hist, 0, sizeof(s->hist)); }

void wfe_init(wfe_t* s, float floor_, float off, float scale) {
  memset(s, 0, sizeof(*s));
  s->floor_ = floor_ > 0 ? floor_ : WFE_FLOOR;
  s->off = floor_ > 0 ? off : WFE_OFF;
  s->scale = floor_ > 0 ? scale : WFE_SCALE;
  for (int i = 0; i < WFE_WIN; i++) s->win[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (float)WFE_WIN);
  for (int i = 0; i < NC / 2; i++) {
    s->tw[2 * i] = cosf(2.0f * PI_F * (float)i / (float)NC);
    s->tw[2 * i + 1] = -sinf(2.0f * PI_F * (float)i / (float)NC);
  }
  for (int k = 0; k < NC; k++) {
    s->rtw[2 * k] = cosf(2.0f * PI_F * (float)k / (float)WFE_FFT);
    s->rtw[2 * k + 1] = -sinf(2.0f * PI_F * (float)k / (float)WFE_FFT);
  }
  for (int i = 0; i < NC; i++) {
    int r = 0;
    for (int b = 0; b < 8; b++)
      if (i & (1 << b)) r |= 1 << (7 - b);
    s->brev[i] = (uint8_t)r;
  }
  // Triangular mel bands, 80..7600 Hz, on FFT bin frequencies k * 16000 / 512.
  const float lo = hz2mel(80.0f), hi = hz2mel(7600.0f);
  float edge[WFE_BANDS + 2];
  for (int i = 0; i < WFE_BANDS + 2; i++) edge[i] = mel2hz(lo + (hi - lo) * (float)i / (float)(WFE_BANDS + 1));
  int wo = 0;
  for (int b = 0; b < WFE_BANDS; b++) {
    const float f0 = edge[b], f1 = edge[b + 1], f2 = edge[b + 2];
    int first = -1, n = 0;
    for (int k = 0; k < WFE_BINS; k++) {
      const float f = (float)k * (float)WFE_RATE / (float)WFE_FFT;
      float w = 0;
      if (f > f0 && f < f2) w = f <= f1 ? (f - f0) / (f1 - f0) : (f2 - f) / (f2 - f1);
      if (w > 0) {
        if (first < 0) first = k;
        n = k - first + 1;
      }
    }
    if (first < 0) {  // narrower than a bin: take the nearest bin
      first = (int)(f1 * WFE_FFT / WFE_RATE + 0.5f);
      n = 1;
    }
    s->band_lo[b] = (int16_t)first;
    s->band_n[b] = (int16_t)n;
    s->wofs[b] = (int16_t)wo;
    for (int j = 0; j < n; j++) {
      const float f = (float)(first + j) * (float)WFE_RATE / (float)WFE_FFT;
      float w = 0;
      if (f > f0 && f < f2) w = f <= f1 ? (f - f0) / (f1 - f0) : (f2 - f) / (f2 - f1);
      s->w[wo + j] = n == 1 && w <= 0 ? 1.0f : w;
    }
    wo += n;
  }
}

// In-place radix-2 complex FFT of NC points (interleaved re, im).
static void fft256(wfe_t* s, float* x) {
  for (int i = 0; i < NC; i++) {
    const int j = s->brev[i];
    if (j > i) {
      float t = x[2 * i]; x[2 * i] = x[2 * j]; x[2 * j] = t;
      t = x[2 * i + 1]; x[2 * i + 1] = x[2 * j + 1]; x[2 * j + 1] = t;
    }
  }
  for (int len = 2; len <= NC; len <<= 1) {
    const int half = len >> 1, step = NC / len;
    for (int i = 0; i < NC; i += len) {
      for (int k = 0; k < half; k++) {
        const float wr = s->tw[2 * k * step], wi = s->tw[2 * k * step + 1];
        float* a = x + 2 * (i + k);
        float* b = x + 2 * (i + k + half);
        const float tr = b[0] * wr - b[1] * wi, ti = b[0] * wi + b[1] * wr;
        b[0] = a[0] - tr; b[1] = a[1] - ti;
        a[0] += tr; a[1] += ti;
      }
    }
  }
}

void wfe_frame_f(wfe_t* s, const int16_t* hop, float* out) {
  float* z = s->buf;
  // Window the 480 samples (320 old + 160 new) into z as 256 complex pairs: z[m] = x[2m] + i x[2m+1], zero padded.
  const int nh = WFE_WIN - WFE_HOP;
  for (int i = 0; i < WFE_FFT; i++) {
    float v = 0;
    if (i < nh) v = (float)s->hist[i] * s->win[i];
    else if (i < WFE_WIN) v = (float)hop[i - nh] * s->win[i];
    z[i] = v;
  }
  memmove(s->hist, s->hist + WFE_HOP, (size_t)(nh - WFE_HOP) * sizeof(int16_t));
  memcpy(s->hist + nh - WFE_HOP, hop, WFE_HOP * sizeof(int16_t));
  fft256(s, z);
  // Split into the real FFT's power spectrum: X[k] = Ze + W^k Zo, k = 0..256.
  float pw[WFE_BINS];
  pw[0] = (z[0] + z[1]) * (z[0] + z[1]);
  pw[NC] = (z[0] - z[1]) * (z[0] - z[1]);
  for (int k = 1; k < NC; k++) {
    const float ar = z[2 * k], ai = z[2 * k + 1];
    const float br = z[2 * (NC - k)], bi = -z[2 * (NC - k) + 1];  // conj(Z[M-k])
    const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
    // Zo = (A - B) / 2i  ->  ((ai - bi) / 2, -(ar - br) / 2)
    const float orr = 0.5f * (ai - bi), oi = -0.5f * (ar - br);
    const float wr = s->rtw[2 * k], wi = s->rtw[2 * k + 1];
    const float xr = er + orr * wr - oi * wi, xi = ei + orr * wi + oi * wr;
    pw[k] = xr * xr + xi * xi;
  }
  for (int b = 0; b < WFE_BANDS; b++) {
    const float* w = s->w + s->wofs[b];
    const float* p = pw + s->band_lo[b];
    float e = 0;
    for (int j = 0; j < s->band_n[b]; j++) e += w[j] * p[j];
    out[b] = logf(e + s->floor_);
  }
}

void wfe_frame(wfe_t* s, const int16_t* hop, int8_t* out) {
  float e[WFE_BANDS];
  wfe_frame_f(s, hop, e);
  for (int b = 0; b < WFE_BANDS; b++) {
    float q = (e[b] - s->off) * s->scale;
    q = q < 0 ? q - 0.5f : q + 0.5f;
    int v = (int)q;
    out[b] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
  }
}

int wfe_run(wfe_t* s, const int16_t* pcm, int n, int8_t* out) {
  const int frames = n / WFE_HOP;
  for (int f = 0; f < frames; f++) wfe_frame(s, pcm + f * WFE_HOP, out + f * WFE_BANDS);
  return frames;
}
