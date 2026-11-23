#pragma once
// The board EspOS S3 runs on, as the OS has always asked for it: a thin layer over the HAL (src/hal/: the board's
// descriptor hal::board(), its capabilities hal::has(), its drivers; the boards themselves in src/boards/;
// docs/porting.md). One firmware for both boards (env s3desk): board::detect() tells them apart at boot, before any
// driver starts (src/boards/boards.cpp).
//   ES3C35P (LCDWIKI 3.5"): ST77922 320x480 over QSPI, its touch at I2C 0x55 (src/boards/es3c35p).
//   ES3C28P (LCDWIKI 2.8"): ILI9341V 240x320 over SPI, FT6336G touch at I2C 0x38 (src/boards/es3c28p).
// The UI is the board's own (hal::uiW() x uiH(), portrait): 320x480 on the 3.5" board, 360x480 on the 2.8" one, which
// shows every picture at 2/3 on its whole 240x320 panel, shapes kept (scale.h).
#include <cstring>

#include "hal/board.h"

constexpr const char* FW_VERSION = "espos-s3-0.6.4";  // 0.6.4 (release candidate 2026-11-23): more widget sizes on Home (weather, countdown, reminders, my widgets, notes, photos, notifications) and free sizes on the saver (device editor and web), the saver keeps its aquarium when memory is tight (the dog's and Home's pictures let go first, a light water when there is still no room), the pop-up says why something was refused instead of a code, cut text fixed (lunar dates, long languages, the library's search in landscape); 0.6.3: never released (its fixes are in 0.6.4); 0.6.2 (released 2026-11-23): free widget sizes on Home (Claude, Clock, Calendar, Markets in spans such as 2x1, 4x1 or 4x3, set on the web or by dragging a corner in edit mode; old S/M/L pages unchanged; the web offers them from fw_code 602), the saver's widgets follow a new dashboard while it shows (a ping goes under the saver over the open aquarium, the connection closed after it), the saver editor no longer lags over the live aquarium (Home and the saver hidden under it, the water held, the aquarium shown as a still picture), the saver hides the screen under it (the aquarium saver: CPU 29 -> 16 %); 0.6.1 (2026-11-23): screen saver apps (a JavaScript app can be the screen saver through Settings > Default apps; Bubbles in the store; a saver app that stops 3 times or runs too slow hands back to the built-in one with a notification), push to talk does nothing when no assistant is installed, the SDK's headless sim for saver apps; 0.6.0 (released 2026-11-23): the Sessions app (the paired Mac's Claude Code sessions: the live terminal in an 8 px mono font with a key row on the keyboard, approve/deny, a pop-up for permission requests over any screen (popnote), spoken notes), app settings and secrets (appset/jsset), API level 2 for JS apps (jsnet with the device credential to an app's own server path, jsfs, jsui, the LAN form, ui.term, voice.dictate, the keyboard's key row, app.selfDraw), the assistant and the saver as slots (the dog is removable like any app: "No assistant installed"), product presets (/sys/preset.json at first boot, a three-way merge for a new version), PawBoop without a memory card (from 0.5.12); 0.5.12: PawBoop without a memory card: optional package parts (a dog's Coin Run, sounds and full voice only with a card), voice_full.pba, "Needs the memory card" on Coin Run (2026-11-21); 0.5.11: the Claude usage widget (three looks, S/M/L: the paired Mac's 5-hour and weekly usage, sessions waiting) (2026-11-21); 0.5.10 (hotfix on 0.5.9): a call or the talk button records in what memory has left (from 6 s) instead of "no room to record" after the aquarium, no "microphone isn't working" for a memory shortage (2026-11-21); 0.5.9: more calls for the dog ("Ki ki", "Lu lu", "Cún ơi", "Milu" next to "Gâu gâu", each on or off; listener 1.1.0, .ewm version 2), apps update themselves (Settings > Update, s3.pkg_auto), the developer's own apps (u.*) update with their code (2026-11-21); 0.5.8: turn the screen any way up (Settings > Display, Rotate held), upside down done by the panel itself (both boards), talking: short spoken answers, a 5 s follow-up that takes normal speech, room hum no longer holds a question open, a call over the aquarium saver waits for room to record, the dog calmer while talking, the saver at 5 pictures a second, a warm pop-up on colour themes (2026-11-21); 0.5.7: boot splash + loading screens, faster boot, store progress/cancel, the 2.8" fills its screen (360x480) with its IPS colours, trial/claim, wake ack/follow-up, PawBoop sounds, aquarium speed (2026-11-20); 0.5.6: the Hồ cá cảnh aquarium, the RGB LED, swipe to close the dog, fonts deflated; 0.5.5 (never released): the dog assistant talks (mic, push to talk, "Gâu" wake word, streamed voice); 0.5.4: PawBoop froze on a letter its font lacked (LVGL assert); dog records read lazily (2026-11-18)

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
