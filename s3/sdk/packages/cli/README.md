# @espos/cli: the `espos` command

One command for EspOS development: make a package, check it, run it in the simulator, in QEMU (the real
firmware) or on a board, and hand it to the store. Install and overview: [../../README.md](../../README.md).
Every command prints its options with `--help`; `espos help --json` prints all of them as data (also written to
[`commands.json`](commands.json) by `npm run build`).

Conventions: human text on stdout, progress and warnings on stderr; `--json` (where a command has it) prints one
JSON document on stdout. Exit codes: 0 ok, 1 problems found or a step failed, 2 bad arguments, 3 a tool or file is
missing, 4 no EspOS S3 found or a port refused. Global options: `--root DIR` (the s3desk repo; `ESPOS_ROOT`),
`--quiet`, `--no-color` (`NO_COLOR=1`).

## Packages

### `espos create DIR [--template js-app|js-app-ts|j2me-game]`

A ready project: `manifest.json` (current format), `espos.json`, the code, a placeholder `icon.jpg` (120x120
baseline JPEG in the manifest's colours), `README.md`, `sim/smoke.txt` (simulator commands run after the app opens),
`.gitignore`; for `js-app-ts` also `tsconfig.json` and `types/espos.d.ts`. The project is checked right after it is
written. Options: `--id` (default `com.example.<dir>`, `jar.<name>` for games; set your own: it never changes once
published), `--name`, `--desc`, `--author`, `--category`; for games `--jar FILE --license TEXT [--keypad]`, which
fill the manifest from the jar (as `espos jar`). Widgets, faces, themes, services and drivers have no template:
they need manifest v2 (planned, docs/platform.md).

### `espos build [DIR] [-o FILE] [--out-dir DIR] [--no-typecheck] [--minify] [--strict] [--json]`

- A project (`espos.json`): `main` becomes `main.js` (TypeScript is type-checked against the SDK's typings and
  bundled with esbuild into one classic script, an IIFE, ES2022); `files` are copied as they are.
- A plain folder with `manifest.json`: every file except dotfiles, hidden folders and `manifest.json`, exactly as `tools/espk.py`;
  the output is byte-identical to it (manifest serialised as Python's `json.dumps`, files in Python's path order).
  The package is written next to the folder, never inside it.
- The package is built in memory, read back and checked; nothing is written when a check fails.
- Output: `<out>/<id>-<version>-<part>.espk` (the store's names), its files and sha256.

### `espos check [TARGETS...] [--strict] [--rules] [--json]`

Folders are built in memory first; `.espk` files with the same id are checked together as the parts of one
package. Each finding has a level, a code and the file it is about:

```
FAIL  apps/dice (dev.espos.dice): 7 errors, 0 warnings
  error   M027   manifest.json: part: missing; write "part": "core" (publish_pkg.py does not find the core part without it)
  error   M021   manifest.json: use "perms" instead of "permissions" (the 0.2.3 field: ...)
```

Codes: `M` manifest, `F` files, `C` code (`main.js`), `P` the `.espk` structure, `TS` TypeScript. `--rules` lists
each rule with its level and the tool it came from. Errors exit 1; `--strict` makes warnings fail too.

### `espos inspect FILE.espk [--base URL] [--extract DIR] [--json]`

Header, manifest, file table (offsets, sizes, CRC-32), sha256, the catalog entry and the checks. `--extract`
unpacks the files and the manifest.

### `espos jar GAME.jar --license TEXT [...]`

The web admin's "Thêm game Java" on the command line: reads `META-INF/MANIFEST.MF` (MIDlet-Name, -Vendor, -Version,
MIDlet-1, MIDlet-Icon, profile, configuration), draws the icon (PNG) at a whole-pixel scale on a 120x120 tile of its
average colour, darker below (icons over 96 px are scaled down to fit instead of cut), and writes
`jar.<slug>-<version>-core.espk`: kind `native`, engine `j2me`, `min_fw` 400, perms `sound`,
`j2me: {midlet, keypad, profile, config}`, plus `"part": "core"` (which publish_pkg.py needs) and `"license"`. The
licence text is required. `--project DIR` writes a project folder instead. Options: `--id`, `--name`, `--desc`,
`--author`, `--category`, `--keypad full|compact|touch`, `--version`, `--code`, `-o`, `--out-dir`.

### `espos types [DIR]`

Writes `types/espos.d.ts` (API level 1, from `@espos/sdk`) for your editor. `build` always checks against the
CLI's own copy.

### `espos store entry FILES... --base URL [--icon-url URL] [-o FILE]`

The catalog `"packages"` entry for the parts of a package: `tools/espk.py --entry` with `publish_pkg.py`'s part rules
(core and parts without a group are required; one default per group), plus `j2me` and `license` as the web admin
writes them.

### `espos store publish FILES... [--yes] [--catalog FILE] [--out FILE] [--replace] [--shot FILE] [--meta FILE]`

Checks first (refuses on errors), then runs `../scripts/publish_pkg.py` (`ESPOS_PUBLISH_SCRIPT`). Without `--yes`
it is `publish_pkg.py --dry-run`: the upload plan and the merged catalog, nothing sent (`--catalog` merges into a
local catalog for an offline check). `--yes` needs `ESPOS_BBK` in the environment; publish_pkg.py reads it there.
`espos` never takes the key as an argument, never prints it and never stores it.

### `espos login [--key-stdin]`, `espos logout`, `espos whoami`

A developer key for your espos.dev account: made on the web (Account › Developer keys), shown there once, kept by the
server only as its sha256. `login` checks it with `GET /v1/dev/me` and saves it in `credentials.json` in the config
folder (mode 0600); `ESPOS_DEV_KEY` is used instead when set. The key is sent only over HTTPS (or HTTP to this
machine, for a local server) and never printed. Never a password.

### `espos upload [DIR|FILE.espk] [--device ID]... [--no-install] [--dry-run]`

Builds and checks like `build`, adds the private-upload rules (`checkPrivate`: U001 sizes, U002 kinds, one part,
id at most 46 characters, a Java game asks only for sound), then `POST /v1/dev/pkgs/upload` (a signed PUT URL), the
PUT, and `POST /v1/dev/pkgs/confirm`. The server checks the package again (web/backend confirm_user_pkg, the same
rules; `web/scripts/user-pkgs-check.mjs` keeps them in step) and installs it as `u.<id>`: a new package on all your
S3 devices (or `--device`), an update (higher `code`, checked before anything is sent) on every device that has it.

### `espos apps [list]`, `espos apps install ID [--device ID]...`, `espos apps rm ID [--yes]`

`GET /v1/dev/pkgs?devices=1`, `POST /v1/dev/pkgs/install`, `POST /v1/dev/pkgs/delete`. An id works with or without
`u.`. `rm` asks in a terminal and needs `--yes` otherwise.

## Running

### `espos sim [TARGETS...] [--headless] [--watch] [--script FILE] [...]`

1. Builds project folders (to their `dist/`) or takes `.espk` files; refuses packages with errors (`--force`).
2. Uses a simulator data folder: `--data DIR`, or `<project>/.espos/sim-data-live`, made as a copy of
   `s3/sim/data-live` (`--base data-run|data|DIR`, `--fresh` for a new copy; copies share blocks on APFS and
   reflink file systems).
3. Installs each package as the device would keep it: JavaScript apps in Máy (`fs/pkgs/<id>/`), others on the card
   (`sd/espos/pkgs/<id>/`), `--vol` to choose. A Java game gets the runtime `dev.espos.j2me` from
   `build-pkgs/` when the data folder lacks it (`espos j2me runtime` builds it).
4. Builds the simulator with `make -C sim` when its sources changed (`sim/build` headless, `sim/build-live` for the
   window; `--sim-build NAME`, `--no-build`).
5. Runs it with a script: boot, `app <id>`, a first screenshot (headless), then `--script` or the project's
   `sim/smoke.txt`.

The window (default): mouse = finger, arrows swipe, H home, B back, S screenshot, M memory, Q quit, commands typed
in the terminal. `--headless`: script time, PNG screenshots in `.espos/shots`, exit 1 when the app throws (the
trace's `app <id>: ...`), `--log` for the whole log. `--watch`: a saved file rebuilds and reinstalls the package and
restarts the simulator (window) or runs the script again (headless); a build with errors keeps the old one running.
An app cannot be reloaded inside a running simulator yet. `--board 28` is refused: the 2.8" board runs only in
QEMU. macOS only today (see the overview).

Script commands (`s3/sim/main.cpp`): `tap X Y`, `long X Y`, `drag X0 Y0 X1 Y1 MS`, `hold ...`, `down/move/up`,
`swipe left|right|up|down`, `homeswipe`, `backswipe`, `home`, `back`, `app ID [ARG]`, `wait MS`, `shot NAME`,
`set KEY VALUE`, `rot N`, `saver`, `wake`, `mem`, `notif ...`, `quit`.

### `espos qemu [--install PKG ...] [--board 28] [--fresh|--keep] [--sdl|--tcp PORT|--headless] [--no-run]`

Wraps `tools/qemu/run.sh`: builds env `s3desk-qemu` (`s3desk-28-qemu` for `--board 28`), puts it into
`build-qemu/flash.bin` keeping the device's data, runs QEMU. `--install` puts packages onto the card image
(`build-qemu/sd[-28].img`, or `--card IMG`) in place, with mtools or macOS's `hdiutil`, so what the firmware saved on
the card and in its flash stays; it refuses while that QEMU runs (its monitor socket answers). With `--fresh` or no
image yet, the image is made first (pio + `mkimage.py`, data from `sim/data-live`), then the packages are added.
Open the app by typing `APP <id>` in the QEMU terminal. Arguments after `--` go to run.sh.

### `espos device <command> [--port PATH]`

The board's dev console (`src/devtools.cpp`) over USB serial, with the safety rules in the overview: only a port
that answers `ESPOS:READY` with kind `s3` is used, deny-listed ports are never opened, unknown ports are opened
only by `device list --probe`.

| command | dev console |
|---|---|
| `list [--probe]` | ports from the OS (none opened without `--probe`) |
| `info` | `ESPOS:HELLO` (also sets the board's clock): firmware, board, linked |
| `shot [OUT.png]` | `SHOT`: the screen (RGB565) with the top layer composited |
| `logs [--seconds S] [--grep RE] [--all]` | the serial log (sysmon/heap lines hidden without `--all`) |
| `app ID [ARG]` | `APP id arg` (ids up to 51 characters; firmware before 0.4.0 read only 23, and you get a warning there) |
| `tap X Y`, `drag X0 Y0 X1 Y1 [MS]`, `home` | `TAP`, `DRAG`, `HOME` |
| `trace`, `mem` | `TRACE` (event history), `MEM` (internal RAM, PSRAM, LVGL) |
| `js CODE` | `JS code`: evaluate in a scratch QuickJS context |
| `send LINE` | any other console line (`ROT 1`, `SAVER`, `REMOTE 1`); not SHOT or provisioning |
| `console` | interactive: your lines to the board, its output here |

Needs the optional npm package `serialport` (installed by `npm install` when a prebuilt binary fits).

## Firmware and tools

| command | what |
|---|---|
| `firmware build [--env ENV] [--build-dir DIR] [--dry-run] [-- pio args]` | `pio run -e ENV`, then the size check |
| `firmware size [--env ENV] [--file BIN] [--json]` | `firmware.bin` against the app slot (app0 in the partition table: 3,932,160 bytes); exit 1 when over |
| `j2me runtime [VERSION] [CODE]` | `tools/j2me/make_runtime.sh`: `build-pkgs/dev.espos.j2me-<v>.espk` |
| `j2me test [--run]` | `j2me/build-midp.sh tests`; `--run` runs each MIDP test class on `tools/j2me/build/j2me-run` |
| `j2me vm-test [--asan] [--gcstress] [--bench]` | `j2me/test/run.sh` |
| `doctor [--json]` | node and npm packages, python3, JDK, zip, make and a C++ compiler, SDL2, PlatformIO and its libdeps, the simulator builds (up to date or not), Espressif QEMU, mtools/hdiutil, firmware size, with install hints |

There is no flash command: boards are flashed by their owner. `--dry-run` on the wrappers prints the command line.

## Environment

| variable | what |
|---|---|
| `ESPOS_ROOT` | the s3desk repo |
| `ESPOS_PYTHON`, `ESPOS_PIO`, `QEMU`, `SDL2_CONFIG` | tools, when not on PATH / in the usual place |
| `ESPOS_PORT` | the S3's serial port (it still has to answer as kind s3) |
| `ESPOS_DENY_PORTS` | ports never opened, comma separated |
| `ESPOS_SIM_BUILD` | the simulator's build folder under `sim/` |
| `ESPOS_BBK` | the store key, read by publish_pkg.py only (`store publish --yes`) |
| `ESPOS_PUBLISH_SCRIPT` | `publish_pkg.py` elsewhere |
| `ESPOS_CONFIG_DIR` | remembered boards, the deny list and the developer key (default `~/.config/espos`) |
| `ESPOS_DEV_KEY` | a developer key used instead of the saved one (CI); never printed |
| `ESPOS_API` | the server for `login`, `upload`, `apps` (default `https://api.espos.dev`) |

## Source

`src/main.ts` (command table, help, `help --json`), `args.ts` (option parser), `pyjson.ts` (JSON exactly as
Python's json module), `espk.ts` (the file format, catalog entries, simulator installs), `validate.ts` (the rules),
`project.ts` (projects, esbuild, TypeScript), `jar.ts`, `image.ts`, `serial.ts` (ports and the dev console),
`commands/*.ts`. Build: `npm run build` (tsc into `dist/`, then `commands.json`). Tests: `npm test` in `sdk/`.
