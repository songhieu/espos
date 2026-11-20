# EspOS app ecosystem (S3) — the shared plan

Owner's words (2026-11-16): apps and games are packages downloaded from a store; the desk web installs and removes
them so there is room (not every app built into the firmware); with a memory card, an app or game can be installed
onto the card; games and apps run from the internal flash or from the card; apps (games, future widgets) can send
notifications; an app is developed on its own as one file with its metadata; "build an ecosystem like a real phone".

## 2. The package file: `.espk` (one file, developed on its own)

Little endian. Built by `tools/espk.py <folder> <out.espk>` from a folder with `manifest.json` and the files.

```
0   "ESPK"
4   u16 format = 1
6   u16 flags = 0
8   u32 manifest_len
12  u32 nfiles
16  manifest JSON (UTF-8), then zero padding to a multiple of 16
    file table: nfiles x 64 bytes { char name[52] (zero padded, may contain '/'); u32 off; u32 size; u32 crc32 }
    file data, each file at a 16-byte aligned `off` (from the start of the .espk), in table order
```

`manifest.json`:
```json
{"id": "vn.pawboop.run", "name": "Chó chạy", "version": "1.0.0", "code": 1,
 "kind": "native", "engine": "run", "min_fw": 300, "category": "game", "author": "PawBoop",
 "desc": "Biscuit chạy qua chợ, công trường, rạp xiếc", "icon": {"bg": ["#ffb347", "#ff7a00"], "glyph": "play"},
 "perms": ["sound", "notify"], "part": "core"}
```
- `kind` "js": `main.js` is the app (QuickJS, `docs/apps.md`); `engine` "js". `kind` "native": `engine` names code in
  the firmware ("pet", "run"); the package carries its data. An engine the firmware does not have: the store says
  "Cần cập nhật phần mềm" (min_fw).
- `icon.jpg` (optional, 120x120 baseline JPEG, square; the OS rounds the corners): the app's picture; else the
  gradient + glyph.
- Parts: a big package is several .espk files with the same `id`, each with `"part": "<name>"`. The `core` part
  holds the full manifest; other parts only `id`, `version`, `code`, `part`. The catalog says which parts exist.
- `perms`: `sound`, `notify` (notifications), `net` (later), `bg` (later: run in the background).

## 3. Storage

- **Internal ("Máy")**: the app area = the flash range of `pb_dog` + `pb_ui` + `pb_voice` (contiguous,
  0x790000–0xEBE000, 7,528,448 bytes; a later table may name it `apps`). Files are stored as contiguous extents
  (4 KB granularity, memory-mapped for reading); the index lives in NVS (namespace `pkgs`). The PawBoop packs already
  on the board are adopted at first boot as package `vn.pawboop.pet` (files at their current places, no copy).
  First-fit allocation; when no gap fits but the total free does, files are moved down one at a time (copy, then
  index update) so a power cut never loses an installed file.
- **Memory card ("Thẻ nhớ")**: SD_MMC 4-bit (CLK 5, CMD 4, D0 6, D1 7, D2 2, D3 3), FAT, `/espos/pkgs/<id>/<file>`
  plus `/espos/pkgs/<id>/manifest.json`. Self-describing (no index). Card out: its apps stay on Home greyed,
  "Cần thẻ nhớ". Card in: they come back (hot plug checked every few seconds or by the card-detect state).
- LittleFS keeps settings, caches, photos as today, and the small JavaScript packages installed on Máy
  (`/pkgs/<id>/`): the 7.5 MB area is for big files (PawBoop alone leaves 8 KB there). JS apps from before (`/apps/<id>.json`, setting `s3.apps`) are
  converted to packages at first boot.
- Simulator: the app area is a file `$SIM_DATA/apparea.bin` (7,528,448 bytes), the card a folder `$SIM_DATA/sd/`
  (no folder = no card).

## 4. API on the device

