#pragma once
// The MIDP layer's pixel work (com.espos.j2me.Native's drawing natives), plain C++ on ARGB pixels (one uint32_t a
// pixel, row after row; drawn pixels are opaque). Every call takes the target, its size and a clip rectangle already
// inside the target. Used by midp.cpp; testable on the host.
#include <cstdint>
#include <string>
#include <vector>

#include "jvm.h"  // PVec: vectors in PSRAM on the board (internal RAM cannot hold a game's pictures)

namespace j2me {
namespace draw {

struct Target {
  uint32_t* px;
  int w, h;
  int cx, cy, cw, ch;  // the clip, inside the target
};

void fill(const Target& t, int x, int y, int w, int h, uint32_t rgb);
void line(const Target& t, int x1, int y1, int x2, int y2, uint32_t rgb, bool dotted);
// MIDP arcs: the box x, y, w, h; angles in degrees from 3 o'clock, counter-clockwise, relative to the box (45 is the
// box's corner). fill: a pie; else the outline (the box is then w + 1 by h + 1, as MIDP says).
void arc(const Target& t, int x, int y, int w, int h, int start, int sweep, uint32_t rgb, bool fill);
void triangle(const Target& t, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t rgb);
void roundRect(const Target& t, int x, int y, int w, int h, int aw, int ah, uint32_t rgb, bool fill);
// The region sx, sy, w, h of src (sw x sh) with a Sprite transform (0..7) at dx, dy (its top-left after the
// transform). alpha: blend by the source's alpha byte (0 skipped); else copied opaque.
void blit(const Target& t, const uint32_t* src, int sw, int sh, int sx, int sy, int w, int h, int transform, int dx,
          int dy, bool alpha);
// Graphics.drawRGB: rows from src + offset, scanlength apart (may be negative), w x h at x, y.
void rgb(const Target& t, const uint32_t* src, int srcLen, int offset, int scan, int x, int y, int w, int h, bool alpha);

// ---- text: fonts 0..5 (small, medium, large; +3 bold), glyphs prepared from the firmware's fonts (midp_font.cpp)
struct Glyph {
  int16_t ofsX, ofsY;  // the box's left from the pen, its top from the line's top
  uint8_t w, h, adv;
  uint32_t off;        // in Font::bits (A8, w x h)
};
struct Font {
  int lineH = 0, baseline = 0;  // the line's height; the baseline from its top
  std::vector<Glyph> glyphs;    // for code points 0..255 (adv 0: none)
  std::vector<uint8_t> bits;
};
// Made once, on the UI thread (LVGL's font code), before any game runs. false: no fonts (text draws nothing).
bool prepareFonts();
const Font* font(int id);
Font* fontSlot(int id);  // midp_font.cpp fills the six fonts, then calls fontsDone()
void fontsDone();
int textWidth(const char16_t* s, int n, int font);
int text(const Target& t, const char16_t* s, int n, int font, int x, int y, uint32_t rgb);

// ---- images: PNG (all colour types and depths, interlaced too) -> w, h, ARGB. false: not a PNG it can read.
bool decodePng(const uint8_t* data, size_t len, int& w, int& h, PVec<uint32_t>& out);

}  // namespace draw
}  // namespace j2me
