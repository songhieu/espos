# A new board

Copy this folder to `src/boards/<yourboard>/`, rename `myboard.h` and the namespace `myboard`, then follow
[docs/porting.md](../../../docs/porting.md) step by step. Every file here says what to fill in:

| File | What |
|---|---|
| `myboard.h` | the descriptor (`hal::Board`): name, chip, capabilities, the panel and where the UI lands on it, the pins |
| `bsp.cpp` | `probe()` (is it this board?) and the BSP: the descriptor + its drivers |
| `display.cpp` | the panel driver (`hal::Display`) |
| `touch.cpp` | the touch controller driver (`hal::Input`) |

Then list `&<yourboard>::BSP` in `src/boards/boards.cpp`.

This folder is compiled with every firmware so it never stops building, but it is listed nowhere, so the linker
drops it (0 bytes in `firmware.bin`).
