#pragma once
// HAL: the display. The descriptor's part (DisplayDesc: the panel, where the UI lands on it, the panel's pins) and the
// panel driver (hal::Display) behind lcd:: (src/lcd.h). docs/porting.md.
#include <lvgl.h>

#include <cstdint>

namespace hal {

// The UI's size (the OS's pixels, portrait) is the board's own: DisplayDesc::ui.w x ui.h, read at run time with
// hal::uiW() / uiH() (hal/board.h). 320x480 on the 3.5" board; 360x480 on the 2.8" one, whose 240x320 panel shows it at
// 2/3, the whole panel. Screens follow os::W() / os::H(); what is laid out for 320 (games, saver pages, widgets) keeps
// 320 and sits in the middle (docs/boards.md).

enum class PanelType : uint8_t { TFT, OLED, EPAPER };
enum class ColorFormat : uint8_t { RGB565, GRAY4, MONO };  // what the panel takes; the UI is rendered in RGB565
// How a panel refreshes. EINK is the Lite profile's, not built yet: partial updates (fast, they leave ghosts) and a
// full refresh every so often, each ~0.3-1 s; the OS would then redraw less often and never animate.
enum class Refresh : uint8_t { INSTANT, EINK };

struct PanelDesc {
  uint16_t w, h;       // its own pixels, portrait
  PanelType type;
  ColorFormat format;
  uint8_t bits;        // bits a pixel on the wire
  Refresh refresh;
  uint8_t rotations;   // the rotations the driver turns to: bit n = LV_DISPLAY_ROTATION_<n x 90>; others stay portrait
};

// The UI (w x h, portrait) and where it goes on the panel: scaled by num/den (1/1; 2/3 on a 240x320 panel, each panel
// pixel the area average of the UI pixels it covers: src/scale.h), its top-left corner at (x, y) on the panel. Touches
// come back the same way (the touch driver's job).
struct UiMap {
  uint16_t w, h;
  uint8_t num, den;
  int16_t x, y;
  constexpr int pw() const { return w * num / den; }  // its size on the panel
  constexpr int ph() const { return h * num / den; }
};

// The panel's bus. QSPI: d0..d3. SPI: d0 = MOSI, d1 = MISO, dc = data/command. te: the tear signal. -1: none.
struct DisplayPins {
  int8_t cs, sclk, d0, d1, d2, d3, dc, rst, bl, te;
};

struct DisplayDesc {
  PanelDesc panel;
  UiMap ui;
  DisplayPins pins;
};

// The panel driver: src/boards/<name>/display.cpp fills one in. Every call from the LVGL thread; lcd.h says what each
// means to the OS.
struct Display {
  // The bus, the panel, the backlight (off), and an LVGL display ui.w x ui.h in RGB565, partial mode, with its own
  // band buffers; its flush callback sends each band to the panel (scaled, turned, byte-swapped as the panel needs).
  lv_display_t* (*begin)();
  void (*setRotation)(lv_display_rotation_t r);  // one it cannot do: stay portrait
  void (*setBacklight)(int percent);             // 0..100, 0 = off
  void (*stats)(uint32_t& frames, uint32_t& avgFlushUs);  // since the last call
  void (*logInvalid)(bool on);                   // print what each frame redraws (may do nothing)
  // Rows [y0, y1) of the UI straight to the panel, past LVGL (lcd::direct: the run game). false: not now, use LVGL.
  bool (*direct)(int y0, int y1, void (*fill)(uint16_t* dst, int y, int rows, void* ctx), void* ctx, bool tear);
  void (*mirror)(bool on);                       // keep a copy of the UI's frame for the remote viewer
  const uint16_t* (*frame)(int& w, int& h);      // that copy, as the screen is turned (nullptr while off)
  // lcd::native (added 2026-11-19, the aquarium): the whole screen at the panel's own size of the UI (the UiMap's
  // num/den, as turned), past LVGL and past any scaling, asked for in pieces. nullptr (a board without it): use LVGL.
  bool (*native)(void (*fill)(uint16_t* dst, int dx, int dy, int x0, int y0, int x1, int y1, void* ctx), void* ctx,
                 bool tear);
};

}  // namespace hal
