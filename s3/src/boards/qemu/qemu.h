#pragma once
// The boards in QEMU (env s3desk-qemu; s3desk-28-qemu builds with BOARD_ES3C28P; docs/simulator.md): the real board's
// descriptor (the same name, so it pairs, syncs and takes updates as that board), less what QEMU does not emulate:
// no codec, no battery, no radio, no LED. Its drivers: QEMU's virtual RGB panel (display.cpp); no touch controller,
// touches come from the dev console (TOUCH, TAP, DRAG -> touch::feed). The network is QEMU's Ethernet (lib/qemu_eth).
#include "boards/es3c28p/es3c28p.h"
#include "boards/es3c35p/es3c35p.h"

namespace qemu {

constexpr hal::Board emulated(hal::Board b) {
  b.caps &= ~hal::QEMU_LACKS;  // hal::has() knows these at compile time in QEMU builds
  b.audio.codec = hal::Codec::NONE;
  return b;
}

#ifdef BOARD_ES3C28P
inline constexpr hal::Board BOARD = emulated(es3c28p::BOARD);
#else
inline constexpr hal::Board BOARD = emulated(es3c35p::BOARD);
#endif

// QEMU builds only (bsp.cpp, display.cpp).
extern const hal::Bsp BSP;
extern const hal::Display display;

}  // namespace qemu
