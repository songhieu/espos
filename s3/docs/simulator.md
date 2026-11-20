# Simulators

Two ways to run EspOS on the computer:
- the live simulator: our code (src/) built for the computer, the hardware modules replaced (sim/stubs.cpp,
  sim/shim), with the board's speed and memory for the run game; a window, the mouse as the finger, the speaker;
- QEMU: the real firmware on Espressif's emulated ESP32-S3 (below), touches from the dev console.

## Live (a window)

```
tools/sim-live.sh                 # builds sim/build-live/espos-live, runs it on sim/data-live
tools/sim-live.sh --fast          # the computer's speed instead of the board's
tools/sim-live.sh --zoom 2        # points per screen pixel (default: the board's real size, 49 x 73 mm; Z toggles)
tools/sim-live.sh SCRIPT          # a script's commands first, then by hand
tools/sim-live.sh --reset         # a fresh sim/data-live (from sim/data-run + the run game's roads20 on its card)
```

- **Screen:** 320x480 RGB565 as the panel gets it, the board's direct path for the run game (`lcd::direct`, 80-row
  bands). Rotation follows the setting. The window shows it at the board's real size (sim/realsize.cpp: the panel's
  0.153 mm pixels against the size macOS gives for the display the window is on, again when it moves to another
  display); Z switches to enlarged (whole pixels).
- **Finger:** the mouse's left button, through the same rules as touch.cpp: system gestures first (os.cpp), a slide
  over 20 px is not a tap, a tap gets the click. Keys: arrows = a swipe in the middle (the run game), H = the home
  swipe from the bottom edge, B or Backspace = the back swipe from the left edge, S = a screenshot (PNG in
  sim/shots), M = memory, Q = quit.
- **Speaker:** sound.cpp's click, chime, test tune and alarm at 16 kHz, the codec's volume curve, Do Not Disturb, the
  click setting; Biscuit's voice lines.
- **Commands** typed in the terminal, the same as a script's (`help`; the board's dev console words work too: `TAP`,
  `DRAG`, `APP`, `HOME`): tap, long, drag, swipe, homeswipe, backswipe, home, back, app, pet, dismiss, run, dev,
  bench, shot, window (the whole window as a PNG), mem, set, rot, saver, wake, wait, quit; `splash PCT` the boot
  screen with its bar at PCT (`splash end`: Home again), `cover PKG PCT` an app's opening cover (`cover end`).
  `SIM_NO_SYNC=1`: no sync yet since power-on (the widgets' skeletons when there is no dashboard, 0.5.7).
- **The board's speed** (default): the run game's decoders run in their own thread like the board's core 0 task and
  each job takes the board's time (src/run/media.cpp `pad`: a road picture 16.3 ms + 3.3 ms from the card, a
  sprite 1 ms + 0.36 us a pixel; ES3C35P, 2026-11-17). What a late wake-up of the computer's sleep costs comes off the
  next jobs, so the pace is the board's on average.
- **The board's memory:** every heap_caps block is counted; `heap_caps_get_free_size` answers as the board would:
  PSRAM 5380 KB for heap_caps (calibrated so the run game starts with 1642 KB free from the card, as measured on the
  board; `SIM_PSRAM_KB`), internal RAM 180 KB. Past it the terminal says "this would fail on the board".
- **Checked against the board** (the run game, roads20 on the card, autopilot): 20 pictures a second (board 19.8-20),
  road 17.4-20/s (18.6-20), road picture 20.3-20.7 ms (19.5-20.0), PSRAM ~1 MB free while playing (~1 MB).
- ThreadSanitizer build: `make -C sim BUILD=build-live-tsan EXTRA=-fsanitize=thread live`.
- Headless (tests, no window): `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

## Languages

`tools/i18n_tour.py TAG [--packs]`: every screen in one language, headless, with the script command
`overflow NAME` (text cut by its box, out of its parent or over other text) after each shot; data sim/data-i18n
(docs/i18n.md). `SIM_DATA=sim/data-i18n-live tools/sim-live.sh sim/scripts/i18n-demo.txt` shows the switches in the
window (English, Vietnamese, German, Japanese, Korean downloaded from the canned store, English).

## Script (screenshots)

`make -C sim` then `sim/build/espos-sim SCRIPT OUTDIR`: script time (as fast as the computer), PPM shots, the
run game's decoders at once on the same thread. `tools/pawboop_run/sim.sh` is the run game's driver (docs/run.md).

## Not modelled

- The CPU: LVGL drawing, the pet's frames, JavaScript apps run at the computer's speed (only the run game's
  decoding is slowed to the board's). Internal RAM, stacks (the JS apps' 16 KB stack), the watchdog, task
  priorities and the second core are not the board's: a stack overflow or an internal RAM shortage shows only on the
  board.
- Network (WiFi, TLS, the store's downloads): canned answers from `sim/data*/net`.
- The memory card's speed outside the run game's decoders; the panel's SPI time and tearing.

## The real firmware in QEMU (the chip emulated)

```
tools/qemu/run.sh                 # builds env s3desk-qemu + build-qemu/flash.bin + sd.img, runs QEMU + the viewer
tools/qemu/run.sh --keep          # the same images again (what the firmware saved stays)
tools/qemu/run.sh --sdl           # QEMU's own window (no mouse), the dev console on the terminal
tools/qemu/run.sh --tcp 5555      # QEMU's own window, the console on a port: tools/qemu/console.py 5555 "APP pet"
tools/qemu/console.py 5555 shot:FILE.ppm   # a screenshot (QEMU's monitor, build-qemu/monitor.sock)
tools/qemu/run.sh --board 28      # the 2.8" board (env s3desk-28-qemu): build-qemu/flash-28.bin, sd-28.img,
                                  # console 5556, web page 8081, sockets *-28.sock; runs beside the 3.5" one
