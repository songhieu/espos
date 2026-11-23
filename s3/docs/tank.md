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

### The food (2026-11-21)

The owner found the old food fake: green tablets and plain dots. `foodPiece()` (water.cpp) now draws each piece, both
in the water and, bigger and untinted, as the chooser's icons (`ui.cpp foodArt`):
- Cám (flakes): thin chips with five uneven corners (four shapes), mostly warm colours with now and then a green
  one. They turn over as they sink, narrower and darker when seen edge on, and lie flat on the sand.
- Viên chìm (pellets): round brown or reddish-brown granules, dark underneath, with a light spot on top.
- Mồi tươi (live bait): bloodworms, twelve overlapping beads in two reds along a wriggle, thinner at the ends.

The package's `f_sink` / `f_live` pieces (tank_ui.pbu) and the hand-off's `f_food` can are no longer drawn; the dock
keeps `i_food`. QA: `tank drop K N` drops N pieces of kind K (0 flakes, 1 pellets, 2 worms) for shots.

### The blinking green and blue blocks (fixed 2026-11-21)

The 2.8" board showed about 15 small saturated green or blue blocks that blinked. They were the 16 drifting specks:
only their place was ever set, so their radius was 0. `disc()` then worked out every weight as NaN. The S3 turns a
NaN into a huge integer, and the mix with that weight painted wild colours; the simulator (arm64) turns it into 0,
so it never showed them. The specks now get their depth, sway and size as engine.js gives them (`setView`), and
`disc()` draws nothing without a size and caps its weight at 32. QEMU (Xtensa) shows the blocks, so check tank
drawing there, not only in the simulator.

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

## Speed on the board (2026-11-19)

The owner's 3.5" board on its side (0.5.6, 6 fish) ran at 4–12 pictures a second:
- per picture: bg 8.4 ms, plants 7.4, fish 6.7, specks 2.3, beams 4.4, algae 34, screens 2;
- with the shop open, the sheet alone took 30–160 ms;
- QEMU had said algae 5 ms: QEMU does not model the board's PSRAM or its CPU speed.

Changes:
- **The algae film is baked.**
  - Its weight per pixel (`Scene::filmW`, PSRAM) is made from the grid only when the grid changes. The sponge
    marks just the part it wiped (`filmDirty`).
  - The background carries the film (`bake()`), and the things drawn over the background take it as they are drawn
    (`put()`): film(thing over bg) = film(thing) over film(bg).
  - The per-picture film pass is gone. Beams, specks and bubbles go over the film (light on the glass).
- **The background by columns.** When pictures go out a column at a time (the 3.5" on its side: `dx != 1`,
  `dy == 1`), `bgc` is kept by columns (`bgCols`), so the background goes out as whole runs (memcpy) read straight
  from PSRAM. Test it in the simulator with `tank shot cols:FILE`, which draws as the board does on its side.
- **The shop sheet as pictures.**
  - The head (title, coins, tabs) and each row are pictures in PSRAM: `drawSheetHead`, `drawRow`, and the last 8
    rows in `rowPics`.
  - They are made again only when what they show changes (`rowKeys`, the head's numbers).
  - A picture of the sheet is copies of them at the scroll's place, so scrolling draws nothing.
- **The water stands still under the sheet.** The part beside it (dimmed water, the top bar) is captured once
  (`frameBegin`/`frameEnd`); the scene is then neither prepared nor drawn. Toasts and flying numbers go on top. It is
  captured again when the bar's numbers change.
- **Under the dog's pop-up** the tank pauses like the other games (`onScreen()` has `!pet::summoned()`, as
  pet/app.cpp and run.cpp): game time and water stop, the last picture stays in LVGL's buffer, and the next picture
  after it closes goes on from there. The saver's tank background holds the same way.
- **Under other OS overlays** (a lock sheet, a notification), LVGL draws the whole screen, so the water goes on at
  4 pictures a second there.
