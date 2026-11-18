// TEMPLATE: how to tell it is this board, and its drivers (docs/porting.md "Detection").
//
// One firmware runs on several boards: at boot, before any driver takes a pin, hal::detect() (src/boards/boards.cpp)
// asks each listed board's probe() in turn and keeps the first that says yes. So probe() must:
//   - look for something only this board has: a chip answering at an I2C address on this board's I2C pins (the touch
//     controller, the codec), as both boards today do (hal::i2cAnswer);
//   - drive no pin that could harm another board: an I2C address byte on two pins is harmless on most boards (a pin
//     that is an input or an SD data line there just sees a few clocks), but driving a reset or enable line high is
//     not. Put boards whose probe drives pins after the boards that would not like it (es3c28p drives its touch reset
//     high, so it is asked after es3c35p, on which that pin is an I2S clock);
//   - be quick (a few ms; up to ~400 ms if a chip needs time after reset);
//   - leave every pin as it found it (i2cAnswer resets its two pins).
// A firmware made only for this board can skip probing: list only this BSP (as BOARD_ES3C28P does in boards.cpp).
//
// Then add `&myboard::BSP` to BSPS in src/boards/boards.cpp and include "boards/myboard/myboard.h" there.
#ifndef ESPOS_QEMU  // QEMU builds use src/boards/qemu instead
#include "myboard.h"

namespace myboard {
namespace {

// The address that answered (any non-zero value means "it is this board"), 0: not this board.
uint8_t probe() {
  if (BOARD.i2c.sda < 0) return 0;
  return hal::i2cAnswer(BOARD.i2c.sda, BOARD.i2c.scl, {BOARD.input.addr}, 0);
}

}  // namespace

// The descriptor, how to recognise the board, its panel driver (display.cpp), its touch driver (touch.cpp; nullptr if
// it has none).
const hal::Bsp BSP = {&BOARD, probe, &display, &input};

}  // namespace myboard
#endif
