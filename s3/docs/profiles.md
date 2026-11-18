# Hardware profiles and the Lite platform (Xteink X4)

Status: design v1, 2026-11-17. The owner asked for the X4 now ("ok làm luôn") and said EspOS will later run on
many chips and displays. This file is the contract between the firmware (S3 `s3/`, X4 `firmware/`), the
package tools (`tools/espk.py`), the web store and the server. It follows `docs/platform.md` (package types,
manifest v2, permissions) and `docs/ecosystem.md` (the `.espk` file, `s3.pkgs`, the `pkgs` report).

What is built today is listed in §13.

## Decisions (owner, 2026-11-17)

| Topic | Decision |
|---|---|
| Package updates | Only when the owner presses "Cập nhật" on the web (it raises the `code` in `x4.pkgs`, §9.1). The server does not raise it by itself. |
| "Dùng làm màn chờ" on the device | Also turns the Dashboard sleep screen on (`dash.enabled` 1) and switches the "Ảnh" sleep mode to Dashboard (§5). |
| Clock digits | No bigger digit font for now: the X4's biggest digits stay 90 px tall (`ink_60_digits`). |
| Unsigned packages copied to the card | Still accepted (§6, "Từ thẻ nhớ") until package signing ships in phase 1, the same as the S3. |
| Themes on the X4 | Later. A theme package installs, but nothing reads it yet (§7). |
| Widget packages on Home | Not selectable in level 1 (OK does nothing); fine as is. |

## 1. Profiles and capabilities

A **device** says what it has as a list of **capabilities** (flags from its board descriptor) and which **runtime
levels** its firmware has. A **package** says what it needs with `requires` (capabilities) and `runtime` + `api.min`.
A package fits a device when every `requires` flag is in the device's list and the device has the runtime at
`api.min` or higher. **Profiles** are only names for common capability sets, used in docs and store labels.

### 1.1 Capabilities (v1)

These strings are canonical: manifests, the sync body, the store and every firmware use them. The S3 HAL maps
its board flags to them one to one (`CAP_*` in `src/hal`, agreed with the BSP work on 2026-11-17); the X4 firmware
lists them in `src/espos/Pkgs.cpp`. A new flag is added here first.

| flag | meaning | Full (S3 boards) | Lite (X4) |
|---|---|---|---|
| `screen.color` | 16-bit colour or better | yes | |
| `screen.gray` | 4 grey levels or more for UI drawing | yes | |
| `screen.eink` | the picture stays without power; slow refresh; partial refresh possible | | yes |
| `screen.fast` | 20+ frames a second (animation, games) | yes | |
| `input.touch` | touch screen | yes | |
| `input.buttons` | at least Back, OK, Up, Down | | yes (7) |
| `mem.psram` | 2 MB+ of external RAM | yes | |
| `audio.out` | speaker | yes | |
| `audio.in` | microphone | yes | |
| `store.sd` | memory card slot | yes | yes |
| `net.wifi`, `net.ble` | radios | yes | yes |
| `power.battery` | the device measures its battery (level for a battery widget or face) | yes | yes |
| `led.rgb` | an RGB status light | yes | |

The screen size is not a flag: the device reports `screen: "800x480"` (its natural orientation) and whether it can
turn (`"rotate": true`). Engines are runtimes, not flags (§1.2).

### 1.2 Runtime levels

The device reports `rt`, an object of runtime → highest API level:

| device | `rt` |
|---|---|
| S3 (0.3.x) | `{"js": 1, "engine.pet": 1, "engine.run": 1, "engine.j2me": 1}` |
| S3 (with this design) | adds `"decl": 1` |
| X4 (espos-0.4.0) | `{"decl": 1}` |

A package's `runtime` + `api.min` must be in that object (`runtime: "decl"`, `api.min: 1` needs `rt.decl >= 1`).
The old manifest's `kind`/`engine` map as: `kind js` → runtime `js`; `kind native, engine pet` → runtime
`engine.pet`.

### 1.3 Profiles (names only)

