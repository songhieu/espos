# EspOS SDK

Tools for writing apps and games for EspOS, the ESP32-S3 operating system in this repo (`s3/`). An app
is a package (`.espk`) installed from the store, onto the device's flash or its memory card; the firmware does not
change for a new app. This folder holds:

| package | what |
|---|---|
| [`packages/cli`](packages/cli/README.md) (`@espos/cli`) | the `espos` command: create, build, check, inspect, wrap J2ME games, run in the simulator or QEMU, talk to a board, firmware and store helpers |
| [`packages/sdk`](packages/sdk/README.md) (`@espos/sdk`) | TypeScript typings of the JavaScript API (API level 1) and JSON schemas for manifests and projects |

Licence: Apache-2.0 (docs/platform.md). Nothing here is published to npm yet.

## Install

Node.js 20 or newer (22 tested), npm 8 or newer.

```
cd s3/sdk
npm install                 # installs esbuild, typescript, jpeg-js, pngjs (+ the optional serialport) and builds the CLI
npm link -w @espos/cli    # optional: puts `espos` on your PATH
espos doctor              # what else is installed: python3, JDK, make, SDL2, PlatformIO, QEMU, the simulator
```

Without `npm link`, run it as `node s3/sdk/packages/cli/bin/espos.js ...` or `npm run espos -- ...` from
`sdk/`. The CLI finds the firmware repo by walking up from the current folder (or from its own place) to the folder
with `platformio.ini` and `src/`; `--root DIR` or `ESPOS_ROOT` points elsewhere.

## Quick start

```
espos create my-timer --template js-app-ts --id vn.you.timer --name "Hẹn giờ"
cd my-timer
espos build                # type-checks, bundles src/ into main.js, checks, writes dist/vn.you.timer-1.0.0-core.espk
espos sim --watch          # the simulator in a window with the app open; a saved file rebuilds and restarts it
espos sim --headless       # the same without a window: screenshots in .espos/shots, exit 1 if the app throws
espos qemu --install dist/vn.you.timer-1.0.0-core.espk     # the real firmware in QEMU, the package on its card
espos login                # once: paste a developer key from espos.dev (Account › Developer keys)
espos upload               # private: every EspOS S3 of your account installs it; a higher "code" updates them
espos store publish dist/*.espk --catalog catalog.json      # what the store upload would do (dry run)
```

Templates: `js-app` (JavaScript), `js-app-ts` (TypeScript), `j2me-game` (a Java phone game: `--jar game.jar
--license TEXT`). There are no widget, face, theme, service or driver templates yet. `espos check` and `espos build`
already take declarative faces, widgets and themes (`"runtime": "decl"`, docs/profiles.md §3-4) with tools/espk.py's
rules (M040-M047, F020-F030 in `espos check --rules`); service and driver need manifest v2 (planned,
docs/platform.md), which no firmware reads yet.

## Commands

| command | what |
|---|---|
| `create DIR [--template js-app\|js-app-ts\|j2me-game]` | a new project |
| `build [DIR]` | bundle TypeScript, check, write the `.espk` (byte-identical to `tools/espk.py` for a plain folder) |
| `check [DIR\|FILE.espk ...]` | the one validator (`--rules` lists every rule and where it comes from) |
| `inspect FILE.espk` | manifest, files, offsets, CRC-32s, sha256, catalog entry; `--extract DIR` |
| `jar GAME.jar --license TEXT` | a J2ME game as a package (`jar.<slug>`, engine `j2me`), as the web admin does |
| `types [DIR]` | write `types/espos.d.ts` (API level 1) into a project |
| `sim [TARGETS] [--headless] [--watch]` | install into a simulator data folder, build the SDL simulator if needed, open the app |
| `qemu [--install PKG] [--board 28]` | `tools/qemu/run.sh` with packages put onto the QEMU card image (its data kept) |
| `device list\|info\|shot\|logs\|app\|tap\|drag\|home\|trace\|mem\|js\|send\|console` | the board's dev console over USB |
| `firmware build\|size [--env ENV]` | PlatformIO build; `firmware.bin` against the 3,932,160-byte app slot (no flashing) |
| `j2me runtime\|test\|vm-test` | the J2ME runtime package, the MIDP layer's tests, the VM tests |
| `login`, `logout`, `whoami` | a developer key from espos.dev (shown once on the web, kept 0600 here; `ESPOS_DEV_KEY` in CI) |
| `upload [DIR\|FILE.espk] [--device ID] [--no-install]` | check (plus the private rules U001, U002), then upload to your account: only your devices install it, as `u.<id>` |
| `apps [list\|install\|rm]` | your uploaded packages and each device's copy; install on devices; delete everywhere |
| `store entry\|publish` | a catalog entry; `scripts/publish_pkg.py` after the checks (dry run without `--yes`) |
| `doctor` | the toolchain, with install hints for macOS and Linux |
| `help [--json]` | help; `--json` prints every command, option and argument |

Every command has `--help`. Exit codes: 0 ok, 1 problems found or a step failed, 2 bad arguments, 3 a tool is
missing, 4 no device / port refused. With `--json` (commands that have it), stdout carries exactly one JSON
document; progress and warnings go to stderr.

