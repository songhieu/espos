# InkDesk device ⇄ server contract (v1)

API base: the InkDesk server's address (the device takes it from USB provisioning, field `api`, or
from the `INKDESK_DEFAULT_API` build flag; see firmware/README.md). Examples use
`https://inkdesk.example.com`.

All bodies are UTF-8 JSON. Strings shown on the e-ink screen are Vietnamese, already formatted by
the server (dates "15/11", times "09:00"), so the device does no locale work.

## Device auth

- Each device owns a **token**: 32 random bytes as 64 lowercase hex chars. It is created on the
  client side (browser `crypto.getRandomValues` for USB install; ESP32 `esp_random()` for pairing)
  and stored on the device only. The server stores `sha256(token)` hex.
- Every device call sends `Authorization: Device <token>`.
- `401 {"error":"unauthorized"}`: unknown, revoked or malformed token → the device clears its link
  state and shows "Chưa liên kết".
- `401 {"error":"not_linked"}`: a pairing is pending (see pairing) — keep polling.

## POST /v1/device/sync  (device auth)

Request:
```json
{
  "fw": "inkdesk-0.1.0",
  "battery": 78,
  "settings_rev": 0,
  "settings": [{"k": "dash.clock", "v": "1", "age_s": 120}],
  "progress": [{"book_id": 5, "percent": 42}],
  "have_books": [5, 7],
  "have_sizes": [{"id": 5, "size": 2100000}, {"id": 7, "size": 1400000}]
}
```
- `settings`: settings the user changed ON THE DEVICE since the last successful sync; `age_s` =
  seconds since that change (0 if unknown). Values are strings.
- `have_books`: ids of InkDesk books already on the SD card.
- `have_sizes` (fw 0.3.2+): byte size of each of those files. When a size equals the uploaded
  size, the server deletes the stored original and stamps `books.delivered_at` (Send to Kindle:
  the web keeps only the history). Without it nothing is deleted.
- All arrays optional; max 50 items each.

Response 200:
```json
{
  "now_ms": 1794632000000,
  "tz_min": 420,
  "device": {"id": 3, "name": "X4 phòng ngủ", "user": "you@example.com"},
  "settings_rev": 1794631999000,
  "settings": {"sys.lang": "vi", "dash.interval_min": "30"},
  "books": [{"id": 5, "title": "Sapiens", "file": "Sapiens.epub", "size": 2100000, "url": "https://…signed…"}],
  "dashboard": { "…": "same object as GET /v1/device/dashboard" }
}
```
- `settings` is the FULL merged set (server wins for keys the device did not send; for keys it did
  send, the most recent change by time wins). Device applies it and stores `settings_rev`.
- `books`: every ready book, delivered ones included (so the device keeps them). `url` is set only
  for ids NOT in `have_books` whose original is still stored, else `null`. It is
  `<api>/v1/device/book/<id>`: GET it WITH the device `Authorization`
  header; it answers `302` with a `Location` to a short-lived signed storage URL (no auth needed
  there). Follow the redirect (do not send the Authorization header to the storage host).
  `404 {"error":"not_found"}` if the book is gone. Books missing from the list were deleted on the web: the device
  deletes `/InkDesk/<file>` for ids it has that are no longer listed.
- `now_ms` / `tz_min`: wall clock for the device (it has no RTC). Stamped as the response is built,
  after the dashboard; the device adopts it when its own clock is 3 s or more off.

## GET /v1/device/dashboard  (device auth)

