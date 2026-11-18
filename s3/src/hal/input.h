#pragma once
// HAL: input. The descriptor's part (InputDesc) and the input driver (hal::Input) that touch::begin() starts.
#include <cstdint>

namespace hal {

enum class InputKind : uint8_t {
  NONE,
  TOUCH,    // a touch screen: one finger
  BUTTONS,  // keys (planned, the Lite profile): up, down, left, right, ok, back as key events
};

struct InputDesc {
  InputKind kind;
  uint8_t addr;     // the controller's I2C address on the board's bus (Board::i2c)
  int8_t rst, irq;  // its reset (active low) and interrupt pins; -1: none
};

// The input driver: src/boards/<name>/touch.cpp. begin() runs once at boot (from touch::begin()), on the board's I2C
// bus (touch::i2cBus(), made on first use): reset the controller, check it answers, start a task that reads every
// report and hands it to touch::report(x, y, down, ms). x, y: the UI's portrait coordinates (0..UI_W-1, 0..UI_H-1, before the screen's
// rotation; a panel smaller than the UI maps back through its UiMap); ms: millis() when it was read.
// false: the controller does not answer.
struct Input {
  bool (*begin)();
};

}  // namespace hal
