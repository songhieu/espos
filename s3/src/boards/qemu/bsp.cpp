// The board in QEMU: picked at build time (src/boards/boards.cpp), never probed: QEMU has no I2C devices to ask.
#ifdef ESPOS_QEMU
#include "qemu.h"

namespace qemu {

const hal::Bsp BSP = {&BOARD, nullptr, &display, nullptr};

}  // namespace qemu
#endif