| profile | capabilities | runtimes | devices |
|---|---|---|---|
| **Full** | `screen.color screen.gray screen.fast input.touch mem.psram audio.out audio.in store.sd net.wifi net.ble power.battery led.rgb` | `js`, engines, `decl` (to build), later `wasm` | ES3C35P, ES3C28P |
| **Lite** | `screen.eink input.buttons store.sd net.wifi net.ble power.battery` | `decl` | Xteink X4 (ESP32-C3, 380 KB RAM, no PSRAM, 800x480 SSD1677, 7 buttons) |

A new board copies the profile closest to it into its descriptor and edits the flags (an S3 with an e-ink panel:
Full minus `screen.color screen.fast input.touch`, plus `screen.eink input.buttons`). Nothing in a package names a
profile.

### 1.4 Why capabilities and not `profiles: ["lite","full"]`

- A package's needs are facts about the package ("needs touch", "needs sound"). The profile is a fact about the
  device. With profile names, every new board between two profiles (C6 + LCD without PSRAM, S3 + e-ink, colour
  e-ink) forces either a new profile that every package must add, or a wrong guess.
- Community boards (platform.md, "Many chips, many screens") cannot be listed by name in packages, which is why
  Bangle's `supports` device list and Garmin's per-device lists do not fit an open platform. Android's
  `uses-feature` (capabilities) does: a new device works with old packages on the day it ships.
- The board descriptor already carries capability flags (platform.md), so the store compares two flag lists; no
  table of profiles to keep in step.
- Declarative packages need almost nothing (`requires: []`): they run on every device with `rt.decl`, which is
  the point of them.

The cost: a store page must turn flags into words. The store shows "Chạy trên: máy giấy điện tử, máy màu" by
testing the package against each known device kind; the owner never reads flags.

## 2. What each profile runs

| type | Full | Lite (now) | Lite (later, if measured to fit) |
|---|---|---|---|
| `face` | `decl` (to build; today's savers are C++) | **`decl`** (sleep screen) | |
| `widget` | `decl` (to build), JS | **`decl`** (Home grid, sleep grids) | |
| `theme` | tokens, wallpapers | **`decl`** data: frame skin, wallpapers, bars (§7) | |
| `app` | JS, engines | no | MicroQuickJS app with a 24-32 KB heap, only while no TLS runs |
| `service` | JS | no (the system sync does the fetching) | |
| `pack` | yes | no | |
| `driver` | WASM/JS (phase 4) | no | |

**Why no code on Lite now.** The X4 has about 380 KB of RAM and no PSRAM. A TLS session needs a 70 KB free block
(GitHub TLS hung at 47 KB after a sync; a fresh boot has about 86 KB, memory note 2026-11-15). The reader, the sync
and the SD fonts already compete for it.
- **MicroQuickJS** (Bellard, MIT): about 100 KB of code, "as little as 10 kB of RAM", an ES5-like subset
  (`docs/research/platform-landscape.md` §2.3). The X4's app slot has about 675 KB free (5,878,761 of 6,553,600
  bytes used), so the code fits. A useful app heap is 24-32 KB, which the X4 can give only outside a sync. It
  would be its own API profile (ES5, no Promises) and needs measuring on the board first.
- **WAMR**: 56-59 KB of interpreter code, but a WASM module's linear memory is one 64 KB page unless it is cut to
  `__heap_base`. That is the X4's whole TLS margin, so only tiny modules with truncated memory could run.
- **Java (J2ME)**: no. The S3's VM is built for PSRAM.

So Lite starts with data-only packages: the firmware draws them, they cannot loop, allocate or call the network,
and they need no review of code.

## 3. The manifest of a declarative package

Manifest v2 (platform.md) with the fields a declarative package uses. Built into an ordinary `.espk` (format 1,
`ecosystem.md` §2) by `tools/espk.py`.

```json
{"id": "dev.espos.face.bigclock", "name": "Đồng hồ lớn", "version": "1.0.0", "code": 10000,
 "type": "face", "runtime": "decl", "api": {"min": 1, "target": 1}, "requires": [],
 "screens": ["800x480", "480x800"], "refresh": "minute", "perms": ["data:weather", "data:calendar"],
 "author": "EspOS", "desc": "Giờ thật to, thứ ngày, thời tiết, âm lịch và việc sắp tới",
 "entry": {"face": "face.json"}, "part": "core"}
```

