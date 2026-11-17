#include "HostCanvas.h"

#include <EpdFont.h>
#include <FontDecompressor.h>
#include <Utf8.h>
#include <builtinFonts/notosans_12_bold.h>
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_regular.h>
#include <builtinFonts/notosans_16_bold.h>
#include <builtinFonts/notosans_16_regular.h>
#include <builtinFonts/notosans_18_bold.h>
#include <builtinFonts/notosans_18_regular.h>
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_regular.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "inkdesk/fonts/ink_24_bold.h"
#include "inkdesk/fonts/ink_30_digits.h"
#include "inkdesk/fonts/ink_44_digits.h"
#include "inkdesk/fonts/ink_60_digits.h"
#include "inkdesk/DeclFonts.h"
#include "inkdesk/fonts/ink_serif_36.h"

using inkdesk::decl::Col;
using inkdesk::decl::Family;
using inkdesk::decl::Font;

namespace {

struct HostFont {
  const EpdFontData* data;
  const EpdFontData* regular;  // the device takes the baseline and line height from the regular face
  Family family;
  bool bold;
  int px;  // point size at the converter's 150 dpi
};

// The same fonts as the device's table (src/inkdesk/DeclFonts.h).
#define HOST_FONT(id, data, regular, family, bold, px) {&data, &regular, Family::family, bold, px},
const HostFont kFonts[] = {INKDESK_DECL_FONTS(HOST_FONT)};
#undef HOST_FONT

FontDecompressor& decompressor() {
  static FontDecompressor d;
  static bool ready = d.init();
  (void)ready;
  return d;
}

}  // namespace

HostCanvas::HostCanvas(const int w, const int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h, 0) {
  for (size_t i = 0; i < sizeof(kFonts) / sizeof(kFonts[0]); i++) {
    Font f;
    f.id = static_cast<int>(i);
    f.family = kFonts[i].family;
    f.bold = kFonts[i].bold;
    f.px = kFonts[i].px;
    f.lineH = kFonts[i].regular->advanceY;
    if (f.family == Family::Digits) {
      // Same placement as DashDraw digitTop/digitHeight: the top and height of the "0" glyph.
      EpdFont font(kFonts[i].data);
      const EpdGlyph* g = font.getGlyph('0');
      if (g) {
        f.inkTop = kFonts[i].regular->ascender - g->top;
        f.inkH = g->height;
      }
    }
    fonts_.push_back(f);
  }
}

HostCanvas::~HostCanvas() = default;

void HostCanvas::set(const int x, const int y, const bool on) {
  touched_++;
  if (x < 0 || y < 0 || x >= w_ || y >= h_) {
    outside_++;
    return;
  }
  px_[static_cast<size_t>(y) * w_ + x] = on ? 1 : 0;
}

void HostCanvas::put(const int x, const int y, const Col c) {
  switch (c) {
    case Col::Ink:
      set(x, y, true);
      break;
    case Col::Paper:
      set(x, y, false);
      break;
    case Col::Gray:
      set(x, y, ((x + y) & 1) == 0);
      break;
    default:
      break;
  }
}

bool HostCanvas::inRound(const int x, const int y, const int rx, const int ry, const int rw, const int rh,
                         int r) const {
  r = std::min(r, std::min(rw, rh) / 2);
  if (r <= 0) return true;
  const int cx = x < rx + r ? rx + r : (x >= rx + rw - r ? rx + rw - r - 1 : x);
  const int cy = y < ry + r ? ry + r : (y >= ry + rh - r ? ry + rh - r - 1 : y);
  const double dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= (r - 0.5) * (r - 0.5) + r;
}

void HostCanvas::fillRect(const int x, const int y, const int w, const int h, const int radius, const Col c) {
  for (int yy = std::max(y, 0); yy < std::min(y + h, h_); yy++) {
    for (int xx = std::max(x, 0); xx < std::min(x + w, w_); xx++) {
      if (inRound(xx, yy, x, y, w, h, radius)) put(xx, yy, c);
    }
  }
}

void HostCanvas::strokeRect(const int x, const int y, const int w, const int h, const int radius, const int lw,
                            const Col c) {
  for (int yy = std::max(y, 0); yy < std::min(y + h, h_); yy++) {
    for (int xx = std::max(x, 0); xx < std::min(x + w, w_); xx++) {
      const bool outer = inRound(xx, yy, x, y, w, h, radius);
      const bool inner = w > 2 * lw && h > 2 * lw &&
                         inRound(xx, yy, x + lw, y + lw, w - 2 * lw, h - 2 * lw, std::max(0, radius - lw)) &&
                         xx >= x + lw && xx < x + w - lw && yy >= y + lw && yy < y + h - lw;
      if (outer && !inner) put(xx, yy, c);
    }
  }
}

