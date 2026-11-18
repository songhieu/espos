#pragma once
// The board EspOS S3 runs on, as the OS has always asked for it: a thin layer over the HAL (src/hal/: the board's
// descriptor hal::board(), its capabilities hal::has(), its drivers; the boards themselves in src/boards/;
// docs/porting.md). One firmware for both boards (env s3desk): board::detect() tells them apart at boot, before any
// driver starts (src/boards/boards.cpp).
//   ES3C35P (LCDWIKI 3.5"): ST77922 320x480 over QSPI, its touch at I2C 0x55 (src/boards/es3c35p).
//   ES3C28P (LCDWIKI 2.8"): ILI9341V 240x320 over SPI, FT6336G touch at I2C 0x38 (src/boards/es3c28p).
// The UI is 320x480 portrait on both (LCD_W x LCD_H): the 2.8" board shows every picture at 2/3 (213x320, centred:
// 13 px of black each side), shapes kept (scale.h).
#include <cstring>

#include "hal/board.h"

constexpr int LCD_W = hal::UI_W, LCD_H = hal::UI_H;  // the UI, portrait, on every board
constexpr const char* FW_VERSION = "espos-s3-0.5.0";  // 0.5.0: renamed to EspOS, languages, landscape (2026-11-18)

namespace board {
inline const char* name() { return hal::board().name; }    // "es3c35p", "es3c28p"
inline const char* label() { return hal::board().label; }  // "ES3C35P · 3,5 inch"
inline void detect() { hal::detect(); }  // first thing at boot; QEMU and the simulator: the build's board
inline bool is28() { return !strcmp(name(), "es3c28p"); }  // older code; new code asks hal::board() or hal::has()
}  // namespace board

// The found board's pins, as the shared code asked for them before the descriptor (new code: hal::board().audio...).
namespace pin {
inline int I2C_SDA() { return hal::board().i2c.sda; }
inline int I2C_SCL() { return hal::board().i2c.scl; }
inline int TOUCH_RST() { return hal::board().input.rst; }
inline int TOUCH_INT() { return hal::board().input.irq; }
inline int I2S_MCLK() { return hal::board().audio.mclk; }
inline int I2S_BCLK() { return hal::board().audio.bclk; }
inline int I2S_WS() { return hal::board().audio.ws; }
inline int I2S_DOUT() { return hal::board().audio.dout; }
inline int I2S_DIN() { return hal::board().audio.din; }
inline int AMP_EN() { return hal::board().audio.amp; }
inline int SD_CLK() { return hal::board().card.clk; }
inline int SD_CMD() { return hal::board().card.cmd; }
inline int SD_D0() { return hal::board().card.d0; }
inline int SD_D1() { return hal::board().card.d1; }
inline int SD_D2() { return hal::board().card.d2; }
inline int SD_D3() { return hal::board().card.d3; }
inline int RGB_LED() { return hal::board().misc.rgbLed; }
inline int BAT_ADC() { return hal::board().power.batAdc; }
}  // namespace pin