- **Room for the saver's water** (0.6.2 hotfix, 2026-11-23). The owner's 3.5" lost the aquarium on its second saver.
  After the dog's greeting, PSRAM was 1208 KB free with its largest block 847 KB. Home's 4 page pictures (~300 KB each)
  and the dog's decoded pictures (3 slots, kept after the pop-up closed) held it. `ensure()` could not make the water,
  said nothing, and the saver drew the deep-water gradient.
  - `room()` runs before the saver's water (and the app) is made: `home::releasePictures()`, `pet::dog::release()`
    when her pop-up is closed, and `pet::pk::trim()`. All are made again when shown.
  - If the background's three buffers (bgRaw, bgc, film) still do not fit, `Scene::allocBg()` makes the light water:
    one layer, bgc is bgRaw (the look's picture as it is: no film, no tint or haze, by rows). The line reads "tank:
    light water: no room for all of it ...". Only with no room for even that is the gradient drawn, with "tank: no
    room for the saver's water (PSRAM ...)".
  - Simulator: `SIM_TANK_LITE=1` makes the light water.
- **Under the saver's editor** (2026-11-23, the owner: dragging the Claude widget on an aquarium page lagged). The
  editor sits over the saver, and the saver's water kept making 5 pictures a second behind it. Each one is ~150 ms on
  the board and made LVGL redraw the whole editor, so a drag stopped 5 times a second. Now `onSaver` holds the water
  (its clock too) while `saveredit::isOpen()` (the editor always opens over the saver, from a long press or from
  Settings). LVGL redraws only what the finger moves, and the water swims on when the editor closes. The editor's
  own page asks `saverBg` too while the saver's water exists. It gets `stillOf`: a copy of that last picture in
  PSRAM, freed with its image, with the fish where they stopped. The editor also hides what it covers while open
  (docs/saver.md §3, Speed: ~22 → ~60 redraws a second while dragging).
- **The top bar, the dock and the fish card** (2026-11-20). They took 15–32 ms a picture on the board (0.5.6).
  - The main cost was not drawing. On its side the board asks for a picture in six pieces, and `ui::draw()` runs for
    each. Every piece read the game again: the comfort checks every fish, the jobs and the lamp read the clock
    (`localtime`), and the number strings were rebuilt. Now `hud()` reads them once a picture. The slow ones are read
    twice a second, or right after a tap (`hudSoon()`).
  - The bar's glass and icons, the bar's numbers, each dock button and the fish card are prepared layers
    (`Layer`). Each is drawn once over black and once over white, which gives every pixel's colour and how much it
    covers (`unmix`). It is kept as runs of covered pixels in PSRAM and remade only when its key changes. A picture
    only mixes the runs onto the water (`layerPut`, O2).
  - What changes all the time stays live: a gauge under 30 blinks, so it is drawn each picture, and so are the
    badges.
  - Result: pictures match straight drawing at 45–48 dB (at most 2 steps of 31 on a pixel). In QEMU on its side
    (12 fish, bar shown), screens went from 13.9 to 2.2 ms.
  - A layer is made 16 rows at a time (small buffers) and kept in one PSRAM block. Six or more pixels alike (the
    glass's inside) are one entry, so putting them reads nothing.
  - A layer seen at part cover would fade as one group (its icon solid on its glass); the page fades each piece by
    itself. So while the bar fades, its pieces are drawn straight. The needs icons pulse all the time, so they are
    three layers, one for each kind of piece (the glasses, the icons, the dots), and fade the same either way.
- **The board's slow maths and memory** (build 6 lines from the board, 2026-11-20).
  - `sinf`, `cosf` and `powf` of the C library work in double precision, which the S3 does in software: about 7 µs
    a call. The swim curve (20 segments a fish) took 6.6 ms of prepare with 6 fish. Now `fsin`, `fcos` and `fpow`
    are float-only (within 2e-4 and 1e-4).
  - The fish are drawn down columns, but their pictures were kept by rows, so each pixel read new PSRAM cache lines.
    `spr()` now also keeps each fish picture by columns (`Spr::t`, colour | alpha << 16) and `blitFish` reads that;
    the pictures are the same as before (`tank plaincmp`).
  - The plants' sway (powf and sinf a row) was worked out by `blitRows` for every piece of a picture (six on its
    side). `prepare()` now works it out once (`Frame::sway`).
  - The background re-bake wrote one pixel per cache line on its side; it now goes in blocks of 16 rows. `makeBg`
    skips a re-bake when the haze or light moved without changing its tables. A drift re-bakes 48 rows a picture
    (`filmSoft`), and `setAlgae` marks only the cells that grew. The game asks for algae every tick, and the bench
    had been re-baking the whole film each picture, 28–48 ms.
  - The fish read the film down columns too: `filmT` is the film weights by columns, made with the algae.
  - QEMU, on its side, 12 fish: draw 12–14 → 6–8 ms, prepare 2–4 → 1 ms. On the board, PSRAM makes the gains
    larger.
  - The items are put far to near by an insertion sort. `std::stable_sort` took 1.9 KB of flash and a buffer
    every picture.
- **Board build 7** (2026-11-20, HUD layers in): with the bar up, 13.8–15.8 pictures a second. Bar and dock cost
  ~5 + ~9 ms whether the layers were on or off. The QA stats line now says how many layers were made or found no
  room, what `layerPut` cost (ms and pixels a picture), and how much PSRAM is free and in one piece.

## Dev

Board: `TANK fps 20`, `TANK tear 1`, `TANK game`, `TANK save`, `TANK stats` (and the "tank: N fish ..." line every
10 s; its draw and prepare parts only with `-DTANK_QA=1` since 2026-11-23, flash for the saver fixes). Simulator (or
a board built with `-DTANK_QA=1`; left out of the release image since 2026-11-19, the app slot is full): `tank <same>` plus `bench` (5, 12, 20 fish, 6 s each: pictures a second,
move/prepare/draw/push ms; `bench 20`), `free planted neon*7,guppy*2`, `time 8`, `coins 500`, `xp 150`, `look reef`, `night 1|0|-`, `speed 4`, `reset` and the QA knobs
(`add ID N`, `sick`, `algae N`, `clean N`, `fed N`, `card [sick]`, `tab N`, `food`, `sponge`, `scroll PX`,
`say TEXT`, `shot FILE.ppm` = the panel's picture with its black bars).

More QA commands (2026-11-20):
- `keep` / `restore`: saves now and copies the save to `tank.qa`, then later puts it back and loads it, with the
  time in between counted as time away. Use them around tests on someone's own aquarium.
- `layers 0|1`: the bar, the dock and the card drawn straight or from their layers, for timing.
- `hudcmp PATH`: the same picture with layers (PATH.ppm) and straight (PATH-direct.ppm). A `cols:` prefix works as
  with `shot`.
- `plaincmp PATH` and `plain 0|1`: the fish by columns and the sway table, or the old paths by rows.
- `layoutcmp PATH`: the same picture drawn by rows (PATH-r.ppm) and as the board on its side (PATH-c.ppm); they
  should match exactly.
- The stats line gets a fourth line, `screens parts ms: bar, bar live, dock, card, sheet, rest`.
- The simulator has `summon [REASON]`, Biscuit's pop-up over the current screen.

## Flash

The tank is ~130 KB of the image. To make room, 21 of the 27 built-in fonts keep their glyph bitmaps deflated in
flash (`src/fonts/zfonts.c`, `tools/fontconv/zfonts.py`, run by make_fonts.sh) and `fonts::begin()` inflates them
into PSRAM (+237 KB PSRAM, ~150 KB less flash); r12/r14/r16/b12/b14/b16 stay as they were (LVGL's default font and
the Java phones' fonts use them by name).
