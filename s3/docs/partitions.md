# Partition tables, firmware tracks and the flash budget

The S3 boards have 16 MB of flash. How it is cut up (the partition table) can only change over USB, never by OTA.
Since 2026-11-16 the boards run table v1 (`partitions_pawboop.csv`). Table v2 (`partitions_v2.csv`, 2026-11-21) gives
each app slot 1 MiB more, taken from the internal app area. The same firmware image runs on both tables: it finds its
app area by name and reports its slot size to the server.

## The two tables

| | v1 `partitions_pawboop.csv` | v2 `partitions_v2.csv` |
|---|---|---|
| nvs | 0x9000, 20 KB | same |
| otadata | 0xE000, 8 KB | same |
| app0 | 0x10000, 0x3C0000 (3,932,160 B) | 0x10000, **0x4C0000 (4,980,736 B)** |
| app1 | 0x3D0000, 0x3C0000 | **0x4D0000**, 0x4C0000 |
| app area | pb_dog 0x790000 + pb_ui 0xCAA000 + pb_voice 0xE8D000 = 7,352 KB | **apps 0x990000, 5,304 KB** |
| spiffs (LittleFS) | 0xEBE000, 1,224 KB | same |
| coredump | 0xFF0000, 64 KB | same |

What stays where it is matters most, because a USB switch keeps it:
- nvs: the link token, WiFi, the package index, PawBoop's save, the claim and licence state;
- LittleFS: settings, the dashboard, photos, and small apps on "Máy" (JavaScript apps, language packs, small fonts).

### The app area under v2

`src/pkg/area.cpp` maps the partition named `apps`. If there is none, it maps the three `pb_*` partitions as one
range, as before. The index of what lies where is kept in NVS (`pkgs`). Its offsets mean nothing once the area moves,
so the area's place is stored in NVS `pkgs/geo`. A missing key means v1, the only table before v2, so a v1 board never
writes the key. At boot, `checkGeometry()` compares the stored place with the mapped one. If they differ, it drops
every area record, the move journal `jr` and the tombstones, writes the new place, and traces
`pkg: app area moved (790000:72e000 -> 990000:52e000): N packages dropped`. On the next sync the store installs again
what the web's `s3.pkgs` lists. Settings are not touched. Packages on LittleFS and on the card stay installed.

PawBoop's packs (about 7.3 MB) do not fit in the 5,304 KB area. Under v2, PawBoop comes from the store onto the
card, as on the units sold. `adoptPet()` only runs on an area of 7,352 KB or more, so it never runs on v2. An install
to "Máy" that cannot fit fails at once, before any download ("Device is 7.7 MB short"), at most 3 times per boot.

## Tracks: which image a board can take