`src/pkg/pkg.h` (packages, volumes, `pkg::File` that reads the same way from both places, `pkg::run`) and
`src/notif.h` (notifications). Engines: `pet` -> `app_pet::open(arg)`, `run` -> `run::open(id, arg)` (`src/run/run.h`),
`js` -> `jsapp::open(id)`. Installed packages appear in `apps::all()` (Home, App Library, search) with their icon.
A package is removed from Home when uninstalled; Home keeps its place if it is only on a missing card.

## 5. The store catalog (public storage `apps-s3/catalog.json`)

Firmware 0.2.3 reads `"apps"` (the old `.espk` list) — keep it as is. New firmware reads `"packages"`:
```json
{"apps": [...old...],
 "packages": [{"id": "vn.pawboop.run", "name": "Chó chạy", "version": "1.0.0", "code": 1, "kind": "native",
   "engine": "run", "min_fw": 300, "category": "game", "author": "PawBoop", "desc": "...",
   "icon": {"bg": ["#ffb347", "#ff7a00"], "glyph": "play"}, "icon_url": ".../vn.pawboop.run-1.0.0.jpg",
   "perms": ["sound", "notify"], "shots": [".../shot1.jpg"],
   "parts": [{"id": "core", "req": true, "url": ".../vn.pawboop.run-1.0.0-core.espk", "size": 2071234, "sha256": "..."},
             {"id": "roads10", "group": "roads", "default": true, "label": "Nhẹ (10 hình/giây)", "url": "...", "size": 5767000, "sha256": "..."},
             {"id": "roads20", "group": "roads", "label": "Đẹp (20 hình/giây)", "url": "...", "size": 11500000, "sha256": "..."}]}]}
```
Install = all `req` parts + one part of each `group` (its `default` unless the user picks another). A group's part may say `"board": "es3c28p"`: it is made for that board only; the device takes the part made for its own board, whatever was asked (src/pkg/store.cpp fitBoard; added 2026-11-19 for the aquarium's backgrounds bg35 / bg28). Categories:
`game`, `tool`, `study`, `health`, `widget`. Files are uploaded by `scripts/publish_pkg.py` with a
temporary bbk_ key that is revoked right after.

## 6. Sync with the web

- **What the user wants** — setting `s3.pkgs` (two-way like every `s3.*` setting, last writer wins):
  `[{"id": "vn.pawboop.run", "parts": ["core", "roads10"], "vol": "sd"}, {"id": "dev.espos.focus", "vol": "flash"},
    {"id": "vn.pawboop.pet", "rm": true}]`. The device installs what is listed and not there (one at a time, in list
  order), moves a package whose `vol` changed, removes entries with `"rm": true` and then drops those entries from
  the list. A package that is installed but not listed is left alone and added to the list by the device.
  Installing or removing on the device updates `s3.pkgs` the same way.
  An install cancelled on the device (0.5.7) does the same: its entry goes, or, for an update, it names the version
  that stayed and loses `code`. It is not tried again until the web asks again: lists it (or, for an update, sends
  its `code`), which is what "Install" and "Update" on the web do. Installs the web asked for that fail are tried at
  most 3 times a boot.
- **What the device has** — the sync body gets `"pkgs"`:
  `{"vols": {"flash": {"total": KB, "free": KB}, "sd": {"present": true, "total": KB, "free": KB}},
    "installed": [{"id", "version", "code", "vol", "parts": [...], "kb", "ok": true}],
    "job": {"id", "state": "download|write|move|remove|done|error", "pct", "err"}}`.
  The server stores it in `devices.pkgs` (JSON) and `devices.pkgs_at` (new columns; never drop or retype columns).
  The web shows it.
- **Notification switches** — setting `s3.notif`: `{"<app id>": false}` turns an app's notifications off (missing =
  on). Built-in ids: `news`, `weather`, `cal`, `clock`, `remind`, `pet`, `desk` (EspOS: the dashboard's notes from the
  web), ... (as in `apps.cpp`). `s3.notif_preview` "0" hides the text in banners.

## 7. Firmware version

The next release is 0.3.0 (code 300). Packages say `min_fw` with that number.
