#pragma once
// Biscuit's pictures (biscuit_M.pbs): JPEG + 1-bit mask -> colours + soft-edge alpha, drawn with the blob
// shadow on any RGB565 buffer, at full size or half size. Carried over from the game designer's fw/pb_ref.c
// (decode, unmask, draw_dog, span); the JPEG decoder is TJpgDec with RGB565 output (tjpgd_pb.c).
#include <stdint.h>

#include "formats.h"

#ifdef SIM
#include <chrono>
#endif

namespace pet {

// A drawing target: RGB565 pixels, `stride` pixels a row; only rows [y0, y1) and columns [0, w) are touched.
struct Canvas {
  uint16_t* px;
  int w, h, stride;
  int y0, y1;
};

// One decoded picture: its box on the stage scaled by 1/2^shift, colours and alpha (0..255) of the box.
struct Pic {
  const pbs2_frame_t* f = nullptr;
  int shift = 0;
  int x = 0, y = 0, w = 0, h = 0;
  uint16_t* rgb = nullptr;
  uint8_t* a = nullptr;
  int cap = 0;  // pixels the buffers hold
};

// Microseconds for measures: the board's clock; the Mac's real clock in the simulator (whose millis() is
// simulated time). Mac numbers say nothing about the board.
#ifdef SIM
inline uint32_t nowUs() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#else
uint32_t nowUs();
#endif

namespace dog {

constexpr int STAGE_W = 320, STAGE_ROWS = 292;  // of the 304 canvas rows, 292 are the stage

// The picture ready to draw. Decoded ahead in the background on the board when want() asked for it in
// time, else decoded now. Valid until the next get() of another picture plus one (three slots).
const Pic* get(const pbs2_frame_t* f, int shift);
void want(const pbs2_frame_t* f, int shift);  // decode the next picture in the background
void release();                               // free every buffer (nothing on screen uses Biscuit)

// The blob shadow, then the picture, with the stage's origin at (ox, oy) of the canvas.
void shadow(Canvas& c, const pbs2_frame_t* f, int ox, int oy, int shift);
void draw(Canvas& c, const Pic& p, int ox, int oy);
// Rows a picture touches (box and shadow), in stage px at 1:1.
void extent(const pbs2_frame_t* f, int& y0, int& y1);
// The highest and lowest drawn row (stage px) of pictures i0 .. i0+n-1 of a clip: from the masks alone.
void span(const pbs2_clip_t* c, int i0, int n, int& top, int& bot);

inline uint16_t blend(uint16_t d, uint16_t s, int a) {
  if (a >= 255) return s;
  if (a <= 0) return d;
  const int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31, sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
  return (uint16_t)(((dr + ((sr - dr) * a + 127) / 255) << 11) | ((dg + ((sg - dg) * a + 127) / 255) << 5) |
                    (db + ((sb - db) * a + 127) / 255));
}
inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

struct Stats {
  uint32_t pictures = 0, decUs = 0, decMaxUs = 0, ahead = 0, late = 0, failed = 0;
};
Stats& stats();  // since the last reset (the game logs it every 10 s)

}  // namespace dog
}  // namespace pet
