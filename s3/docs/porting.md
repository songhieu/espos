# Porting EspOS to a new board

EspOS runs the same skeleton on every board: the launcher, the widgets, settings, the store, packages, sync and
the runtimes do not know which board they are on. What changes from board to board lives in one folder,
`src/boards/<name>/`, behind a small hardware abstraction layer (HAL) in `src/hal/`. This guide adds a board in ten
steps, with the ES3C28P (LCDWIKI 2.8") as the worked example: it was the second board, and it shows the hard case, a
panel smaller than the UI.

## How the hardware layer is built

```
 OS core (home, apps, settings, store, run game, JS, J2ME ...)
   |  lcd::  touch::  sound::  power::  pkg card  board::     <- the OS's own APIs, unchanged
   v
 src/hal/      hal::board()  -> the descriptor (hal::Board: data in flash)
               hal::bsp()    -> its drivers (hal::Display, hal::Input) and probe()
   ^
 src/boards/   boards.cpp    the registry: which boards this firmware knows, in probe order
               es3c35p/      ES3C35P 3.5": ST77922 QSPI panel, its own touch
               es3c28p/      ES3C28P 2.8": ILI9341 SPI panel at 2/3, FT6336G touch
               qemu/         the build's board as Espressif's QEMU emulates it
               _template/    a commented skeleton to copy
```

| File | What it holds |
|---|---|
| `src/hal/board.h` | `hal::Board` (the descriptor), `hal::Bsp`, `hal::board()`, `hal::has()`, `hal::detect()` |
| `src/hal/caps.h` | the `CAP_*` flags, `CAP_NAMES` (each flag's package-facing name, `capName()`, `capFlag()`), the profiles |
| `src/hal/display.h` | the panel's part of the descriptor (`PanelDesc`, `UiMap`, `DisplayPins`) and the panel driver `hal::Display` |
| `src/hal/input.h` | `InputDesc` and the input driver `hal::Input` (touch today, keys planned) |
| `src/hal/audio.h`, `storage.h`, `power.h` | the codec, the card slot, the battery: descriptor parts read by `sound.cpp`, `pkg/card.cpp`, `power.cpp` |
| `src/hal/hal.cpp` | the board's I2C bus (`hal::i2c()`) and `hal::i2cAnswer()` for probes |
| `src/boards/<name>/<name>.h` | the board's descriptor, `constexpr` |
| `src/boards/<name>/bsp.cpp` | `probe()` and the `hal::Bsp`: descriptor + drivers |
| `src/boards/<name>/display.cpp` | the panel driver |
| `src/boards/<name>/touch.cpp` | the touch controller driver |

Two kinds of hardware support:
- **Drivers per board** (function tables): the panel and the touch controller. Every board has its own.
- **Drivers in the OS set up from the descriptor**: the speaker (`Codec::ES8311` over I2S), the memory card (SDMMC, 1 or
  4 lines), the battery (an ADC pin and a divider). A board with the same kind of chip only fills in its pins. A board
  with another kind (another codec, a card on SPI, a fuel gauge) adds a value to the enum in `src/hal/` and its case in
  that OS driver.

The UI's size is the board's (`display.ui.w` x `ui.h`, read with `hal::uiW()` / `hal::uiH()`): 320x480 on the
ES3C35P, 360x480 on the ES3C28P, whose 240x320 panel shows it at 2/3, the whole panel (`UiMap`). Screens follow it
(`os::W()` / `os::H()`); what is laid out for 320x480 (the games, the saver's pages, widget boxes) keeps that size in
the middle (`os::colX()` / `colY()`). Keep the UI at least 320 wide and 480 high (portrait).

## The descriptor

`hal::Board` is plain data, `inline constexpr` in the board's header, so it costs flash only (about 76 bytes plus its
two strings) and the board's drivers read it at compile time.

| Field | ES3C28P | Notes |
|---|---|---|
| `name` | `"es3c28p"` | Sent to the server with every sync and in `diag`; firmware updates are matched to it. Never change it. |
| `label` | `"ES3C28P · 2,8 inch"` | Shown to people. |
| `chip` | `ESP32S3` | |
| `caps` | `PROFILE_FULL` | What the board has (below). |
| `display.panel` | 240x320, TFT, RGB565, 16 bits, `INSTANT`, rotations `0b0101` | `rotations`: bit n = `LV_DISPLAY_ROTATION_<n x 90>` the driver can do. |
| `display.ui` | `{360, 480, 2, 3, 0, 0}` | The UI (360x480) scaled by 2/3, the whole panel. 1:1 boards: `{320, 480, 1, 1, 0, 0}`. |
| `display.pins` | cs 10, sclk 12, d0 (MOSI) 11, d1 (MISO) 13, dc 46, bl 45 | QSPI panels use d0..d3; `te` is the tear signal. |
| `i2c` | sda 16, scl 15 | The board's own bus: touch and codec. |
| `input` | `TOUCH`, 0x38, rst 18, irq 17 | |
| `audio` | `ES8311`, 0x18, I2S 4/5/7/8/6, amp 1 (LOW = on) | `Codec::NONE` and no `CAP_AUDIO_OUT`: silent. |
| `card` | `SDMMC`, 4 lines, `try1bit`, 38/40/39/41/48/47 | `try1bit`: D0 alone when 4 lines get no answer. |
| `power` | ADC 9, divider 2, 3350-4150 mV | No `CAP_BATTERY`: a steady 85 %. |
| `misc` | RGB LED 42, BOOT 0 | |

## Capabilities and profiles

`caps` is a set of `CAP_*` flags (`src/hal/caps.h`). Each has one package-facing name, the string a manifest's
`requires` and the device's `caps` list use; `docs/profiles.md` §1.1 is the canonical list, and `hal::CAP_NAMES` is the
same table in the firmware (a `static_assert` keeps it 1:1: every flag named once, no name twice).

| Flag | Name | Meaning | ES3C35P | ES3C28P | QEMU |
|---|---|---|---|---|---|
| `CAP_COLOR` | `screen.color` | 16-bit colour or better | yes | yes | yes |
| `CAP_GRAY` | `screen.gray` | 4 grey levels or more for UI drawing | yes | yes | yes |
| `CAP_EINK` | `screen.eink` | e-paper: the picture stays without power, slow refresh | | | |
| `CAP_FAST` | `screen.fast` | 20+ frames a second (animation, games) | yes | yes | yes |
| `CAP_TOUCH` | `input.touch` | a touch screen | yes | yes | yes |
| `CAP_BUTTONS` | `input.buttons` | at least Back, OK, Up, Down | | | |
| `CAP_PSRAM` | `mem.psram` | 2 MB+ of external RAM | yes | yes | yes |
| `CAP_AUDIO_OUT` | `audio.out` | a speaker | yes | yes | |
| `CAP_AUDIO_IN` | `audio.in` | a microphone | yes | yes | |
| `CAP_SDCARD` | `store.sd` | a memory card slot | yes | yes | yes |
| `CAP_WIFI` | `net.wifi` | WiFi | yes | yes | |
| `CAP_BLE` | `net.ble` | Bluetooth LE | yes | yes | |
| `CAP_BATTERY` | `power.battery` | a battery the firmware measures | yes | yes | |
| `CAP_RGB_LED` | `led.rgb` | an RGB LED | yes | yes | |

A new capability is three rows added together: the flag and its `CAP_NAMES` entry in `src/hal/caps.h`, and its line in
`docs/profiles.md` (whose §1.1 gets the name first). The X4 firmware and `tools/espk.py` use the same strings. The
ES3C28P's `screen.fast` and `audio.in` come from the vendor's page and are not yet checked on a
board. QEMU has network (its emulated Ethernet), but no radios.

The OS already gates on the flags: no `CAP_AUDIO_OUT`, `sound::begin()` stays quiet; no `CAP_SDCARD`, no card task; no
`CAP_BATTERY`, `power::` reports 85 %. Code asks `hal::has(hal::CAP_AUDIO_OUT)`, never the board's name. In QEMU builds
`has()` knows at compile time what QEMU never emulates (`hal::QEMU_LACKS`: codec, microphone, battery, radios, LED), so
the QEMU image leaves those drivers out.

Profiles are only names for common sets, used in docs and store labels; a package names capabilities, never a profile
(`docs/profiles.md` §1.3-1.4):
- **Full** (`hal::PROFILE_FULL`): `screen.color screen.gray screen.fast input.touch mem.psram audio.out audio.in
  store.sd net.wifi net.ble power.battery led.rgb`. ESP32-S3 with PSRAM and a colour touch LCD: both boards today.
- **Lite** (`hal::PROFILE_LITE`): `screen.eink input.buttons store.sd net.wifi net.ble power.battery`. No PSRAM,
  e-paper, keys: the
  ESP32-C3 Xteink X4 (a separate firmware today), which runs the declarative, data-only types (`face`, `widget`,
  `theme`).

A new board starts from the profile closest to it and adds or takes away flags (an S3 with an e-paper panel: Full minus
`CAP_COLOR | CAP_FAST | CAP_TOUCH`, plus `CAP_EINK | CAP_BUTTONS`).

**Planned, not wired yet in this firmware:** the device sends its `caps` as these names (`hal::capName()` for each set
bit) with its board name; the store offers only packages whose `requires` the device has, and the installer refuses the
rest (`hal::capFlag()` reads a manifest's names; an unknown name fits no device). The e-paper refresh model
(`Refresh::EINK`: partial updates, a full refresh now and then) and keys (`InputKind::BUTTONS`) are in the descriptor as
concepts; no driver here uses them yet.

## Adding a board, step by step

### 1. Check that the board fits

The Full profile needs an ESP32-S3 with 8 MB PSRAM and 16 MB flash (the partition table `partitions_pawboop.csv` is
16 MB: a 3.75 MB app slot, LittleFS, PawBoop's packs), a colour panel of at least 240x320 and a touch controller. Get
the vendor's schematic or demo code: the pin `#define`s of its LCD, touch and SD examples are what you need.

ES3C28P: the vendor's page (lcdwiki.com/2.8inch_ESP32-S3_Display) lists every pin; its demos give the ILI9341 init.

### 2. Copy the template

```
cp -R src/boards/_template src/boards/es3c28p
mv src/boards/es3c28p/myboard.h src/boards/es3c28p/es3c28p.h
```

Rename the namespace `myboard` to `es3c28p` in the four files and fix the `#include "myboard.h"` lines. Every file
says what to fill in. The template is compiled with every firmware (so it always builds) but listed nowhere, so the
linker drops it.

### 3. Fill in the descriptor

`src/boards/es3c28p/es3c28p.h` is the filled-in example (the table above). Pins you have not checked on a board: say
so in a comment, and prefer fallbacks (`card.try1bit`).

### 4. Write the panel driver

`display.cpp` fills in a `hal::Display`: `begin`, `setRotation`, `setBacklight`, `stats`, `logInvalid`, `direct`,
`mirror`, `frame` (`src/hal/display.h` says what each must do). Keep all its functions in an anonymous namespace; only
the table is exported (`es3c28p::display`).

What the ES3C28P's driver does, and why:
- SPI at 40 MHz with a D/C line (`esp_lcd_new_panel_io_spi`), the ILI9341 init from the vendor (`MADCTL 0x48`: portrait,
  BGR). No tear signal on this board.
- LVGL renders the 360x480 UI in bands of 69 rows (a multiple of 3); an `LV_EVENT_INVALIDATE_AREA` callback widens each
  area to whole rows aligned to 3, so every band scales cleanly.
- Each band goes through `scale::down32` (src/scale.h: each panel pixel the area average of the 3x3 UI pixels it covers)
  into one of two small DMA buffers, sent while LVGL renders the next band.
- `begin()` clears the whole panel once (before the first picture) and leaves the backlight off.
- `direct()` (the run game's fast path) does the same with the game's rows; `mirror()`/`frame()` keep the 360x480 UI
  for the remote viewer.
- All four ways: landscape turns the panel itself (MADCTL) and sends the 480x360 UI at 2/3 (docs/boards.md).

A first port can return `false` from `direct()` (the game then draws through LVGL) and leave `setRotation` portrait.
Internal RAM is the tight resource: LVGL's two band buffers are the biggest use (ES3C35P: 2 x 50 KB). Look at the boot
log's `heap after display` line.

### 5. Write the touch driver

`touch.cpp` fills in a `hal::Input` with one function, `begin()`, called by `touch::begin()` at boot. `touch::i2cBus()`
gives it the board's I2C bus (`hal::i2c()`: made on first use on the descriptor's `i2c` pins, shared with the codec).
It resets the controller, checks it answers, and starts a task on core 0 that reads every report and calls
`touch::report(x, y, down, ms)`. Coordinates are the UI's portrait ones (0..uiW()-1, 0..uiH()-1);
LVGL turns them with the screen. The OS side (the queue, the system gestures, the 20 px slop, the tap click) is
`src/touch.cpp` and is the same for every board.

ES3C28P: the FT6336G is polled 100 times a second (its interrupt line is not needed), and each point goes back through
the `UiMap`: `x = px * 3 / 2`, `y = py * 3 / 2`.

### 6. Write `probe()`

One firmware runs on several boards: `hal::detect()` asks each listed board's `probe()` at boot, before any driver
takes a pin, and keeps the first that answers (`bsp.cpp`). A probe looks for a chip only this board has, on this
board's pins, with `hal::i2cAnswer(sda, scl, {addresses}, ms)`, which makes an I2C bus, asks, and releases the pins.

The rules:
- **Harm no other board.** An I2C address byte on two pins is harmless on most boards (an input or an SD data line just
  sees a few clocks). Driving a reset or enable line is not: a probe that does must come after every board where that
  pin matters.
- Be quick: a few ms, up to ~400 ms if a chip needs time after reset.
- Return the address that answered (non-zero) or 0.

ES3C28P: its touch answers ~300 ms after its reset line goes high, so `probe()` drives GPIO 18 high, then asks for the
codec (0x18) or the touch (0x38) on 16/15 for up to 400 ms. On the ES3C35P, 16/15 are the codec's I2S data lines and 18
its bit clock, so the ES3C28P is asked second: on a 3.5" board the first probe (its codec and touch on 38/39) answers
and the second never runs. On a 2.8" board, 38/39 are the card's CLK and D0, where an address byte gets no answer.
The boot log says what was found: `board: es3c28p (0x18 answered, N ms)`.

### 7. List it in the registry

In `src/boards/boards.cpp`, include `boards/es3c28p/es3c28p.h` and add `&es3c28p::BSP` to `BSPS`, at a place in the
order that keeps step 6's rules. When nobody answers, the first board in the list is used.

A firmware made only for one board skips probing: list only its BSP (`BOARD_ES3C28P` builds do that).

### 8. The speaker, the card, the battery

If the board has the same kinds as today's (an ES8311 codec, an SDMMC slot, a battery on an ADC divider), the
descriptor is all: `sound.cpp`, `pkg/card.cpp` and `power.cpp` read their pins from it. Leave out a `CAP_*` flag for
what the board does not have. Another kind of chip: add its value to `hal::Codec`, `hal::CardBus` or the power part,
and its case to that file.

### 9. Test without the board

- **Build**: `~/.platformio/penv/bin/pio run -e s3desk` (the image for every board), and the simulator
  (`make -C sim -j8`). The simulator uses only the descriptor (`src/boards/boards.cpp`, `EXTRA=-DBOARD_ES3C28P` for
  the 2.8" one's: its 360x480 UI).
- **QEMU** (docs/simulator.md) runs the real firmware with the build's board: `src/boards/qemu/qemu.h` takes the
  board's descriptor less what QEMU does not emulate (codec, battery, radio), and QEMU's virtual panel shows the UI 1:1
  or, with `BOARD_ES3C28P`, at 2/3 like the 2.8" panel (`src/boards/qemu/display.cpp`). The ES3C28P was checked this
  way before a board was in hand: `tools/qemu/run.sh --board 28`. A board with another UI map needs its case there.
- **Nothing else changed**: run both existing boards in QEMU and compare screenshots with the ones before your change
  (Home, Settings, the control panel, a rotation, PawBoop).

### 10. Test on the board

Flash the `s3desk` image, then check on the serial console:
- `board: <name> (0x.. answered, N ms)`, and `board: <label>` printed by `main.cpp`;
- `heap after display: N KB internal` (internal RAM left for WiFi and TLS: keep it close to the other boards');
- the touch: `TOUCHLOG 1` echoes every sample; tap the corners and check the axes;
- `SOUND test`, the card (`card: mounted`), the battery percentage in the status bar;
- the run game's frame rate (`RUNBENCH`) if you wrote `direct()`.

Then add the board to `docs/boards.md`: its pins, what was checked and how.

## Size budget

`firmware.bin` must stay under 3,932,160 bytes (the app slot of table v1, 0x3C0000; table v2 has 4,980,736, see
docs/partitions.md for the tracks and `tools/fwsize.py`): every board's drivers are in the one image.
On 2026-11-17 it was 3,876,112 bytes. A board costs about 3.5 KB for a panel driver, 0.5-0.7 KB for a touch driver and
0.3 KB for its descriptor and probe. Check before and after:

```
~/.platformio/penv/bin/pio run -e s3desk                     # "Flash: ... used N bytes"
ls -l .pio/build/s3desk/firmware.bin
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-size -A .pio/build/s3desk/firmware.elf \
  | grep -E "dram0|iram0"                                   # internal RAM: .dram0.data + .dram0.bss
```

Zero new internal RAM for boards you do not run on: the descriptor and the driver tables are `const` (flash), and a
driver's buffers are allocated in its `begin()`, which only the found board runs. Keep static variables to pointers and
counters.

## Checklist

- [ ] `pio run -e s3desk`, `-e s3desk-qemu`, `-e s3desk-28-qemu` build; `make -C sim -j8` builds.
- [ ] `firmware.bin` under 3,932,160 bytes; `.dram0.data` + `.dram0.bss` not grown by more than your board's few
      pointers.
- [ ] The existing boards look the same in QEMU (screenshots before and after).
- [ ] Your probe drives no pin that matters on a board asked before or after it.
- [ ] `docs/boards.md` has your board.
