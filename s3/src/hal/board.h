#pragma once
// EspOS's hardware abstraction layer (HAL): what the OS core may know about the board it runs on. docs/porting.md.
//
// A board support package (BSP, src/boards/<name>/) gives:
//   - a descriptor, hal::Board: plain data in flash (name, chip, capability flags, the panel and how the 320x480 UI
//     lands on it, the pins grouped by function);
//   - its drivers: hal::Display (the panel, display.h) and hal::Input (the touch controller, input.h);
//   - probe(): "is it this board?", asked at boot by the registry (src/boards/boards.cpp).
// Capability flags and their package-facing names: caps.h.
// The OS keeps its own APIs over this (lcd::, touch::, sound::, power::, pkg card, board::). The speaker, the memory
// card and the battery are drivers in the OS (sound.cpp, pkg/card.cpp, power.cpp) set up from the descriptor
// (audio.h, storage.h, power.h); a board with another kind of chip there adds that kind.
#include <cstdint>
#include <initializer_list>

#include "driver/i2c_master.h"
#include "hal/audio.h"
#include "hal/caps.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/power.h"
#include "hal/storage.h"

namespace hal {

enum class Chip : uint8_t { ESP32S3, ESP32C3, ESP32C6, ESP32P4 };

struct I2cDesc {
  int8_t sda, scl;  // the board's own I2C bus (touch, codec); -1: none
};

struct MiscPins {
  int8_t rgbLed, boot;
};

// The descriptor: one per board, constexpr in src/boards/<name>/<name>.h. Pins are GPIO numbers, -1 = not wired.
struct Board {
  const char* name;   // "es3c35p": sent to the server (sync, diag); firmware updates are matched to it
  const char* label;  // "ES3C35P · 3,5 inch": shown to people
  Chip chip;
  uint32_t caps;      // CAP_* (caps.h; packages see them by name: hal::CAP_NAMES)
  DisplayDesc display;
  I2cDesc i2c;
  InputDesc input;
  AudioDesc audio;
  CardDesc card;
  PowerDesc power;
  MiscPins misc;
};

// A board support package: the descriptor, its drivers, and how to recognise the board.
struct Bsp {
  const Board* board;
  uint8_t (*probe)();      // is it this board? The I2C address that answered, 0: no. nullptr: never asked
  const Display* display;  // the panel driver (lcd:: calls it)
  const Input* input;      // the touch controller (nullptr: none; QEMU's touches come from the dev console)
};

// QEMU emulates none of these chips (src/boards/qemu/qemu.h takes them out of the board's caps). In QEMU builds has()
// knows it at compile time, so the image leaves their drivers out, as it always did.
constexpr uint32_t QEMU_LACKS = CAP_AUDIO_OUT | CAP_AUDIO_IN | CAP_BATTERY | CAP_RGB_LED | CAP_WIFI | CAP_BLE;
#ifdef ESPOS_QEMU
constexpr uint32_t NEVER = QEMU_LACKS;
#else
constexpr uint32_t NEVER = 0;
#endif

const Bsp& bsp();  // the board found at boot (src/boards/boards.cpp)
inline const Board& board() { return *bsp().board; }
// The UI's size, portrait (DisplayDesc::ui; display.h): the board found at boot's.
inline int uiW() { return board().display.ui.w; }
inline int uiH() { return board().display.ui.h; }
inline bool has(uint32_t caps) { return !(caps & NEVER) && (board().caps & caps) == caps; }
void detect();  // first thing at boot, before any driver takes a pin

// The board's own I2C bus (Board::i2c: the touch controller, the codec), made on first use. Boards only (hal.cpp).
i2c_master_bus_handle_t i2c();

// For probe(): the first of `addrs` answering on an I2C bus made on sda/scl, asked again for forMs (0: once); 0 = none.
// The pins are let go after. Boards only (src/hal/hal.cpp).
uint8_t i2cAnswer(int sda, int scl, std::initializer_list<uint8_t> addrs, uint32_t forMs);

}  // namespace hal
