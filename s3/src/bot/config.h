#pragma once
// EspOS Bot (env bot128): the Xiaozhi-style ESP32-S3 N16R8 board with a 128x64 SSD1306 OLED, an I2S MEMS mic and an
// I2S amp (no speaker fitted yet), no touch. Its wiring is Xiaozhi's "bread-compact" (NoAudioCodecSimplex).
#ifndef FW_VERSION
#define FW_VERSION "espos-bot-0.1.0"
#endif
#ifndef ESPOS_API
#define ESPOS_API "https://api.espos.dev"
#endif
#ifndef ESPOS_WEB
#define ESPOS_WEB "https://espos.dev"
#endif
#define BOT_BOARD "bot128"

namespace bot {
namespace pins {
constexpr int SDA = 41, SCL = 42;                 // the OLED (0x3C)
constexpr int BOOT = 0;                           // inside the case
constexpr int VOL_UP = 40, VOL_DOWN = 39;         // the volume switches (Xiaozhi bread-compact): Vol+ is the head pat
constexpr int MIC_WS = 4, MIC_SCK = 5, MIC_DIN = 6;  // I2S0 in
constexpr int SPK_DOUT = 7, SPK_BCLK = 15, SPK_LRCK = 16;  // I2S1 out (the amp)
}  // namespace pins
}  // namespace bot