Every sync body carries `diag.slot` (the running app partition's size in bytes) and `diag.ptab` (1 or 2), from
`ota::slotBytes()` and `ota::table()`. The server stores `diag` on the device row, so the ping and the web can read
them too.

The rule is the same everywhere: **an S3 is offered only an image whose size fits its slot.** A device that does not
send `slot` (firmware 0.5.8 and older) runs v1, so its slot is 3,932,160 bytes. The firmware row needs no new field:
its `size` is compared with the slot. Should an image one day depend on table v2 for some other reason, add an
integer `min_ptab` column (empty = 1) and compare it with `diag.ptab` in the same places.

- **device_sync** offers the newest release of the board's kind that fits (`fw_update`). If a newer release does not
  fit, it also answers `fw_usb: {version, notes, date, size}`.
- **The device** (`desk.cpp applySync`) keeps the first as the OTA offer. It keeps `fw_usb.version` in
  `FwOffer::usb`. It also turns an offer bigger than its own slot into `usb`, so an old server can never start an OTA
  that cannot fit. Settings › Update then shows "Update over USB to get new features: Version X needs a one-time
  update over USB from espos.dev. Settings, WiFi and the link stay." The two strings still need translating
  (tools/i18n.py); until then they show in English.
- **device_ping** raises `fw` for the newest release, and also for the newest one that fits, when it came out after the
  last sync.
- **list_devices** gives an S3 the newest release that fits as `firmware`, and a newer one as `firmware_usb`.
  **device_firmware** "update" asks for the newest release that fits.

These server changes are prepared on the hosted service, not deployed yet. Until they are
live, the firmware's own check keeps an OTA that is too big from starting.

### Building and releasing each track

| | track v1 | track v2 |
|---|---|---|
| env | `s3desk` (table v1) | `s3desk-v2` (table v2) |
| PlatformIO refuses an image over | 3,932,160 B | 4,980,736 B |
| release budget (`tools/fwsize.py`) | 3,890,000 B | 4,920,000 B |
| bundle (`tools/make_bundle.py`) | table v1 + PawBoop packs in pb_* | table v2, no packs |
| reaches by OTA | every board | boards on v2 (and v1 boards while the image fits 3,932,160 B) |

- `tools/fwsize.py [firmware.bin]` prints the size against both slots and both budgets. It exits 1 when the image is
  over its track's budget. The track comes from the `partitions.bin` beside the image.
- `web/scripts/release-s3.sh` runs that check first (step 0). `PIO_ENV=s3desk-v2` releases a v2 build: the bundle that
  the web installer and Kho máy write then carries table v2.
- `make_bundle.py` reads the build's own `partitions.bin`. The packs go in only when the table has `pb_*`
  partitions. The manifest says `"ptab": 1|2`.
- Until the boards in use are switched, release from `s3desk`, so one image reaches every board. After the switch,
  release from `s3desk-v2`. While an image is under 3,932,160 bytes, v1 boards still take it by OTA. Above that, they
  stop at the last image that fits and show the USB message. A fix for them is a build from `s3desk` with a version
  code between theirs and the newest. The server offers it to them because it fits, while v2 boards keep the newest.

## Switching a board over USB: tools/migrate_v2.py

```
python3 tools/migrate_v2.py                          # the boards it would accept (USB data only, opens no port)
python3 tools/migrate_v2.py --serial XX:XX:XX:XX:XX:XX           # the plan: nothing read or written
python3 tools/migrate_v2.py --serial XX:XX:XX:XX:XX:XX --write   # do it
```

Build `pio run -e s3desk-v2` first, or pass `--build DIR`. The script runs in PlatformIO's Python, which has pyserial
and esptool 5.

| | v1 board | board already on v2 | `--factory` (Kho máy, a new unit) |
|---|---|---|---|
| erased | 0x4D0000–0xEBE000 (app1 + the new app area: v1 bytes) | nothing | the whole flash |
| written | 0x0 bootloader, 0x8000 table v2, 0xE000 otadata (boot_app0: next boot = app0), 0x10000 the app | same | same |
| kept | nvs, LittleFS, coredump | nvs, LittleFS, the app area and its packages | nothing |

Before writing, it reads the old table, nvs, otadata and LittleFS into `../backup/migrate-v2/<MAC>-<time>/`, 1 MB at
a time, because big reads fail over this USB. After writing, it reads the table, nvs and LittleFS back. The table must
be v2 and the other two must match the backup byte for byte. Then the board restarts. At first boot under v2 the
firmware drops the stale area records and the store reinstalls packages.

Port safety: only Espressif USB serial/JTAG ports (303a:1001) are accepted, and the target is picked by its USB serial
number (`--serial`, the MAC). A port on the deny list is never opened, not even with `--port`. The deny list is
`ESPOS_DENY_PORTS` and `deny_ports` in
`~/.config/espos/config.json`. esptool refuses a chip that is not an ESP32-S3 (the X4 reader has the same USB ids).
The script also checks that the chip's MAC is the one asked for and that the flash is 16 MB. A table that is neither
v1 nor v2 is refused, because LittleFS might be elsewhere. `--factory` on a board that already has an EspOS table
needs `--wipe` as well.

The web installer and Kho máy (`web/app/src/flash.ts`, `admin/Provision.tsx`) write bootloader, table, otadata and
app without erasing anything else. A v2 bundle does the same switch for a customer's board: nvs and LittleFS stay,
and the stale app area is handled by the firmware.

## QEMU

- `tools/qemu/run.sh --v2 [--board 28]` uses its own image, `build-qemu/flash[-28]-v2.bin`, with table v2 and the same
  firmware. `tools/qemu/mkimage.py OUT --table partitions_v2.csv [--build DIR]` makes one.
- `tools/migrate_v2.py --image FLASH.bin --build DIR --write` runs the switch on an image file, for example a v1 image
  that has data in it.
- QA console (`ESPOS_QA`, so QEMU and `s3desk-qa`): `PART` prints the table, the running slot and its size.
  `OTAFILE /sdcard/fw.bin` writes an image file to the other slot the way an OTA does, then restarts.
  `PKGMOVE <id> flash|card` works like the store's Move.

Results on 2026-11-21 (`s3desk-qemu` and `s3desk-28-qemu`, network restricted, unlinked):
- A v1 image with PawBoop adopted in the area (7,336 KB) was switched with `migrate_v2.py --image`. nvs and LittleFS
  read back identical. Boot: `app area moved ... 1 packages dropped`, `app area 5304 KB mapped`,
  `PART table v2, running app0 at 0x10000, app slot 4980736`. Settings were still there.
- `PKGMOVE vn.pawboop.run flash` (13 MB) was refused: "Device is 7.7 MB short".
- `PKGMOVE dev.espos.j2me flash`: the bytes land at the start of the v2 area, identical to the package (checked in the
  flash image). The CRC read back through the memory map then fails, so the move is rolled back. The same happens on
  a v1 image. QEMU's flash cache does not see writes, so installs to "Máy" cannot finish in QEMU on either table. They
  work on the board.
- OTA: a 4,658,976-byte image (bigger than v1's slot) was written by `OTAFILE` into app1 at 0x4D0000 and booted from
  there. A restart without a sync went back to app0, which is the bootloader's rollback of an image still on
  probation.
- `ESPOS:FACTORY_RESET` on v2: settings gone, packages and the table untouched, no second "moved".
- 2.8" board, a fresh v2 image: boots, `PART` v2, the area mapped (a fresh unit writes `geo` once: 0 packages dropped).

## Limits to keep in mind

- OTA downloads the whole image into PSRAM, in install mode, before writing it. A boot shows about 7.5 MB of PSRAM
  free. The LVGL pool (1.25 MB), the fonts (270 KB) and the screen come out of that in install mode, which leaves
  about 5.7 MB by estimate. So the largest v2 image (4.98 MB) fits with roughly 0.7 MB to spare. Check `TRACE` in
  install mode on a board before the first release over 4.5 MB.
- The app area is mapped into the data address space (64 KB pages): 5,304 KB under v2, 7,352 KB under v1. A bigger app
  slot maps more code instead. Under v2 the total is smaller than under v1.
- LittleFS stays 1,224 KB on both tables.

## Flash diet of 2026-11-21 (no table change)

Measured one at a time on a frozen copy of the tree (env `s3desk`; the start was 3,888,432 B):

| change | bytes | kept |
|---|---|---|
| `-fno-exceptions` (build_unflags `-fexceptions`). Nothing catches a C++ exception, so a throw ended in `abort()` before too. Gone: `.eh_frame` 114,768 → 4,964 B, the landing pads in `.text` (−138 KB), and `.gcc_except_table` (−55 KB) | **−303,600** | yes |
| `LV_USE_GRID 0` (no grid layout anywhere) | −3,824 | yes |
| `LV_LOG_USE_FILE_LINE 0` (LVGL warnings on the serial port without file:line) | −4,032 | yes |
| LTO on src/ (`build_src_flags = -flto=auto`, unflag `-fno-lto`) | −47,312 | no: see below |
| `-Os` on the 37 files still at `-O2` (upper bound) | −50,368 | no: they are the hot ones (drawing, games, decoders) |
| QuickJS at `-Os` | 0 | already so |
| Bluetooth: NimBLE is enabled in the prebuilt libraries, but only 388 B of `libbt.a` is linked (Arduino's controller memory release) | 0 | nothing to remove |
| trace / log strings: about 10 KB of `trace::add` text plus 3.7 KB of `Serial` prints, in a 201 KB pool of all string literals | a few KB at most | not done |

Kept: −311,456 B. The partition-table code adds 1,280 B, so the image is about 3.58 MB, roughly 310 KB under the v1
budget, with or without `ESPOS_ONBOARD`.

Not done, and needing a decision:
- **LTO**: 47 KB more (49.6 KB on the QEMU build). The Arduino libraries stay non-LTO and only our sources are
  optimised across files. In QEMU it boots and runs a JavaScript app, PawBoop and a Java game without a fault.
  WiFi, TLS and the flash-write paths with the cache off cannot be tried there. Risks: the `--wrap=log_printf` link,
  IRAM functions inlined across files, and weak overrides. Test it on a board before using it.
- **CA bundle**: `x509_crt_bundle` is 69 KB of Mozilla roots. A short list (Cloudflare's CAs) would save about 60 KB,
  but a CA rotation by the host would cut every board off.
- Two printf engines (`_vfprintf_r` and `_svfprintf_r`, 26 KB) come from newlib.
