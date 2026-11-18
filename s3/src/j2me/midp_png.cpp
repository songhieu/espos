// PNG for MIDP's Image.createImage (midp_draw.h): every colour type (grey, RGB, palette, grey + alpha, RGBA), bit
// depths 1-16, tRNS transparency, Adam7 interlacing. The zlib data goes through the VM's inflater.
#include <algorithm>
#include <cstring>

#include "midp_draw.h"

namespace j2me {
bool inflateRaw(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen);  // vm_zip.cpp
namespace draw {

namespace {

inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

inline int paeth(int a, int b, int c) {
  const int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Undo the filters of one image (w x h) whose rows are `stride` bytes, bpp bytes a pixel (at least 1).
bool unfilter(uint8_t* data, int h, size_t stride, int bpp) {
  uint8_t* prev = nullptr;
  for (int y = 0; y < h; y++) {
    uint8_t* row = data + y * (stride + 1);
    const int f = row[0];
    uint8_t* p = row + 1;
    for (size_t i = 0; i < stride; i++) {
      const int a = i >= (size_t)bpp ? p[i - bpp] : 0, b = prev ? prev[i] : 0, c = prev && i >= (size_t)bpp ? prev[i - bpp] : 0;
      switch (f) {
        case 0: break;
        case 1: p[i] = (uint8_t)(p[i] + a); break;
        case 2: p[i] = (uint8_t)(p[i] + b); break;
        case 3: p[i] = (uint8_t)(p[i] + ((a + b) >> 1)); break;
        case 4: p[i] = (uint8_t)(p[i] + paeth(a, b, c)); break;
        default: return false;
      }
    }
    prev = p;
  }
  return true;
}

struct Info {
  int w, h, depth, type;
  uint32_t pal[256];
  int palN = 0;
  bool trnsGrey = false, trnsRgb = false;
  int tGrey = 0, tR = 0, tG = 0, tB = 0;
  int channels() const { return type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : 4; }
};

// Sample n (0-based) of a row at depth d (1, 2, 4, 8 or 16 -> the high byte).
inline int sample(const uint8_t* row, int n, int d) {
  switch (d) {
    case 8: return row[n];
    case 16: return row[n * 2];
    case 4: return (row[n >> 1] >> ((1 - (n & 1)) * 4)) & 15;
    case 2: return (row[n >> 2] >> ((3 - (n & 3)) * 2)) & 3;
    default: return (row[n >> 3] >> (7 - (n & 7))) & 1;
  }
}
inline int sample16(const uint8_t* row, int n) { return row[n * 2] << 8 | row[n * 2 + 1]; }

uint32_t pixel(const Info& in, const uint8_t* row, int x) {
  const int d = in.depth;
  const int scale = d == 1 ? 255 : d == 2 ? 85 : d == 4 ? 17 : 1;
  switch (in.type) {
    case 0: {
      const int g = sample(row, x, d) * scale;
      const bool clear = in.trnsGrey && (d == 16 ? sample16(row, x) : sample(row, x, d)) == in.tGrey;
      return (clear ? 0u : 0xFF000000u) | (uint32_t)g << 16 | g << 8 | g;
    }
    case 2: {
      const int r = sample(row, x * 3, d), g = sample(row, x * 3 + 1, d), b = sample(row, x * 3 + 2, d);
      bool clear = false;
      if (in.trnsRgb)
        clear = d == 16 ? sample16(row, x * 3) == in.tR && sample16(row, x * 3 + 1) == in.tG && sample16(row, x * 3 + 2) == in.tB
                        : r == in.tR && g == in.tG && b == in.tB;
      return (clear ? 0u : 0xFF000000u) | (uint32_t)r << 16 | g << 8 | b;
    }
    case 3: {
      const int i = sample(row, x, d);
      return i < in.palN ? in.pal[i] : 0xFF000000u;
    }
    case 4: {
      const int g = sample(row, x * 2, d), a = sample(row, x * 2 + 1, d);
      return (uint32_t)a << 24 | (uint32_t)g << 16 | g << 8 | g;
    }
    default: {
      const int r = sample(row, x * 4, d), g = sample(row, x * 4 + 1, d), b = sample(row, x * 4 + 2, d),
                a = sample(row, x * 4 + 3, d);
      return (uint32_t)a << 24 | (uint32_t)r << 16 | g << 8 | b;
    }
  }
}

}  // namespace

bool decodePng(const uint8_t* data, size_t len, int& w, int& h, PVec<uint32_t>& out) {
  static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (len < 33 || memcmp(data, SIG, 8)) return false;
  Info in{};
  int interlace = 0;
  bool haveHdr = false;
  PVec<uint8_t> z;  // the IDAT data (PSRAM, as every big buffer here)
  for (size_t p = 8; p + 12 <= len;) {
    const uint32_t n = be32(data + p);
    const uint8_t* type = data + p + 4;
    const uint8_t* body = data + p + 8;
    if (n > len - p - 12) return false;
    if (!memcmp(type, "IHDR", 4) && n >= 13) {
      in.w = (int)be32(body), in.h = (int)be32(body + 4), in.depth = body[8], in.type = body[9];
      interlace = body[12];
      haveHdr = true;
    } else if (!memcmp(type, "PLTE", 4)) {
      in.palN = (int)std::min<uint32_t>(256, n / 3);
      for (int i = 0; i < in.palN; i++) in.pal[i] = 0xFF000000u | body[i * 3] << 16 | body[i * 3 + 1] << 8 | body[i * 3 + 2];
    } else if (!memcmp(type, "tRNS", 4)) {
      if (in.type == 3) {
        for (uint32_t i = 0; i < n && i < (uint32_t)in.palN; i++) in.pal[i] = (in.pal[i] & 0xFFFFFF) | (uint32_t)body[i] << 24;
      } else if (in.type == 0 && n >= 2) {
        in.trnsGrey = true, in.tGrey = body[0] << 8 | body[1];
      } else if (in.type == 2 && n >= 6) {
        in.trnsRgb = true;
        in.tR = body[0] << 8 | body[1], in.tG = body[2] << 8 | body[3], in.tB = body[4] << 8 | body[5];
      }
    } else if (!memcmp(type, "IDAT", 4)) {
      z.insert(z.end(), body, body + n);
    } else if (!memcmp(type, "IEND", 4)) {
      break;
    }
    p += 12 + n;
  }
  if (!haveHdr || in.w <= 0 || in.h <= 0 || in.w > 4096 || in.h > 4096 || z.size() < 2) return false;
  if (in.depth != 1 && in.depth != 2 && in.depth != 4 && in.depth != 8 && in.depth != 16) return false;
  if (in.type == 1 || in.type == 5 || in.type > 6) return false;
  // tRNS values compare at the image's depth (16-bit ones in full; 8-bit and less: the low byte holds it)
  if (in.depth <= 8) in.tGrey &= 0xFF, in.tR &= 0xFF, in.tG &= 0xFF, in.tB &= 0xFF;
  const int bitsPx = in.channels() * in.depth, bpp = std::max(1, bitsPx / 8);
  // the passes (Adam7: 7 sub-images; else one)
  static const int X0[7] = {0, 4, 0, 2, 0, 1, 0}, Y0[7] = {0, 0, 4, 0, 2, 0, 1}, DX[7] = {8, 8, 4, 4, 2, 2, 1},
                   DY[7] = {8, 8, 8, 4, 4, 2, 2};
  const int passes = interlace ? 7 : 1;
  size_t total = 0;
  int pw[7], ph[7];
  for (int k = 0; k < passes; k++) {
    pw[k] = interlace ? (in.w - X0[k] + DX[k] - 1) / DX[k] : in.w;
    ph[k] = interlace ? (in.h - Y0[k] + DY[k] - 1) / DY[k] : in.h;
    if (pw[k] > 0 && ph[k] > 0) total += (size_t)ph[k] * (((size_t)pw[k] * bitsPx + 7) / 8 + 1);
  }
  PVec<uint8_t> raw(total);
  if (!inflateRaw(z.data() + 2, z.size() - 2, raw.data(), raw.size())) return false;  // zlib: 2 header bytes
  out.assign((size_t)in.w * in.h, 0);
  size_t at = 0;
  for (int k = 0; k < passes; k++) {
    if (pw[k] <= 0 || ph[k] <= 0) continue;
    const size_t stride = ((size_t)pw[k] * bitsPx + 7) / 8;
    if (!unfilter(raw.data() + at, ph[k], stride, bpp)) return false;
    for (int y = 0; y < ph[k]; y++) {
      const uint8_t* row = raw.data() + at + y * (stride + 1) + 1;
      const int oy = interlace ? Y0[k] + y * DY[k] : y;
      for (int x = 0; x < pw[k]; x++) {
        const int ox = interlace ? X0[k] + x * DX[k] : x;
        out[(size_t)oy * in.w + ox] = pixel(in, row, x);
      }
    }
    at += (size_t)ph[k] * (stride + 1);
  }
  w = in.w, h = in.h;
  return true;
}

}  // namespace draw
}  // namespace j2me
