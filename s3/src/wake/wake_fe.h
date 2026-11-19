// Wake-word front end: 16 kHz 16-bit mono -> one frame of 40 log-mel energies (int8) every 10 ms.
// The SAME file is compiled into the training tools on the Mac (ctypes, host/libwake.dylib) and into the firmware,
// so the model sees identical numbers in training and on the board.
//
//   window 30 ms (480 samples, Hann) every 10 ms (160 samples), 512-point real FFT, power spectrum,
//   40 triangular mel bands 80..7600 Hz (HTK mel), e = ln(band + floor), q = clamp(round((e - off) * scale)).
// floor/off/scale come from the model file (so a later model can change them); the shape above is fixed.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WFE_RATE 16000
#define WFE_HOP 160
#define WFE_WIN 480
#define WFE_FFT 512
#define WFE_BINS (WFE_FFT / 2 + 1)
#define WFE_BANDS 40

typedef struct {
  float floor_, off, scale;          // quantisation (from the model header)
  int16_t hist[WFE_WIN - WFE_HOP];   // the previous 320 samples
  float win[WFE_WIN];                // Hann window
  float tw[WFE_FFT / 4 * 2];         // cos/sin for the 256-point complex FFT (128 pairs)
  float rtw[WFE_FFT / 2 * 2];        // cos/sin for the real-FFT split (256 pairs)
  uint8_t brev[WFE_FFT / 2];         // bit reversal for 256 points
  int16_t band_lo[WFE_BANDS];        // first FFT bin of each band
  int16_t band_n[WFE_BANDS];         // number of bins in the band
  int16_t wofs[WFE_BANDS];           // offset of the band's weights in w[]
  float w[WFE_BINS * 2];             // triangular weights, packed
  float buf[WFE_FFT];                // work: 256 complex values
} wfe_t;

// floor_ <= 0: defaults (WFE_FLOOR, WFE_OFF, WFE_SCALE).
#define WFE_FLOOR 400.0f
#define WFE_OFF 19.0f
#define WFE_SCALE 9.5f
void wfe_init(wfe_t* s, float floor_, float off, float scale);
void wfe_reset(wfe_t* s);  // forget the audio history (keeps the tables)
// Push exactly WFE_HOP new samples, get WFE_BANDS features.
void wfe_frame(wfe_t* s, const int16_t* hop, int8_t* out);
// The float log energies before quantisation (for calibration on the host).
void wfe_frame_f(wfe_t* s, const int16_t* hop, float* out);
// Whole buffer: n samples -> floor(n / WFE_HOP) frames written to out (frames x WFE_BANDS). Returns the frame count.
// Starts from the current history (call wfe_reset first for a fresh clip).
int wfe_run(wfe_t* s, const int16_t* pcm, int n, int8_t* out);

#ifdef __cplusplus
}
#endif
