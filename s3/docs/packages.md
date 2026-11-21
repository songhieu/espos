# Making a package for EspOS (S3)

An app or a game for the S3 desk is a **package**: one `.espk` file with a manifest and the app's files. The
store installs it over WiFi into the device's flash ("Máy") or onto the memory card ("Thẻ nhớ"); the user can
remove it or move it between the two to make room, like on a phone. The firmware never has to change for a new
JavaScript app. The contract between the parts is `docs/ecosystem.md`; this page is the how-to.

## 1. Two kinds

| kind | engine | what the package holds | example |
|---|---|---|---|
| `js` | `js` | `main.js`, the whole app (QuickJS, the `ui.*` API in `docs/apps.md`) | Focus, Vocabulary |
| `native` | `pet`, `run`, `lang`, `font` | the data of an engine built into the firmware (pictures, sounds, levels; a language's texts or letters, docs/i18n.md) | PawBoop, Coin Run, Tiếng Việt |

A JavaScript app is small and runs on any firmware that has the package system (0.3.0, `min_fw` 300). A native
package needs its engine in the firmware; a device without it shows "Needs a software update".

## 2. The folder

```
myapp/
  manifest.json
  main.js          (kind js)
  icon.jpg         (optional: 120x120 baseline JPEG, square; the device rounds the corners)
  data/level1.bin  (any other files; sub-folders are kept in the names, at most 51 bytes per name)
```

`manifest.json` (the full one, in the `core` part):

```json
{"id": "com.you.timer", "name": "Kitchen timer", "version": "1.0.0", "code": 10000,
 "kind": "js", "engine": "js", "min_fw": 300, "category": "tool", "author": "You",
 "desc": "A cooking timer with a bell", "icon": {"bg": ["#ff6b5a", "#e0383b"], "glyph": "timer"},
 "perms": ["sound", "notify"], "part": "core",
 "i18n": {"vi": {"name": "Hẹn giờ", "desc": "Hẹn giờ nấu ăn, có chuông"}, "de": {"name": "Küchenwecker"}}}
```

| field | meaning |
|---|---|
| `id` | reverse domain name, unique, never changes (it names the folder on the card and the app's saved data) |
| `version`, `code` | what people read, and a whole number that grows with every release (the store offers "Update" when the catalog's `code` is higher than the installed one) |
| `kind`, `engine` | see the table above |
| `min_fw` | the firmware code it needs (0.3.0 = 300) |
| `category` | `game` Games, `tool` Tools, `study` Study, `health` Health, `widget` Widgets |
| `name`, `desc`, `author` | English, short, plain; never "…" (the device shows text in full) |
| `i18n` | optional: `{"<tag>": {"name": ..., "desc": ...}}` for other languages (`docs/i18n.md`); the device shows the ones of the language it shows. In `main.js`, `app.lang` is that language |
| `icon` | `bg`: two colours of the gradient; `glyph`: timer, alarm, bell, list, drop, flag, globe, sun, moon, photo, edit, font, home, search, speaker, play, lap, pin, leaf, power, grid. Used when there is no `icon.jpg` |
| `perms` | `sound` (sound.*), `notify` (notify(), notify.schedule, app.badge), `net` and `bg` (later). Without one, the call throws; the store shows them in plain words |
| `part` | `core` for the main part (see below) |
| `addon_of` | optional: the id of the app this package adds to (§4b) |

## 3. Build and check

```
python3 tools/espk.py myapp/ out/com.you.timer-1.0.0-core.espk     # checks the manifest, then builds
python3 tools/espk.py --check out/com.you.timer-1.0.0-core.espk    # header, table, every file's CRC32
python3 tools/espk.py --entry https://.../apps-s3 out/com.you.timer-1.0.0-core.espk   # its catalog entry
```

The JavaScript apps in `s3/apps/` are built by `python3 apps/pack.py OUTDIR BASE_URL [native.espk ...]`: for
each app the old `.espk` (firmware 0.2.3) and the `.espk`, and `catalog.json` with both lists (`"apps"` for
0.2.3, `"packages"` for 0.3.0+). Native packages built with `tools/espk.py` are added to the catalog by passing
their files. PawBoop's package (`vn.pawboop.pet`, from `sim/data/pawboop/`) is built the same way.

## 4. Big packages: parts

A package can come in several `.espk` files with the same `id`, each with `"part": "<name>"`. The core part holds
the full manifest; other parts only `id`, `version`, `code`, `part` (and, for the catalog, `group`, `label`,
`default`). Install = all required parts + one part of each group:

```json
"parts": [{"id": "core", "req": true, "url": "...-core.espk", "size": 2071234, "sha256": "..."},
          {"id": "roads10", "group": "roads", "default": true, "label": "Nhẹ (10 hình/giây)", "url": "...", "size": 5767000, "sha256": "..."},
          {"id": "roads20", "group": "roads", "label": "Đẹp (20 hình/giây)", "url": "...", "size": 11500000, "sha256": "..."}]
```

A part's choice lives in its own manifest (`group`, `label` in English, `labels` {"vi": ...} in other languages,
`default`), so `apps/pack.py` / `tools/i18n.py catalog` carry it into the catalog; a builder's
`<id>-<version>-catalog.json` beside the parts, `apps/catalog-text.json` `"parts"` and the catalog before (same
version) fill in what a manifest lacks. The build refuses a catalog where a part of a multi-part package is neither
required nor in a group, or an add-on lost `addon_of`/`set`/`needs` (`tools/espk.py check_catalog`; cases in
`tools/test_catalog.py`). The device shows a part's label in the language shown.
The store shows a group as a choice ("Chọn gói tải: Nhẹ / Đẹp" with sizes). Two parts must not hold a file with the
same name; put each part's files in its own sub-folder (`core/...`, `roads/...`) so the engine reads the same
names whichever part was picked.

## 4b. Add-ons

A package can add to another one: `"addon_of": "<main id>"` in the core manifest (and so in its catalog entry),
e.g. the run game "Chạy lấy xu" (`vn.pawboop.run`, `"addon_of": "vn.pawboop.pet"`) where Biscuit earns coins for
her food. An add-on is not an app of its own:
- no icon on Home, in the App Library or in search; `pkg::find`, `pkg::open` and `pkg::run` work as usual (the main
  app starts it, e.g. `pkg::run("vn.pawboop.run", arg)`);
- the store does not list it; the main app's page shows it under "Phần thêm" with its own part choice, place,
  Cài / Gỡ / Chuyển and progress. `apps::open("store", "<add-on id>")` opens the main app's page at that section
  (what the main app calls when its add-on is missing);
- it can live on another volume than its app (PawBoop fills Máy, the run goes to the card); it needs its app
  installed first (the store says "Cần cài PawBoop trước"; from the web it waits until the app is in);
- removing the app offers to remove its add-ons too ("Gỡ cả Chạy lấy xu"); the Dung lượng page lists an add-on
  under its app. Sync and `s3.pkgs` treat it like any package.
- shelves: `"set": "dog"` or `"map"` puts an add-on under that heading on its app's page ("Dogs", "Run maps"; PawBoop's
  ten dogs and eight run maps), sorted by name, after the plain add-ons; opened for one of them, the page scrolls to
  its shelf. `"needs": "<id>"`: another package it is useless without (a run map needs the run game): the store asks
  for that one first ("Install {0} first."), sync waits for it like an add-on waits for its app.

## 5. Reading the files (native engines)

```cpp
#include "pkg/pkg.h"
pkg::File f;
if (pkg::open("vn.pawboop.run", "core/run_dog.pbs", f)) {
  if (f.data()) { /* in the internal flash: memory-mapped, zero copy (any alignment: memcpy before u16/u32 loads) */ }
  else { f.read(off, buf, len); /* on the card: read what you need (any task, thread safe) */ }
}
```

`pkg::loadAll(f)` copies a whole file into PSRAM. A package on a card that is out: `open()` fails; its icon stays
on Home greyed and opening it says "Cần thẻ nhớ". Files in the internal flash keep their bytes until the next
restart even if the package is removed while open, so mapped pointers never dangle. Engines are started by
`pkg::run(id, arg)`: `js` -> `jsapp::open`, `pet` -> `app_pet::open`, `run` -> `run::open(id, arg)`.

## 6. Where it goes on the device

| place | what | how |
|---|---|---|
| Máy, app area | native packages | the flash range of `pb_dog`+`pb_ui`+`pb_voice` (7.5 MB), files as 4 KB extents, memory-mapped; index in NVS `pkgs` |
| Máy, LittleFS | JavaScript packages | `/littlefs/pkgs/<id>/` with `manifest.json` (they are a few KB; the app area stays for big data) |
| Thẻ nhớ | any package | `/sdcard/espos/pkgs/<id>/` with `manifest.json` (FAT32, 4-bit SD_MMC, checked every few seconds) |

Installing: each part is downloaded (several byte ranges at once), its SHA-256 checked against the catalog and
each file's CRC32 checked (the internal flash is read back), written to the chosen place, and the package is
committed (NVS index, or a folder rename) only when every part is good; a failure frees everything. An update
installs the new version next to the old one and then drops the old one. Updates come when the catalog's `code` is
above the installed one: from "Update" (device or web), and by themselves while the app is not in use unless the
user turned that off (setting `s3.pkg_auto`, `docs/ecosystem.md` §6). So raise `code` with every release. When the app area has room in total but
no gap big enough, files are slid down one at a time with a journal in NVS (a power cut resumes the move at boot).
Not enough room: the store says how many MB are missing and lists the biggest apps there with Gỡ / Chuyển.

What the web sees and asks: setting `s3.pkgs` (install / move / remove, synced both ways) and the sync body's
`"pkgs"` (volumes, installed packages, the running job), `docs/ecosystem.md` §6.

## 7. Try it in the simulator

```
cp -R sim/data sim/data-me                         # your own data folder
make -C sim -j10 BUILD=build-me
```

- The internal app area is `$SIM_DATA/apparea.bin` (made at the first run with PawBoop's packs at their places,
  like the board; `SIM_NO_PACKS=1` for an empty one). The card is the folder `$SIM_DATA/sd/` (no folder = no
  card; the script line `set sim.card 0` pulls it out, `set sim.card 1` puts it back). NVS is `$SIM_DATA/nvs/`.
- **Through the store** (tests the installer): put the `.espk` files and a `catalog.json` with your entry
  (`tools/espk.py --entry`) into `$SIM_DATA/net/` named like the URL path, `/` replaced by `_`
  (`_storage_bb_149_apps-s3_catalog.json`, `_storage_bb_149_apps-s3_com.you.timer-1.0.0-core.espk`). Then
  `app store` in a script. `SIM_FW_CODE=300` makes the simulator act as firmware 0.3.0; `SIM_PKG_KBPS` sets the
  download speed (default 1024 KB/s of simulator time).
- **Straight onto the card** (quick, for engine work): `python3 tools/espk.py --install-sim sim/data-me part.espk
  ...` unpacks it into `$SIM_DATA/sd/espos/pkgs/<id>/` (`--vol flash` puts a JavaScript app in Máy instead).
- `app <id>` opens an installed package's app; `app store <id>` opens its page in the store.
- What the next sync would report (`"pkgs"`) is written to `$SIM_DATA/pkgs-report.json` after every change;
  `set s3.pkgs [...]` plays the web asking for installs, moves (`"vol"`) and removals (`"rm": true`).
- Updates by themselves: `sync` in a script plays a sync (reconcile, and the catalog read while `s3.pkg_auto` is on);
  `SIM_AUTO_IDLE=2000` shortens the minute nobody must touch the device first. Test data: `sim/data-pkgauto` (Dice
  1.0.0 and Focus 1.0.0 installed, the catalog in its `net/` offering 1.1.0 and 1.2.0).

## 8. Publishing

The store's publishing tool (`scripts/publish_pkg.py`) uploads the `.espk` files and the catalog to public storage
`apps-s3/` with a temporary key that is revoked right after. Keep the old `"apps"` list in `catalog.json`:
firmware 0.2.3 reads it.