Light call used on every timer wake.
```json
{
  "now_ms": 1794632000000, "tz_min": 420,
  "date": "14.11.2026", "weekday": "THỨ BẢY",
  "lunar": "6/10", "lunar_year": "Bính Ngọ",
  "weather": {"city": "Hà Nội", "temp": 24, "desc": "Mây rải rác", "min": 19, "max": 26, "rain": 20,
              "icon": "cloudsun",
              "hours": [{"h": "13h", "t": 25, "icon": "cloudsun"}]},
  "events": [{"time": "09:00", "title": "Họp nhóm tuần"}],
  "notes": [{"title": "CI · build thành công", "time": "10:12"}],
  "layout": "portrait",
  "next_s": 1800
}
```
- Any block may be `null`/absent (user turned it off or no data) → the device skips it.
- `icon` ∈ `sun|cloudsun|cloud|rain|storm|fog|snow|moon`.
- Max: events 4, notes 2, hours 4. Titles ≤ 60 chars (server truncates).
- `next_s`: seconds until the next data refresh the server suggests (= user setting, quiet hours
  applied). The device may wake earlier for the minute clock without calling the server.

## Pairing (fallback when there is no USB install)

1. Device makes a token and a random 6-digit code, then
   `POST /v1/pair/start` (no auth) `{"code":"482913","token_hash":"<sha256 hex>","name":"Xteink X4"}`
   → `200 {"ok":true,"expires_s":600}` · `409 {"error":"code_taken"}` (pick another code).
2. Device shows the code + QR of `<web>#/pair?code=482913` (`<web>` is the web app's address).
3. Device polls `GET /v1/device/whoami` (device auth) every 5 s:
   `401 not_linked` while pending → `200 {"device":{"id":3,"name":"…","user":"…"}}` once the user
   confirmed on the web. Then run a sync.

## USB provisioning (browser → device over the USB CDC serial port, 115200 8N1)

Line protocol, one message per line (`\n`), max 8 KB per line. The device must listen whenever it
is awake (main loop) and ignore anything else on the line. Its own log lines also go to the same
port; the browser ignores lines that do not start with `INKDESK:`.

```
browser → INKDESK:HELLO <epoch ms>     (the browser's clock; the X4 has no RTC)
device  → INKDESK:READY {"fw":"inkdesk-0.1.0","linked":false}
browser → INKDESK:PROVISION {"api":"https://inkdesk.example.com","token":"<64 hex>",
           "device":{"id":3,"name":"X4 phòng ngủ","user":"you@example.com"},
           "wifi":[{"ssid":"NhaMinh","pass":"…"}],
           "settings":{"sys.lang":"vi","read.font":"bookerly"},"settings_rev":1794631999000,
           "now_ms":1794631999000}
device  → INKDESK:OK            (or INKDESK:ERR <reason>)
```
After `OK` the device saves everything, then shows the welcome screen (or reboots into it).
The WiFi password typed in the installer never goes to the server. (WiFi added later on the web
does, until the device acknowledges it: see "WiFi from the web" below.)

## Setting keys (all values are strings)

| key | values | default | on device |
|---|---|---|---|
| `sys.lang` | `vi`, `en` | `vi` | UI language |
| `sys.sleep_min` | `5`,`10`,`15`,`30` | `10` | auto-sleep timeout |
| `read.font` | `bookerly`, `notosans`, `notoserif` | `bookerly` | reader font family (map to what exists) |
| `read.size` | `small`, `medium`, `large`, `xlarge` | `medium` | reader font size |
| `read.spacing` | `1.2`, `1.4`, `1.6` | `1.4` | line spacing |
| `dash.enabled` | `0`,`1` | `1` | sleep screen = dashboard |
| `dash.layout` | `desk`, `calendar`, `portrait` (`landscape` = old name of `desk`) | `desk` | dashboard layout |
| `dash.interval_min` | `5`,`15`,`30`,`60` | `30` | data refresh |
| `dash.clock` | `0`,`1` | `1` | minute clock without WiFi |
| `dash.charging_min` | `0`,`1` | `1` | `1` = refresh every minute on USB power |
| `dash.save_pct` | `0`,`10`,`20`,`30` | `20` | On battery below this %, the device stops waking itself (no data refresh, no minute clock, no picture change, no auto-sync on Home) until charged; `0` = never (fw 0.3.3+) |
| `dash.quiet_from` / `dash.quiet_to` | `HH:MM` | `23:00` / `06:00` | no wakes in this window |
| `sleep.mode` | `dashboard`, `images`, `cover`, `auto` | `dashboard` | what the sleep screen shows (`auto`: cover while a book is open, else dashboard) |
| `sleep.rotate` | `sleep`, `30`, `60`, `1440` | `sleep` | pictures: change at each sleep, or every N minutes while asleep |
| `sleep.order` | `seq`, `random` | `seq` | pictures: next one in upload order or at random |
| `sleep.clock` | `0`,`1` | `1` | pictures: time and battery strip along the bottom |

Web-only keys (never sent to the device): `dash.city`, `dash.lat`, `dash.lon`, `dash.ics_url`,
`dash.blocks`.

## More sync fields

**Sync request** also carries:
- `battery_mv` (smoothed battery voltage) and `usb` (bool). The server keeps them in `battery_log`
  to measure real drain (the percentage alone settles quickly off the charger).
- `have_walls`: ids of the sleep-screen pictures on the SD card.
- `wifi`: `{"saved":[{"ssid","ok","code","age_s"}], "acks":[{"id","ok","why"}]}`. Saved networks by
  name only, with the device's last join result (`code` is the WiFi status: 3 connected, 1 not
  found, 4 failed); `acks` confirm the changes received in the previous sync.

