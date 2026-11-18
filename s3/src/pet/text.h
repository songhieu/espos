#pragma once
// Text drawn straight into an RGB565 buffer with an LVGL font (the game's picture is one buffer, not LVGL
// objects): the dog's bubble as the page draws it (sim/pet.html sayWrap/sayBox/bubble/tail), its name in the
// header, the splash's line. Glyphs come from the font chain (Lexend, then the OS's fonts).
#include <Arduino.h>
#include <lvgl.h>

namespace pet {
namespace text {

struct Target {
  uint16_t* px;
  int w, h, stride;  // pixels; rows [y0, y1) may be touched
  int y0, y1;
};

int width(const lv_font_t* f, const char* s);
// One line with its vertical middle at `midY` (the page's textBaseline "middle"), starting at x.
void draw(const Target& t, const lv_font_t* f, const char* s, int x, float midY, uint16_t color);

// The page's bubble: one row when it fits 280 px, else the two rows of nearest width (a third only when two
// would not fit the screen); w = the widest row + 24, h = 10 + 16 a row.
struct Bubble {
  String rows[3];
  int n = 0, w = 0, h = 0;
};
Bubble layout(const lv_font_t* f, const String& s);
// The pill (#2b2f3a, radius 13) with its rows in white, at (x, y) (the tail is the game's piece).
void bubble(const Target& t, const lv_font_t* f, const Bubble& b, int x, int y);
// A filled rounded box (antialiased corners).
void roundBox(const Target& t, int x, int y, int w, int h, int r, uint16_t color);

}  // namespace text
}  // namespace pet
