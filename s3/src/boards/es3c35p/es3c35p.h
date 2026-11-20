#pragma once
// ES3C35P (LCDWIKI 3.5" ESP32-S3 display, 16 MB flash, 8 MB octal PSRAM): ST77922 320x480 over QSPI (display.cpp), the
// panel's own touch at I2C 0x55 (touch.cpp), an ES8311 codec, a 4-bit SDIO card slot, a battery on a 1:2 divider.
// Pins from the vendor demos (LCDWIKI Example_08_LVGL_Demos); checked on the board with the self-test in
// s3test.
#include "hal/board.h"

namespace es3c35p {

inline constexpr hal::Board BOARD = {
    .name = "es3c35p",
    .label = "ES3C35P · 3,5 inch",
    .chip = hal::Chip::ESP32S3,
    .caps = hal::PROFILE_FULL,  // all of it; the mic passed the self-test
    .display = {.panel = {.w = 320, .h = 480, .type = hal::PanelType::TFT, .format = hal::ColorFormat::RGB565,
                          .bits = 16, .refresh = hal::Refresh::INSTANT, .rotations = 0b1111},
                .ui = {.w = 320, .h = 480, .num = 1, .den = 1, .x = 0, .y = 0},
                .pins = {.cs = 10, .sclk = 12, .d0 = 11, .d1 = 13, .d2 = 14, .d3 = 9, .dc = -1, .rst = -1, .bl = 41,
                         .te = 42}},
    .i2c = {.sda = 38, .scl = 39},
    .input = {.kind = hal::InputKind::TOUCH, .addr = 0x55, .rst = 48, .irq = 47},
    .audio = {.codec = hal::Codec::ES8311, .addr = 0x18, .mclk = 17, .bclk = 18, .ws = 21, .dout = 15, .din = 16,
              .amp = 1, .ampOn = 0},  // the amplifier's shutdown line: LOW = on
    .card = {.bus = hal::CardBus::SDMMC, .width = 4, .try1bit = false, .clk = 5, .cmd = 4, .d0 = 6, .d1 = 7, .d2 = 2,
             .d3 = 3},
    .power = {.batAdc = 8, .divider = 2, .emptyMv = 3350, .fullMv = 4150},
    .misc = {.rgbLed = 40, .boot = 0},
};

// The board's build only (bsp.cpp, display.cpp, touch.cpp).
extern const hal::Bsp BSP;
extern const hal::Display display;
extern const hal::Input input;

}  // namespace es3c35p
