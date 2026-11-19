# Third-party code and fonts in `s3/`

Everything in this folder is Apache-2.0 ([LICENSE](LICENSE)) except the items below, which keep
their own licences.

## In this folder

| Component | Files | Licence | Licence text |
|---|---|---|---|
| QuickJS-ng 0.10.1 (Fabrice Bellard, Charlie Gordon, Ben Noordhuis, Saúl Ibarra Corretgé), unchanged | `lib/quickjs/src` | MIT | `lib/quickjs/src/LICENSE` |
| libopus 1.4 (Xiph.Org and contributors), decoder files only, unchanged | `lib/opus` | BSD-3-Clause | `lib/opus/COPYING` |
| esp_new_jpeg 1.0.2 (Espressif), prebuilt `libesp_new_jpeg.a` and headers | `lib/esp_new_jpeg` | ESPRESSIF MIT License (free for use on Espressif products) | `lib/esp_new_jpeg/LICENSE` |
| ESP-IDF 5.5.5 OpenCores Ethernet MAC and DP83848 PHY drivers (Espressif), for the QEMU build only | `lib/qemu_eth/src` | Apache-2.0 | `lib/qemu_eth/LICENSE` |
| `esp_lcd_st77922` and `esp_lcd_touch` (Espressif) | `src/vendor/*.c`, `src/vendor/*.h` | Apache-2.0 | SPDX headers in the files |
| ST77922 panel init sequence, from the board vendor's LVGL demo (`esp_bsp.c`) | `src/vendor/st77922_init.c` | register values from the vendor's example | |
| TJpgDec R0.03 (ChaN) | `src/pet/tjpgd_pb.c`, `src/pet/tjpgd_pb.h` | ChaN's TJpgDec licence (free use, keep the notice) | header of `tjpgd_pb.c` |
| LVGL's symbol glyphs, copied from `lv_font_montserrat_*.c` (the glyphs come from Font Awesome Free) | `src/fonts/sym_*.c` | LVGL: MIT; Font Awesome Free font: SIL OFL 1.1 | [LVGL licence](https://github.com/lvgl/lvgl/blob/master/LICENCE.txt), [Font Awesome Free licence](https://fontawesome.com/license/free) |
| Ubuntu font family (Canonical), and the bitmap fonts made from it | `data/fonts/Ubuntu-*.ttf`, `src/fonts/ub_*.c`, `src/fonts/zfonts.c` (the same bitmaps deflated by `tools/fontconv/zfonts.py`), the Vietnamese and Cyrillic font packages that `tools/i18n.py` builds | Ubuntu Font Licence 1.0 | `data/fonts/UFL.txt` |
| Noto Sans SC, JP and KR (Adobe, Google), and the glyphs made from them | `data/fonts/noto/*.otf`, `src/fonts/lang_names.c`, the CJK font packages that `tools/i18n.py` builds | SIL Open Font License 1.1 | `data/fonts/noto/LICENSE-OFL.txt` |

## Fetched when you build (not in this folder)

| Component | Used by | Licence |
|---|---|---|
| LVGL 9.2.2 | firmware, simulator | MIT |
| ArduinoJson 7.2.1 | firmware, simulator | MIT |
| Arduino core for ESP32 (pioarduino 55.03.311) | firmware | LGPL-2.1 |
| ESP-IDF (prebuilt in the Arduino core) | firmware | Apache-2.0 |
| esbuild, TypeScript, jpeg-js, pngjs, serialport | `sdk/` (npm) | MIT, Apache-2.0, BSD-3-Clause, MIT, MIT |
| lv_font_conv | `tools/fontconv` (npm) | MIT |
| Astro, Starlight, Fontsource Inter and JetBrains Mono, sharp | `site/` (npm) | MIT, MIT, SIL OFL 1.1, Apache-2.0 |

The Arduino core is LGPL-2.1: if you ship firmware binaries, follow its terms (offer the core's
source and a way to relink).

## Not in this repository

- **Games.** MegaBlock (William Robertson, xylophones.net, Apache-2.0) and Space Shooter (public
  domain) are packaged for the store from their own downloads; their `.jar` files are not here.
  The docs show screenshots of them. Other phone games are never included: users load their own.
- **PawBoop's packs.** PawBoop and its dog Biscuit (`src/pet`, `src/run`) are the EspOS owner's own
  game. The packs with its pictures and voice lines (`pb_dog`, `pb_ui`, `pb_voice`, the run game's
  roads, and the bubble fonts made from Lexend, SIL OFL 1.1) are built and published separately and are not in
  this repository.
- **The wake word's data and models.** `wake/` holds the keyword spotter's code, training scripts and evaluation
  reports on generated voices. The training data (TTS voices, public speech, music, noise and dog-bark corpora under
  their own licences, listed in `wake/README.md`) and the trained models (`.ewm`) are not in this repository.
- **The aquarium's art.** Hồ cá cảnh (`src/tank`) is part of the PawBoop game; its pictures and data are built into
  its own package and are not in this repository.
- **Simulator data.** `sim/data*` folders hold a device's settings, cached answers and packages;
  they are made on your computer and never committed.

## Original work

The J2ME class library (`j2me/classlib`) and virtual machine (`src/j2me`) are written for EspOS:
they implement the CLDC 1.1 / MIDP 2.0 APIs from their specifications. The icons and wallpapers
(`src/art.c`, drawn by `tools/art/make_art.py`) are EspOS's own.
