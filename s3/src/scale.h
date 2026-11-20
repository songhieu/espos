#pragma once
// RGB565 pictures made smaller for a panel with fewer pixels than the UI (board.h: the 2.8" board shows its 360x480
// UI at 2/3 on its 240x320). Each output pixel is the average of the input pixels it covers, by area:
//   4 -> 3 (3/4): out0 = (3 a + b) / 4, out1 = (b + c) / 2, out2 = (c + 3 d) / 4
//   3 -> 2 (2/3): out0 = (2 a + b) / 3, out1 = (b + 2 c) / 3
// in both directions. Plain C++ (the board, QEMU, the simulator use the same code). Native-endian RGB565 in and out.
#include <cstdint>

namespace scale {

struct C3 {
  int r, g, b;
};
inline C3 unpack(uint16_t p) { return {p >> 11, (p >> 5) & 63, p & 31}; }
inline uint16_t pack(int r, int g, int b, int div) {  // rounded
  return (uint16_t)(((r + div / 2) / div) << 11 | ((g + div / 2) / div) << 5 | ((b + div / 2) / div));
}

// One row, 4 -> 3 across: n4 input pixels (a multiple of 4) to n4 * 3 / 4, as channel sums x4 (exact, no rounding
// yet) so the vertical pass rounds once.
inline void row43(const uint16_t* s, int n4, C3* out) {
  for (int i = 0; i < n4; i += 4, s += 4, out += 3) {
    const C3 a = unpack(s[0]), b = unpack(s[1]), c = unpack(s[2]), d = unpack(s[3]);
    out[0] = {3 * a.r + b.r, 3 * a.g + b.g, 3 * a.b + b.b};
    out[1] = {2 * (b.r + c.r), 2 * (b.g + c.g), 2 * (b.b + c.b)};
    out[2] = {c.r + 3 * d.r, c.g + 3 * d.g, c.b + 3 * d.b};
  }
}

// rows4 input rows (a multiple of 4) of w (a multiple of 4) pixels, stride sw -> rows4 * 3 / 4 rows of w * 3 / 4.
// tmp: 4 * (w * 3 / 4) C3 of scratch.
inline void down43(const uint16_t* src, int w, int rows4, int sw, uint16_t* dst, int dw, C3* tmp) {
  const int ow = w * 3 / 4;
  for (int y = 0; y < rows4; y += 4) {
    for (int k = 0; k < 4; k++) row43(src + (y + k) * sw, w, tmp + k * ow);
    const C3 *a = tmp, *b = tmp + ow, *c = tmp + 2 * ow, *d = tmp + 3 * ow;
    uint16_t* o = dst + (y / 4 * 3) * dw;
    for (int x = 0; x < ow; x++) {  // sums are x4 across; x4 again down: /16
      o[x] = pack(3 * a[x].r + b[x].r, 3 * a[x].g + b[x].g, 3 * a[x].b + b[x].b, 16);
      o[dw + x] = pack(2 * (b[x].r + c[x].r), 2 * (b[x].g + c[x].g), 2 * (b[x].b + c[x].b), 16);
      o[2 * dw + x] = pack(c[x].r + 3 * d[x].r, c[x].g + 3 * d[x].g, c[x].b + 3 * d[x].b, 16);
    }
  }
}

// The same, 3 -> 2: rows3 (a multiple of 3) rows of w pixels (a multiple of 3: 360, halves of 240) -> rows3 * 2 / 3
// rows of out(w) pixels. tmp: 3 * out(w) C3.
inline int out32(int w) { return w / 3 * 2; }
// One row, 3 -> 2 across, as channel sums x3 (exact): out(w) of them.
inline void row32(const uint16_t* s, int w, C3* out) {
  for (int i = 0; i < w; i += 3, out += 2) {
    const C3 a = unpack(s[i]), b = unpack(s[i + 1]), c = unpack(s[i + 2]);
    out[0] = {2 * a.r + b.r, 2 * a.g + b.g, 2 * a.b + b.b};
    out[1] = {b.r + 2 * c.r, b.g + 2 * c.g, b.b + 2 * c.b};
  }
}
inline void down32(const uint16_t* src, int w, int rows3, int sw, uint16_t* dst, int dw, C3* tmp) {
  const int ow = out32(w);
  for (int y = 0; y < rows3; y += 3) {
    for (int k = 0; k < 3; k++) row32(src + (y + k) * sw, w, tmp + k * ow);
    const C3 *a = tmp, *b = tmp + ow, *c = tmp + 2 * ow;
    uint16_t* o = dst + (y / 3 * 2) * dw;
    for (int x = 0; x < ow; x++) {  // x3 across, x3 down: /9
      o[x] = pack(2 * a[x].r + b[x].r, 2 * a[x].g + b[x].g, 2 * a[x].b + b[x].b, 9);
      o[dw + x] = pack(b[x].r + 2 * c[x].r, b[x].g + 2 * c[x].g, b[x].b + 2 * c[x].b, 9);
    }
  }
}

}  // namespace scale
