#pragma once
#include <lvgl.h>

// The display, driven as an LVGL display: the board's panel driver (hal::Display, src/hal/display.h; one per board in
// src/boards/<name>/display.cpp). The UI is the board's own size (hal::uiW() x uiH(): 320x480 on the 3.5" board,
// 360x480 on the 2.8" one); a smaller panel shows it scaled (the board's UiMap). ES3C35P: pixels go out by DMA while LVGL renders the next band; 180 deg uses the panel's own
// scan flip; 90/270 rotate each band in software.
namespace lcd {
lv_display_t* begin();                    // bus + panel init, LVGL display with two band buffers
void setRotation(lv_display_rotation_t r);  // also flips touch through LVGL
// Upside down done by the panel itself (its scan mirrored, MADCTL MX+MY): LVGL, the games and the aquarium draw as
// if upright (180 -> LVGL 0, 270 -> LVGL 90), at no cost; touch mirrors its points to match (touch.cpp). The drivers
// set it in setRotation (owner, 2026-11-20: the board will stand with its USB on top, and the software 180 turn made
// the aquarium saver take 160 ms a loop).
bool flipped();
void setFlipped(bool on);
void setBacklight(int percent);           // 0..100 (0 = off)
void stats(uint32_t& frames, uint32_t& avgFlushUs);  // since the last call
void logInvalid(bool on);  // print what each frame redraws
// Rows [y0, y1) straight to the panel, past LVGL (a game redrawing the whole screen 20 times a second): fill()
// draws `rows` rows from row y into dst (RGB565 native order, hal::uiW() px a row) in LVGL's band buffers, one while
// DMA sends the other. From the LVGL thread only, between refreshes; portrait, either way up (false: use LVGL).
// tear: wait for the panel's tear signal first (false right after an LVGL flush of the same picture).
bool direct(int y0, int y1, void (*fill)(uint16_t* dst, int y, int rows, void* ctx), void* ctx, bool tear);
// What the panel shows, for remote viewing (src/remote.cpp): while on, every band LVGL or a game sends is also kept
// in a logical frame in PSRAM (turning it on redraws the screen once). frame(): that frame, RGB565, w x h as the
// screen is turned; nullptr while off.
void mirror(bool on);
const uint16_t* frame(int& w, int& h);
// The whole screen drawn at the panel's own size of the UI (hal::UiMap: 320x480 on the 3.5" board, 240x320 on the 2.8"
// one; 480x320 / 320x240 turned), straight to the panel, past LVGL and past the 2.8" board's 2/3 scale (the aquarium
// draws its water at that size: src/tank). The driver asks fill() for pieces: the rectangle [x0, x1) x [y0, y1) of the
// picture, pixel (x, y) at dst + (x - x0) * dx + (y - y0) * dy (RGB565 native order; the strides let the 3.5" board
// held on its side take columns straight into its rows). From the LVGL thread, between refreshes. false: use LVGL.
using NativeFill = void (*)(uint16_t* dst, int dx, int dy, int x0, int y0, int x1, int y1, void* ctx);
bool native(NativeFill fill, void* ctx, bool tear);
}  // namespace lcd
