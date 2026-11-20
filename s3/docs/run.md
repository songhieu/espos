# Chạy lấy xu (PawBoop's run game) on EspOS

Biscuit's three-lane dodge run from the PawBoop phone game ("RUN"), from the game designer's handoff
(`pawboop_run_handoff`: BRIEF_RUN.md, `sim/run.html`). A package of the OS (`docs/ecosystem.md`): id
`vn.pawboop.run`, kind native, engine `run`, an add-on of PawBoop (`"addon_of": "vn.pawboop.pet"`): the owner's
"gộp 2 game cho lại là 1 đi game chạy để lấy vàng mua đồ ăn cho chó ở game kia" (2026-11-16). The rules, numbers, pictures, HUD and cards are the page's (English, as
in the phone game). Where the brief and the page differ, the page was followed.

## Eight maps, ten dogs (2.0.0, hand-off 3, 2026-11-18)

- The run package `vn.pawboop.run` 2.0.0 holds only `core/run_things.pbs` (26 things), `core/run_ui.pbu` (a start card
  for each map) and `run.json` (0.2.0: the ring bazaar, construction, carnival, city, lantern, park, snow, beach and
  each map's things). Each map is its own add-on, `vn.pawboop.map.<map>` (`"needs": "vn.pawboop.run"`): its road at 20
  pictures a second and the gate to the next map of the ring (`link_<map>_<next>.pbv`), used only when that map is
  installed too ("a link needs both of its maps"); otherwise the 1 s fade between the two installed maps.
  `World::openRoads` opens the maps' packages (`pkg::open`, it runs on the decoder task); run 1.0's roads parts still
  work when no map package is installed. Up to 16 road files (8 maps, 8 gates): a fade key now has 4 bits a road
  file and the fade's weight in 64 steps (`media::fadeKey`).
- The dog is the chosen one (`s3.pet_dog`, `src/pet/dogs.h`): its package's `run.pbs` (the same clip names, flags and
  anchor as Biscuit's); run 1.0's `core/run_dog.pbs` when it has none.
- The start card: "‹ ›" buttons either side when more than one map is installed choose the map the run starts in
  (kept in NVS `run`/`map`). The start cards are read one at a time when shown (eight would hold 0.9 MB of PSRAM).
- The dog speaks in the run (its voice pack, no new lines): `run_start` on the start card, `run_hit` when a heart is
  lost, `run_over` when it gets dizzy, `run_best` on the end card after a new best.
- "The coins you collect go to {0}'s purse": the chosen dog's name.
- Checked (live simulator, board speed, everything on the card): every map's start card and things, every gate
  (bazaar > construction > carnival > city > lantern > park > snow > beach > bazaar), 20 pictures a second, road
  17/s, the arrows, every dog running; QEMU (the real firmware): Bao's run from the card.

## One game: PawBoop (the run earns her food)

- Home shows PawBoop only. In the pet screen's header, between the level chip and the coin chip (x 152..231,
  y 33..54), an orange pill "Chạy lấy xu" (the run's own orange, `src/pet/app.cpp`). Tapping it:
  installed and available -> `pkg::run("vn.pawboop.run", "pet")`, the run opens over her screen (the back gesture
  and "Về nhà" come back to her); on a card that is out -> "Cần thẻ nhớ" (`pkg::run`); not installed ->
  `apps::open("store", "vn.pawboop.run")` (the store shows it as PawBoop's add-on). Her decoded pictures are freed
  while the run is open (PSRAM for the run).
- Opened from the pet (`arg "pet"`), the run's start card gets "Xu chạy được sẽ về túi Biscuit" and "‹ Về nhà" under
  it; the end card gets "‹ Về nhà" (tap elsewhere: run again, as the card says). Drawn once by LVGL (kit:: look) and
  baked into sprites (`bake()`), because the game draws past LVGL.
- The coins go into her purse when the run ends (or is left), as before (`pet::game::addCoins` + save), and the
  run tells the pet (`pet::app::arrived(n)`): when her screen is in front again, "+N xu" rises from her purse and
  she says her @coin line "Vàng nè khanh! Bổn công chúa kiếm được đó!" (no new line). The header shows the new
  total at once. Rules, numbers and lines unchanged (+10 % when she is healthy stays).
- Simulator: `tools/pawboop_run/tests/merge.txt` (the whole loop: pet -> run -> end card -> Về nhà -> +N xu).

## Files

| Path | What |
|---|---|
| `src/run/run.h` | `run::open(pkgId, arg)` (engine "run"), `run::bench()`, `run::dev(cmd)` |
| `src/run/run.cpp` | the screen: one 320x480 RGB565 buffer in PSRAM shown by an `lv_image`, the page's `present()`, `hud()`, `card()`; swipes; stats; bench |
| `src/run/logic.cpp/.h` | the page's rules carried over line by line (`R`, `act`, `spawnRow`, `step`, `hit`, `pay`, `track`, `linkAhead`, `dogFrame`, `setClip`, `thingFrame`), numbers from `run.json`; doubles like the page's JavaScript |
| `src/run/draw.cpp/.h` | the pixel loops: road scale x2 (smooth / plain), the page's blob shadow, sprites, UI pieces (built `-O2`; the rest of `src/run` is built for size) |
| `src/run/media.cpp/.h` | packs (PBS2 dog and things, PBU1 pieces: whole in PSRAM), roads (PBV1, one picture at a time), JPEG + mask decoding (the pet's private TJpgDec), sprite cache, road slots, decoder tasks |
| `tools/pawboop_run/make_pkgs.py` | builds the three `.espk` parts + icon + catalog entry into `build-pkgs/`; `--sim DIR` installs core + one roads part on the simulator's memory card (`tools/espk.py --install-sim`) |
| `tools/pawboop_run/sim.sh`, `simdrv.cpp` | the simulator with the run's script commands (`run`, `dev`, `swipe`, `bench`, `shots`) |
| `tools/pawboop_run/tests/*.txt` | scripts: `basic` (start card, moves, end card), `links` (all three links), `fades` (roads10s: the three maps through fades), `asan` (several full runs, close, reopen, bench), `petcard` (the pet with its packs on the card) |
| `tools/pawboop_run/pkgstub.cpp` | simulator only: the first temporary `pkg.cpp` (files from `$SIM_DATA/pkgs/<id>/`), for when `src/pkg` does not build |

Every file is read through `pkg::open(id, name, f)`: from the internal flash `f.data()` is memory-mapped; from the
card `f.data()` is null and `f.read()` is used. Same code both ways.

## The package (`build-pkgs/`, not uploaded)

| Part | Files | .espk bytes |
|---|---|---|
| `core` (req) | `core/run_dog.pbs`, `core/run_things.pbs`, `core/run_ui.pbu`, `run.json`, `icon.jpg` (120x120, Biscuit mid-leap on the app's orange) | 2,010,629 |
| `roads10` (group roads, default) | `roads10/road_<map>.pbv` x3, `roads10/link_<a>_<b>.pbv` x3 | 5,767,249 |
| `roads20` (group roads) | the same at 20 pictures a second | 11,528,834 |
| `roads10s` (group roads) | `roads10s/road_<map>.pbv` x3: roads10's maps, no link files ("Gọn (3 map, không cảnh nối)") | 3,144,401 |

core + roads10s = 5,154,966 bytes: fits the internal app area (7,528,448) once PawBoop is removed; core + roads10 =
7,777,814 does not. roads10 stays the default. Installed: one part of the group (roads20, else roads10, else
roads10s is used when more than one is there).

Version 1.0.1 (code 10001), name "Chạy lấy xu", `addon_of` vn.pawboop.pet. `vn.pawboop.run-1.0.1-catalog.json` has each part's size, SHA-256, group, label and default for the store (urls are
added by publish). The files are checked against the handoff's SHA-256 list and copied unchanged; the builder is
deterministic (adding roads10s left core, roads10, roads20 and the icon byte for byte the same).

## How a picture is drawn (20 a second)

The page's `present()` becomes a list of drawing steps (`build()` in run.cpp), drawn band by band (`draw.cpp`):
1. **Road**: the picture for this distance (232x240, `track()`), scaled x2, columns `X0 .. X0+319` of the 464 px
   wide world, `X0 = round(72 + x * 69)` (x = her place in lanes). *Smooth* (default): bilinear x2 (screen pixels on
   a source pixel copy it, the ones between average their neighbours, two pixels per 32-bit operation; 8 ms a
   picture on the board). *Plain*: pixel doubling (3.7 ms; `RUN smooth 0`). roads10/roads10s: a picture covers
   two 0.4 m steps.
2. **Things in front of her** (distance > 0.15 m), far to near: blob shadow (the page's radial gradient, 16.16
   fixed point), sprite at `(bx - X0, by)`; the picture is the one whose `sx` (cm) is nearest the distance.
3. **Biscuit**: blob shadow (half width `sr`, 0.3 x as high), sprite at `(bx - sx + gx, by - sy + 385.5)`; not drawn
   every 2nd picture while safe after a hit; on a clip change the pose before is drawn over the new one at 2/3, 1/3.
4. **Things she has passed** (≤ 0.15 m).
5. **HUD** and the cards (`menu_<map>`, `over3` / `over4`, values right aligned at x 262).

**Straight to the panel** (`lcd::direct()`, added to src/lcd.cpp): rows 24..479 are drawn into LVGL's two 80-row
DMA band buffers (internal RAM) one while the other is sent, past LVGL's image render. LVGL still draws the status
bar's 24 rows (from the PSRAM buffer, `lv_refr_now` right before, so one tear-signal wait a picture), and the game
draws the OS's home line itself. Portrait either way up (the board runs at 180 degrees). When anything of the OS is
over the game (a toast, a notification, Control Center, Biscuit's pop-up) or the screen is rotated sideways, the
whole picture goes into the PSRAM buffer and LVGL shows it as before. The whole picture also goes there once when
the game leaves the front and once when it comes back. `RUN frame` draws one picture that way (a `SHOT` reads
LVGL's buffer).

Fast blends: UI pieces, shadows and sprite edges mix in 32 alpha steps with the three colours of a pixel in one
32-bit multiply-add (within 1/32 of the exact blend); the UI pack is copied into PSRAM with every piece 2-byte
aligned (most sit at odd offsets in the file).

Layout: the OS status bar keeps the top 24 rows over the road's sky (dark text, `os::barDarkFor`), so the HUD sits
24 px lower than on the phone (as the pet game's header). The game's image covers the whole screen under the OS's
top layer: the back strip (left 18 px) and the home strip (bottom 26 px) get their touches first. Portrait only
(landscape shows a message).

Controls (the page): a swipe = 22 px of finger travel, acted on while the finger moves; a touch released before
that is a tap (start, run again).

## Decoding

- JPEG (2026-11-17): Espressif's decoder, `lib/esp_new_jpeg` (the prebuilt `libesp_new_jpeg.a` 1.0.2 for the S3 from
  the `esp_new_jpeg` component, ESPRESSIF MIT License, decoder only; +75 KB of flash). A road picture 14-15 ms
  (TJpgDec: 50 ms), sprites 0.6 us a pixel with the card read (TJpgDec: 1.0). Colours within 1 step of TjpgDec's
  (`RUNBENCH` compares them). It wants one whole JPEG and a 16-byte aligned output: a sprite is put together as
  header + scan + EOI in the decoder's buffer, decoded pictures are allocated aligned. Its tables would go to internal
  RAM first (10 KB a picture, two decoders at once: the minimum fell to 2 KB): `media.cpp` replaces its allocator
  (`jpeg_calloc*`), internal only while `jpegReserve` (96 KB) stays free, so PSRAM in practice (1 ms slower).
  TJpgDec (the pet's copy) stays for a picture Espressif's decoder refuses (counted in the stats line) and in the
  simulator.
- With it roads20 plays every picture (20 a second, as the page's default "Nhẹ, 20/giây"); the script simulator still
  uses every 2nd one (`World::skip`, TJpgDec), the live simulator at the board's speed every one (`media::fast()`).
- Card reads (`pkg::in::readAt`): plain `open`/`lseek`/`read`. A 14 KB road picture: 2.3 ms; through stdio's 128-byte
  FILE buffer it was 8.2 ms, unbuffered stdio 380 ms (one byte a call). `CARDBENCH <path>` on the dev console
  of a qa build (env s3desk-qa) measures the ways.
- Dog (289 KB) and things (1.1 MB) PBS2 packs: memory-mapped when installed in the internal flash (no copy); from the
  card only their tables and frame records are in PSRAM, a frame's bytes are read when it is decoded.
- Sprites are decoded into a cache in PSRAM (up to 3 MB, PSRAM free - 1 MB: 618 KB from the card), least recently
  used out; her run loop (16 pictures) is kept for good. After each picture her next two poses and each thing one,
  two and three steps on are wished to the decoder (things within 12 m urgent: before the other wishes); a swipe
  wishes her new move's first three poses at once. Her pose for this picture is decoded on the UI thread when it is
  not ready (at most 14k px, ~6 ms): an older pose standing in made her leap start late on the screen. A thing's
  picture not ready: decoded on the UI thread when small (≤ 2048 px) or within 6 m (one a picture), else its picture
  drawn last stands in, moved to where the thing is now (its ground point).
- Cache rules (fixed 2026-11-17, "vừa đến gần là chướng ngại vật bị lùi xa ra"): with all 320 entries kept the
  decoder took no more wishes, so a thing stayed on an old picture up the road (up to 10.8 m behind) and jumped when
  it came near; a wish now drops the least recently used picture. A picture wished again is kept (it had its first
  wish's time, so it went before the pictures just drawn). A picture decoded ahead waits a picture rather than take
  the cache more than 64 KB past its budget ("held" in the stats line; the most it held: "max").
- "Chướng ngại vật biến mất lúc gần chạy qua" (2026-11-17): a decode on the UI thread dropped the picture that stood
  in for a thing drawn later in the same picture, so a thing going past her was not drawn for up to 10 pictures.
  Pictures drawn or wished in the last two pictures are never dropped now. Her run loop (450 KB, kept for good) is
  no longer counted in the budget: from the card it left the things 170 of the 618 KB. A near thing is decoded on
  the UI thread only up to 24k px (`NEAR_PX`, ~10 ms; a crate stack by her is 96k px, 36 ms). Live simulator at
  the board's speed: not drawn 0 (was 18-37 in 10 s), things' old pictures 0-0.6 %, cache ~1.1 MB with her run
  loop, within the board's PSRAM.
- Roads: 6 slots of 232x240. Each picture plans the next 5 distinct road pictures (across a loop's end, a link or a
  fade); the decoder fills them in that order; a fade decodes both pictures and mixes them. A picture not ready in
  time: the one shown last stays (counted as "late"). The first picture waits (spinner) until its road is decoded.
- Decoder: a task on core 0 (priority 2, like the pet's; stack in internal RAM). A second one on core 1 at the idle
  priority exists (`RUN helper 1`) but is off.
- The stats line (TRACE every 10 s, `RUN stats`) ends with "old pictures: dog a/b, things c/d, not drawn e, near n,
  back up to x cm" (pictures that stood in for one not decoded in time; near: within 6 m; back: how far up the road
  the oldest stand-in was recorded) and "TJpgDec n (refused m)".
- Measured on the board 2026-11-17 (core + roads20 on the memory card, opened from PawBoop, autopilot): 19.8-20
  pictures a second, road 18.6-20/s (late 0-6 in 10 s), dog old pictures 0, things 1-2 %, card read 3.2 ms a road
  picture, PSRAM ~1 MB free, internal RAM never under 21 KB.

## Rules (the page) and what the OS adds

- Speed 8 m/s +0.5 every 100 m up to 12; rows every 15 m closing to 9.5 m; hearts every 130-200 m while a life is
  missing; 2 lives (max 3); hit = stumble + 1.5 s safe; no life = dizzy, the run slows to a stop, the card after
  1.2 s. A map's loop 3 times (192 m), then the link, then the next map (bazaar -> construction -> carnival ->
  bazaar); 10 m before to 6 m after a link only coins. Only installed maps are used; one map alone loops.
- A link with no file (roads10s, a missing file, or two maps whose own link does not exist) becomes the fade of
  carnival>bazaar (run.json's fade link: 20 pictures = 8 m, about 1 s, the next map's things from picture 10):
  picture k mixes the map's own picture k (its loop going on) with the next map's picture (loop - 20 + k), weight
  (k + 1) / 21, so it ends into the next map's first picture. The handoff's fade was measured against the loops to
  find this (mixing weights 0.04 .. 0.97, close to linear; it was cut from other frames, so it cannot be matched
  exactly). With roads10s each mixed picture covers two steps; the decoder decodes both pictures and mixes them
  (one road job, a second 111 KB buffer per decoder, allocated on the first fade). The page itself loops a map
  forever when a link is missing; this is EspOS's change (2026-11-16).
- In the air = the picture's flags bit 1; low = bit 2. Coins and hearts are not picked up above 1.1 m (the page's
  `hgt < 1.1`): the pack has no height, so the page's heights of the 21-picture jump clip are kept in
  `logic.cpp` (`JUMP_CM`); another jump clip falls back to the shadow's size.
- Pay at the end: coins + 1 per 25 m, +10 % rounded up (`Math.ceil(base * 0.1)`, the same double arithmetic)
  when the pet's four needs average 70 or more (`pet::game::s().need`). The coins go to the pet game's coins
  (`pet::game::addCoins` + `save`). Leaving in the middle of a run pays what was earned so far.
- The best distance is kept in NVS (`run` / `best`, centimetres). Runs under `run::dev` (auto, god, warp, end) or the bench pay nothing.
- The game pauses while it is not the screen in front or the screen saver is on.

## Simulator

The live simulator (`tools/sim-live.sh`, sim/main.cpp + sim/live.cpp, SDL2) runs the whole OS in a window in real
time, the mouse as the finger, with the board's sounds, and the run game as on the board: its decoders in their own
thread, each picture taking the board's time (road 16.3 ms + 3.3 ms from the card, a sprite 1 ms + 0.36 us a pixel,
measured 2026-11-17), and heap_caps PSRAM counted against the board's (calibrated: 1642 KB free when the run starts
from the card), so the sprite cache gets the board's 618 KB. `--fast`: the computer's speed. Measured in it
(2026-11-17, roads20 on its card, autopilot): 20 pictures a second, road 17.4-20/s, road picture 20.3-20.7 ms, things
old pictures 0.1-0.7 %, PSRAM ~1 MB free at the end, like the board. ThreadSanitizer: no race
(`BUILD=build-live-tsan EXTRA=-fsanitize=thread make -C sim live`).

The script simulator:

```
cp -R sim/data sim/data-run
python3 tools/pawboop_run/make_pkgs.py --sim sim/data-run --roads 10   # or 20: on the card
tools/pawboop_run/sim.sh tools/pawboop_run/tests/basic.txt OUT                 # builds sim/build-run + driver
python3 tools/espk.py --install-sim sim/data-run10s build-pkgs/*-core.espk build-pkgs/*-roads10s.espk   # fades test
SIM_DATA=sim/data-run10s tools/pawboop_run/sim.sh tools/pawboop_run/tests/fades.txt OUT
BUILD=build-run-asan EXTRA="-fsanitize=address -fno-omit-frame-pointer" tools/pawboop_run/sim.sh tools/pawboop_run/tests/asan.txt OUT
PKGSTUB=1 ...   # only if src/pkg does not build: a stub reading $SIM_DATA/pkgs/<id>/<name>
```
Driver commands: `run [map|bench]`, `dev auto|manual|god|warp M|end|smooth 0|1|helper 0|1|stats`, `swipe
left|right|up|down`, `bench`, `shots N MS NAME`, `back`, `home`, plus the simulator's own.
Installed, it also opens in the normal simulator and from Home: `app vn.pawboop.run` (pkg::run -> run::open).

Checked in the simulator (2026-11-16): start card; lanes and the pan; jump; slide; hits and the blink; dizzy;
end card with and without the healthy bonus; run again; the links bazaar -> construction (the gate),
construction -> carnival, carnival -> bazaar (fade); roads10 and roads20; leaving and opening again. ASan: four
full runs with all links, close, reopen in another map, the bench: no error.

## Measured on the board (2026-11-16, ES3C35P, firmware 0.3.0 + this work, core + roads10s in the internal flash)

| | before (LVGL image render) | now |
|---|---|---|
| pictures a second, normal play | 10.9-12.1 | **19.8-20.0** (0-1 late steps in 10 s) |
| compose (UI thread) | 44.7 ms (max 170) | 17-21 ms (max ~30) |
| push | 34.2 ms (LVGL render 22 + flush 11) | 19.5 ms (status bar rows + tear wait + DMA) |
| road scale smooth / plain | 22.4 / 13.4 ms | 8.1 / 3.7 ms |
| shadows | (in sprites 19 ms) | 1.3-1.9 ms |
| end card shown | | 20.0 pictures a second (card 10 ms) |
| move -> picture on the panel | | 51 ms avg, 57 max (injected swipes) |
| road decode (TJpgDec, flash) | 49.8 ms | same; 10-15 road pictures a second needed, late 0-2 in 10 s with a warm sprite cache |
| sprite decode | 1.0 us a pixel, near things up to 93 ms | same (no longer on the UI thread) |
| open the game (installed in the flash) | | ready in ~80 ms (memory-mapped packs) |

The first minute of a session shows some late road pictures at full speed while the sprite cache fills
(core 0 decodes sprites and roads).

## To measure on the board

`RUNBENCH` (dev console, the game must be installed: core + a roads part). It prints:
```
RUNBENCH <id>, roads10|roads20, files in the internal flash|the memory card
RUNBENCH scale smooth x2: avg/max ms         RUNBENCH scale plain x2: avg/max ms
RUNBENCH sprite decode (near things): N pictures avg/max ms, us a pixel
RUNBENCH sprite decode (the dog's run): avg/max ms
RUNBENCH road decode (roads10|20, flash|card): avg/max ms
RUNBENCH live smooth (default): P pics/s (late steps L), road R/s (late T) | compose avg/max [road scale, build,
  sprites, shadows, HUD] | push avg/max (panel|LVGL) | move->picture avg/max | decode road / sprites (ahead,
  helper, inline, waited) | card read avg/max | cache KB
RUNBENCH live plain scale: ...      RUNBENCH live smooth + core 1 helper: ...      RUNBENCH done
```
While the game is open, the same line goes to TRACE every 10 s (`run: ...`).

Targets (the brief): game 20 pictures a second (`pics/s` ≥ 19.5, late steps ≈ 0), road 10 (roads10) or 20 (roads20)
a second with few late ones, move->picture < 100 ms. Push = LVGL render + flush of the whole screen, from
`LV_EVENT_REFR_START` to `REFR_READY` (includes the tear sync). Card: read ms per road picture and the max (stalls).
Expected bottleneck: TJpgDec on core 0 (a road picture is 13 KB / 55,680 px). If roads20 comes late most of the
time, roads10 is the part to offer by default (it already is); if even sprites come late, the next step is
Espressif's esp_new_jpeg (SIMD, about 3-4x faster on the S3; a prebuilt copy is in `managed_components`, it adds
flash) for the roads.

## The pet from the card

`src/pet/dog.cpp` reads a picture's bytes through `pk::data(f, buf)` / `pk::mask(f, buf)` / `pk::dataMax()` instead
of `f + 1`. `src/pet/packs.cpp` provides them: in the internal flash the bytes in place;
on the memory card the dog's index and every picture's 22-byte record in PSRAM (read once in 32 KB pieces, ~25 KB
kept), a picture's bytes read when it is decoded, the pieces' table in PSRAM and each piece read once when first
drawn, the voice pack whole in PSRAM (200 KB). The other pet files only use `pk::` accessors. Checked in the
simulator (game, bubbles, food sheet, coins, book, Home; ASan clean) and with PawBoop installed on the
card through the store.

## Size

The run game adds about 46 KB to the firmware (43,868 bytes measured by linking with and without `src/run` on
2026-11-16, then the fades and the panel path: the image went from 3,883,328 to 3,890,928 bytes; the app slot is
3,932,160). `logic.cpp` and `run.cpp` are built `-Os`, the pixel loops (`draw.cpp`) and the decoder (`media.cpp`)
`-O2`. `-DRUN_BENCH=0` leaves out the bench, the dev switches and the autopilot (a few KB).
`src/lcd.cpp` (lead): + `lcd::direct()` (about 40 lines).

## Dev console

`RUNBENCH` and `RUN <cmd>` (src/devtools.cpp): `RUN auto | manual | god | warp 180 | end | smooth 0|1 |
helper 0|1 | stats | frame`. Runs helped this way pay no coins.

## Open points

- The status bar stays over the road's sky; the HUD is 24 px lower than on the phone.
- The pack has no height per picture; the jump clip's heights for the coin rule are copied from the page.
- Paying the coins when leaving in the middle of a run is the OS's choice (the page has no leaving).
- `carnival>bazaar` is a fade, not a real link; roads10s fades between all maps.
- At full speed with a cold sprite cache (the first minute) some road pictures come late (TJpgDec: 50 ms a road
  picture on core 0; esp_new_jpeg would be 3-4x faster but adds ~75 KB of code).
- The panel path draws the OS's home line itself and steps aside for anything else of the OS shown over the game
  (it checks lv_layer_top / lv_layer_sys); a new kind of always-visible overlay would make it fall back to LVGL.
