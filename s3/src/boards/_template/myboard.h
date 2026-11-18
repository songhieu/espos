#pragma once
// TEMPLATE: a new board's descriptor. Copy src/boards/_template/ to src/boards/<yourboard>/, rename this file to
// <yourboard>.h and the namespace `myboard` to <yourboard> everywhere in the folder, then fill in every field below.
// docs/porting.md walks through it with the ES3C28P as the example (src/boards/es3c28p/es3c28p.h is a filled-in one).
//
// The descriptor is plain data (constexpr, in flash): the OS reads it through hal::board(); the drivers in this
// folder read it at compile time (BOARD.display.pins.cs is a constant, as fast as a #define). Pins are GPIO numbers,
// -1 = not wired. Where to find them: the vendor's schematic or its demo code (look for the pin #defines of its LCD,
// touch and SD examples); check each on the board before you trust it.
//
// This folder is compiled with every firmware (so it always builds) but listed nowhere, so the linker drops it.
#include "hal/board.h"

namespace myboard {

inline constexpr hal::Board BOARD = {
    // Sent to the server with every sync and in diagnostics; firmware updates are matched to it. Lower case, short,
    // the vendor's model number. Never change it once devices are out.
    .name = "myboard",
    // Shown to people (Settings › Giới thiệu, the web's device page).
    .label = "My board · 3,5 inch",
    .chip = hal::Chip::ESP32S3,
    // What the board has (src/hal/caps.h: CAP_*, each with its package-facing name, docs/profiles.md). Start from the
    // profile closest to the board and add or take away flags. The OS gates on them (no CAP_AUDIO_OUT: sound:: stays
    // quiet; no CAP_SDCARD: no card task; no CAP_BATTERY: a steady 85 %), and the store will offer only the packages
    // whose `requires` the board has.
    .caps = (hal::PROFILE_FULL & ~(hal::CAP_AUDIO_OUT | hal::CAP_AUDIO_IN | hal::CAP_BATTERY | hal::CAP_RGB_LED)),
    .display = {
        // The panel itself, portrait. rotations: bit n = LV_DISPLAY_ROTATION_<n x 90> your display.cpp can turn to.
        .panel = {.w = 320, .h = 480, .type = hal::PanelType::TFT, .format = hal::ColorFormat::RGB565, .bits = 16,
                  .refresh = hal::Refresh::INSTANT, .rotations = 0b0001},
        // Where the 320x480 UI lands: 1/1 on a 320x480 panel. A 240x320 panel: {2, 3, 13, 0} (the ES3C28P). Other
        // ratios need their own scaling in display.cpp and touch.cpp (src/scale.h has 3/4 and 2/3).
        .ui = {.num = 1, .den = 1, .x = 0, .y = 0},
        // SPI: d0 = MOSI, d1 = MISO (-1 if the panel cannot be read), dc = data/command. QSPI: d0..d3, dc = -1.
        .pins = {.cs = -1, .sclk = -1, .d0 = -1, .d1 = -1, .d2 = -1, .d3 = -1, .dc = -1, .rst = -1, .bl = -1,
                 .te = -1},
    },
    // The board's own I2C bus: the touch controller (and the codec, if any) hang on it.
    .i2c = {.sda = -1, .scl = -1},
    // The touch controller: its address on that bus, its reset and interrupt pins.
    .input = {.kind = hal::InputKind::TOUCH, .addr = 0x38, .rst = -1, .irq = -1},
    // No speaker: Codec::NONE and no CAP_AUDIO_OUT. An ES8311: its address (0x18) and the I2S pins (see es3c35p.h).
    .audio = {.codec = hal::Codec::NONE, .addr = 0, .mclk = -1, .bclk = -1, .ws = -1, .dout = -1, .din = -1,
              .amp = -1, .ampOn = 0},
    // The card slot on the S3's SDMMC host (any pins). try1bit: fall back to D0 alone while the wiring is unchecked.
    .card = {.bus = hal::CardBus::SDMMC, .width = 4, .try1bit = true, .clk = -1, .cmd = -1, .d0 = -1, .d1 = -1,
             .d2 = -1, .d3 = -1},
    // The battery's ADC pin and divider (both boards today: 1:2), and the voltages for 0 % and 100 %.
    .power = {.batAdc = -1, .divider = 2, .emptyMv = 3350, .fullMv = 4150},
    .misc = {.rgbLed = -1, .boot = 0},
};

// The board's build only (bsp.cpp, display.cpp, touch.cpp).
extern const hal::Bsp BSP;
extern const hal::Display display;
extern const hal::Input input;

}  // namespace myboard
