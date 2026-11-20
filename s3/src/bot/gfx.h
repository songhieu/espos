#pragma once
// EspOS Bot: the 128x64 1-bit picture in the SSD1306's own order (8 rows a byte, one page = a row of 128 bytes), and
// what draws into it: the scenes of art.h (run-length coded), digits, icons, the fonts (Ubuntu 11 with every
// Vietnamese letter, Ubuntu Bold 16), word wrap with no "…" ever (long text scrolls), QR codes. No Arduino here: the
// host build (sim/bot) draws the same pictures into PNGs.
#include <stdint.h>

#include <string>
#include <vector>

namespace bot {
namespace gfx {

constexpr int W = 128, H = 64;
extern uint8_t fb[W * H / 8];

void clear();
inline void px(int x, int y, bool on) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
  uint8_t& b = fb[(y >> 3) * W + x];
  if (on) b |= 1 << (y & 7);
  else b &= ~(1 << (y & 7));
}
inline bool get(int x, int y) {
  return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H && (fb[(y >> 3) * W + x] >> (y & 7) & 1);
}
void fill(int x0, int y0, int x1, int y1, bool on);  // inclusive
void rect(int x0, int y0, int x1, int y1);            // an outline
void hline(int x0, int x1, int y);

void frame(uint16_t id);                              // an art.h frame over the whole picture
void frameInto(uint16_t id, uint8_t* dst);            // ... into another 1 KB buffer (overlay masks)
void invert();
void shift(int dx, int dy);                           // the whole picture moves; what comes in is dark

// A clip box for the text calls (inclusive); clipAll() undoes it.
void clip(int x0, int y0, int x1, int y1);
void clipAll();

// Bitmaps {w, h, rows MSB first}: digits, icons.
int bitmap(const uint8_t* g, int x, int y, bool on = true);

// Big digits (art::DIGIT_SETS[set]: "0123456789:" and the set's extras, e.g. " ,-./°"): the string's width, and
// drawn at x (align 0 left, 1 centre on x, 2 right ending at x), as art2's Glyphs.draw. Unknown characters are
// skipped. colon false: the ':' is left out (its room kept).
int digitsW(uint8_t set, const std::string& s);
void digits(uint8_t set, const std::string& s, int x, int y, int align = 0, bool colon = true, bool on = true);

// F11: Ubuntu 11 with every Vietnamese letter (13-px lines; y = the line's top, its baseline at y + ascent);
// F16B: Ubuntu Bold 16, ASCII (codes, titles); T5: Tiny5 8 px (9-px lines; y = the top of the capitals).
enum Font : uint8_t { F11, F16B, T5 };
std::string upper(const std::string& utf8);           // Vietnamese letters too (ă → Ă, ơ → Ơ, ế → Ế ...)
int lineH(Font f);                                    // the distance between two lines of that font
int textW(Font f, const std::string& utf8);
bool covers(Font f, const std::string& utf8);         // every letter is in that font (F16B: ASCII only)
// y is the top of the line; align as digits().
void text(Font f, const std::string& utf8, int x, int y, int align = 0, bool on = true);
// Lines no wider than w, broken at spaces (a word longer than w is cut between letters, never shortened).
std::vector<std::string> wrap(Font f, const std::string& utf8, int w);

// A QR code of `s` with its quiet zone, `scale` pixels a module, its top left at x, y. Its size in pixels, 0 when
// it does not fit (the caller then shows the words only).
int qrSize(const std::string& s, int scale);
int qr(const std::string& s, int x, int y, int scale);

}  // namespace gfx
}  // namespace bot
