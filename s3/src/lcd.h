#pragma once
#include <lvgl.h>

// The display, driven as an LVGL display: the board's panel driver (hal::Display, src/hal/display.h; one per board in
// src/boards/<name>/display.cpp). The UI is 320x480 portrait on every board; a smaller panel shows it scaled
// (the board's UiMap). ES3C35P: pixels go out by DMA while LVGL renders the next band; 180 deg uses the panel's own
// scan flip; 90/270 rotate each band in software.
namespace lcd {
lv_display_t* begin();                    // bus + panel init, LVGL display with two band buffers
void setRotation(lv_display_rotation_t r);  // also flips touch through LVGL
void setBacklight(int percent);           // 0..100 (0 = off)
void stats(uint32_t& frames, uint32_t& avgFlushUs);  // since the last call
void logInvalid(bool on);  // print what each frame redraws
// Rows [y0, y1) straight to the panel, past LVGL (a game redrawing the whole screen 20 times a second): fill()
// draws `rows` rows from row y into dst (RGB565 native order, LCD_W px a row) in LVGL's band buffers, one while
// DMA sends the other. From the LVGL thread only, between refreshes; portrait, either way up (false: use LVGL).
// tear: wait for the panel's tear signal first (false right after an LVGL flush of the same picture).
bool direct(int y0, int y1, void (*fill)(uint16_t* dst, int y, int rows, void* ctx), void* ctx, bool tear);
// What the panel shows, for remote viewing (src/remote.cpp): while on, every band LVGL or a game sends is also kept
// in a logical frame in PSRAM (turning it on redraws the screen once). frame(): that frame, RGB565, w x h as the
// screen is turned; nullptr while off.
void mirror(bool on);
const uint16_t* frame(int& w, int& h);
}  // namespace lcd
