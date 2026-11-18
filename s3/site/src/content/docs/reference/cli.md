---
title: CLI
description: 'Every espos command, argument and option, generated from the CLI (version 0.1.0).'
---

:::note[Generated page]
This reference is generated from `espos help --json` (espos 0.1.0) by `site/scripts/gen-cli.mjs`. To change it, change the CLI's command table and run `npm run gen:cli` in `site/`.
:::

EspOS developer tools: make, check and run packages (.espk) in the simulator, QEMU or on a board.

```sh
espos <command> [arguments] [options]
```

The CLI is **Preview**: commands and options may still change before 1.0. It works inside a checkout of the firmware repository, and runs the repository's simulator, QEMU, PlatformIO and Python tools for you. Setup: [Set up your tools](/getting-started/install/).

## Commands

| Command | What it does |
|---|---|
| [`espos create`](#espos-create) | Make a new package project from a template. |
| [`espos build`](#espos-build) | Bundle (TypeScript), check and write the .espk. |
| [`espos check`](#espos-check) | Check packages: manifest, files, icon, sizes, permissions vs the APIs the code calls. |
| [`espos inspect`](#espos-inspect) | Show what is inside an .espk: manifest, files, offsets, CRCs, sha256, its catalog entry. |
| [`espos jar`](#espos-jar) | Wrap a J2ME game (.jar) into a game package (engine j2me), as the web admin does. |
| [`espos types`](#espos-types) | Write the API level 1 typings (types/espos.d.ts) into a project, for the editor. |
| [`espos store`](#espos-store) | Catalog entries and publishing to the store (wraps scripts/publish_pkg.py). |
| [`espos store entry`](#espos-store-entry) | Print the catalog "packages" entry for the parts of a package (tools/espk.py --entry with publish_pkg.py's part rules). |
| [`espos store publish`](#espos-store-publish) | Check, then publish packages with scripts/publish_pkg.py. Without --yes it is a dry run. |
| [`espos sim`](#espos-sim) | Run packages in the SDL simulator: install, build the simulator if needed, open the app. |
| [`espos qemu`](#espos-qemu) | Run the real firmware in Espressif's QEMU (tools/qemu/run.sh), with packages put onto its card. |
| [`espos device`](#espos-device) | Talk to an EspOS S3 over USB: list, info, shot, logs, app, tap, drag, home, trace, mem, js, send, console. |
| [`espos device list`](#espos-device-list) | Serial ports and which one is an EspOS S3 (nothing is opened without --probe). |
| [`espos device info`](#espos-device-info) | The S3's answer to HELLO: firmware, board, linked (also sets its clock). |
| [`espos device shot`](#espos-device-shot) | Screenshot to PNG (screen + top layer). |
| [`espos device logs`](#espos-device-logs) | Stream the S3's serial log. |
| [`espos device app`](#espos-device-app) | Open an app or package (APP id [arg]). |
| [`espos device tap`](#espos-device-tap) | Tap at x y (screen coordinates of the current rotation). |
| [`espos device drag`](#espos-device-drag) | Drag from x0 y0 to x1 y1 in ms. |
| [`espos device home`](#espos-device-home) | Go to Home. |
| [`espos device trace`](#espos-device-trace) | The device's event history (TRACE: touches, screens, slow loops, heap). |
| [`espos device mem`](#espos-device-mem) | Internal RAM, PSRAM and LVGL memory (MEM). |
| [`espos device js`](#espos-device-js) | Evaluate JavaScript in a scratch QuickJS context (JS code). |
| [`espos device send`](#espos-device-send) | Send one dev console line and print the answer (not SHOT: use device shot). |
| [`espos device console`](#espos-device-console) | Interactive dev console: your lines to the S3, its output here. |
| [`espos firmware`](#espos-firmware) | Build the firmware (PlatformIO) and check firmware.bin against the 3,932,160-byte app slot. |
| [`espos firmware build`](#espos-firmware-build) | pio run -e ENV, then the size check. |
| [`espos firmware size`](#espos-firmware-size) | firmware.bin's size vs the app slot (exit 1 when over). |
| [`espos j2me`](#espos-j2me) | Java (J2ME) engine tools: the runtime package, the MIDP layer's tests, the VM tests. |
| [`espos j2me runtime`](#espos-j2me-runtime) | Build the runtime package dev.espos.j2me (class library) into build-pkgs/ (tools/j2me/make_runtime.sh). |
| [`espos j2me test`](#espos-j2me-test) | Compile the MIDP layer and its logic tests (j2me/build-midp.sh tests); --run also runs them on the VM. |
| [`espos j2me vm-test`](#espos-j2me-vm-test) | Build the VM runner and run every VM test, compared with the JDK (j2me/test/run.sh). |
| [`espos doctor`](#espos-doctor) | Check the toolchain (node, npm packages, python, JDK, make, SDL2, PlatformIO, QEMU, simulator builds) with install hints. |
| [`espos help`](#espos-help) | Help for a command; --json prints every command, option and argument (the docs site's reference). |
| [`espos version`](#espos-version) | Print the CLI's version. |

## Global options

| Option | Description |
|---|---|
| `-h, --help` | Show help for the command. |
| `--json` | Machine-readable output on stdout (commands marked json). |
| `--root` | The s3desk firmware repo (default: found from the current folder or the CLI's own place). Environment: `ESPOS_ROOT`. |
| `-q, --quiet` | Less output. |
| `--no-color` | No colours (also NO_COLOR=1). |

## Output and exit codes

Human text on stdout, warnings and progress on stderr; with --json (commands marked json) stdout carries exactly one JSON document.

| Code | Meaning |
|---|---|
| `0` | success |
| `1` | the command ran and found problems or a step failed |
| `2` | bad arguments |
| `3` | a tool or file the command needs is missing |
| `4` | no EspOS S3 found, or a serial port refused for safety |

## Environment

| Variable | Meaning |
|---|---|
| `ESPOS_ROOT` | the s3desk firmware repo (default: found by walking up for platformio.ini + src/) |
| `ESPOS_PYTHON` | python3 to run the repo's tools |
| `ESPOS_PIO` | PlatformIO's pio (default: PATH, then ~/.platformio/penv/bin/pio) |
| `QEMU` | Espressif's qemu-system-xtensa (default: ../tools-ext/qemu next to s3desk) |
| `ESPOS_PORT` | the serial port of the EspOS S3 (it must still answer as kind s3) |
| `ESPOS_DENY_PORTS` | serial ports never opened, comma separated (also deny_ports in config.json; none by default) |
| `ESPOS_SIM_BUILD` | the simulator's build folder name under sim/ (default build, build-live for --live) |
| `ESPOS_BBK` | the store upload key, read only by scripts/publish_pkg.py (store publish --yes) |
| `ESPOS_PUBLISH_SCRIPT` | scripts/publish_pkg.py if it is not next to s3desk |
| `ESPOS_CONFIG_DIR` | where remembered devices are kept (default ~/.config/espos) |
| `NO_COLOR` | no colours |

## espos create

Make a new package project from a template.

```sh
espos create DIR [options]
```

| Argument | Required | Description |
|---|---|---|
| `dir` | yes | the folder to create (its name is the default display name) |

| Option | Description |
|---|---|
| `-t, --template NAME` | the kind of project One of `js-app`, `js-app-ts`, `j2me-game`. Default: `"js-app"`. |
| `--id ID` | package id, reverse domain (default com.example.&lt;dir&gt;, jar.&lt;name&gt; for games); never changes once published |
| `--name TEXT` | display name (at most 40 characters, no the ellipsis character) |
| `--desc TEXT` | one line for the store (at most 300 characters) |
| `--author TEXT` | author (at most 40 characters) |
| `--category NAME` | store category One of `game`, `tool`, `study`, `health`, `widget`. |
| `--jar FILE` | j2me-game: the game's .jar (fills name, version, MIDlet, icon) |
| `--license TEXT` | j2me-game: where the game comes from and its licence (required with --jar) |
| `--keypad NAME` | j2me-game: the suggested keypad One of `full`, `compact`, `touch`. |
| `--force` | write into a folder that is not empty |

> js-app: JavaScript app (main.js, API level 1): a counter that keeps its number

> js-app-ts: TypeScript app (src/*.ts bundled into main.js, typed by @espos/sdk): a countdown with a reminder

> j2me-game: a J2ME (Java phone) game wrapped as a package: game.jar + icon, engine j2me (firmware 0.4.0)

> No widget, face, theme, service or driver templates: those package types need manifest v2 (planned, docs/platform.md), which no firmware reads yet.

```sh
espos create my-counter
espos create focus-timer --template js-app-ts --id vn.you.focus --name "Focus timer"
espos create megablock --template j2me-game --jar MegaBlock.jar --license "Apache-2.0, xylophones.net"
```

## espos build

Bundle (TypeScript), check and write the .espk.

A project (espos.json) packs its main (bundled with esbuild when it is TypeScript) and its files; a plain folder with manifest.json packs every file but dotfiles and hidden folders, exactly as tools/espk.py does (byte-identical output). Nothing is written when a check fails.

```sh
espos build [DIR] [options]
```

| Argument | Required | Description |
|---|---|---|
| `dir` | no | the project or package folder (default .) |

| Option | Description |
|---|---|
| `-o, --out FILE` | the .espk to write (default: &lt;out&gt;/&lt;id&gt;-&lt;version&gt;-&lt;part&gt;.espk; for a plain folder, next to it) |
| `--out-dir DIR` | the folder to write it to |
| `--typecheck` | type-check TypeScript (--no-typecheck to skip) Default: `true`. |
| `--minify` | minify the bundled main.js |
| `--strict` | warnings fail the build too |

Supports `--json`.

```sh
espos build
espos build ../apps/dice --out /tmp/dice.espk
espos build --json | jq .sha256
```

## espos check

Check packages: manifest, files, icon, sizes, permissions vs the APIs the code calls.

The one validator: every rule of tools/espk.py, scripts/publish_pkg.py, the web admin and the device (src/pkg/espk.cpp), the strictest where they differ, plus checks of main.js (syntax, permissions, API level 1). Folders are built in memory first; .espk files with the same id are checked together as the parts of one package.

```sh
espos check [TARGETS...] [options]
```

| Argument | Required | Description |
|---|---|---|
| `targets...` | no | folders or .espk files (default .) |

| Option | Description |
|---|---|
| `--strict` | warnings fail too |
| `--typecheck` | type-check TypeScript projects (--no-typecheck to skip) Default: `true`. |
| `--rules` | list the rules (code, level, what, where it comes from) |

Supports `--json`.

```sh
espos check
espos check dist/*.espk
espos check --rules
```

## espos inspect

Show what is inside an .espk: manifest, files, offsets, CRCs, sha256, its catalog entry.

```sh
espos inspect FILE [options]
```

| Argument | Required | Description |
|---|---|---|
| `file` | yes | the .espk |

| Option | Description |
|---|---|
| `--base URL` | base URL for the catalog entry's part URLs |
| `--extract DIR` | also unpack the files and manifest.json into DIR |

Supports `--json`.

```sh
espos inspect dist/vn.you.timer-1.0.0-core.espk
espos inspect game.espk --extract /tmp/game
```

## espos jar

Wrap a J2ME game (.jar) into a game package (engine j2me), as the web admin does.

Reads META-INF/MANIFEST.MF (MIDlet-Name, -Vendor, -Version, MIDlet-1, MIDlet-Icon, profile, configuration), makes the icon a 120x120 baseline JPEG on a tile of its average colour, and writes jar.&lt;slug&gt;-&lt;version&gt;-core.espk (kind native, engine j2me, min_fw 400, perms sound). The licence text is required.

```sh
espos jar FILE [options]
```

| Argument | Required | Description |
|---|---|---|
| `file` | yes | the game's .jar |

| Option | Description |
|---|---|
| `--license TEXT` | required: where the game comes from and its licence ("GPL-3.0, github.com/a/b", "author's permission by email 2026-11-17") |
| `--id ID` | package id (default jar.&lt;slug of the name&gt;) |
| `--name TEXT` | store name (default MIDlet-Name) |
| `--desc TEXT` | one line: how to play |
| `--author TEXT` | author (default MIDlet-Vendor) |
| `--category NAME` | store category One of `game`, `tool`, `study`, `health`, `widget`. Default: `"game"`. |
| `--keypad NAME` | suggested keypad One of `full`, `compact`, `touch`. Default: `"full"`. |
| `--version V` | version (default MIDlet-Version, cleaned) |
| `--code N` | version code (default from the version: 1.2.3 = 10203) |
| `-o, --out FILE` | the .espk to write |
| `--out-dir DIR` | the folder for &lt;id&gt;-&lt;version&gt;-core.espk (default .) |
| `--project DIR` | write a project folder (manifest.json, espos.json, game.jar, icon.jpg) instead of an .espk |
| `--force` | with --project: write into a folder that is not empty |

Supports `--json`.

```sh
espos jar MegaBlock-1.0.0.jar --license "Apache-2.0, xylophones.net" --desc "Falling blocks"
espos jar game.jar --license "public domain" --project games/space
```

## espos types

Write the API level 1 typings (types/espos.d.ts) into a project, for the editor.

```sh
espos types [DIR]
```

| Argument | Required | Description |
|---|---|---|
| `dir` | no | the project (default .) |

## espos store

Catalog entries and publishing to the store (wraps scripts/publish_pkg.py).

### espos store entry

Print the catalog "packages" entry for the parts of a package (tools/espk.py --entry with publish_pkg.py's part rules).

```sh
espos store entry [FILES...] [options]
```

| Argument | Required | Description |
|---|---|---|
| `files...` | no | .espk parts (or project folders, built first) |

| Option | Description |
|---|---|
| `--base URL` | required: where the .espk files will be (URLs are BASE/&lt;file name&gt;) |
| `--icon-url URL` | the store picture's URL (icon_url) |
| `-o, --out FILE` | also write the entry to FILE |

Supports `--json`.

```sh
espos store entry dist/vn.you.timer-1.0.0-core.espk --base https://example.com/apps-s3
```

### espos store publish

Check, then publish packages with scripts/publish_pkg.py. Without --yes it is a dry run.

Runs the validator first (refuses on errors), then publish_pkg.py. The upload key is read only from the environment variable ESPOS_BBK by publish_pkg.py itself; espos never takes it as an argument, prints it or stores it. Without --yes, publish_pkg.py --dry-run prints the plan and the merged catalog.

```sh
espos store publish [FILES...] [options]
```

| Argument | Required | Description |
|---|---|---|
| `files...` | no | .espk parts (or project folders, built first) |

| Option | Description |
|---|---|
| `--yes` | really upload (needs ESPOS_BBK in the environment) |
| `--dry-run` | only print the plan (the default without --yes) |
| `--catalog FILE` | merge into this local catalog.json instead of the live one (offline check) |
| `--out FILE` | also write the merged catalog to FILE |
| `--replace` | allow the same version again with other bytes |
| `--shot FILE.jpg` | a store screenshot (baseline JPEG) Can be repeated. |
| `--meta FILE.json` | part choices from a build tool's catalog entry Can be repeated. |

> Needs python3 and ../scripts/publish_pkg.py next to s3desk (or ESPOS_PUBLISH_SCRIPT).

```sh
espos store publish dist/*.espk --catalog catalog.json
ESPOS_BBK=bbk_... espos store publish dist/*.espk --yes
```

## espos sim

Run packages in the SDL simulator: install, build the simulator if needed, open the app.

Builds project folders (or takes .espk files), installs them into a simulator data folder (by default &lt;project&gt;/.espos/sim-data-live, a copy of s3/sim/data-live), builds the simulator with make when its sources changed, and runs it with a script that opens the first app. Headless runs write PNG screenshots and exit 1 when the app throws ("app &lt;id&gt;: ..." in the trace). JavaScript apps go to the device's flash (fs/pkgs), other packages to the card; Java games get the J2ME runtime from build-pkgs/.

```sh
espos sim [TARGETS...] [options]
```

| Argument | Required | Description |
|---|---|---|
| `targets...` | no | project folders or .espk files (default: the current folder if it has manifest.json) |

| Option | Description |
|---|---|
| `--live` | the window (the default; mouse = finger, keys, sound): sim/build-live/espos-live |
| `--headless` | no window, script time, screenshots, exit 1 on an app error (CI): sim/build/espos-sim |
| `-s, --script FILE` | simulator commands run after the app opens (default: &lt;project&gt;/sim/smoke.txt) |
| `--app ID` | the app to open (default: the first package) |
| `--arg TEXT` | app.arg for the opened app |
| `--open` | open the app (--no-open: only install and boot) Default: `true`. |
| `--data DIR` | the simulator data folder to use (default &lt;project&gt;/.espos/sim-&lt;base&gt;) |
| `--base NAME` | what a new data folder is copied from: data-live, data-run, data or a folder Default: `"data-live"`. |
| `--fresh` | start from a new copy of the base data |
| `--vol VOL` | where to install: sd (the card) or flash (the device's flash, JavaScript only) One of `sd`, `flash`. |
| `--out DIR` | screenshots (default &lt;project&gt;/.espos/shots) |
| `-w, --watch` | rebuild, reinstall and restart (live) or run again (headless) when a file changes |
| `--build` | build the simulator when needed (--no-build: use the last binary) Default: `true`. |
| `--sim-build NAME` | the simulator's build folder under sim/ Environment: `ESPOS_SIM_BUILD`. |
| `--fast` | live: the computer's speed instead of the board's |
| `--zoom N` | live: points per screen pixel |
| `--board N` | 35 only: the 2.8" board (28) runs in QEMU (espos qemu --board 28) |
| `--log` | headless: print the simulator's whole log |
| `--timeout S` | headless: stop after S seconds Default: `120`. |
| `--force` | install even when the checks find errors |

> --watch restarts the simulator: an app cannot be reloaded inside a running simulator yet.

> Script commands (sim/main.cpp): tap X Y, long X Y, drag X0 Y0 X1 Y1 MS, swipe left|right|up|down, back, home, app ID [ARG], wait MS, shot NAME, set KEY VALUE, rot N, saver, wake, mem, quit.

> macOS only today: the simulator uses malloc_zone_statistics (sim/main.cpp) and macOS frameworks (live window).

```sh
espos sim
espos sim --watch
espos sim --headless
espos sim dist/jar.megablock-1.0.0-core.espk --script play.txt
espos sim . --headless --fresh --out shots
```

## espos qemu

Run the real firmware in Espressif's QEMU (tools/qemu/run.sh), with packages put onto its card.

Wraps tools/qemu/run.sh: builds env s3desk-qemu (s3desk-28-qemu for --board 28), puts it into build-qemu/flash.bin keeping the device's data (NVS link token, LittleFS, the card), and runs QEMU with the viewer. --install puts packages onto the card image in place (mtools, or hdiutil on macOS) before QEMU starts; with --fresh (or no image yet) the image is made first from sim/data-live, then the packages are added.

**Platform:** macOS (run.sh's viewer and mkimage.py's card use macOS tools); QEMU from Espressif (esp-develop-9.2.2) in ../tools-ext/qemu or $QEMU

```sh
espos qemu [options] [-- ...]
```

| Option | Description |
|---|---|
| `--board N` | 28 = the 2.8" board (its own image, card, console port 5556, web 8081) |
| `-i, --install PKG` | .espk (or project folder) to put onto the card Can be repeated. |
| `--fresh` | a new image from sim/data-live (unlinked, the card made again) |
| `--keep` | the image as it is: no firmware build |
| `--card IMG` | the card image to change (default build-qemu/sd[-28].img) |
| `--sdl` | QEMU's own window (no mouse), the dev console on this terminal |
| `--tcp PORT` | QEMU's own window, the dev console on a TCP port (tools/qemu/console.py PORT "APP id") |
| `--headless` | no window, the dev console on port 5555 (5556 for --board 28) |
| `--run` | start QEMU (--no-run: only build/put the packages on the card) Default: `true`. |
| `--force` | install even when the checks find errors |

Arguments after `--` go to more arguments for tools/qemu/run.sh.

> The card image must not change while QEMU runs: --install refuses when its monitor socket answers.

> Dev console in QEMU: APP &lt;id&gt;, TAP x y, DRAG ..., SHOT; screenshots: tools/qemu/console.py PORT shot:FILE.ppm.

```sh
espos qemu
espos qemu --install dist/vn.you.timer-1.0.0-core.espk
espos qemu --board 28 --install . --headless
espos qemu --install game.espk --no-run
```

## espos device

Talk to an EspOS S3 over USB: list, info, shot, logs, app, tap, drag, home, trace, mem, js, send, console.

Port safety: ports are listed from the OS without opening them; a port is opened only to ask ESPOS:HELLO and used only if it answers ESPOS:READY with kind s3; ports on the deny list are never opened. Without --port, device commands open only an S3 remembered by its USB serial number or the default /dev/cu.usbmodem1101; `device list --probe` identifies other Espressif ports (and remembers the S3). No flashing here.

**Platform:** macOS and Linux; needs the optional npm package serialport

### espos device list

Serial ports and which one is an EspOS S3 (nothing is opened without --probe).

```sh
espos device list [options]
```

| Option | Description |
|---|---|
| `--probe` | open Espressif ports (not denied ones) to ask ESPOS:HELLO |

Supports `--json`.

### espos device info

The S3's answer to HELLO: firmware, board, linked (also sets its clock).

```sh
espos device info [options]
```

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |

Supports `--json`.

### espos device shot

Screenshot to PNG (screen + top layer).

```sh
espos device shot [OUT] [options]
```

| Argument | Required | Description |
|---|---|---|
| `out` | no | the PNG (default shot-&lt;time&gt;.png) |

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |

Supports `--json`.

### espos device logs

Stream the S3's serial log.

```sh
espos device logs [options]
```

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--seconds S` | stop after S seconds (default: Ctrl-C) |
| `--grep RE` | only lines matching |
| `--all` | include sysmon/heap lines |

### espos device app

Open an app or package (APP id [arg]).

```sh
espos device app ID [ARG] [options]
```

| Argument | Required | Description |
|---|---|---|
| `id` | yes | app or package id (firmware before 0.4.0 reads only 23 characters of it) |
| `arg` | no | what the app gets as app.arg |

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device tap

Tap at x y (screen coordinates of the current rotation).

```sh
espos device tap X Y [options]
```

| Argument | Required | Description |
|---|---|---|
| `x` | yes | 0-319 (portrait) |
| `y` | yes | 0-479 (portrait) |

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device drag

Drag from x0 y0 to x1 y1 in ms.

```sh
espos device drag X0 Y0 X1 Y1 [MS] [options]
```

| Argument | Required | Description |
|---|---|---|
| `x0` | yes | start x |
| `y0` | yes | start y |
| `x1` | yes | end x |
| `y1` | yes | end y |
| `ms` | no | duration (default 250) |

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device home

Go to Home.

```sh
espos device home [options]
```

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device trace

The device's event history (TRACE: touches, screens, slow loops, heap).

```sh
espos device trace [options]
```

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device mem

Internal RAM, PSRAM and LVGL memory (MEM).

```sh
espos device mem [options]
```

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device js

Evaluate JavaScript in a scratch QuickJS context (JS code).

```sh
espos device js [CODE...] [options]
```

| Argument | Required | Description |
|---|---|---|
| `code...` | no | one line of JavaScript |

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device send

Send one dev console line and print the answer (not SHOT: use device shot).

```sh
espos device send [LINE...] [options]
```

| Argument | Required | Description |
|---|---|---|
| `line...` | no | the command, e.g. ROT 1, SAVER, REMOTE |

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |
| `--wait S` | seconds to print the device's answer |

Supports `--json`.

### espos device console

Interactive dev console: your lines to the S3, its output here.

```sh
espos device console [options]
```

| Option | Description |
|---|---|
| `-p, --port PATH` | the S3's serial port (it must answer ESPOS:READY with kind s3) Environment: `ESPOS_PORT`. |

## espos firmware

Build the firmware (PlatformIO) and check firmware.bin against the 3,932,160-byte app slot.

No flashing here: the board is flashed by its owner. The size check fails (exit 1) when firmware.bin is bigger than the app slot (app0 in the partition table, 0x3C0000).

### espos firmware build

pio run -e ENV, then the size check.

```sh
espos firmware build [options] [-- ...]
```

| Option | Description |
|---|---|
| `-e, --env ENV` | PlatformIO environment (platformio.ini): s3desk, s3desk-qemu, s3desk-28-qemu, ... Default: `"s3desk"`. |
| `--build-dir DIR` | PLATFORMIO_BUILD_DIR (e.g. .pio/build-you, so parallel builds do not collide) |
| `--dry-run` | print the command, run nothing |

Arguments after `--` go to more arguments for pio run.

Supports `--json`.

```sh
espos firmware build
espos firmware build --env s3desk-qemu
espos firmware build --build-dir .pio/build-me
```

### espos firmware size

firmware.bin's size vs the app slot (exit 1 when over).

```sh
espos firmware size [options]
```

| Option | Description |
|---|---|
| `-e, --env ENV` | PlatformIO environment (platformio.ini): s3desk, s3desk-qemu, s3desk-28-qemu, ... Default: `"s3desk"`. |
| `--build-dir DIR` | PLATFORMIO_BUILD_DIR (e.g. .pio/build-you, so parallel builds do not collide) |
| `--file BIN` | a firmware.bin to check instead |

Supports `--json`.

```sh
espos firmware size
espos firmware size --json
```

## espos j2me

Java (J2ME) engine tools: the runtime package, the MIDP layer's tests, the VM tests.

### espos j2me runtime

Build the runtime package dev.espos.j2me (class library) into build-pkgs/ (tools/j2me/make_runtime.sh).

```sh
espos j2me runtime [VERSION] [CODE] [options]
```

| Argument | Required | Description |
|---|---|---|
| `version` | no | default 1.0.0 |
| `code` | no | default 10000 |

| Option | Description |
|---|---|
| `--dry-run` | print the command, run nothing |

> Needs javac (JDK 9+) and zip.

### espos j2me test

Compile the MIDP layer and its logic tests (j2me/build-midp.sh tests); --run also runs them on the VM.

```sh
espos j2me test [options]
```

| Option | Description |
|---|---|
| `--dry-run` | print the command, run nothing |
| `--run` | run each MIDP test class on tools/j2me/build/j2me-run (boot class path with the stand-in natives) |

> Needs the class library's java.* in j2me/build/classlib (j2me/build.sh or espos j2me vm-test builds it).

### espos j2me vm-test

Build the VM runner and run every VM test, compared with the JDK (j2me/test/run.sh).

```sh
espos j2me vm-test [options]
```

| Option | Description |
|---|---|
| `--dry-run` | print the command, run nothing |
| `--asan` | AddressSanitizer + UBSan build |
| `--gcstress` | a full GC at every allocation |
| `--bench` | also the benchmark |

## espos doctor

Check the toolchain (node, npm packages, python, JDK, make, SDL2, PlatformIO, QEMU, simulator builds) with install hints.

```sh
espos doctor
```

Supports `--json`.

## espos help

Help for a command; --json prints every command, option and argument (the docs site's reference).

```sh
espos help [COMMAND...]
```

| Argument | Required | Description |
|---|---|---|
| `command...` | no | a command (and sub-command) |

Supports `--json`.

```sh
espos help sim
espos help device shot
espos help --json > commands.json
```

## espos version

Print the CLI's version.

```sh
espos version
```

Supports `--json`.

## Check rules

`espos check` and `espos build` apply these rules. Errors stop a build; warnings stop it only with `--strict`. `espos check --rules` prints this list.

| Code | Level | Rule | Source |
|---|---|---|---|
| `M001` | error | id: lowercase letters, digits, '.', '_', '-', 1-48 characters, starting with a letter or digit (the language packs dev.espos.lang.&lt;tag&gt; keep their tag's capitals) | web admin (espk.py: &lt;=48, no '/'; publish: &lt;=64) |
| `M002` | warning | id should be a reverse-domain name (vn.you.app) | docs |
| `M003` | warning | id longer than 23 characters and min_fw below 400: the dev console's APP command before firmware 0.4.0 cuts it | src/devtools.cpp |
| `M010` | error | name: text of 1-40 characters | publish_pkg.py (espk.py: non-empty) |
| `M011` | error | version: 1-20 characters of 0-9 a-z . + - | publish_pkg.py + web admin cleanVersion |
| `M012` | error | code: a whole number 1..1999999999 (not 1.0, not true) | publish_pkg.py + espk.py |
| `M013` | error | kind: "js" or "native" | all |
| `M014` | error | engine: js, pet, run, j2me, lang or font; kind js goes with engine js; native with another | espk.py + publish_pkg.py |
| `M015` | error | min_fw: a whole number &gt;= 0 (300 = 0.3.0), required | publish_pkg.py (web admin assumes 300, publish 0) |
| `M016` | warning | min_fw below what the package needs (300 for packages, 400 for j2me, 500 for lang and font) | web admin (J2ME_MIN_FW) + tools/i18n.py |
| `M017` | error | category: game, tool, study, health or widget (required) | publish_pkg.py |
| `M018` | error | author: text of at most 40 characters; desc: at most 300 | publish_pkg.py |
| `M019` | error | perms: a list of sound, notify, net, bg | espk.py + publish_pkg.py |
| `M020` | warning | perms net and bg are reserved: no firmware grants them yet | src/js/jsapp.cpp |
| `M021` | error | "permissions" (the 0.2.3 field) without "perms", or with a different list: the device adds both up, the store reads only perms | pkg.cpp vs publish_pkg.py |
| `M022` | error | icon: {"bg": ["#rrggbb", "#rrggbb"], "glyph": one of the glyphs} | espk.py + publish_pkg.py |
| `M023` | warning | no icon.jpg and no icon colours: the device draws a grey square | publish_pkg.py |
| `M024` | error | type: "app" (default), "pack", or "face"/"widget"/"theme" (declarative, M040-M047); service/driver need manifest v2 (planned) | espk.py |
| `M025` | error | addon_of: the id of another package | espk.py + publish_pkg.py |
| `M026` | error | no the ellipsis character anywhere in the manifest (the device shows text in full) | espk.py |
| `M027` | error | part: "core" or a short lowercase name, required (publish_pkg.py finds the core by it) | publish_pkg.py |
| `M028` | error | core "parts": [{id, group, label, default, req}] naming parts of this package | publish_pkg.py |
| `M029` | warning | j2me: {midlet, keypad (full\|compact\|touch), profile, config} for engine j2me | web admin + docs/j2me-games.md |
| `M030` | warning | field not read by the firmware, the store or the tools | - |
| `M031` | warning | manifest v2 field (docs/platform.md): kept, but firmware 0.5.0 does not read it | - |
| `M032` | error | the manifest is not a JSON object / not JSON | all |
| `M033` | error | parts of one package share id, version and code; exactly one core; no part twice | publish_pkg.py |
| `M034` | error | group parts: a label of 1-40 characters, at most one default per group; core is never a choice | publish_pkg.py |
| `M035` | error | i18n: {"&lt;language tag&gt;": {"name": text of 1-40 characters, "desc": text of at most 300}} | espk.py check_i18n + src/pkg/pkg.cpp, store.cpp |
| `M036` | warning | i18n: a key the S3 never shows (it reads exactly en, vi, zh-Hans, es, fr, de, ja, ko, pt-BR, ru, id), or a field other than name and desc | src/i18n.cpp LANGS + src/pkg/store.cpp |
| `M037` | warning | i18n: letters the device cannot draw in that language (Latin-1 for en, es, fr, de, pt-BR, id; plus the Vietnamese letters for vi, Cyrillic for ru) | tools/i18n.py BUILTIN + font packs |
| `M038` | warning | engine lang/font: the OS's own packs (tools/i18n.py): type "pack", "lang" a language tag; the device uses a language only as dev.espos.lang.&lt;lang&gt; and the font its language table names | src/i18n.cpp |
| `M040` | error | face/widget/theme: "runtime": "decl" | espk.py check_decl_manifest |
| `M041` | error | face/widget/theme: api {"min": 1..1, "target": n} (optional; min defaults to 1, the declarative level) | espk.py check_decl_manifest |
| `M042` | error | face/widget/theme: requires: a list of capability flags (screen.color, screen.gray, screen.eink, screen.fast, input.touch, input.buttons, mem.psram, audio.out, audio.in, store.sd, net.wifi, net.ble, power.battery, led.rgb) | espk.py check_decl_manifest (CAPS) |
| `M043` | error | face/widget/theme: screens: a list of sizes like "800x480" | espk.py check_decl_manifest |
| `M044` | error | face/widget/theme: refresh: minute, hour, data (the default) or day | espk.py check_decl_manifest |
| `M045` | error | face/widget/theme: perms: a list of data:weather, data:calendar, data:reading, data:news, data:markets | espk.py check_decl_manifest |
| `M046` | error | face/widget/theme: entry {"&lt;type&gt;": "&lt;file&gt;.json"} naming the layout (or theme) file | espk.py check_decl_manifest |
| `M047` | warning | face/widget/theme: kind, engine, min_fw, category, j2me, permissions are app fields; nothing reads them in a declarative package | docs/profiles.md §3 |
| `F001` | error | file names: 1-51 bytes, no '..', '//', leading or trailing '/', \ : * ? " &lt; &gt; \|, control characters, not manifest.json | device safeName + espk.py + publish_pkg.py |
| `F002` | error | kind js needs main.js in the core part | all |
| `F003` | error | engine j2me (not a pack) needs game.jar in the core part | docs/j2me-games.md |
| `F004` | error | icon.jpg: a square baseline JPEG (the device cannot draw progressive ones), at most 200,000 bytes | espk.py + publish_pkg.py + apps.cpp |
| `F005` | warning | icon.jpg is not 120x120 | espk.py + publish_pkg.py |
| `F006` | error | main.js below 512 KB (the device does not open bigger ones) | src/js/jsapp.cpp |
| `F007` | error | a part with no files | publish_pkg.py |
| `F008` | warning | a folder without espos.json packs every file: build outputs (*.espk, dist/, node_modules/) end up in the package | tools/espk.py |
| `F009` | error | at most 512 files per part; at most 64 MB per file | device (src/pkg/espk.cpp) |
| `F010` | warning | the package is bigger than the internal app area (7.5 MB): it fits only on a memory card | docs/ecosystem.md |
| `F011` | warning | engine lang needs strings.bin; engine font needs r&lt;px&gt;.bin (or b&lt;px&gt;.bin) files | src/i18n.cpp + src/fonts.cpp |
| `F020` | error | face/widget: the layout file named by entry is in the package (themes: not checked) | espk.py check_decl_file |
| `F021` | error | face/widget: the layout file is at most 12288 bytes (the X4 reads it in one go) | espk.py check_decl_file |
| `F022` | error | face/widget: the layout file is a JSON object (UTF-8, no BOM) | espk.py check_decl_file |
| `F023` | error | face/widget: the layout file has "decl": 1 (the format level) | espk.py check_decl_file |
| `F024` | error | face/widget: "layouts": a non-empty list | espk.py check_decl_file |
| `F025` | error | face/widget: each layout's "size": [w, h], whole numbers above 0 | espk.py check_decl_file |
| `F026` | error | "items" are lists; each item's "t": box, line, circle, text, icon, image, bar, list or group | espk.py check_decl_file |
| `F027` | error | an image's "src" (without a {binding}) is a file of the package | espk.py check_decl_file |
| `F028` | error | lists and groups go 3 deep at most | espk.py check_decl_file + docs/profiles.md §4.3 |
| `F029` | error | no the ellipsis character in an item (text always fits on the device) | espk.py check_decl_file |
| `F030` | error | the manifest's "screens" (when given) are the layout sizes | espk.py check_decl_file |
| `C001` | error | main.js does not parse (QuickJS runs it as a classic script, ES2023) | - |
| `C002` | error | import/export: main.js is one classic script (bundle TypeScript or modules with espos build) | src/js/jsapp.cpp JS_EVAL_TYPE_GLOBAL |
| `C003` | warning | code calls notify / notify.schedule / notify.cancel / app.badge without the notify permission (it throws) | src/js/jsapp.cpp |
| `C004` | warning | code calls sound.* without the sound permission (it throws) | src/js/jsapp.cpp |
| `C005` | info | a permission is declared but the code never uses it | - |
| `C006` | warning | uses something API level 1 does not have (console, setTimeout, setInterval, fetch, require, XMLHttpRequest, localStorage) | src/js/jsapp.cpp prelude |
| `C007` | warning | async/await or .then(): API level 1 never runs Promise jobs, so the callbacks never run | src/js/jsapp.cpp |
| `C008` | warning | a the ellipsis character in main.js (screens show text in full: shrink or wrap) | docs |
| `C009` | warning | main.js is not valid UTF-8 | - |