**Command reference for the docs site:** `espos help --json` (every command, sub-command, argument, option,
default, environment variable, note and example). A copy made at build time is in
[`packages/cli/commands.json`](packages/cli/commands.json).

## One validator

`espos check` (and `build`, `sim`, `qemu --install`, `store`) applies every rule of the four places that checked
packages before, the strictest where they differed:

| | tools/espk.py | scripts/publish_pkg.py | web admin | device (src/pkg/espk.cpp) | espos |
|---|---|---|---|---|---|
| id | <= 48, no `/` | `[a-z0-9][a-z0-9._-]{0,63}` or a language pack's | `{0,47}` | | `[a-z0-9][a-z0-9._-]{0,47}` or `dev.espos.lang.<tag>` |
| part | default core | required | default core | | required |
| version | any text | `[0-9A-Za-z.+-]{1,20}` | lowercased in URLs | | `[0-9a-z.+-]{1,20}` |
| engine | default js | required | default | | required, known engine |
| min_fw | | default 0 | default 300 | | required |
| category | default tool | required | default tool | | required |
| file names | <= 51 bytes, no `..` | no `..` segment, `\` | | + `: * ? " < > \|`, `//` | all of them |
| files / manifest | 512 / 32 KB (read) | 4096 | | 512 / 32 KB | 512 / 32 KB |
| flags | | 0 | | | 0 |
| `i18n` (names in other languages) | an object of `{name 1-40, desc <= 300}` | the same, kept in the catalog | | reads `name` (and the store `desc`) of the language shown | the same errors (M035); warns about tags the S3 never shows (M036) and letters it cannot draw (M037) |
| engine `lang` / `font` | accepted | accepted; ids `dev.espos.lang.<tag>` keep the tag's capitals | | only its own ids (`src/i18n.cpp`) | accepted; warns when the device would not use the pack (M038, F011) |

plus checks no tool had: `main.js` parses (ES2023 classic script, no `import`), calls to `notify`/`app.badge`
without the `notify` permission and to `sound.*` without `sound`, things API level 1 lacks (`console`, `setTimeout`,
`fetch`, Promise callbacks), `main.js` under 512 KB, `icon.jpg` under 200,000 bytes, ids longer than the dev
console's 23 characters (firmware before 0.4.0, packages with min_fw below 400), and the ellipsis character (U+2026) in the code. The other tools are left as they are and keep working.

## Projects

A folder with `manifest.json` is packed the way `tools/espk.py` packs it: every file except dotfiles, hidden folders and
`manifest.json` files. A project adds `espos.json` to choose what goes in:

```json
{"main": "src/main.ts", "files": ["icon.jpg", "data"], "out": "dist"}
```

`main` becomes `main.js` (bundled by esbuild into one classic script when it is TypeScript or `"bundle": true`),
`files` are copied as they are (folders keep their structure), `out` is where `build` writes
`<id>-<version>-<part>.espk`. Schema: `packages/sdk/schemas/project.schema.json`. The manifest is the current
format (docs/packages.md, `packages/sdk/schemas/manifest.schema.json`); manifest v2 is only a draft schema.

## Board safety (`espos device`)

- Ports are listed from the OS's USB data without opening them.
- A port is opened only to send `ESPOS:HELLO` and is used only if it answers `ESPOS:READY` with `"kind":"s3"`;
  anything else is closed without another byte.
- Ports on the deny list are never opened, not even with `--port`: `ESPOS_DENY_PORTS` and
  `~/.config/espos/config.json` `"deny_ports"` (both empty by default).
- Without `--port`, only an S3 remembered by its USB serial number (`~/.config/espos/devices.json`) or the default
  `/dev/cu.usbmodem1101` is opened; `espos device list --probe` identifies other Espressif ports. Opening an
  ESP32's USB port can reset it, and the EspOS X4 reader has the same USB ids (303a:1001).
- There is no flash command.

## Platforms

`create`, `build`, `check`, `inspect`, `jar`, `types`, `store`, `device` work on macOS and Linux. Today the SDL
simulator builds only on macOS (`sim/main.cpp` calls `malloc_zone_statistics`; the live build links
`-framework ApplicationServices`), and `tools/qemu/run.sh` (viewer, `DYLD_FALLBACK_LIBRARY_PATH`) and
`tools/qemu/mkimage.py` (the card through `hdiutil`) are macOS scripts. `qemu --install` changes the card image
with mtools when present, else with `hdiutil` (macOS). The CLI says so when it runs into these.

## Tests

```
cd s3/sdk && npm test
```

node:test suites in `packages/cli/test`: Python-compatible JSON (against python3), every validator rule, `.espk`
round trips, byte-for-byte equality with `tools/espk.py` (the example apps with their manifests converted as
`apps/pack.py` does, and a folder with nested/hidden folders, Unicode names, floats and odd key order) and with
`--install-sim`, J2ME jar wrapping (MegaBlock, `ESPOS_TEST_JAR`), `create` -> `build` -> `check` for each template,
TypeScript errors, the command reference, store dry runs against a local catalog, the typings against the
runtime's prelude, device commands against a fake dev console on a pseudo-terminal (no hardware), and a headless
simulator run when `sim/build/espos-sim` exists (`ESPOS_TEST_SIM_BUILD=NAME` builds `sim/NAME` first).
