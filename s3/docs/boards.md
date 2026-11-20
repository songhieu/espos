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

## The 2.8" board: a 360x480 UI at 2/3, the whole panel

Since 2026-11-20 (the owner: "vẫn còn khoảng trống 2 bên") the 2.8" board's UI is 360x480 (`display.ui` in its
descriptor; `hal::uiW()` / `uiH()` at run time), so the same 2/3 gives exactly its 240x320 panel: no black bars, the
same sharpness. Before, it showed the 3.5" board's 320x480 at 213x320 with 13 px of black each side. Each picture goes
to the panel at 2/3 (src/scale.h `down32`: every panel pixel is the area average of the 3x3 UI pixels it covers, so
shapes and text keep their proportions).

- The OS's screens follow the screen's size (`os::W()` / `os::H()`): lists, Settings, the Store, sheets, the keyboard,
  the Control Center, Today, the claim and lock screens are 360 wide. Home keeps its 4 x 5 grid of 78 x 74 cells and
  the widgets their boxes, in the middle (GX 24); in landscape (480x360) the grid, the dock and the dots sit 20 px
  lower (LY).
- What is laid out for 320 x 480 (480 x 320 turned) keeps that size in the middle (`os::colX()` / `colY()`,
  `os::column(body)`): PawBoop (white around it; landscape: its parts, 20 px lower), the run game (dark sides, no
  status bar; its direct path fills the sides), the aquarium (its 213 x 320 water in the middle of the panel's 240,
  black sides, `panelPiece`), the saver's pages (`desksaver::canvas`; the background covers the whole screen) and the
  device's saver editor. Full-screen photos (wallpaper, page backgrounds, the saver, Photos) take the 320x480 /
  480x320 copy of their orientation and cover the screen with it (photo.cpp `screenDim`, desksaver `holdPic`).
- LVGL renders bands of 69 rows (a multiple of 3; 360 x 69 x 2 = 49,680 bytes, about the 3.5" board's), areas widened
  to whole rows and aligned to 3; each band is scaled into one of two small DMA buffers and sent while LVGL renders
  the next.
- The run game's direct path (lcd::direct) does the same with its rows (y0 aligned to 3).
- Touch: the panel's (x, y) back to the UI's: `x = px * 3/2`, `y = py * 3/2` (src/boards/es3c28p/touch.cpp readFt, from
  the descriptor's UiMap).
- Landscape (rotations 90 and 270): the UI is 480x360 and goes out at 2/3 = the turned panel's 320x240. The ILI9341
  turns itself (MADCTL row/column exchange: 0xE8 for 90, 0x28 for 270, 0x48 portrait), so there is no shadow frame or
  software turning; LVGL's bands are 51 rows of 480 (the rounder keeps whole groups of 3), each scaled in two halves of
  240 so the 3 x 240 scratch is enough. Touches need nothing new. The MADCTL directions are not yet seen on a board.
- Remote control and the mirror (lcd::frame) send the 360x480 UI (the page sizes its canvas from the frame).

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
