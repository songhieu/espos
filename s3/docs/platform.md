# EspOS as an open IoT platform

The plan agreed with the owner on 2026-11-17. Research behind it: `docs/research/platform-landscape.md` (app
platforms, runtimes, hosted + self-host, security, licensing) and `docs/research/peripherals.md` (hardware access).

## Goal

The S3 firmware becomes an open-source operating system for ESP32-class IoT devices:
- **The base firmware is the OS.** Everything else is an installable package that declares permissions: today's
  apps, themes, and drivers for plugged-in devices.
- **Apps talk to the system only through the EspOS API** (versioned interfaces).
- **Developing for it feels like mobile development:** an SDK, typings, a CLI, simulators, live reload on a device,
  and a store.
- **Existing apps are rewritten on the same API with no loss of performance.**
- **Servers:** the hosted service (sign up with us), or self-host your own.

## Decisions (owner, 2026-11-17)

| Topic | Decision |
|---|---|
| Third-party code | JavaScript/TypeScript (QuickJS) **and** WebAssembly (WAMR) from the start. No third-party native code: the ESP32-S3 cannot isolate one app from another. |
| System apps | C++ on the same public API (a thin layer over the services), to keep native speed. |
| Native engines | pet, run, j2me stay first-party; their packages carry data only. |
| Servers | The hosted service stays as it is. The Go server (`espos/server`) is the self-host implementation. One protocol spec and one conformance suite run against both. |
| Licences | Firmware, SDK, templates and spec: Apache-2.0. Server: AGPL-3.0. Earlier releases stay MIT. Contributions use the DCO. A trademark policy covers the name. No paid-only device features. |
| Order | Finish the J2ME games → OS foundation + SDK → self-host + open source → IoT hardware. |

## Many chips, many screens (owner, 2026-11-17)

- EspOS will run on many chips and displays.
- The community must be able to build the firmware for their own hardware while keeping this platform's skeleton.
- So the hardware layer becomes a HAL with board support packages (`boards/<name>/`, a board descriptor with
  capability flags, a template and `docs/porting.md`). Done 2026-11-17: `src/hal/` (the descriptor `hal::Board`, the
  display/input interfaces, `CAP_*` flags, `PROFILE_FULL`/`PROFILE_LITE`), `src/boards/` (es3c35p, es3c28p, qemu,
  `_template`, the registry), `docs/porting.md`; no store filtering yet.
- Devices fall into hardware profiles:
  - **Full**: ESP32-S3 + PSRAM + colour touch LCD (the S3 boards today).
  - **Lite**: no PSRAM, e-ink, buttons (the Xteink X4, ESP32-C3). It runs the data-only declarative types first:
    `face`, `widget`, `theme` (design: `docs/profiles.md`).
- Packages say which capabilities they need (`requires`, the canonical names in `docs/profiles.md` §1.1, mapped 1:1
  from `CAP_*` in `src/hal/caps.h`); devices report `caps`, `rt` (runtime levels) and `screen` in the sync body; the
  store filters by device. Full and Lite are only names for capability sets.
- The Xteink X4 is the first Lite device and should be done now, not in phase 5. Started 2026-11-17: declarative
  faces and widgets (format level 1, `docs/profiles.md` §4) on the X4, synced through `x4.pkgs`.

## Architecture

### Layers
- **Stays in the firmware:**
  - home/launcher, the widget/face/theme renderer, settings, store and installer, notifications;
  - data-feed sync (weather, calendar, markets, news), the network/TLS queue, OTA, power, input;
  - the runtimes (QuickJS, WAMR, engines), the device framework, safe mode.
- **EspOS API** with an integer API level. Modules:
  - `ui`, `app` (lifecycle `onOpen/onShow/onHide/onClose`), `storage`, `notify`, `sound`, `net`;
  - `data` (weather/calendar/markets/news), `theme`, `time`/`alarm`, `intent`, `sensors`/`devices`, `bg`.
  - One contract, three bindings: C++, JS, WASM (host functions with the same names and the same permission
    checks).

