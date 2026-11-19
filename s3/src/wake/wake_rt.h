// Wake-word runtime: a small int8 streaming keyword spotter (no TFLite, no allocations after load).
//
// Model file (.ewm, "EspOS wake model", little-endian, written by train/export.py):
//   header (ewm_hdr_t), then the tensors in this order, each starting on a 4-byte boundary:
//     stem   int8 W[ch][k0*40] (frame-major), float m[ch], float b[ch]
//     block  int8 DW[ch][k], float m[ch], float b[ch], int8 PW[ch][ch], float m[ch], float b[ch], float r
//     head   int8 W[hc][ch], float m[hc], float b[hc]
//     out    int8 W[nout][hc], float m[nout], float b[nout]
// Activations are uint8 (after ReLU, zero point 0). Each layer: acc (int32) -> y = acc * m + b (+ residual * r)
// -> ReLU -> round -> clamp 0..255. The output layer gives logits (float).
//
// The front end (fe/wake_fe.c) makes one int8 frame every 10 ms; the network steps every `stride` frames
// (30 ms) and returns one probability per output ("gau" = any gâu, "gau2" = gâu gâu).
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EWM_MAGIC 0x314D5745u  // "EWM1"
#define EWM_MAX_BLOCKS 8
#define EWM_MAX_OUT 4

typedef struct {
  uint32_t magic;
  uint16_t version, hdr_size;
  float fe_floor, fe_off, fe_scale;  // front-end quantisation (wfe_init)
  uint8_t bands, k0, stride, blocks;
  uint8_t k, hc, nout, smooth;       // smooth: steps averaged before the threshold
  uint16_t ch, refractory;           // refractory: steps ignored after a detection
  uint8_t dil[EWM_MAX_BLOCKS];
  float thr[EWM_MAX_OUT];            // default thresholds per output
  char names[EWM_MAX_OUT][12];
  uint32_t total_size;               // whole file, for checking
  uint32_t crc32;                    // of the bytes after the header
} ewm_hdr_t;

typedef struct {
  const int8_t* w;
  const float* m;
  const float* b;
} ewm_layer_t;

typedef struct {
  const ewm_hdr_t* h;
  ewm_layer_t stem, dw[EWM_MAX_BLOCKS], pw[EWM_MAX_BLOCKS], head, out;
  float r[EWM_MAX_BLOCKS];
  // state (in `mem`)
  int8_t* fring;      // k0 frames x bands
  uint8_t* ring[EWM_MAX_BLOCKS];  // per block: ((k-1)*dil+1) x ch inputs
  int rlen[EWM_MAX_BLOCKS], rpos[EWM_MAX_BLOCKS];
  uint8_t *a, *z, *o, *y;  // scratch vectors (ch, ch, ch, hc)
  uint32_t frames;     // frames pushed since reset
  float hist[EWM_MAX_OUT][8];  // last probabilities, for smoothing
  int hpos, quiet;     // quiet: refractory steps left
  float prob[EWM_MAX_OUT];     // last smoothed probabilities
} ewm_t;

// Bytes of state for this model (allocate them, e.g. in PSRAM, and pass to ewm_init).
uint32_t ewm_state_size(const void* model);
// model: the file's bytes (kept by the caller, may be memory-mapped flash). Returns 0 or a negative error:
// -1 bad magic/version, -2 size, -3 crc, -4 shape out of range.
int ewm_init(ewm_t* m, const void* model, uint32_t size, void* mem);
void ewm_reset(ewm_t* m);
// Push one front-end frame. Returns -1 when no network step ran, otherwise a bit mask of the outputs whose
// smoothed probability crossed thr[] (0 = a step ran, nothing fired). prob[] holds the smoothed values.
int ewm_push(ewm_t* m, const int8_t* frame, const float* thr);
// Raw logits of the last step (for tests).
extern float ewm_last_logits[EWM_MAX_OUT];

#ifdef __cplusplus
}
#endif
