// Run the board's wake-word code (fe/wake_fe.c + rt/wake_rt.c) on the Mac.
//   wakecli MODEL.ewm FILE.raw|FILE.wav [thr0 thr1]   16 kHz mono 16-bit (a .wav header is skipped)
//   wakecli MODEL.ewm --feats FILE.i8                  int8 frames (N x 40) straight into the network
// Prints one line per detection: time, output, probability; and the time spent per second of audio.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wake_fe.h"
#include "wake_rt.h"

static void* slurp(const char* p, long* n) {
  FILE* f = fopen(p, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void* b = malloc((size_t)*n);
  if (fread(b, 1, (size_t)*n, f) != (size_t)*n) *n = 0;
  fclose(f);
  return b;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: wakecli MODEL.ewm FILE.wav|--feats FILE.i8 [thr0 thr1]\n");
    return 2;
  }
  long msize, n;
  void* model = slurp(argv[1], &msize);
  if (!model) return 1;
  ewm_t m;
  void* mem = calloc(1, ewm_state_size(model));
  int e = ewm_init(&m, model, (uint32_t)msize, mem);
  if (e) {
    fprintf(stderr, "model error %d\n", e);
    return 1;
  }
  const ewm_hdr_t* h = m.h;
  const int feats = !strcmp(argv[2], "--feats");
  const char* file = feats ? argv[3] : argv[2];
  const int ta = feats ? 4 : 3;
  float thr[EWM_MAX_OUT];
  for (int i = 0; i < EWM_MAX_OUT; i++) thr[i] = argc > ta + i ? (float)atof(argv[ta + i]) : ewm_thr(&m, i);
  uint8_t* data = slurp(file, &n);
  if (!data) return 1;
  printf("model %ld B, state %u B, ch %d, blocks %d, outputs:", msize, ewm_state_size(model), h->ch, h->blocks);
  for (int o = 0; o < h->nout; o++) printf(" %s>=%.2f", ewm_name(&m, o), thr[o]);
  printf("\n");
  clock_t t0 = clock();
  long frames = 0;
  int8_t fr[WFE_BANDS];
  if (feats) {
    for (long f = 0; f + 40 <= n; f += 40, frames++) {
      int r = ewm_push(&m, (const int8_t*)data + f, thr);
      for (int o = 0; r > 0 && o < h->nout; o++)
        if (r & (1 << o)) printf("%8.2f s  %-6s %.3f\n", frames * 0.01, ewm_name(&m, o), m.prob[o]);
    }
  } else {
    static wfe_t fe;
    wfe_init(&fe, h->fe_floor, h->fe_off, h->fe_scale);
    long off = (n > 44 && !memcmp(data, "RIFF", 4)) ? 44 : 0;
    const int16_t* pcm = (const int16_t*)(data + off);
    const long ns = (n - off) / 2;
    for (long s = 0; s + WFE_HOP <= ns; s += WFE_HOP, frames++) {
      wfe_frame(&fe, pcm + s, fr);
      int r = ewm_push(&m, fr, thr);
      for (int o = 0; r > 0 && o < h->nout; o++)
        if (r & (1 << o)) printf("%8.2f s  %-6s %.3f\n", frames * 0.01, ewm_name(&m, o), m.prob[o]);
    }
  }
  const double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
  printf("%.1f s of audio, %.3f ms CPU per second of audio (Mac)\n", frames * 0.01, frames ? sec * 1000 / (frames * 0.01) : 0);
  return 0;
}