**Sync response** also carries:
- `wallpapers`: `[{"id","orient":"p|l","url"}]` — `url` only for pictures the device lacks; it
  answers 302 to signed storage (`GET /v1/device/wallpaper/:id`, device auth). Files are 1-bit BMPs
  at panel size, dithered in the browser; stored as `/.crosspoint/inkdesk/wall/<id>.bmp`. Pictures
  no longer listed are deleted.
- `wifi_ops`: `[{"id","op":"add|remove","ssid","pass"}]` queued on the web. The device applies them
  and acks on the next sync; the server then clears the password.
- Books now include the morning paper (`kind = news` on the server): an EPUB per day,
  `Bao-sang-YYYY-MM-DD.epub`, made by the `news` worker and kept N issues deep.

**Web routes** (end user): `GET/POST /v1/wallpapers`, `/v1/wallpapers/upload|confirm|delete`,
`GET/POST /v1/devices/:id/wifi` (`op`: `add`, `remove`, `cancel`), `GET/POST /v1/news`,
`POST /v1/news/now`. Schedule `news_tick` (hourly at :07 UTC) starts the issues whose hour has come.

## Firmware notes

Where these differ from the sections above, this is what the device does.

- **USB provisioning**: the device's serial RX buffer is 2 KB. Send `PROVISION` only after `READY`, as one line under 2 KB. The device answers `OK` and opens the welcome screen without rebooting. It must be awake (any screen); after a flash it boots normally into Home. Its log lines share the port.
- **Settings from the device**: changes are found by comparing against the last applied set, so `age_s` is always `0` (if both sides changed a key between syncs, the device value wins). Keys the device applies: `sys.lang` (`vi`/`en`), `sys.sleep_min`, `read.size` (12/14/16/18 pt), `read.spacing`. `read.font` is ignored (only Noto Sans is built in). Keys the device can change itself: those four plus `dash.enabled`, `dash.interval_min`, `dash.charging_min`, `dash.save_pct`, `dash.layout`.
- **Quiet hours**: read from `dash.quiet_from` / `dash.quiet_to` in the merged `settings`, so send them there (defaults 23:00 / 06:00).
- **progress**: sent only for InkDesk books in the device's recent-books list (at most 10).
- **Dashboard**: `next_s` is ignored. The device wakes every `dash.interval_min` (every 60 s on USB power when `dash.charging_min` is `1`), skipping quiet hours, and fetches `GET /v1/device/dashboard` on each wake. `layout` in the dashboard object wins over `dash.layout` when present. No live clock is drawn.
- **Battery**: X4 normally cuts battery power in sleep. While the dashboard is enabled and the device is linked, sleep keeps the battery connected so the timer can fire, which drains it faster.
- **Pairing**: `name` is `"Xteink X4"`; up to 3 codes are tried on `409`.

