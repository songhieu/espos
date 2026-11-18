# EspOS for ESP32-S3 screens

EspOS on small ESP32-S3 colour touch displays: a desk operating system with Home pages of apps
and widgets, a Today view, a Control Center, a saver, notifications, themes and wallpapers,
languages, and apps you install from a store without new firmware (JavaScript apps, J2ME phone
games, declarative widgets and faces). It syncs with an EspOS server: the hosted one at
[espos.dev](https://espos.dev), or [your own](../server/README.md).

Developer docs: **[espos.dev/docs](https://espos.dev/docs)** (their sources are in [`site/`](site)).

## Boards

| Board | Screen | Firmware |
|---|---|---|
| ES3C35P | 3.5" ST77922 320x480, QSPI, capacitive touch, ES8311 codec, SD card | env `s3desk` |
| ES3C28P | 2.8" ILI9341V 240x320, SPI, FT6336G touch, ES8311 codec, SD card | env `s3desk` (the same firmware finds its board at boot) |

Both need 16 MB flash and 8 MB octal PSRAM. Porting to another board: [docs/porting.md](docs/porting.md)
and [docs/boards.md](docs/boards.md).

## What is here

| Folder | |
|---|---|
| `src/` | The firmware: the OS shell (`os.cpp`, `home.cpp`), apps (`app_*.cpp`), widgets (`wid/`), packages and store (`pkg/`), the JavaScript app host (`js/`), the J2ME virtual machine (`j2me/`), the PawBoop pet and run games (`pet/`, `run/`), board support (`boards/`, `hal/`), sync with the server (`desk.cpp`) |
| `include/`, `lib/` | LVGL's configuration; vendored libraries (QuickJS-ng, libopus, esp_new_jpeg, the QEMU Ethernet driver) |
| `sim/` | The simulator: the firmware's UI built for your computer (headless or in a window) |
| `tools/` | QEMU runner (`tools/qemu`), live simulator, dev console clients, package tools (`espk.py`), language tools (`i18n.py`), font and art generators |
| `sdk/` | The `espos` CLI and the `@espos/sdk` typings and schemas: create, build, check and run apps |
| `apps/` | Sample JavaScript apps (focus timer, dice, vocabulary, water) |
| `j2me/` | The J2ME class library (CLDC 1.1 / MIDP 2.0) and its tests |
| `i18n/` | Translations (English is built in; the others ship as language packages) |
| `docs/` | Design notes: packages, apps, widgets, the saver, boards and porting, simulators, the platform plan |
| `site/` | The developer docs site (Astro Starlight), published at espos.dev/docs |
| `data/fonts/` | The source fonts the bitmap fonts and the font packages are made from |

## Build the firmware

Needs [PlatformIO](https://platformio.org) (the platform and libraries are fetched on the first
build).

```sh
cd s3
pio run -e s3desk                 # .pio/build/s3desk/firmware.bin, must stay under 3,932,160 bytes
pio run -e s3desk -t upload       # flash a board over USB
```

The board uses the partition table `partitions_pawboop.csv` (two 3.75 MB app slots for updates
over WiFi, PawBoop's packs, LittleFS). A partition table cannot change over WiFi: the first
install is over USB, from the web app's installer or with esptool.

### Server address

The firmware talks to the server named at build time by two flags in `platformio.ini` (env
`s3desk`, and env `s3desk-qemu` for QEMU) and in `sim/Makefile`:

| Flag | Default | |
|---|---|---|
| `ESPOS_API` | `https://api.espos.dev` | The server the device calls: pairing, sync, news, the store's catalog. |
| `ESPOS_WEB` | `https://espos.dev` | The web app's address, for the pairing link and QR code (`<ESPOS_WEB>/app#/pair?code=…`). |

For your own server, set both to its address (for example `https://espos.example.com`), build
and flash. The store's catalog is read from `ESPOS_API` too; a self-hosted server has no store
yet, so apps are installed from a memory card or with the SDK.

## Simulators

```sh
pio pkg install -e s3desk         # once: LVGL and ArduinoJson into .pio/libdeps, which the simulator uses
mkdir -p sim/data sim/data-live   # a device's data folders (empty: a new, unlinked device)
make -C sim -j8                   # sim/build/espos-sim: headless
sim/build/espos-sim sim/scripts/i18n-quick.txt /tmp/shots   # runs a script, writes screenshots
tools/sim-live.sh                 # a window: the mouse as the finger, the speaker (SDL2)
tools/qemu/run.sh                 # the real firmware in Espressif's QEMU (env s3desk-qemu)
```

A simulator keeps a device's data (settings, cached server answers, packages) in a `sim/data*`
folder (`sim/data` by default, another with `SIM_DATA=folder`). These folders are made on your
computer and are never committed: they hold your WiFi names, link tokens and settings.
Details: [docs/simulator.md](docs/simulator.md).

## SDK and apps

```sh
cd sdk
npm install                       # builds the CLI
npm test
node packages/cli/bin/espos.js create my-timer --template js-app-ts --id com.you.timer --name "Timer"
```

See [sdk/README.md](sdk/README.md), [docs/apps.md](docs/apps.md) and
[docs/packages.md](docs/packages.md), or the guides at [espos.dev/docs](https://espos.dev/docs).

## Docs site

```sh
cd site
npm install
npm run dev                       # http://localhost:4321
npm run build && npm run check:links
```

## Translations

`i18n/en.json` lists every string; `i18n/<tag>.json` is a translation. `python3 tools/i18n.py status`
shows what is missing; [i18n/TRANSLATING.md](i18n/TRANSLATING.md) explains the rules.

## Licence

Apache-2.0, see [LICENSE](LICENSE) and [NOTICE](NOTICE). Vendored libraries and fonts keep their
own licences: [THIRD_PARTY.md](THIRD_PARTY.md). PawBoop's picture and voice packs and the games
offered in the store are not part of this repository.
