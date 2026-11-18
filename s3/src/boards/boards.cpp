// The registry: the boards this firmware knows, and which one it runs on. docs/porting.md "Detection".
// hal::detect() runs first thing at boot (main.cpp, board::detect), before any driver takes a pin: it asks each board's
// probe() in the order below and keeps the first that answers. A new board goes into the list where asking it can
// harm none of the boards before it, and its probe must not drive a pin that is an output on a board after it.
#include <Arduino.h>

#include "boards/es3c28p/es3c28p.h"
#include "boards/es3c35p/es3c35p.h"
#include "boards/qemu/qemu.h"
#include "hal/board.h"
#include "trace.h"

namespace {

#if defined(SIM)
// The simulator (sim/): the build's board, its descriptor only (sim/stubs.cpp stands in for the drivers).
#ifdef BOARD_ES3C28P
const hal::Bsp SIM_BSP = {&es3c28p::BOARD, nullptr, nullptr, nullptr};
#else
const hal::Bsp SIM_BSP = {&es3c35p::BOARD, nullptr, nullptr, nullptr};
#endif
const hal::Bsp* const BSPS[] = {&SIM_BSP};

#elif defined(ESPOS_QEMU)
// QEMU (env s3desk-qemu, s3desk-28-qemu): the build's board as QEMU emulates it (src/boards/qemu).
const hal::Bsp* const BSPS[] = {&qemu::BSP};

#elif defined(BOARD_ES3C28P)
// A build for one board (not made by default).
const hal::Bsp* const BSPS[] = {&es3c28p::BSP};

#else
// One firmware for both boards (env s3desk), asked in this order; nobody answering: the first (the 3.5" board, as every
// firmware before detection).
//   es3c35p: its codec (0x18) and touch (0x55) on GPIO 38/39. On the 2.8" board 38/39 are the memory card's CLK and
//            D0: an address byte there gets no answer (the card listens on CMD only).
//   es3c28p: its codec and touch (0x38) on GPIO 16/15, its touch reset (GPIO 18) driven high first. Asked second, so
//            the 3.5" board never sees 16/15 (its codec's I2S data lines) or 18 driven.
const hal::Bsp* const BSPS[] = {&es3c35p::BSP, &es3c28p::BSP};
#endif

constexpr uint8_t N = sizeof(BSPS) / sizeof(BSPS[0]);
uint8_t cur = 0;  // BSPS[cur] is the board

}  // namespace

namespace hal {

const Bsp& bsp() { return *BSPS[cur]; }

void detect() {
  if (N < 2) return;  // one board in this build: nothing to ask
  const uint32_t t0 = millis();
  uint8_t a = 0;
  for (uint8_t i = 0; i < N && !a; i++)
    if (BSPS[i]->probe && (a = BSPS[i]->probe())) cur = i;
  trace::add("board: %s (0x%02x answered, %lu ms)", board().name, a, (unsigned long)(millis() - t0));
}

}  // namespace hal
