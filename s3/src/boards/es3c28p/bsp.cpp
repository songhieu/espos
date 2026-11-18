// ES3C28P: how to tell it is this board, and its drivers. Listed in src/boards/boards.cpp, after es3c35p.
#ifndef ESPOS_QEMU  // QEMU: src/boards/qemu
#include <Arduino.h>

#include "es3c28p.h"

namespace es3c28p {
namespace {

// Its FT6336G touch (0x38) and ES8311 (0x18) on GPIO 16/15. The touch answers ~300 ms after its reset line goes high
// (the codec at once). On the 3.5" board 16/15 are the codec's I2S data lines and 18 its BCLK: es3c35p is asked first,
// so this runs only where nobody answered on 38/39.
uint8_t probe() {
  pinMode(BOARD.input.rst, OUTPUT);
  digitalWrite(BOARD.input.rst, HIGH);
  return hal::i2cAnswer(BOARD.i2c.sda, BOARD.i2c.scl, {BOARD.audio.addr, BOARD.input.addr}, 400);
}

}  // namespace

const hal::Bsp BSP = {&BOARD, probe, &display, &input};

}  // namespace es3c28p
#endif