### Package types (manifest `type`)
| type | What it is |
|---|---|
| `app` | Full-screen app with a lifecycle. |
| `widget` | Home-screen card: declarative JSON + data bindings, drawn natively; an optional `render()` with a time budget. |
| `face` | Clock face / screen saver, declarative. |
| `theme` | Data only: colour tokens, wallpapers, optional icons and fonts. |
| `service` | Short background job on a schedule, queued with the system sync. |
| `pack` | Data for a native engine (today's `kind: native`). |
| `driver` | A plugged-in device's driver (see Hardware). |

Every type may carry a `settings` schema (rendered by both the web desk and the device) and an optional `web` page
(a sandboxed iframe on the web desk).

**Manifest v2 fields:**
- `type`;
- `runtime` (`js` | `wasm` | `engine` | `decl`: declarative faces, widgets and themes drawn by the firmware,
  `docs/profiles.md` §3-§4);
- `api {min, target}`, separate from `min_fw` (for `decl`: the format level);
- `requires`: capability flags the package needs (`input.touch`, `screen.color`, `mem.psram` and the
  others in `docs/profiles.md` §1.1), `[]` for most declarative packages;
- `screens` (`320x480`, `240x320`, `800x480`, `480x800`);
- `refresh` (faces and widgets: `minute` | `hour` | `data` | `day`, the most often it wants redrawing);
- `theme` (`system` | `custom`);
- `perms`;
- `entry`.

### Permissions
- **How they work:**
  - declared in the manifest;
  - shown in plain words in the store;
  - accepted at install, on the device and on the web;
  - switchable per app in Settings › Apps;
  - checked in every binding through one `perm::check(app, perm)`. `app.can()` lets code test first.
- System apps go through the same broker. Unknown ids are no longer trusted.
- **List, version 1:**
  - `notify` (daily cap), `sound`, `bg`, `screen.awake`, `remote`;
  - `net:<host>`, `net:*`, `net:lan`, through the OS network queue (the device token is never exposed);
  - `storage:<KB>` (default 64);
  - `data:weather|calendar|markets|news|reading` (calendar gives events only, never the ICS link; reading: the
    book being read and reading statistics);
  - `location:city`;
  - `wifi.info` (SSID and signal, never the password);
  - `theme.write`;
  - `sensor:<channel>`;
  - `actuator:<channel>` (plus a per-device prompt the first time);
  - `hw:*` (drivers only).

### Hardware (IoT)
Three layers (details: `docs/research/peripherals.md`):
1. **Bus primitives in the firmware:** I2C, SPI, UART, 1-Wire (RMT), GPIO, ADC, PWM, BLE scan/GATT, USB host.
2. **`driver` packages:**
   - Two forms:
     - WASM: first-party drivers, and vendor C drivers ported to it (Bosch BME280; Sensirion SHT4x/SCD4x are BSD-3).
     - JS, in ECMA-419 shapes: community drivers and BLE parsers.
   - Each driver declares `match` rules:
     - I2C address + chip-ID check;
     - 1-Wire family;
     - BLE service (ESS 0x181A, BTHome v2 0xFCD2);
     - USB VID/PID;
     - `manual` for UART and GPIO.
   - Each driver declares the typed channels it `provides`.
   - Only signed drivers hold `hw:*`, and only for devices the OS bound to them. The touch and codec addresses are
     never handed out.
3. **Apps see typed channels with fixed units** (they map to Home Assistant device classes):
   - temperature °C, humidity %RH, pressure hPa, co2 ppm, pm25/pm10, light lx;
   - motion/contact/presence, power/energy, battery, button;
   - switch/light (actuators).

**OS services:**
- one sampler per channel, with history (a PSRAM ring of about 11 KB per channel per day, minute aggregates on
  flash/SD);
- offline rules (if condition/time → notify, actuate, webhook), edited on the web;
- server sync, and MQTT with Home Assistant discovery;
- driver OTA through the store, with auto-disable after repeated faults;
- actuator fail-safe states.

A Devices page in Settings holds every device and grant.

**Bus order:**
1. BLE (no wiring, no pin conflicts);
2. Qwiic I2C;
3. GPIO/relays and 1-Wire;
4. UART;
5. USB host, last: it takes the PHY used by USB-Serial/JTAG.

Today's boards have few free pins, and external I2C shares the touch/codec bus. Next boards should have a separate
Qwiic port.

**Simulator:**
- a fake bus layer with chip register models, BLE advert encoders and 1-Wire ids;
- `sim/devices.json` value scripts;
- dev-console plug/set commands;
- `espos driver test`;
- the same layer under QEMU.

### Theme
- Role-based colour tokens (background, text, secondary text, accent, card, border, danger…) in light and dark.
- Every hard-coded colour in the OS moves to tokens; the web reads the same token file.
- A `theme` package is data only. Apps with `theme: system` follow it.

### SDK and tools
- **`@espos/sdk`** (npm):
  - typings per API level with `@since`;
  - JSON schemas;
  - templates;
  - an esbuild bundler (TypeScript);
  - C/Rust headers for WASM.
- **`espos` CLI**, one validator, replacing `tools/espk.py`, `apps/pack.py`, `tools/sim-live.sh`,
  `tools/qemu/run.sh` and `scripts/publish_pkg.py`. Commands:
  - `create --template app|widget|face|theme|service|driver`, `build`/`check` (declared permissions vs the APIs used);
  - `sim` (prebuilt SDL simulator, mounts the folder, live reload), `sim --web` (LVGL compiled with Emscripten);
  - `run --device <ip>` (LAN push + live reload), `logs`, `shot`;
  - `sign`, `publish`, `driver test`.
- **Developer mode on the device:** a one-time code, unsigned "dev" packages allowed, logs over a WebSocket.
- **Simulators** for macOS/Linux/Windows (drop `malloc_zone_statistics`, Homebrew paths, `hdiutil`, this machine's
  paths). QEMU stays for firmware tests and CI.
- **Tests and CI:** headless app tests with screenshot comparison; CI for s3desk.
- **Signing:**
  - SHA-256 per file in `.espk` (replacing CRC32);
  - Ed25519 developer signatures (updates need the same key);
  - store-signed catalogs with an expiry;
  - the device verifies packages read from the SD card too.

### Performance rules for rewriting apps
- **JS UI v2:** a retained tree updated by key (no full rebuild per event), drawn by LVGL.
- JS/WASM run off the UI thread, with time limits. Heavy work stays in C++ services.
- Widgets, faces and themes are declarative by default.
- **A budget per app:**
  - open time;
  - frame rate while scrolling (sysmon);
  - minimum internal RAM and PSRAM (`MEM`);
  - flash.
  
  Measured on the board before and after (`tools/qa.py`). An app that misses its budget keeps its old version.
- **Free flash before adding WAMR** (the slot is 3.68 of 3.75 MB):
  - move apps out to packages;
  - compress fonts (about −259 KB);
  - review the partition table;
  - set a per-release size budget (`firmware.bin` < 3,932,160 B).

### Servers
- **EspOS Protocol v1** (OpenAPI + schemas) is the source of truth: pairing/tokens, `s3.*` settings sync, data
  feeds, catalogs and packages, events, remote control, OTA.
- **The Go server** needs:
  - engine work: `for_each`, `storage_get`, `{{ var.* }}`, `api_key` routes, public storage with Range, `expires_at`;
  - firmware per board kind;
  - a re-import of the hosted backend;
  - the S3 web app ported;
  - multi-user (password/OIDC);
  - a Docker image.
- **How a device picks its server:**
  - the default host is built in;
  - a Server field (setup and Settings) or a QR code from a self-hosted web app;
  - the device reads `https://<server>/.well-known/espos.json` (endpoints, catalogs + keys, OTA channel);
  - the store and pairing URLs are derived from it;
  - the server key is pinned at pairing, and a custom CA is optional.
- **Catalogs.** Manifest-v2 packages are listed in public storage `apps/catalog.json` (`{"v": 2, "packages": [...]}`,
  `docs/profiles.md` §10); `apps-s3/catalog.json` stays for S3 firmware that reads only v1 entries. A Lite device
  never reads a catalog: the server resolves its downloads in the sync answer.
- **Multi-source signed catalogs.** A self-hosted server can mirror the official store. Store content belongs to its
  authors.

## Roadmap
| Phase | What |
|---|---|
| 0 (now) | J2ME games: VM, MIDP, the `j2me` engine, the on-screen keypad, user jar uploads (`user_pkgs`). One firmware for both boards (`board::detect`). Run game fixes. |
| 1 Foundation | Manifest v2 and API level 1 (today's `ui.*` frozen). Protocol v1 draft. Permission broker + consent + per-app switches. Per-app storage with quotas, wiped on uninstall; notifications cleaned up on uninstall; package id checks. JS: Promise jobs, `console.log`, retained UI v2. Colour tokens v1 (OS + web). SHA-256 + Ed25519. Free flash, then WAMR (interpreter) on the same bindings. Reserve the `sensor:`/`actuator:`/`hw:` and `driver` names. |
| 2 SDK v1 + app migration | `@espos/sdk`, CLI, simulator with live reload, developer mode, docs, CI. Declarative widget/face/theme (format level 1: `docs/profiles.md` §4; renderer `Decl.cpp` on the X4 since 2026-11-17, to port to LVGL for the S3). Migrate within budget, in this order: (1) markets, news, notes, reminders, focus/vocab, calculator, lunar; (2) faces and themes; (3) weather and calendar once `data:*` exists. Home, settings, store and alarms stay in the core. |
| 3 Self-host + open source | Go server with full S3 support + conformance suite. `/.well-known/espos.json` + Server setting. Multi-source store. Licences, DCO, docs, Docker. Publish s3desk through the sync script (`scripts/sync_oss_firmware.sh` pattern). |
| 4 IoT hardware | Sensors v1: BLE (BTHome/ESS) + on-board sensors, `sensors` API, sampling + history, Devices page, fake buses. Sensors v2: driver packages (WASM SHT4x/BME280/SCD4x, JS light sensor), GPIO button/relay/PIR, actuators with per-device consent, rules, MQTT + HA discovery. Sensors v3: community drivers, DS18B20/1-Wire, UART, USB host, headless sensor-node profile, Matter bridge study. |
| 5 | Background services, WASM AOT, developer portal and review, browser simulator, more boards, hosted tier (running costs only). The e-ink C3 (Lite) profile moved earlier: started 2026-11-17, `docs/profiles.md`. |

## Verification
- **Every phase:**
  - headless simulator + QEMU scripts;
  - ASan/TSan;
  - each app's budget measured on the board before and after;
  - screenshot comparison;
  - `firmware.bin` size check.
- **Permissions:**
  - a call without the permission is refused;
  - switching it off works at once;
  - a package edited on the SD card is refused (bad signature);
  - uninstalling removes the app's data and scheduled notifications.
- **SDK:** from a clean Linux and macOS machine: `espos create` → `sim` (edit, see the change) → `run --device`
  → `publish` to a test store.
- **Self-host:**
  - `docker compose up` the Go server;
  - pair an S3 (QEMU), sync, install from that server's catalog, remote control, OTA;
  - the conformance suite passes on both servers.
- **Hardware:** fake sensors in the simulator, then real ones (BLE, then Qwiic). An app without `sensor:<channel>`
  reads nothing.
