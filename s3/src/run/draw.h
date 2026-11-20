#pragma once
// The run's pixel loops (draw.cpp, built for speed). They draw into a Canvas: some whole rows of the 320x480 screen
// (an 80-row DMA band sent straight to the panel, or the whole PSRAM buffer LVGL shows), RGB565 native order.
#include <stdint.h>

#include "media.h"

namespace run {
namespace draw {

constexpr int W = 320, H = 480;

struct Canvas {
  uint16_t* px;    // the first pixel of row y0
  int y0, y1;      // the rows it holds: [y0, y1)
  int stride = W;  // pixels from a row to the next (a wider screen's band: the 2.8" board's 360)
  uint16_t* row(int y) const { return px + (y - y0) * stride; }
};

inline uint16_t blend(uint16_t d, uint16_t s, int a) {  // src over dst, a = 0..255 (fw/pb_ref.c)
  if (a >= 255) return s;
  if (a <= 0) return d;
  const int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31, sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
  return (uint16_t)(((dr + ((sr - dr) * a + 127) / 255) << 11) | ((dg + ((sg - dg) * a + 127) / 255) << 5) |
                    (db + ((sb - db) * a + 127) / 255));
}
inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

// The smooth scale's rows across (scratch: 2 x W 16-bit pixels, given as W words); start() before each picture.
struct Smooth {
  uint32_t* rows = nullptr;
  int tag[3] = {-1, -1, -1};
  const uint16_t* src = nullptr;
  int X0 = 0;
  void start(const uint16_t* s, int x0) { src = s, X0 = x0, tag[0] = tag[1] = tag[2] = -1; }
};

// The road picture (232x240) scaled x2, its columns X0 .. X0+319 of the 464 px wide world. Plain: pixel doubling.
// Smooth: bilinear x2 (screen pixels on a source pixel copy it, the ones between average their neighbours).
void roadPlain(const Canvas& c, const uint16_t* src, int X0);
void roadSmooth(const Canvas& c, Smooth& s);
void fill(const Canvas& c, uint16_t color);
void blobInit();
void blob(const Canvas& c, double cx, double cy, double rx, double ry, double a);  // the page's blob()
void sprite(const Canvas& c, const media::Sprite* s, int X, int Y, int opa);       // opa 0..255
// The same into a dw x dh box (nearest pixel): an older picture of a thing standing in for the one not decoded yet,
// drawn at that one's size and place (run.cpp thing()).
void spriteScaled(const Canvas& c, const media::Sprite* s, int X, int Y, int dw, int dh, int opa);
void piece(const Canvas& c, const media::Ui& ui, const pbu_piece_t* p, int x, int y);
void bar(const Canvas& c, int x, int y, int w, int h, uint16_t color, int opa);    // a pill (the OS's home line)

}  // namespace draw
}  // namespace run