## Firmware updates over WiFi

- Table `firmware` (one row per release): `version` ("inkdesk-X.Y.Z"), `code` (X*10000+Y*100+Z),
  `url` (public storage, content-hashed), `sha256`, `size`, `notes` (one item per line), `released` ("DD/MM").
- Release: build, collect the bundle with `firmware/scripts/inkdesk_bundle.sh`, then
  `inkdesk firmware add <bundle> -notes "…"` on the server. It stores the files, writes
  `/firmware/latest.json` for the web installer and inserts the `firmware` row.
- `devices` gains `fw_req` (version code the web asked for, 0 = none), `fw_auto` (install while
  charging) and `ota_state` (`{state: requested|ok|failed, version, why, at}`).
- `POST /v1/devices/:id/firmware` (end_user): `{"action":"update"|"cancel"}` or `{"auto":true|false}`.
  `GET /v1/devices` also returns `firmware` (the newest release) and each device's `fw_code`,
  `fw_req`, `fw_auto`, `ota_state`.
- Sync request: `fw_code`, and `ota: [{version, ok, why?, age_s?}]` after an install attempt
  (`why`: download, checksum, image check, wrong chip, rollback). Sync response: `fw_update`
  (`{version, url, sha256, size, notes, date, install}`) when a newer release exists, else null.
  `install` is true when the web asked for it, or `fw_auto` and the request says `usb: true`.
- Device: installs from Home after a clean-heap restart (battery ≥ 30% or USB), streams the image
  into the spare OTA slot, checks SHA-256 and the image, selects it, restarts. The new image stays
  on probation (bootloader rollback) until Home renders or it has run 20 s; a restart before that
  goes back to the old firmware, which reports `rollback`. At most 3 tries per version.
  About 4.5 minutes for 5.8 MB (slot erase ~18 s, download ~23 KB/s over TLS).

## Reading statistics

- Device: active reading only. A page counts from when it is shown until the next forward turn,
  2 s to 10 min per page; sessions under 10 s are dropped. Sessions go to
  `/.crosspoint/inkdesk_reading.log`, folded into `/.crosspoint/inkdesk_reading.json`
  (60 days; per-book detail for 14 days and while unsent). Sessions read without a clock land on
  the day the clock returns.
- Sync request: `reading: [{d: "YYYY-MM-DD", k: "id:<book>"|path, id?, t, s, p}]`, absolute
  per-day per-book totals, at most 40 entries; the server keeps the larger value per
  (device, day, book_key) in table `reading_days`.
- Setting `read.goal_min` (10, 15, 20, 30, 45, 60; default 30), changeable on the web and on the
  device (Thống kê đọc, OK cycles the goal).
- `GET /v1/stats` (end_user): `{goal, today, days: [{d, s, p}] (400 days), books (this month:
  key, title, seconds, pages, last, percent?), streak, best, has_device}`. A streak counts days
  reaching the goal; today counts once reached.

## Widgets

- Settings `home.w`, `sleep.wl`, `sleep.wp` hold a layout as compact JSON:
  `{"v":1,"f":1,"k":"card","i":[[kind,x,y,w,h,style,option,flags],…]}` — grid 4×7 (Home, sleep portrait)
  or 6×4 (sleep landscape); `f:0` hides the sleep screen's battery band; `k` is the frame style of the
  screen: `card` (default, rounded), `line` (rules only), `bare`, `bold` (square, thick), `shadow` (cards
  with a 5 px shadow; widgets are drawn 5 px smaller); `flags` `"i"` draws the widget white on black
  (pictures keep their colours). Empty = the built-in layout. `dash.layout` `wl` / `wp` makes the sleep
  screen use `sleep.wl` / `sleep.wp`.
