#pragma once
// ES3C28P (LCDWIKI 2.8" ESP32-S3 display, 16 MB flash, 8 MB octal PSRAM): ILI9341V 240x320 over SPI at 40 MHz + D/C
// (display.cpp), FT6336G touch at I2C 0x38 (touch.cpp), an ES8311 codec, an SDIO card slot, a battery on a 1:2
// divider. The 320x480 UI goes to the panel at 2/3: 213x320 from x 13, 13 px of black each side; in landscape the
// 480x320 UI is 320x213 from row 13 (docs/boards.md).
// Pins from the vendor's page (lcdwiki.com/2.8inch_ESP32-S3_Display, read 2026-11-17), not yet tried on a board.
#include "hal/board.h"

namespace es3c28p {

inline constexpr hal::Board BOARD = {
    .name = "es3c28p",
    .label = "ES3C28P · 2,8 inch",
    .chip = hal::Chip::ESP32S3,
    // screen.fast and audio.in from the vendor's page (40 MHz SPI ~31 ms a panel; the codec's ADC wired to GPIO 6), not
    // yet seen on a board.
    .caps = hal::PROFILE_FULL,
    // All four ways: landscape is the 480x320 UI at 2/3 (320x213 on the turned panel, display.cpp).
    .display = {.panel = {.w = 240, .h = 320, .type = hal::PanelType::TFT, .format = hal::ColorFormat::RGB565,
                          .bits = 16, .refresh = hal::Refresh::INSTANT, .rotations = 0b1111},
                .ui = {.num = 2, .den = 3, .x = 13, .y = 0},
                .pins = {.cs = 10, .sclk = 12, .d0 = 11, .d1 = 13, .d2 = -1, .d3 = -1, .dc = 46, .rst = -1, .bl = 45,
                         .te = -1}},  // backlight: high = on
    .i2c = {.sda = 16, .scl = 15},
    .input = {.kind = hal::InputKind::TOUCH, .addr = 0x38, .rst = 18, .irq = 17},
    .audio = {.codec = hal::Codec::ES8311, .addr = 0x18, .mclk = 4, .bclk = 5, .ws = 7, .dout = 8, .din = 6, .amp = 1,
              .ampOn = 0},  // the amplifier's shutdown line: LOW = on
    // The vendor's page lists the data lines without saying which is which: 1 line is tried when 4 get no answer.
    .card = {.bus = hal::CardBus::SDMMC, .width = 4, .try1bit = true, .clk = 38, .cmd = 40, .d0 = 39, .d1 = 41,
             .d2 = 48, .d3 = 47},
    .power = {.batAdc = 9, .divider = 2, .emptyMv = 3350, .fullMv = 4150},
    .misc = {.rgbLed = 42, .boot = 0},
};

// The board's build only (bsp.cpp, display.cpp, touch.cpp).
extern const hal::Bsp BSP;
extern const hal::Display display;
extern const hal::Input input;

}  // namespace es3c28p
