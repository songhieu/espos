# Hồ cá cảnh (the aquarium): engine "tank"

From the PawBoop designer's hand-off of 2026-11-19 (`handoff_tank`,
BRIEF_TANK.md; a copy of what is used is in `data/tank/handoff`). The browser page `esp32/sim/tank.html` is the
reference for look and behaviour. Owner's words: fish and tank like photographs, smooth motion first,
45 species fresh and marine, many keeper's rules, upright and on its side, 3.5" and 2.8" boards, and the standby screen
is the tank.

## Files

| | |
|---|---|
| `src/tank/tank.h` | the engine's face: `open()` (pkg::run engine "tank"), `saverBg()` (saver page background), `dev()` |
| `src/tank/tank_int.h` | the parts' shared header: data, the water (Scene), drawing target, rules, screens |
| `src/tank/data.cpp` | `tank.json` read by tables into PSRAM structs; the PBU1 packs' tables; pictures copied padded into PSRAM on first use; the look's background at the frame's size (resampled when only the other board's part is installed); the package's Vietnamese Ubuntu fonts |
| `src/tank/water.cpp` | the water: `fw/tank_ref.c` carried over (its functions and numbers) + what `ref/engine.js` adds (finger, taps, rings, coins, sponge, turning the board, lamp and dirt fading). `prepare()` once a frame, `draw()` per piece of the picture |
| `src/tank/gfx.cpp` | the screens' drawing at the frame's scale: glass panels (blurred shadow, made once per size), text lines (made once, shrunk by area on the 2.8" board), icons |
| `src/tank/game.cpp` | `ref/game.js`: every rule and sentence; save `/littlefs/tank.sav` (binary: ids as words) once a minute and on what matters |
| `src/tank/ui.cpp` | `ref/ui.js`: top bar, dock, need chips, food chooser, sponge, fish card, shop sheet (7 tabs), toasts, standby face |
| `src/tank/screen.cpp` | the app screen, frame loop, panel path, saver background, bench, dev commands |
| `tools/tank/make_pkgs.py` | the store package (below), `--sim DIR` installs it on a simulator card |

## Drawing

- Units are the screen's (320 x 480 or 480 x 320); the picture is that times SC: 1 on the 3.5" board, 2/3 on the
  2.8" board, where the water and the screens are drawn straight at the panel's 213 x 320 (320 x 213 on its side),
  nothing is shrunk afterwards (the hand-off's suggestion; less than half the pixels).
- `lcd::native()` (added to `hal::Display`, src/lcd.h): the whole screen in pieces, past LVGL. 3.5" upright: the 80-row
  DMA bands; 3.5" on its side: blocks of 80 logical columns filled straight in the panel's row order (no shadow frame,
  no turning pass); 2.8": the panel's own pixels in its scaled-band buffers. The water is never drawn under the shop
  sheet. When something of the OS lies over the game (notification, Control Center) or a panel has no native path
  (the simulator), the same picture goes into a PSRAM buffer that LVGL shows.
- 2.8": black bars kept (13 px each side upright, 13/14 rows on its side), as the OS: the touch map, the status bar,
  the gestures and every other screen live in that 2/3 box, and the art is 2:3. Filling 240 x 320 would need new
  backgrounds and a third layout for one app.
- Pixels: 565 colours spread to scale in one multiply (the reference's `ex/mix`), pictures padded by one clear pixel
  so the sample between pixels has one bounds test, 16.16 steps (no seams between pieces), the beams' table and the
  body's powers worked out once, the background re-tinted only when the light or the dirt moved a visible step.

## Packages

`vn.pawboop.tank` 1.0.0, engine `tank`, min_fw 506: core (tank.json, fish/things/thumbs packs, tank_ui.pbu = the
machines' glyphs and the two foods drawn from ui.js's canvas paths, font/r12,r16,b12-b16.bin = dev.espos.font.vi's
files, icon.jpg) 1.2 MB + group "bg": bg35 (default, "board": "es3c35p", 3.0 MB) or bg28 ("board": "es3c28p",
1.3 MB). The device takes the part made for its board, whatever the web asked (store.cpp fitBoard). Built by `tools/tank/make_pkgs.py`
into `build-pkgs/espos/s3/` with `vn.pawboop.tank-1.0.0-catalog.json` for `scripts/publish_pkg.py --meta`.
Not published.

## Saver background

`{"t":"tank"}` (docs/saver.md §2, "dim" as the others): the user's own tank, ~12 pictures a second through LVGL, the
page's clock and widgets on top, a soft dark fade at the top and the need chips at the bottom left (the standby face).
No aquarium installed: a deep-water gradient. The device's background sheet offers it on saver pages when installed.

## Dev

Board: `TANK bench` (5, 12, 20 fish, 6 s each: pictures a second, move/prepare/draw/push ms; `TANK bench 20`),
`TANK fps 20`, `TANK tear 1`, `TANK free planted neon*7,guppy*2`, `TANK game`, `TANK time 8`, `TANK coins 500`,
`TANK xp 150`, `TANK look reef`, `TANK night 1|0|-`, `TANK speed 4`, `TANK reset`. Simulator: `tank <same>` plus the
QA knobs (`add ID N`, `sick`, `algae N`, `clean N`, `fed N`, `card [sick]`, `tab N`, `food`, `sponge`, `scroll PX`,
`say TEXT`, `shot FILE.ppm` = the panel's picture with its black bars).

## Flash

The tank is ~130 KB of the image. To make room, 21 of the 27 built-in fonts keep their glyph bitmaps deflated in
flash (`src/fonts/zfonts.c`, `tools/fontconv/zfonts.py`, run by make_fonts.sh) and `fonts::begin()` inflates them
into PSRAM (+237 KB PSRAM, ~150 KB less flash); r12/r14/r16/b12/b14/b16 stay as they were (LVGL's default font and
the Java phones' fonts use them by name).