- Pixel boxes: Home area left 12, top 44, right 468, bottom 752, gap 8; sleep area 12 px from the edges
  (bottom 50 with the battery band), gap 10; x0 = left + trunc(x·(cw+gap)), x1 = left + trunc((x+w)·(cw+gap) − gap)
  (same for y). The web (`placeItems`) and the device (`place`) must agree: photo renders are made at these sizes.
- Kinds and styles: `clock` (big, flip, analog, analog_min, words, lunar), `date` (sheet, compact),
  `month` (lunar, solar), `events`, `countdown` (tet, date + option `YYYY-MM-DD|label`), `weather`
  (now, strip, hourly, days), `reading` (cover, text; up to 4×4), `stats` (ring, week), `news`, `notify`,
  `battery`, `books` (covers, list: books read lately), `photo`, `app` (library, news, stats, sync,
  transfer, apps, settings, update; Home only), `gold`, `stocks`, `fx`, `crypto`. Market options are
  comma lists of codes: gold PNJ codes (SJC, N24K, KB, TL, PNJ, 24K, 999, 9920, 99, 22K, 75, 68, 65, 61,
  58.5, 41, 37.5, 33; default SJC,N24K), stock symbols or indices (VNINDEX, VN30, HNX, UPCOM; default the
  three indices), Vietcombank currencies (default USD,EUR,JPY,CNY), coins (BTC, ETH, BNB, SOL, XRP, DOGE,
  TON; default BTC,ETH).
- Photo option: `<mode><every><fit><ink><brightness>|id,id,…` — mode `r` random / `s` in turn / `f` keep
  the first; every `v` each time the screen is opened / `h` hour / `d` day; fit `c` fill / `n` fit inside;
  ink `a` fine dots (Atkinson) / `t` bold (threshold); brightness `00`–`99` (50 = as is); up to 12 ids of
  table `photos`.
- Photos: `POST /v1/photos/upload` `{size,w,h,thumb}` → `{id,url}` (PUT the JPEG original, ≤ 3 MB, at most
  1600 px), `POST /v1/photos/confirm` `{id}`, `GET /v1/photos`, `POST /v1/photos/delete` `{id}` (with its
  renders), `GET /v1/photos/:id/source` → `{url}` (the original, to render again), `POST /v1/photos/render`
  `{id,dim:"456x298",p:"ca50",size}` → `{exists:true}` or `{exists:false,url}` (PUT the 1-bit BMP, ≤ 100 KB).
  On save the web renders every picture of every photo widget at its box size. The device downloads
  `GET /v1/device/photos/:id/:dim/:p` (302 to storage; 404 when not rendered) into
  `/.crosspoint/inkdesk/photos/<id>_<dim>_<p>.bmp` at each sync and removes the files no layout uses.
- The server checks layouts (known kinds, inside the grid, no overlap, ≤ 20 items, Home keeps the
  `app/apps` tile, no `app` on the sleep screens, frame style, photo options) and answers 422 with the reason.
- Text on the device is never cut with an ellipsis: fonts shrink (18 → … → 8 pt) or wrap.

## Shared caches

Devices and the web never call outside sources; the server refreshes caches every 15 minutes:
- `market_tick` (schedule `*/15`): PNJ gold (all codes, change vs the previous day), VPS indices
  (trading hours) and the stock symbols any layout uses, Vietcombank rates (hourly), CoinGecko coins →
  table `market_cache` (`gold`, `index`, `stk`, `fx`, `crypto`). `build_dashboard` copies only what the
  device's widgets choose into the dashboard (`gold`, `stocks`, `fx`, `crypto`, `news.top`).
- `news_refresh` (schedule `*/15`) enqueues the news worker in `refresh` mode: new VnExpress stories of
  the sections readers follow are fetched once into storage (`newscache/art/<id>.json`, pictures
  shrunk in `newscache/img/`), headlines in table `news_cache`. Issues (the hourly `due` run and the
  "make one now" button) are packed from the cache only.
- `GET /v1/markets` (end_user) returns the cached quotes for the web's widget previews.