void HostCanvas::line(const int x1, const int y1, const int x2, const int y2, const int lw, const Col c) {
  const int steps = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
  const int a = (lw - 1) / 2, b = lw / 2;
  for (int i = 0; i <= steps; i++) {
    const int x = steps ? x1 + (x2 - x1) * i / steps : x1;
    const int y = steps ? y1 + (y2 - y1) * i / steps : y1;
    for (int dy = -a; dy <= b; dy++) {
      for (int dx = -a; dx <= b; dx++) put(x + dx, y + dy, c);
    }
  }
}

void HostCanvas::fillCircle(const int cx, const int cy, const int r, const Col c) {
  if (r <= 0) return;
  fillRect(cx - r, cy - r, r * 2, r * 2, r, c);
}

void HostCanvas::strokeCircle(const int cx, const int cy, const int r, const int lw, const Col c) {
  if (r <= 0) return;
  strokeRect(cx - r, cy - r, r * 2, r * 2, r, lw, c);
}

void HostCanvas::invert(const int x, const int y, const int w, const int h) {
  for (int yy = std::max(0, y); yy < std::min(h_, y + h); yy++) {
    for (int xx = std::max(0, x); xx < std::min(w_, x + w); xx++) px_[static_cast<size_t>(yy) * w_ + xx] ^= 1;
  }
}

// A port of DashDraw::drawIcon (src/inkdesk/DashDraw.cpp) on this canvas.
void HostCanvas::icon(const char* name, const int cx, const int cy, const int s, const Col c) {
  if (c == Col::Paper) fillRect(cx - s / 2, cy - s / 2, s, s, 0, Col::Paper);
  const int lw = s >= 40 ? 3 : 2;
  auto circle = [&](int x, int y, int r, bool black) {
    if (r > 0) fillRect(x - r, y - r, r * 2, r * 2, r, black ? Col::Ink : Col::Paper);
  };
  auto ring = [&](int x, int y, int r, int w) {
    circle(x, y, r, true);
    circle(x, y, r - w, false);
  };
  auto cloudShape = [&](int x, int y, int inset, bool black) {
    const int baseW = s * 80 / 100 - inset * 2, baseH = s * 34 / 100 - inset * 2;
    const int baseX = x - s * 40 / 100 + inset, baseY = y + inset;
    if (baseW > 0 && baseH > 0) fillRect(baseX, baseY, baseW, baseH, std::max(1, baseH / 2), black ? Col::Ink : Col::Paper);
    circle(x - s * 10 / 100, y + s * 2 / 100, s * 22 / 100 - inset, black);
    circle(x + s * 16 / 100, y + s * 8 / 100, s * 15 / 100 - inset, black);
  };
  auto cloud = [&](int x, int y) {
    cloudShape(x, y, 0, true);
    cloudShape(x, y, lw, false);
  };
  auto sun = [&](int x, int y, int sz) {
    const int rad = sz * 20 / 100;
    const int slw = sz >= 40 ? 3 : 2;
    ring(x, y, rad, slw);
    static const int dx[8] = {0, 7, 10, 7, 0, -7, -10, -7};
    static const int dy[8] = {-10, -7, 0, 7, 10, 7, 0, -7};
    for (int i = 0; i < 8; i++) {
      line(x + dx[i] * rad * 14 / 100, y + dy[i] * rad * 14 / 100, x + dx[i] * rad * 20 / 100,
           y + dy[i] * rad * 20 / 100, slw, Col::Ink);
    }
  };
  if (!strcmp(name, "sun")) {
    sun(cx, cy, s);
  } else if (!strcmp(name, "cloudsun")) {
    sun(cx - s * 18 / 100, cy - s * 16 / 100, s * 70 / 100);
    const int s2 = s * 85 / 100, x = cx + s * 6 / 100, y = cy - s * 4 / 100;
    const int lw2 = s2 >= 40 ? 3 : 2;
    auto shape = [&](int inset, bool black) {
      const int baseW = s2 * 80 / 100 - inset * 2, baseH = s2 * 34 / 100 - inset * 2;
      if (baseW > 0 && baseH > 0)
        fillRect(x - s2 * 40 / 100 + inset, y + inset, baseW, baseH, std::max(1, baseH / 2), black ? Col::Ink : Col::Paper);
      circle(x - s2 * 10 / 100, y + s2 * 2 / 100, s2 * 22 / 100 - inset, black);
      circle(x + s2 * 16 / 100, y + s2 * 8 / 100, s2 * 15 / 100 - inset, black);
    };
    shape(0, true);
    shape(lw2, false);
  } else if (!strcmp(name, "moon")) {
    const int rad = s * 28 / 100;
    circle(cx, cy, rad, true);
    circle(cx + rad * 55 / 100, cy - rad * 35 / 100, rad, false);
  } else if (!strcmp(name, "fog")) {
    for (int i = -1; i <= 1; i++) line(cx - s * 35 / 100, cy + i * s / 6, cx + s * 35 / 100, cy + i * s / 6, lw, Col::Ink);
  } else if (!strcmp(name, "battery")) {
    strokeRect(cx - s * 40 / 100, cy - s * 20 / 100, s * 74 / 100, s * 40 / 100, 3, lw, Col::Ink);
    fillRect(cx + s * 34 / 100, cy - s * 8 / 100, s * 6 / 100 + 1, s * 16 / 100, 0, Col::Ink);
  } else {
    const bool precip = !strcmp(name, "rain") || !strcmp(name, "storm") || !strcmp(name, "snow");
    const int cloudY = precip ? cy - s * 18 / 100 : cy - s * 8 / 100;
    cloud(cx, cloudY);
    const int top = cloudY + s * 34 / 100 + s / 14;
    if (!strcmp(name, "rain")) {
      for (int i = -1; i <= 1; i++) {
        const int x = cx + i * s / 5;
        line(x, top, x - s / 12, top + s / 6, lw, Col::Ink);
      }
    } else if (!strcmp(name, "storm")) {
      line(cx + s / 12, top, cx - s / 20, top + s / 10, lw, Col::Ink);
      line(cx - s / 20, top + s / 10, cx + s / 16, top + s / 10, lw, Col::Ink);
      line(cx + s / 16, top + s / 10, cx - s / 12, top + s / 4, lw, Col::Ink);
    } else if (!strcmp(name, "snow")) {
      for (int i = -1; i <= 1; i++) circle(cx + i * s / 5, top + s / 12, lw + 1, true);
    }
  }
  if (c == Col::Paper) invert(cx - s / 2, cy - s / 2, s, s);
}

