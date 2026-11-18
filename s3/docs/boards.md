# Boards

One firmware for both boards (env `s3desk`, `espos-s3-0.4.0`). At boot, `hal::detect()` (src/boards/boards.cpp, called
as `board::detect()`) asks who answers on which I2C pins:
- the 3.5" board's codec/touch on GPIO 38/39;
- else the 2.8" board's FT6336/codec on 16/15;
- else the 3.5" board, as before.

Each board is a board support package in src/boards/<name>/ (docs/porting.md): its descriptor (`hal::Board`: panel,
UI map, capabilities, pins by function), its panel and touch drivers, and its probe. The OS reads the found board through
`hal::board()`; the codec, the card and the battery are set up from the descriptor. QEMU cannot be asked, so there the
board is the build's: `s3desk-qemu` and `s3desk-28-qemu` (src/boards/qemu).

| | ES3C35P (3.5") | ES3C28P (2.8") |
|---|---|---|
| env | `s3desk` (both boards) | `s3desk` (both boards); QEMU: `s3desk-28-qemu` |
| panel | ST77922 320x480, QSPI | ILI9341V 240x320, SPI 40 MHz (+ D/C) |
| touch | the vendor's ST77922 driver, I2C 38/39 | FT6336G at 0x38, I2C 16/15, reset 18 |
| codec | ES8311, I2S 17/18/21/15/16 | ES8311, I2S 4/5/7/8/6 |
| card | SDIO 4-bit 5/4/6/7/2/3 | SDIO 38/40/39/41/48/47 (1-bit tried if 4-bit fails) |
| found by | its codec at I2C 0x18 on 38/39 | its touch 0x38 / codec 0x18 on 16/15 |
| BSP | src/boards/es3c35p/ (display.cpp, touch.cpp) | src/boards/es3c28p/ (display.cpp, touch.cpp) |

Pins of the 2.8" board: from the vendor's page (lcdwiki.com/2.8inch_ESP32-S3_Display, read 2026-11-17).

## The 2.8" board: the same UI at 2/3

The UI stays 320x480 on both boards: every screen, app, widget layout and the run game are laid out for it, and the
JS apps and the store's packages assume it. On the 2.8" board each picture goes to the panel at 2/3 (src/scale.h
`down32`: every panel pixel is the area average of the 3x3 UI pixels it covers, so shapes and text keep their
proportions), 213x320, centred with 13 px of black each side.

- LVGL renders bands of 78 rows (a multiple of 3), areas widened to whole rows and aligned to 3; each band is scaled
  into one of two small DMA buffers and sent while LVGL renders the next.
- The run game's direct path (lcd::direct) does the same with its rows (y0 aligned to 3).
- Touch: the panel's (x, y) back to the UI's: `x = (px - 13) * 3/2`, `y = py * 3/2` (src/boards/es3c28p/touch.cpp readFt, from the descriptor's UiMap).
- Landscape (rotations 90 and 270, since 2026-11-18): the UI is 480x320 as on the 3.5" board and goes out at 2/3 =
  320x213, the full panel width, panel rows 13-225 of 240 (13 black rows above, 14 below). The ILI9341 turns itself
  (MADCTL row/column exchange: 0xE8 for 90, 0x28 for 270, 0x48 portrait), so there is no shadow frame or software
  turning; LVGL's bands are 51 rows of 480 (the rounder keeps whole groups of 3), each scaled in two halves of 240
  so the 3 x 213 scratch is enough; the UI's last 2 rows make one panel row (`scale::tail32`). Touches need nothing
  new: the UI keeps its place on the glass in every rotation. The MADCTL directions are not yet seen on a board.
- Remote control and the mirror (lcd::frame) send the 320x480 UI, as on the 3.5" board.

## Updates and the web

- One image for both boards, so one firmware record and one OTA path.
- The device sends `board` (`es3c35p` or `es3c28p`) in every sync body and in `diag`.
- The server returns it in the device list (`list_devices`), and the web names the screen: "EspOS S3 · màn 2,8 inch".
- The USB installer shows the board from the device's `ESPOS:READY` hello.

## Checked

- 2026-11-17, in QEMU (`tools/qemu/run.sh --board 28`, docs/simulator.md): Home, the app library, Settings, PawBoop,
  the run game from the card (19.7 pictures a second), the black bars, touch through the viewer.
- Not yet on the board: the ILI9341 init and colour order, the FT6336 reading and its axes, the codec, the card's
  4-bit wiring, the battery divider, the real frame rate (40 MHz SPI: ~31 ms a full panel).