```

The viewer (tools/qemu/view.cpp, build-qemu/view): QEMU runs without a window; the viewer shows the frame the
firmware keeps in the panel's memory (QMP pmemsave of 0x20000000, ~30 a second) at the board's real size (Z:
enlarged) and sends the mouse as finger samples (dev console `TOUCH d|u X Y` -> `touch::feed`, the touch task's own
queue: gestures, slop, click as on the board). Keys as the live simulator's; dev commands typed in the terminal.

```
```

- Espressif's QEMU (esp-develop-9.2.2-20261109, aarch64 macOS, in `../tools-ext/qemu`, sha256 checked against the
  release) runs the firmware image as the board does: the ROM boot, the bootloader, both Xtensa cores, FreeRTOS,
  octal PSRAM 8 MB, the 16 MB flash with the board's partition table. What the firmware does there is the board's
  code: LVGL, the stack checks, the heaps, the watchdogs, Espressif's JPEG decoder, FatFs on the card, LittleFS.
- `env:s3desk-qemu` (platformio.ini, `ESPOS_QEMU`) differs only where the board's chips are not emulated:
  the display is QEMU's virtual panel (src/boards/qemu/display.cpp: the frame in the panel's memory at 0x20000000, areas sent
  through its registers at 0x21000000, the whole frame each time and once a second, the right way up); touches come
  from the viewer's mouse and the dev console (TOUCH, TAP, DRAG); the network is QEMU's OpenCores Ethernet instead
  of WiFi (lib/qemu_eth: ESP-IDF 5.5.5's driver, left out of the prebuilt framework; QEMU `-nic
  user,model=open_eth`: NAT, DHCP, DNS through this computer), so the same TLS, sync, pairing and store code runs;
  no sound (sound::begin), a fixed battery (power::poll); Serial is UART0 (QEMU's serial port); it pairs as
  "EspOS S3 (QEMU)". The board's build is the same as without it.
- build-qemu/flash.bin (tools/qemu/mkimage.py): the firmware, PawBoop's packs in pb_dog/pb_ui/pb_voice, LittleFS
  made from sim/data-live (fs/: packages, notifications; settings.json and dash.json in the board's form) by
  build-qemu/mklfs (tools/qemu/mklfs.c, the framework's own lfs.c with the board's sizes). build-qemu/sd.img: the
  memory card, sim/data-live/sd as 64 MB FAT32 (QEMU's SD host is emulated: "card: mounted").
- Checked 2026-11-17: Home with the dashboard; PawBoop from its partitions (PET: she answers); Ảnh; the JS apps
  Xúc xắc (rolled) and Tập trung (stack used 2.6 of 16 KB); the run game from the card, opened from PawBoop, the
  autopilot playing at 19.9 pictures a second.
- Linked to EspOS (2026-11-17): `APP settings account` showed a pairing code fetched over HTTPS; confirmed on
  the web it syncs like the board. tools/qemu/run.sh puts a new firmware into the image and keeps its data (NVS:
  the link token; LittleFS; the card), as flashing the board; `--fresh` starts over unlinked.
- The 2.8" board (ES3C28P, docs/boards.md) in QEMU (2026-11-17): the window is its 240x320 panel; its UI (360x480
  since 2026-11-20) is kept in PSRAM and goes to the panel at 2/3, the whole window (src/scale.h, as
  src/boards/es3c28p/display.cpp on the board); the viewer maps the mouse back to the UI's coordinates and shows it at a
  2.8" panel's size. Checked:
  Home, the app library, Settings, PawBoop, the run game from the card at 19.7 pictures a second.
- Not the board: QEMU's speed is the computer's (no PSRAM, cache or card timing; use the live simulator's board
  speed for that), internal RAM is more free (no WiFi driver: 112 KB with the network up), no touch controller,
  codec, radio.

- Turned (2026-11-18): QEMU's panel stays portrait, as the glass sits in the hand; the firmware turns its frame into it
  (src/boards/qemu/display.cpp, lv_draw_sw_rotate) and prints `lcd: rotation N`, and the viewer turns its picture
  (and the mouse) to show it the right way up. Both boards, all four rotations (`ROT n` on the dev console).
- Screen tours in any language and rotation: `python3 tools/i18n_tour.py TAG OUT --rot 1 [--sim PATH] [--data DIR]`
  (docs/i18n.md): every screen shot and checked for cut or overlapping text.

## Timing at the board's speed in QEMU (2026-11-19)

QEMU at its own speed runs the firmware ~10 times faster than the board and changes with the computer's load. With
`-icount shift=2,align=off,sleep=off` the guest's clock follows the instructions it runs (4 ns each), which matched the
board for CPU work (the art inflated in 58 ms against the board's 64 ms; float and PSRAM heavy work came out ~1.8 times
faster than the board): use it to compare a change before and after from the same copy of the flash image. It runs
~10 times slower than real time. QEMU's network (slirp) ends TCP on this computer, so lwIP's 5.7 KB receive window
and WiFi's power save never limit a download there: download speeds only on the board.

## Other emulators (researched 2026-11-17)

- Wokwi (commercial): ESP32-S3 with PSRAM, but only its parts (ILI9341 240x320 + FT6206, SPI microSD); none of this
  board's chips. Renode: Xtensa, ESP32-S3 only as a community work in progress. Cirkit Designer: an instruction
  level ESP32-S3 in the browser, sketches compiled on their server.