// BMP: 1, 8 or 24 bit, uncompressed; fitted inside the box (never enlarged), ordered dither to 1 bit.
bool HostCanvas::image(const std::string& path, const int x, const int y, const int w, const int h) {
  std::ifstream in(path, std::ios::binary);
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return false;
  auto u32 = [&](size_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | static_cast<uint32_t>(b[o + 3]) << 24; };
  auto u16 = [&](size_t o) { return static_cast<uint32_t>(b[o] | b[o + 1] << 8); };
  const uint32_t off = u32(10), hdr = u32(14);
  const int bw = static_cast<int>(u32(18));
  int bh = static_cast<int>(u32(22));
  const bool topDown = bh < 0;
  bh = std::abs(bh);
  const uint32_t bpp = u16(28), comp = u32(30);
  if (bw <= 0 || bh <= 0 || comp != 0 || (bpp != 1 && bpp != 8 && bpp != 24)) return false;
  const size_t stride = ((static_cast<size_t>(bw) * bpp + 31) / 32) * 4;
  if (off + stride * bh > b.size()) return false;
  const size_t pal = 14 + hdr;
  const double sc = std::min({1.0, static_cast<double>(w) / bw, static_cast<double>(h) / bh});  // like drawBitmap: never enlarged
  const int dw = std::max(1, static_cast<int>(bw * sc)), dh = std::max(1, static_cast<int>(bh * sc));
  const int x0 = x + (w - dw) / 2, y0 = y + (h - dh) / 2;
  static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int yy = 0; yy < dh; yy++) {
    const int sy = std::min(bh - 1, static_cast<int>(yy / sc));
    const size_t row = off + stride * (topDown ? sy : bh - 1 - sy);
    for (int xx = 0; xx < dw; xx++) {
      const int sx = std::min(bw - 1, static_cast<int>(xx / sc));
      int lum;
      if (bpp == 24) {
        const size_t p = row + sx * 3;
        lum = (b[p] * 29 + b[p + 1] * 150 + b[p + 2] * 77) >> 8;
      } else {
        const int idx = bpp == 8 ? b[row + sx] : (b[row + sx / 8] >> (7 - sx % 8)) & 1;
        const size_t p = pal + idx * 4;
        lum = p + 2 < b.size() ? (b[p] * 29 + b[p + 1] * 150 + b[p + 2] * 77) >> 8 : 0;
      }
      const int px = x0 + xx, py = y0 + yy;
      set(px, py, lum * 16 / 256 <= bayer[py & 3][px & 3]);
    }
  }
  return true;
}

int HostCanvas::textWidth(const Font& f, const char* utf8) const {
  EpdFont font(kFonts[f.id].data);
  int w = 0, h = 0;
  font.getTextDimensions(utf8, &w, &h);
  return w;
}