| field | rule |
|---|---|
| `type` | `face`, `widget` or `theme` |
| `runtime` | `"decl"` |
| `api` | `{"min": 1, "target": 1}`: the declarative format level (§4) |
| `requires` | capability flags (§1.1); `[]` for almost every declarative package. `screen.color` for a face that only makes sense in colour |
| `screens` | the layout sizes in the entry file (`"800x480"`, `"480x800"`, `"320x480"`); the store shows them; any device can still scale (§4.2) |
| `refresh` | faces and widgets: `minute`, `hour`, `data` (the default) or `day` (§4.9) |
| `perms` | the data the bindings read: `data:weather`, `data:calendar`, `data:reading`, `data:news`, `data:markets`. Time, date, lunar date and battery need none |
| `entry` | `{"face": "face.json"}`, `{"widget": "widget.json"}` or `{"theme": "theme.json"}` |
| no `kind`, no `engine` | those belong to manifest v1 apps |

Files: the entry JSON, and pictures under `img/` (BMP for e-ink: 1, 2, 4, 8 or 24 bit; the X4 dithers to black
and white). Limits on Lite: package ≤ 512 KB, entry JSON ≤ 12 KB, ≤ 8 pictures of ≤ 100 KB each.

## 4. The declarative format, level 1

One JSON file, the same for faces and widgets. The firmware reads it straight from the card into one ArduinoJson
document, draws, and frees it (no model kept in RAM). The host tool (`firmware/test/face_render`) draws the same
file into a PNG with the same code.

### 4.1 The file

```json
{"decl": 1,
 "layouts": [
   {"size": [800, 480], "bg": "paper", "items": [ ... ]},
   {"size": [480, 800], "bg": "paper", "items": [ ... ]}]}
```

- `decl`: the format level. A device refuses a file whose level is above its `rt.decl`.
- `layouts`: one per screen shape. `size` is the design size in pixels; every coordinate below is in that space.
- `lists` (optional): the package's own data, `{"quotes": [{"q": "Học, học nữa, học mãi.", "a": "Lênin"}]}` or lists of plain strings,
  read by a `list` item with `"of": "pkg.quotes"` (§4.4). The whole file stays within 12 KB.

### 4.2 Picking a layout and scaling

The target is the screen (face) or the widget's box (widget, inset 6 px inside the OS frame).
1. Take the layout with the same size; else the one whose width/height ratio is closest; a tie goes to the bigger
   one.
2. Scale it uniformly by `min(targetW / w, targetH / h)` and centre it. Line widths and radii scale too, never
   below 1 px. Text sizes scale and then snap to a font the device has (§4.6).

A face for 800x480 therefore also runs on a 320x480 colour board (at 0.4) and in a 456x210 Home box.

### 4.3 Items

Every item has `t` (its type) and may have `if` (§4.5). Coordinates are integers.

| `t` | fields | draws |
|---|---|---|
| `box` | `x y w h`, `fill`, `stroke`, `lw` (1), `r` (radius, 0) | a rectangle, filled and/or outlined |
| `line` | `x y x2 y2`, `stroke` (ink), `lw` (1) | a straight line |
| `circle` | `x y` (centre), `r`, `fill`, `stroke`, `lw` | a circle |
| `text` | `x y w h`, `text`, `size` (24), `font` (`sans`), `bold` (false), `align` (`left`/`center`/`right`), `valign` (`top`/`middle`/`bottom`), `lines` (1), `color` (ink) | text that always fits (§4.6) |
| `icon` | `x y w h`, `name`, `color` | a system icon centred in the box, `min(w,h)` big |
| `image` | `x y w h`, `src` (a file of the package) | a picture shrunk to fit the box (never enlarged), centred |
| `bar` | `x y w h`, `value`, `max` (100), `fill` (ink), `stroke` (ink), `r` | a progress bar |
| `list` | `of`, `max`, `pick`, `x y`, `dx dy` (row step), `items` | its `items` once per entry of a data list (§4.4), shifted by `dx,dy` per row; `"pick": "day"` draws one entry, chosen by the day of the year |
| `group` | `x y`, `items` | its `items` shifted by `x,y` (to share one `if`) |

