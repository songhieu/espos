# EspOS device ⇄ server contract (v1)

The routes every EspOS device uses to talk to its server: the ESP32-S3 desks (`kind` `s3`) and the
EspOS Bot (`kind` `bot`). The hosted server (api.espos.dev) and the self-hosted one in `server/`
answer the same way. What one kind of device keeps in its settings and adds to its sync is written
next to its firmware, in `s3/docs/`: `settings-parity.md` (every setting), `widgets.md` and
`saver.md` (Home pages and the saver), `ecosystem.md` and `packages.md` (packages and the store),
`assistant-ai.md` (the dog assistant), `lock.md` (licences on the hosted service), `bot.md` (the Bot).

API base: the server's address. A device takes it from USB provisioning (field `api`) or from its
build (`ESPOS_API`, see `s3/README.md`). Examples use `https://espos.example.com`. All bodies are
UTF-8 JSON. Texts the device shows (dates, weather words, events) come formatted by the server.

## Device auth

- Each device owns a **token**: 32 random bytes as 64 lowercase hex chars. It is made on the client
  side (the browser's `crypto.getRandomValues` for a USB install; the ESP32's `esp_random()` for
  pairing) and kept on the device only. The server stores `sha256(token)` hex.
- Every device call sends `Authorization: Device <token>`.
- `401 {"error":"unauthorized"}`: unknown, revoked or malformed token → the device forgets its link
  and offers to pair again.
- `401 {"error":"not_linked"}`: a pairing is pending (see pairing): keep polling.

## POST /v1/device/sync  (device auth)

Request (an S3 desk):
```json
{
  "fw": "espos-s3-0.5.9", "fw_code": 509, "board": "es3c35p",
  "battery": 78, "battery_mv": 4012, "usb": false,
  "settings_rev": 0,
  "settings": [{"k": "s3.volume", "v": "60", "age_s": 120}],
  "diag": {"slot": 3932160, "ptab": 1, "ping": 45},
  "wifi": {"saved": [{"ssid": "Home", "ok": true, "code": 3}], "acks": [{"id": 12, "ok": true}]}
}
```
- `settings`: the settings changed ON THE DEVICE since the last good sync; `age_s` = seconds since
  that change (0 if unknown). Values are strings.