// The same placement as GfxRenderer::drawText: baseline at the regular face's ascender,
// advances + kerning snapped as one step; any non-zero 2-bit value is ink in black and white.
void HostCanvas::text(const Font& f, const int x, const int y, const char* utf8, const Col c) {
  texts_.push_back(utf8);
  const HostFont& hf = kFonts[f.id];
  EpdFont font(hf.data);
  const int baseline = y + hf.regular->ascender;
  int penX = x;
  int32_t prevAdvance = 0;
  uint32_t prevCp = 0;
  const char* cursor = utf8;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const unsigned char**>(&cursor)))) {
    if (utf8IsCombiningMark(cp)) continue;
    cp = font.applyLigatures(cp, cursor);
    if (prevCp != 0) penX += fp4::toPixel(prevAdvance + font.getKerning(prevCp, cp));
    EpdGlyph solid;
    const EpdGlyph* g = font.getGlyphMetrics(cp, solid);
    if (!g) continue;
    prevAdvance = g->advanceX;
    prevCp = cp;
    const int gx = penX + g->left, gy = baseline - g->top;
    if (g == &solid) {
      fillRect(gx, gy, g->width, g->height, 0, c);
      continue;
    }
    const uint8_t* bmp = nullptr;
    const uint32_t index = static_cast<uint32_t>(g - hf.data->glyph);
    bmp = hf.data->groups ? decompressor().getBitmap(hf.data, g, index) : hf.data->bitmap + g->dataOffset;
    if (!bmp) continue;
    for (int yy = 0; yy < g->height; yy++) {
      for (int xx = 0; xx < g->width; xx++) {
        const int pos = yy * g->width + xx;
        const int v = hf.data->is2Bit ? (bmp[pos >> 2] >> (6 - (pos & 3) * 2)) & 3 : (bmp[pos >> 3] >> (7 - (pos & 7))) & 1;
        if (v) put(gx + xx, gy + yy, c == Col::Paper ? Col::Paper : Col::Ink);
      }
    }
  }
}

bool HostCanvas::writePng(const std::string& path) const {
  std::vector<unsigned char> raw;
  raw.reserve(static_cast<size_t>(h_) * (w_ + 1));
  for (int y = 0; y < h_; y++) {
    raw.push_back(0);
    for (int x = 0; x < w_; x++) raw.push_back(px_[static_cast<size_t>(y) * w_ + x] ? 0 : 255);
  }
  uLongf zlen = compressBound(raw.size());
  std::vector<unsigned char> z(zlen);
  if (compress2(z.data(), &zlen, raw.data(), raw.size(), 9) != Z_OK) return false;
  z.resize(zlen);
  std::ofstream out(path, std::ios::binary);
  auto chunk = [&](const char* type, const std::vector<unsigned char>& data) {
    const uint32_t len = static_cast<uint32_t>(data.size());
    const unsigned char l[4] = {static_cast<unsigned char>(len >> 24), static_cast<unsigned char>(len >> 16),
                                static_cast<unsigned char>(len >> 8), static_cast<unsigned char>(len)};
    out.write(reinterpret_cast<const char*>(l), 4);
    out.write(type, 4);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    uLong crc = crc32(0, reinterpret_cast<const Bytef*>(type), 4);
    crc = crc32(crc, data.data(), data.size());
    const unsigned char cb[4] = {static_cast<unsigned char>(crc >> 24), static_cast<unsigned char>(crc >> 16),
                                 static_cast<unsigned char>(crc >> 8), static_cast<unsigned char>(crc)};
    out.write(reinterpret_cast<const char*>(cb), 4);
  };
  out.write("\x89PNG\r\n\x1a\n", 8);
  std::vector<unsigned char> ihdr = {static_cast<unsigned char>(w_ >> 24), static_cast<unsigned char>(w_ >> 16),
                                     static_cast<unsigned char>(w_ >> 8),  static_cast<unsigned char>(w_),
                                     static_cast<unsigned char>(h_ >> 24), static_cast<unsigned char>(h_ >> 16),
                                     static_cast<unsigned char>(h_ >> 8),  static_cast<unsigned char>(h_),
                                     8, 0, 0, 0, 0};
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  return static_cast<bool>(out);
}

bool JsonData::value(const char* name, std::string& out) const {
  JsonVariantConst v = root_["values"][name];
  if (v.isNull()) return false;
  if (v.is<const char*>()) out = v.as<const char*>();
  else if (v.is<long long>()) out = std::to_string(v.as<long long>());
  else if (v.is<bool>()) out = v.as<bool>() ? "1" : "";
  else return false;
  return true;
}

int JsonData::count(const char* list) const { return static_cast<int>(root_["lists"][list].size()); }

bool JsonData::field(const char* list, const int index, const char* name, std::string& out) const {
  JsonVariantConst v = root_["lists"][list][index][name];
  if (v.isNull()) return false;
  if (v.is<const char*>()) out = v.as<const char*>();
  else if (v.is<long long>()) out = std::to_string(v.as<long long>());
  else return false;
  return true;
}
