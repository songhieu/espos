# Java (J2ME) games on the S3

Old phone games (`.jar`, MIDP 2.0 / CLDC 1.1) run unchanged on the `j2me` engine. The VM itself is described in
`docs/j2me.md`.

## Pieces

| Piece | Where |
|---|---|
| VM | `src/j2me/jvm.h`, `jvm.cpp`, `vm_*.cpp` (docs/j2me.md) |
| MIDP natives | `src/j2me/midp.cpp` (+ `midp.h`): the 26 natives of `com.espos.j2me.Native`. Every pixel call checks the arrays and rectangles it is given. |
| Pixel work | `src/j2me/midp_draw.cpp`: fills, lines, arcs, triangles, round rects, blits with the 8 Sprite transforms and alpha, drawRGB, text. `midp_png.cpp`: PNG (every colour type and depth, tRNS, Adam7). `midp_font.cpp`: the 12/14/16 px Ubuntu fonts copied out of LVGL once, on the UI thread, for code points 0-255. |
| Class library | `j2me/classlib`: `java.*`, `javax.microedition.{midlet,lcdui,lcdui.game,rms,media}`, `com.nokia.mid.{ui,sound}`, `com.espos.j2me` (Native, Lifecycle). Tests: `j2me/test/midp` (458 checks). |
| The game's screen | `src/j2me/app_j2me.cpp`: the game's 240x320 on top in full screen (no status bar: `os::fullScreenFor`), the keypad under it, the ⋯ menu (continue, sound on/off, quit), tones on the system's 16 kHz stream. |
| Engine hook | `src/pkg/pkg.cpp`: `engineKnown("j2me")`, `pkg::run` → `j2me_app::open`. |

## Packages

- **Runtime `dev.espos.j2me`.** It holds `classlib.jar` (about 175 KB) and is `"type": "pack"`, so it gets no icon.
  - It is not in the firmware: the slot has about 78 KB left.
  - Build it with `tools/j2me/make_runtime.sh [version] [code]`, which writes `build-pkgs/dev.espos.j2me-<v>.espk`.
  - The store installs it before the first Java game (`store::installAs`, `J2ME_RUNTIME`).
- **A game:**
  - `game.jar` (+ optional `icon.jpg`);
  - `"kind": "native"`, `"engine": "j2me"`, `"min_fw": 400`;
  - `"j2me": {"midlet", "keypad", "profile", "config"}`.
  - The device reads the MIDlet class from the jar's `META-INF/MANIFEST.MF` (`MIDlet-1`).
  - Record stores (saves, scores) go next to the package: `/sdcard/espos/data/<id>/rms/` or
    `/littlefs/data/<id>/rms/`.
- **User uploads.**
  - The web's Game Java tab builds the package in the browser.
  - The server returns it to the owner's S3 devices in the sync answer, as `user_pkgs` (ids `u.<slug>`).
  - The device keeps them next to the store catalog (`store::setUserPkgs`) and installs them through `s3.pkgs`.
- Firmware 0.4.0 (code 400) is the first with the engine.

## Memory, threads, screen size

- **The VM's task.**
  - Its 16 KB stack is in PSRAM (`xTaskCreatePinnedToCoreWithCaps`). After a download, internal RAM was often split
    to an 11 KB largest block, and games refused to open.
  - A task with a PSRAM stack must not touch flash, because the cache is off while flash is read or written. So the
    record stores' file work runs on the UI thread through `MidpHost::fileJob`.
  - The task suspends itself when it ends, and the UI thread deletes it with `vTaskDeleteWithCaps`.
  - Opening a game first drops the kept HTTPS connection (`desk::releaseConnection`).
- **Java heap and threads.**
  - The heap is what PSRAM can spare when the game opens: at most 3 MB, keeping 1.4 MB for classes, threads and the
    OS.
  - Each Java thread gets 4096 stack slots (32 KB).
  - Measured with a 288 KB commercial jar: about 1 MB of PSRAM left while it plays.
- **Screen size.**
  - Games get 240x320 unless they ask for another size. A port made for another phone often hard-codes its screen
    in its Canvas: one game's Canvas subclass returns 320x240.
  - When a Canvas reports another size, `Display` calls `Native.screenHint`. The device then saves `<data>/screen`
    and opens the game again at that size, once the old VM has finished.
  - The picture sits centred at the top and the keypad goes under it. Sizes up to 320x330 are used.

## Updating the runtime

The runtime is a `pack`, so the store does not list it. After a sync, an installed pack whose catalog code is
higher is updated by itself (`store::reconcile`).

To publish a new runtime:
1. `tools/j2me/make_runtime.sh <version> <code>`;
2. `sh web/scripts/publish-pkgs.sh build-pkgs/dev.espos.j2me-<version>.espk`.

## Keys

The keypad sends Nokia/Sony Ericsson codes:
- digits 48-57, `*` 42, `#` 35;
- arrows -1 up, -2 down, -3 left, -4 right;
- OK -5;
- soft keys -6 and -7.

The soft keys show a Command's label. Without one they read "Phím trái"/"Phím phải" and reach the game as -6/-7:
full-screen games draw their own labels.

GameCanvas key bits come from the same keys. A key tapped between two `getKeyStates()` calls still counts once.

## Try it

- **In the simulator:**

  ```
  python3 tools/espk.py --install-sim sim/data-live build-pkgs/dev.espos.j2me-1.0.0.espk build-pkgs/jar.megablock-1.0.0.espk
  tools/sim-live.sh sim/scripts/j2me-megablock.txt        # or j2me-spaceshooter.txt
  ```

- **In QEMU (the real firmware):**
  1. Rebuild the card with the packages: `python3 -c "import sys; sys.path.insert(0,'tools/qemu'); from pathlib import Path; import mkimage; mkimage.card(Path('sim/data-live/sd'), Path('build-qemu/sd.img'))"`.
  2. Run `tools/qemu/run.sh`.
  3. Type `APP jar.megablock` at the dev console.

Checked on 2026-11-17, in the live simulator and in QEMU:
- **MegaBlock** (Apache-2.0, build-pkgs/jar-megablock):
  - menu;
  - a game with falling, moving and rotating pieces;
  - the "Pause" command on the left soft key.
- **Space Shooter** (public domain, build-pkgs/jar-spaceshooter):
  - splash, main menu;
  - the game with sprites, stars, enemies and shots.

## Checked with user uploads

On 2026-11-17 the owner uploaded a commercial J2ME game (their own copy, not in the store) on the web. The QEMU
device installed it from `user_pkgs` onto the card.

To get it to run, four things were fixed:
- the missing internal RAM;
- GameCanvas sizing its buffer from the overridable `getWidth()` (it now uses the screen);
- an `OutOfMemoryError` when the game started a thread;
- its 320x240 screen.

It now plays its story intro.

## Not yet
- Sampled audio (wav/midi/mp3) plays nothing; tones and tone sequences play.
- The game screen is fixed at 240x320: no scaling of smaller games, no landscape layout (mockup figure 2).
- No keypad modes (compact, touch).
- Untested on the board.
