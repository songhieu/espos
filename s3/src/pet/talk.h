#pragma once
// The dog's turn moved into the OS (2026-11-22, lane 4b): src/turn.h, the client of any assistant app in the slot;
// PawBoop is one of them (its face: pet::face(), summon.cpp). pet::talk:: still names it.
#include "../turn.h"

namespace pet {
namespace talk = ::turn;
}  // namespace pet
