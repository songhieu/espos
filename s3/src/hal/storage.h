#pragma once
// HAL: storage. The memory card, mounted at /sdcard by src/pkg/card.cpp (packages on the card, the run game's roads, Java
// games), from this part of the descriptor. The internal flash (LittleFS, NVS, the PawBoop packs) is the partition
// table's (partitions_pawboop.csv), the same on every 16 MB board.
#include <cstdint>

namespace hal {

enum class CardBus : uint8_t { NONE, SDMMC, SDSPI /* planned */ };

struct CardDesc {
  CardBus bus;
  uint8_t width;  // data lines (SDMMC: 1 or 4)
  bool try1bit;   // when 4 lines get no answer, try D0 alone (for pins not yet checked on a board)
  int8_t clk, cmd, d0, d1, d2, d3;
};

}  // namespace hal
