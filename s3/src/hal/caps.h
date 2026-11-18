#pragma once
// HAL: capabilities. What a board has, as flags in its descriptor (Board::caps), and the one table that names them for
// packages: a manifest's `requires` and the device's `caps` list use these strings (docs/profiles.md §1.1, the
// canonical list; docs/porting.md "Capabilities"). Every CAP_* has exactly one name and every name one CAP_*: a new
// capability is a flag here, a row in CAP_NAMES and a row in profiles.md's table, together.
#include <cstdint>
#include <cstring>

namespace hal {

enum : uint32_t {
  CAP_COLOR = 1u << 0,      // screen.color: 16-bit colour or better
  CAP_GRAY = 1u << 1,       // screen.gray: 4 grey levels or more for UI drawing
  CAP_EINK = 1u << 2,       // screen.eink: e-paper (Refresh::EINK, planned)
  CAP_FAST = 1u << 3,       // screen.fast: 20+ frames a second (animation, games)
  CAP_TOUCH = 1u << 4,      // input.touch: a touch screen (InputKind::TOUCH)
  CAP_BUTTONS = 1u << 5,    // input.buttons: at least Back, OK, Up, Down (InputKind::BUTTONS, planned)
  CAP_PSRAM = 1u << 6,      // mem.psram: 2 MB+ of external RAM
  CAP_AUDIO_OUT = 1u << 7,  // audio.out: a speaker through a codec (AudioDesc)
  CAP_AUDIO_IN = 1u << 8,   // audio.in: a microphone (the codec's ADC; no OS API yet)
  CAP_SDCARD = 1u << 9,     // store.sd: a memory card slot (CardDesc)
  CAP_WIFI = 1u << 10,      // net.wifi
  CAP_BLE = 1u << 11,       // net.ble
  CAP_BATTERY = 1u << 12,   // power.battery: a battery the firmware measures (PowerDesc)
  CAP_RGB_LED = 1u << 13,   // led.rgb: an RGB LED (MiscPins::rgbLed; nothing drives it yet)
};

struct CapName {
  uint32_t flag;
  const char* name;
};

// The capability names, 1:1 with the flags above (docs/profiles.md §1.1; agreed with the X4 Lite work, 2026-11-17:
// the X4 firmware and tools/espk.py use the same strings). A new flag: its name goes into profiles.md first.
inline constexpr CapName CAP_NAMES[] = {
    {CAP_COLOR, "screen.color"}, {CAP_GRAY, "screen.gray"},     {CAP_EINK, "screen.eink"},
    {CAP_FAST, "screen.fast"},   {CAP_TOUCH, "input.touch"},    {CAP_BUTTONS, "input.buttons"},
    {CAP_PSRAM, "mem.psram"},    {CAP_AUDIO_OUT, "audio.out"},  {CAP_AUDIO_IN, "audio.in"},
    {CAP_SDCARD, "store.sd"},    {CAP_WIFI, "net.wifi"},        {CAP_BLE, "net.ble"},
    {CAP_BATTERY, "power.battery"}, {CAP_RGB_LED, "led.rgb"},
};
constexpr int CAP_COUNT = sizeof(CAP_NAMES) / sizeof(CAP_NAMES[0]);

// One flag's name (nullptr: not a single known flag).
constexpr const char* capName(uint32_t flag) {
  for (const CapName& c : CAP_NAMES)
    if (c.flag == flag) return c.name;
  return nullptr;
}

// A name's flag, for a manifest's `requires` (0: unknown: a package needing it fits no device).
inline uint32_t capFlag(const char* name) {
  for (const CapName& c : CAP_NAMES)
    if (!strcmp(c.name, name)) return c.flag;
  return 0;
}

namespace detail {
constexpr bool capsOneToOne() {  // each row one bit, no bit twice, no name twice, no gaps
  uint32_t seen = 0;
  for (int i = 0; i < CAP_COUNT; i++) {
    const uint32_t f = CAP_NAMES[i].flag;
    if (!f || (f & (f - 1)) || (seen & f)) return false;
    seen |= f;
    for (int j = 0; j < i; j++) {
      const char *a = CAP_NAMES[i].name, *b = CAP_NAMES[j].name;
      while (*a && *a == *b) a++, b++;
      if (*a == *b) return false;
    }
  }
  return seen == (1u << CAP_COUNT) - 1;
}
}  // namespace detail
static_assert(detail::capsOneToOne(), "CAP_NAMES must name every CAP_* flag exactly once");

// Profiles: names for common capability sets, used in docs and store labels; packages name capabilities, never a
// profile (docs/profiles.md §1.3).
//   Full: ESP32-S3 + PSRAM + a colour touch LCD (both boards today).
//   Lite: no PSRAM, e-paper, keys, e.g. the ESP32-C3 Xteink X4 (a separate firmware today).
constexpr uint32_t PROFILE_FULL = CAP_COLOR | CAP_GRAY | CAP_FAST | CAP_TOUCH | CAP_PSRAM | CAP_AUDIO_OUT |
                                  CAP_AUDIO_IN | CAP_SDCARD | CAP_WIFI | CAP_BLE | CAP_BATTERY | CAP_RGB_LED;
constexpr uint32_t PROFILE_LITE = CAP_EINK | CAP_BUTTONS | CAP_SDCARD | CAP_WIFI | CAP_BLE | CAP_BATTERY;

}  // namespace hal