- `diag`: health and capability numbers the web shows. `slot` (the app slot's bytes) and `ptab`
  (the partition table) let the server offer only a firmware image that fits.
- `wifi`: saved networks by name only, with the last join result (`code` 3 connected, 1 not found,
  4 failed); `acks` confirm the WiFi changes received in the previous sync.
- Each kind adds its own fields (the S3's packages report, for one): see its docs in `s3/docs/`.

Response 200:
```json
{
  "now_ms": 1794632000000, "tz_min": 420,
  "device": {"id": 3, "name": "Bàn làm việc", "user": "you@example.com"},
  "settings_rev": 1794631999000,
  "settings": {"sys.lang": "vi", "dash.interval_min": "30"},
  "dashboard": {"…": "same object as GET /v1/device/dashboard"},
  "wifi_ops": [{"id": 12, "op": "add", "ssid": "Home", "pass": "…"}],
  "fw_update": null,
  "user_pkgs": []
}
```
- `settings` is the FULL merged set: the server wins for keys the device did not send; for keys it
  sent, the most recent change wins. The device applies it and keeps `settings_rev`.
- `now_ms` / `tz_min`: the wall clock and the time zone (the boards have no RTC). The device adopts
  the clock when its own is 3 s or more off.
- `wifi_ops`: WiFi added or removed on the web. The device applies them and acks them in the next
  sync; the server then clears the password.
- `user_pkgs`: the account's own apps (ids `u.*`), see `s3/docs/packages.md`.
- `lic` (hosted service only): the account's licence. No `lic`, as on a self-hosted server, means
  no licensing: nothing locks (`s3/docs/lock.md`).

## GET /v1/device/ping  (device auth)

`{"fw": false, "rev": 1794990360224}`, asked every 45 s: `rev` is the `settings_rev` a sync would
bring, so a change made on the web reaches the device within a ping; `fw` is true when a newer
firmware is offered. The device syncs when either changed.

## GET /v1/device/dashboard  (device auth)

The data the device's widgets show, also part of each sync.
```json
{
  "now_ms": 1794632000000, "tz_min": 420,
  "date": "14.11.2026", "weekday": "THỨ BẢY",
  "lunar": "6/10", "lunar_year": "Bính Ngọ",
  "weather": {"city": "Hà Nội", "temp": 24, "desc": "Mây rải rác", "min": 19, "max": 26, "rain": 20,
              "icon": "cloudsun", "hours": [{"h": "13h", "t": 25, "icon": "cloudsun"}]},
  "events": [{"time": "09:00", "title": "Họp nhóm tuần"}],
  "notes": [{"title": "CI · build thành công", "time": "10:12"}],
  "next_s": 1800
}
```
- Any block may be `null` or absent (turned off, or no data): the device skips it.
- `icon` ∈ `sun|cloudsun|cloud|rain|storm|fog|snow|moon`.
- The server copies in only what the device's widgets use: markets (`gold`, `stocks`, `fx`,
  `crypto`), news, the agenda and the S3's longer weather.

## Pairing

1. The device makes a token and a random 6-digit code, then
   `POST /v1/pair/start` (no auth)
   `{"code":"482913","token_hash":"<sha256 hex>","name":"EspOS S3","kind":"s3"}`
   → `200 {"ok":true,"expires_s":600}` · `409 {"error":"code_taken"}` (try another code).
2. The device shows the code and a QR code of `<web>/app#/pair?code=482913` (`<web>`: the web app's
   address, `ESPOS_WEB`).
3. The device polls `GET /v1/device/whoami` (device auth) every 5 s:
   `401 not_linked` while pending → `200 {"device":{"id":3,"name":"…","user":"…"}}` once the person
   confirmed on the web (`POST /v1/pair/confirm`). Then it syncs.

## USB provisioning (browser → device over the USB serial port)

One message per line (`\n`). The device listens whenever it runs; its own log lines share the port,
and the browser ignores lines that do not start with `ESPOS:`.

```
browser → ESPOS:HELLO <epoch ms>     (the browser's clock)
device  → ESPOS:READY {"fw":"espos-s3-0.5.9","kind":"s3","board":"es3c35p","linked":false}
browser → ESPOS:PROVISION {"api":"https://espos.example.com","token":"<64 hex>",
           "device":{"id":3,"name":"Bàn làm việc","user":"you@example.com"},
           "wifi":[{"ssid":"Home","pass":"…"}],
           "settings":{"sys.lang":"vi"},"settings_rev":1794631999000,"now_ms":1794631999000}
device  → ESPOS:OK            (or ESPOS:ERR <reason>)
```
After `OK` the device saves everything and opens its welcome screen. The WiFi password typed in the
installer never goes to the server.

## Shared setting keys (all values are strings)

| key | values | default | |
|---|---|---|---|
| `sys.lang` | a language tag: `en`, `vi`, `de`, `es`, `fr`, `id`, `ja`, `ko`, `pt-BR`, `ru`, `zh-Hans` | `en` | the device's language (others than English come as language packages) |
| `dash.interval_min` | `5`, `15`, `30`, `60` | `30` | how often the data refreshes |
| `dash.quiet_from` / `dash.quiet_to` | `HH:MM` | `23:00` / `06:00` | quiet hours |

Web-only keys (never sent to a device): `dash.city`, `dash.lat`, `dash.lon`, `dash.ics_url`.
The desk's own keys (`s3.*`) are listed in `s3/docs/settings-parity.md`; the Bot's (`bot.*`) in
`s3/docs/bot.md`.

## Firmware updates over WiFi

- Table `firmware` (one row per release): `version` (`espos-s3-X.Y.Z`), `code` (X*10000+Y*100+Z),
  `kind`, `url` (public storage, content-hashed), `sha256`, `size`, `notes` (one item per line),
  `released`.
- Release: build (`pio run -e s3desk`), collect a bundle with `s3/tools/make_bundle.py`, then
  `espos firmware add <bundle> -notes "…"` on the self-hosted server. It stores the files, writes
  `/firmware/latest.json` for the web installer and inserts the `firmware` row.
- `POST /v1/devices/:id/firmware` (end user): `{"action":"update"|"cancel"}` or
  `{"auto":true|false}`. `GET /v1/devices` also returns `firmware` (the newest release that fits)
  and each device's `fw_code`, `fw_req`, `fw_auto`, `ota_state`.
- Sync request: `fw_code`, and `ota: [{version, ok, why?}]` after an install attempt. Sync response:
  `fw_update` (`{version, url, sha256, size, notes, date, install}`) when a newer release that fits
  exists, else `null`; `install` is true when the web asked for it (or `fw_auto` while on USB power).
- Device: streams the image into the spare OTA slot, checks SHA-256 and the image, selects it and
  restarts. The new image stays on probation until a sync works: with no good sync in its first
  15 minutes it goes back to the old firmware, which reports `rollback`.

## Shared caches

Devices and the web never call outside sources; the server refreshes caches every 15 minutes:
- `market_tick`: PNJ gold, VPS stock indices and the symbols any device uses, Vietcombank rates,
  CoinGecko coins → table `market_cache`.
- `news_refresh`: new VnExpress stories of the sections people follow, fetched once into storage
  (pictures shrunk), headlines in table `news_cache`.
- `GET /v1/markets` (end user) returns the cached quotes for the web's widget previews.