Icons (level 1): the weather set `sun cloudsun cloud rain storm snow fog moon`, and `battery`. Nesting
(`list`/`group` inside `list`/`group`) goes 3 deep at most; a layout draws at most 160 things after lists are
expanded and visits at most 4,000 items and list rows. So that any file draws in bounded time, positions stay
within 2,000 px of the target box, `w`/`h` are at most 2,000, `lw` at most 32, a box's `r` at most half its
shorter side, a circle's `r` at most 1,000, icons at most 256 px; lines are clipped to the target, and all the
drawing may touch at most 4,000,000 pixels (what is past a limit is skipped). Unknown item types and fields
are skipped, so a level-1 device still draws a level-1 file that has a few
newer optional fields.

### 4.4 Data bindings

Text, icon names, image paths, bar values and conditions may contain `{name}` or `{name|filter}`. `{{` is a literal
`{`. A name with no value gives `""`.

| name | example | perm |
|---|---|---|
| `time.hm`, `time.h`, `time.m`, `time.ampm` | `07:45`, `07`, `45`, `SA`/`CH` (only with the 12-hour setting `sys.clock_fmt`) | |
| `time.live` | `1` when the screen is redrawn every minute; else `""` (a face then shows the date, not a stale time) | |
| `date.d`, `date.dd`, `date.m`, `date.mm`, `date.y` | `7`, `07`, `11`, `11`, `2026` | |
| `date.wd`, `date.wds`, `date.month`, `date.dm`, `date.full`, `date.yday` | `Thứ Ba`, `T3`, `Tháng 11`, `17/11`, `Thứ Ba, 17/11/2026`, `320` | |
| `lunar.d`, `lunar.m`, `lunar.dm`, `lunar.year` | `6`, `10`, `6/10`, `Bính Ngọ` | |
| `battery.pct`, `sync.hm` | `82`, `07:40` (data downloaded at) | |
| `weather.ok`, `weather.temp`, `weather.min`, `weather.max`, `weather.rain`, `weather.desc`, `weather.icon`, `weather.city` | `1`, `28`, `24`, `31`, `40`, `Nhiều mây`, `cloudsun`, `Hà Nội` | `data:weather` |
| list `weather.hours`: `item.h item.t item.icon` | 4 entries | `data:weather` |
| list `weather.days`: `item.d item.min item.max item.icon` | 3 entries | `data:weather` |
| `cal.count`, `cal.next.time`, `cal.next.title` | `3`, `09:00`, `Họp nhóm` | `data:calendar` |
| list `cal.events`: `item.time item.title` | up to 10 | `data:calendar` |
| `read.ok`, `read.title`, `read.author`, `read.pct`, `read.today_min`, `read.goal_min`, `read.streak` | `1`, `Nhà giả kim`, `Paulo Coelho`, `42`, `18`, `30`, `5` | `data:reading` |
| `news.count`, `news.day`; list `news.top`: `item.title` | `12`, `17/11` | `data:news` |
| in a list: `item.<field>` (`item` alone for a list of strings), `i` (1, 2, 3), `last` (`1` on the list's last entry) | | |
| package lists: `of: "pkg.<name>"` reads `lists.<name>` of the same file | | |

A binding whose permission the manifest does not list gives `""` (the package still draws). Filters: `upper`,
`lower`. Markets (`market.*`) come with level 2.

### 4.5 Conditions

`"if"` is one comparison: `"weather.ok"` (true when not `""`, `"0"` or `"false"`), `"!cal.count"`,
`"weather.temp >= 30"`, `"cal.count > 3"`, `"i == 1"`. Operators: `== != > >= < <=`. Both sides that read as
numbers compare as numbers; otherwise `==`/`!=` compare text. The left side is a binding name; the right side a
number or a word.

### 4.6 Text and fonts

- `size` is the font size in design pixels, like CSS `font-size` (scaled with the layout). `font`: `sans`,
  `serif`, `digits` (numbers and `:%+,-./°` only) or `ui` (the system's small label font). `bold`: the weight
  asked for; a size that exists in one weight only uses it.
- The device takes the biggest font of that family whose size is at most `size`. When the text is too wide: it
  wraps onto up to `lines` lines if they fit the box height; otherwise the next smaller font is tried, then the
  smaller `ui` fonts. The smallest wraps as much as it needs. Text is **never cut and never ends with "…"**
  (owner rule). A `digits` text that holds a letter uses `sans`.
- `lines` is a hint, not a limit: text that cannot fit in that many lines at any size wraps onto more lines
  with the smallest font rather than being cut.
- Line heights are the font's own (larger than the size: about 1.35 ×). `valign` centres the lines in the box;
  `digits` text is placed by the height of its digits, so big numbers centre visually.
- X4 fonts (size → line height): sans 25 → 34, 29 → 40, 33 → 45, 37 → 51 (NotoSans 12-18, regular and bold),
  50 → 68 (bold); serif 75 → 102 (bold); digits 62, 92, 125 (bold; digit height 45, 66, 90); ui 21 → 24,
  25 → 29 (Ubuntu, regular and bold). S3: its LVGL fonts by the same rule.
- `align` and `valign` place the text block inside its box.

### 4.7 Colours

Values: tokens `ink` (text), `paper` (background), `muted` (secondary), `accent`, `card`, `none`, or `#rrggbb`.

| | colour screen | grey screen | black-and-white e-ink (X4) |
|---|---|---|---|
| tokens | from the current theme (light/dark) | grey levels of the theme | `ink`, `accent` → black; `paper`, `card` → white; `muted` → black text, 50 % dither fill |
| `#rrggbb` | as is | nearest grey | luminance < 35 % → black; > 75 % → white; between: dithered fill, black text and thin lines |

Text is always solid (dithered text cannot be read). Packages that use tokens look right on every screen; hex
colours are for colour faces and degrade by luminance.

### 4.8 Pictures

`image` reads a BMP from the package (1/2/4/8/24 bit). E-ink: drawn with the reader's dithering, shrunk to fit but
never enlarged (make pictures at the size they show). Colour boards (later): PNG and baseline JPEG too. A missing or
bad picture draws nothing.

### 4.9 Refresh hints (e-ink)

The manifest's `refresh` is the most often the package wants to be redrawn:

| refresh | X4 sleep face | X4 Home widget |
|---|---|---|
| `minute` | woken every minute, partial refresh (when the user's "Đồng hồ" switch `dash.clock` is on and the battery is above the saving level) | Home has no minute timer: drawn with Home |
| `hour` | woken at the top of every hour, plus data downloads | drawn with Home |
| `data` (default) | redrawn only when data is downloaded (`dash.interval_min`, 15-60 min) | drawn with Home |
| `day` | woken just after midnight, plus data downloads | drawn with Home |

The device may draw less often (battery saving, quiet hours `dash.quiet_*`), never more often. A full refresh
against ghosting stays the system's choice.

### 4.10 Example

```json
{"decl": 1, "layouts": [{"size": [800, 480], "items": [
  {"t": "text", "if": "time.live", "x": 0, "y": 40, "w": 800, "h": 140, "text": "{time.hm}", "size": 125,
   "font": "digits", "bold": true, "align": "center"},
  {"t": "text", "if": "!time.live", "x": 0, "y": 40, "w": 800, "h": 140, "text": "{date.dm}", "size": 125,
   "font": "digits", "bold": true, "align": "center"},
  {"t": "text", "x": 0, "y": 200, "w": 800, "h": 56, "text": "{date.wd|upper}", "size": 50, "bold": true,
   "align": "center"},
  {"t": "line", "x": 200, "y": 286, "x2": 600, "y2": 286, "lw": 3},
  {"t": "group", "if": "weather.ok", "items": [
    {"t": "icon", "x": 250, "y": 310, "w": 80, "h": 80, "name": "{weather.icon}"},
    {"t": "text", "x": 350, "y": 318, "w": 220, "h": 70, "text": "{weather.temp}°", "size": 62, "font": "digits",
     "bold": true}]}]}]}
```

## 5. Faces and widgets on the X4

- **Sleep screen.** `dash.layout` takes `pkg:<id>` or `pkg:<id>@480x800` (a layout of that size; the screen turns to
  match). On the device: Ứng dụng › Dashboard › Kiểu cycles the built-in styles, then each installed face (a face
  with both shapes appears twice, "ngang" and "dọc"). The face is drawn into the whole 800x480 (or 480x800) screen
  with no status bar or footer: the face owns the screen.
- **Home and sleep grids.** A new widget kind `pkg` in `home.w`, `sleep.wl`, `sleep.wp`:
  `["pkg", x, y, w, h, "", "<package id>"]`. The OS draws the frame (skin, invert) and the widget draws inside it.
  A missing package draws "Chưa cài gói". It takes no focus on Home (OK does nothing) in level 1.
- **Installed list.** Ứng dụng › Gói đã cài: one row per package (name, type, version, "Đang dùng" on the face
  in use). OK opens the package: Xem thử, Dùng làm màn chờ (faces), Gỡ gói (asks twice). Dùng làm màn chờ also
  turns the Dashboard sleep screen on and moves the "Ảnh" sleep mode to Dashboard (decided). Removing a package on
  the device takes it out of `x4.pkgs` (§9.1), so the web does not put it back; removing the face in use sets the
  sleep style back to `desk`.

## 6. Storage and install on the X4

- Folder: `/EspOS/pkgs/<id>/` on the card: the package's files plus `manifest.json` = the core manifest +
  `"files": [{"n","s","c"}]` (name, size, CRC32) + `"parts": ["core"]`. The same layout as the S3's card
  (`/espos/pkgs/<id>/`, FAT is case-insensitive), so a card moves between devices.
- Install (one package at a time, after the books in a sync):
  1. download the `.espk` to `/EspOS/pkgs/.dl/<id>.espk` (resumable, `HttpDownloader`; no device token is sent to
     another host);
  2. check its size and SHA-256 against the sync answer;
  3. read the header, manifest (≤ 8 KB) and file table; refuse a manifest whose `id` differs, whose `runtime` is
     not `decl` at a level the device has, whose `requires` the device lacks, a file name with `..` or a leading `/`,
     and more than 64 files;
  4. write the files to `/EspOS/pkgs/.new-<id>/`, checking each one's CRC32 as it is written;
  5. write `manifest.json`, remove the old `/EspOS/pkgs/<id>/`, rename `.new-<id>` to `<id>`, delete the download.
  A failure removes the partial folder and is reported in `pkgs.job`; the old version stays.
- Memory: 1 KB copy buffer and the manifest document during install; the TLS download is the same as a book's.
  The heap (free and largest block) is logged before the download, after it and after the unpack.
- Sideload: an `.espk` copied to `/EspOS/pkgs/` (USB or the file transfer page) is unpacked the next time
  Gói đã cài opens, with the same checks except SHA-256 (no catalog entry). Shown as "Từ thẻ nhớ". Kept until
  package signing ships (phase 1, Ed25519); then a copied package must carry a valid signature, as on the S3.

## 7. Themes on e-ink

A theme package is data (`theme.json`). On a colour board it carries colour tokens (platform.md "Theme"). On e-ink,
colours collapse to black and white, so the X4 uses the parts that make sense there:

```json
{"decl": 1,
 "tokens": {"light": {"paper": "#ffffff", "ink": "#1b1b1b", "muted": "#6b7280", "accent": "#e8590c"},
            "dark": {"paper": "#111111", "ink": "#f1f1f1", "muted": "#9ca3af", "accent": "#ff922b"}},
 "eink": {"skin": "line", "bars": "invert", "wallpapers": ["img/w1.bmp", "img/w2.bmp"]}}
```

- `skin`: the widget frame style the X4 already has (`card`, `line`, `bare`, `bold`, `shadow`); applied to the
  layouts that do not set their own.
- `bars`: `plain` or `invert` (black status and footer bars).
- `wallpapers`: sleep pictures added to the "Ảnh" sleep mode.
- Fonts stay the user's choice (reading font from the web), not the theme's.
- Dark tokens are ignored on e-ink (no night mode on paper).

Themes on the X4 come later (decided 2026-11-17; §13): the format is fixed here so packages can carry both parts.

## 8. Packages that need more than Lite

The store shows them greyed for an X4 with the reason, built from the first failing check:
"Cần màn hình cảm ứng", "Cần màn hình màu", "Cần loa", "Cần bộ nhớ lớn" (`mem.psram`), "Cần cập nhật phần mềm"
(runtime level). A package with `requires: []` and `runtime: decl` shows on both.

## 9. Sync contract (X4)

### 9.1 What the user wants: setting `x4.pkgs`

Two-way like every setting, last writer wins. A JSON list, at most 24 entries and 2,000 bytes:

```json
[{"id": "dev.espos.face.bigclock", "code": 10000}, {"id": "dev.espos.face.today", "code": 10000},
 {"id": "dev.espos.face.old", "rm": true}]
```

- `code`: the version wanted. Installed code lower than it (or not installed) → install the version the answer
  carries (§9.2). "Cập nhật" on the web raises `code`; nothing else does (decided: no automatic updates).
- `"rm": true`: the device removes the package, then drops the entry (and sends the list back).
- Removing on the device drops the entry. A package on the card that is not listed (sideloaded) is left alone.
- Kept short on purpose: the X4 keeps every setting in RAM; URLs do not go here.

### 9.2 What to download: the answer's `pkgs`

For a device of kind `x4`, the sync answer carries the catalog entries of the packages listed in `x4.pkgs`
without `rm`, already resolved (the X4 never downloads or parses a catalog):

```json
"pkgs": [{"id": "dev.espos.face.bigclock", "name": "Đồng hồ lớn", "version": "1.0.0", "code": 10000,
          "type": "face", "url": "https://<api host>/storage/bb/149/apps/dev.espos.face.bigclock-1.0.0.espk",
          "size": 4107, "sha256": "9b0b7b598b849976d4989954be03979d1a663b65dfebf56562f814545472263f"}]
```

Only packages that fit the device (§1) are listed; one that does not fit is left out (the web already shows why).
A missing `pkgs` key means "unknown this time": the device changes nothing.

### 9.3 What the device has: the sync body

```json
"caps": ["screen.eink", "input.buttons", "store.sd", "net.wifi", "net.ble", "power.battery"], "rt": {"decl": 1},
"screen": "800x480",
"pkgs": {"vols": {"sd": {"present": true}},
         "installed": [{"id": "dev.espos.face.bigclock", "version": "1.0.0", "code": 10000, "vol": "sd",
                        "kb": 6, "ok": true, "type": "face", "name": "Đồng hồ lớn"}],
         "job": {"id": "dev.espos.face.today", "state": "error", "pct": 0, "err": "checksum"}}
```

The same shape as the S3's report (`ecosystem.md` §6) with `vols.flash` left out (packages live only on the card)
and `type` added. `job` is the last install or removal of this boot. `"src": "card"` marks a sideloaded package.

## 10. Store and catalog

- **One catalog for v2 packages**: public storage `apps/catalog.json` = `{"v": 2, "packages": [...]}`. Each entry is
  the manifest's public fields (`id name version code type runtime api requires screens refresh perms author desc
  icon`) + `parts` (as today) + `shots`. `apps-s3/catalog.json` stays as it is for S3 firmware that reads only
  `"packages"`/`"apps"` v1 (it would list a face as an app it cannot run).
- **The web** reads both catalogs and filters by the selected device: its kind's capabilities and runtime levels
  (from the last sync body, or a built-in table per kind until a device has synced: `x4` = Lite, `s3` = Full).
  For an X4 the store shows "Mặt màn chờ" and "Tiện ích" tabs, the device's shape (800x480), "Cài lên máy"
  (adds to `x4.pkgs`), "Đang chờ máy đồng bộ" until the report lists it, "Cập nhật" when the catalog code is
  higher than the report's, and "Gỡ" (`rm`).
- **The device** (a future S3 with `rt.decl`) reads `apps/catalog.json` and filters the same way.
- **publish_pkg.py**: v2 packages (`runtime` present) go into `apps/catalog.json`; v1 packages stay in
  `apps-s3/catalog.json`.

## 11. Other chips and screens

- The format is in screen pixels of a design size and scales, so a package made for one shape works on others;
  authors add a layout per shape when they want it exact.
- Each board's descriptor lists its `caps`, `screen`, and colour depth; each firmware maps the colour rules of §4.7
  for its depth and the font rule of §4.6 to its own fonts. No package changes when a board is added.
- One renderer per display class, not per board: e-ink 1-bit (X4, from this work), LVGL colour (S3), and later
  grey e-ink and colour e-ink (Spectra 6 would use the colour path with dithering).
- Inputs: level 1 has no input in packages. Level 2 can add `tap`/`ok` targets that open an app or switch a value;
  a device without touch maps them to its focus ring (`input.buttons`).
- Runtimes grow by level: `decl` 2 (markets, settings schema, more icons), `js` for Full, `mquickjs` for Lite only
  after the measurements in §2.

## 12. Server and web changes

Server (`web/backend/workflows`):
1. `device_sync` and `save_device_settings`: a new setting `x4.pkgs` (default `""`) for kind `x4` only, checked
   like `checkPkgsSetting` but ≤ 24 entries and ≤ 2,000 bytes, keys `id`, `code`, `rm`; accept the device's value
   uncut (today device values are cut to 60 characters).
2. `dash.layout` allowed values: also `pkg:<id>` and `pkg:<id>@<w>x<h>` (`pkgID` + `[0-9]{3}x[0-9]{3}`).
3. Layout check (`checkLayout`): widget kind `pkg` whose option is a package id; allowed in `home.w`, `sleep.wl`,
   `sleep.wp`; not for kind `s3` until the S3 draws decl widgets.
4. Sync body: accept `pkgs` for kind `x4` (`vols.flash` optional, `type` and `src` kept), store it in
   `devices.pkgs`/`pkgs_at` as for the S3; store `caps`, `rt`, `screen` (new JSON columns `caps`; never drop or
   retype columns).
5. Sync answer for kind `x4`: `pkgs` = the `apps/catalog.json` entries for `x4.pkgs` ids without `rm` that fit
   (`requires` ⊆ caps, `rt[runtime] >= api.min`), each as `{id, name, version, code, type, url, size, sha256}`
   from its `core` part. Read the catalog once per sync (`storage_get` of the public file, or a `store_pkgs` table
   if that is easier). Leave `pkgs` out when the catalog cannot be read.
6. `publish_pkg.py`: v2 entries to `apps/catalog.json` (§10).

Web (`web/app`):
1. Store tab for X4 devices (§10) with the 800x480 previews (the shots in the catalog; later the web port of the
   renderer, `web/app/src/ink`).
2. Giao diện máy › Màn chờ: installed faces as choices next to the built-in styles (writes `dash.layout`).
3. Widget editor: a "Gói" group in the palette with the installed widget packages (kind `pkg`), and the preview
   drawn by the decl renderer port.

## 13. Status (2026-11-17)

Built in this round (X4 firmware `firmware/`, build only, not flashed):
- `src/espos/Decl.{h,cpp}`: the level-1 renderer (all of §4 except markets), drawing through a small canvas
  interface; no Arduino code, so it also builds on a computer.
- `src/espos/DeclGfx.{h,cpp}`: the e-ink canvas (GfxRenderer, the reader's fonts and icons), the data bindings
  from the widgets' data, `drawFace` and `drawWidget`.
- `src/espos/Pkgs.{h,cpp}`: the card store, `.espk` install (SHA-256 + CRC32, temp folder then rename),
  sideload, removal, `x4.pkgs` handling, the sync body (`caps`, `rt`, `screen`, `pkgs`).
- Sleep screen `pkg:<id>[@WxH]` with the `refresh` hint in the wake plan (`DashboardView.cpp`); Ứng dụng ›
  Dashboard › Kiểu cycles installed faces; widget kind `pkg` (`Widgets.cpp`); Ứng dụng › Gói đã cài
  (`src/activities/apps/PkgListActivity.*`).
- `test/face_render/`: `FaceRenderTest` (gtest, part of `pio run -t unit-tests`) and `face_render`, a tool that
  draws a package folder into a PNG with the X4's own font files:
  `face_render <pkg folder> --size 800x480 --data fixtures/sample.json --out face.png` (or `--widget 456x210`).
- Samples: `build-pkgs/x4-face-bigclock` (800x480 + 480x800), `build-pkgs/x4-face-today` (800x480),
  `build-pkgs/x4-widget-quote` (a widget with a package list), each with its built `.espk`.
- `tools/espk.py` checks the manifest and layout file of `face`/`widget`/`theme` packages and puts the v2 fields
  in `--entry`.

Not built: themes on the X4 (§7; a theme package installs but nothing reads it yet), the server and web parts
(§12), the S3 decl renderer. The installer has not run on a device yet: it needs the server's answer (§9.2), or a
`.espk` copied to the card (sideload) to try it without the server.
