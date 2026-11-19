// ES3C35P: how to tell it is this board, and its drivers. Listed in src/boards/boards.cpp.
#ifndef ESPOS_QEMU  // QEMU: src/boards/qemu
#include "es3c35p.h"

namespace es3c35p {
namespace {

// Its ES8311 (0x18) and its touch (0x55) on GPIO 38/39. The self-test saw the codec answer there before its clock runs
// (s3test, "I2C" step).
uint8_t probe() { return hal::i2cAnswer(BOARD.i2c.sda, BOARD.i2c.scl, {BOARD.audio.addr, BOARD.input.addr}, 0); }

}  // namespace

const hal::Bsp BSP = {&BOARD, probe, &display, &input};

}  // namespace es3c35p
#endif
