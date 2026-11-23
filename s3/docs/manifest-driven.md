# One package for the device, the server and the web: everything in the manifest (design)

Status: design, approved 2026-11-21 with [app-slots.md](app-slots.md); phase 1 of
[app-platform-plan.md](app-platform-plan.md) builds sections 1 and 2.

The owner's words:

> "các app bây giờ có nguyên tắc để xây 1 bộ vừa chạy dưới máy vừa có thể config từ server -> dựa vào meta để define
> thứ cần thiết trên server... để sau này viết gì kể cả giao diện trên app cho các phần app này cũng đi qua manifest"

So: an app is one package. Its manifest says what the user can set, what the server must do for it, and what the web
shows. A developer writes no server code and no web code for the common cases, and our own apps work the same way.

What exists today: the manifest reference already lists `settings` ("drawn by both the device and the web desk") and
`web`, but nothing implements them. The SDK schema has an empty slot for `settings`; device settings are validated
by hard-coded allowlists in `put_device_settings` (`s3.*`, `dash.*`, `bot.*`, `sys.*`) and synced by fixed key lists
in `device_sync` (`deviceDefaults`, `s3Shared`); JS apps keep private values with `store.get/set` only.

---

## 1. The `settings` schema

### 1.1 Shape

```json
"settings": [
  {"key": "city", "type": "city", "label": "City"},
  {"key": "units", "type": "enum", "label": "Units", "default": "c",
   "options": [{"v": "c", "label": "Celsius"}, {"v": "f", "label": "Fahrenheit"}]},
  {"key": "seconds", "type": "bool", "label": "Show seconds", "default": false},
  {"key": "speed", "type": "number", "label": "Speed", "min": 1, "max": 10, "step": 1, "default": 4},
  {"key": "shop_url", "type": "url", "label": "Shop address"},
  {"key": "ck", "type": "secret", "label": "Consumer key", "where": "device"},
  {"key": "cs", "type": "secret", "label": "Consumer secret", "where": "device"},
  {"key": "tags", "type": "list", "label": "Tags", "of": {"type": "string", "max": 20}, "max_items": 10},
  {"key": "wake", "type": "time", "label": "Wake up", "default": "07:00"},
  {"type": "section", "label": "Advanced"},
  {"key": "note", "type": "text", "label": "Note", "max": 500,
   "i18n": {"vi": {"label": "Ghi chú"}, "de": {"label": "Notiz"}}}
]
```

| Type | Value stored | Rules | Device control | Web control |
|---|---|---|---|---|
| `string` | text | `max` (≤ 200, default 80) | one-line field + keyboard | text input |
| `text` | text | `max` (≤ 2,000, default 500) | multi-line field | textarea |
| `number` | decimal text | `min`, `max`, `step`, `int` | stepper (or slider when `max − min ≤ 100`) | number input |
| `bool` | `1` / `0` | | switch | switch |
| `enum` | one `v` | `options` (2 to 20, each `{v, label}`) | segmented (≤ 3) or a list | segmented or select |
| `list` | JSON array | `of` (a `string` or `enum` item), `max_items` (≤ 50) | rows with add/remove | chips with add/remove |
| `time` | `HH:MM` | | time picker | time input |
| `date` | `YYYY-MM-DD` | | date picker | date input |
| `color` | `#rrggbb` | | swatches | colour input |
| `url` | `https://` address | https only, ≤ 300 | field + keyboard | input |
| `city` | `{"name","lat","lon"}` as JSON | geocoded by the server | the OS city picker | the web's place picker |
| `photo` | a photo id | the web's photo upload | (shows it) | upload |
| `secret` | not readable | `where: "device"` or `"server"` (1.4); bound to requests by the manifest's `auth` | "Set" / "Change" / "Clear" | (1.4) |
| `section` | none | a heading | heading | heading |

Every field: `key` (1 to 24 of `a-z0-9_`, unique), `label` (≤ 40), optional `help` (≤ 120), `default`, and
`i18n: {"<lang>": {"label", "help", "options": {"<v>": "<label>"}}}` in the device's 11 languages. At most 40 fields
per package.

**Left out on purpose:** conditions between fields, regular expressions, nested objects, per-field permissions. They
can come later as additive fields; v1 forms stay predictable.

### 1.2 Where the values live: `app.<id>.<key>`

- Device setting keys `app.<package id>.<key>`, one string each (a `list` and a `city` are JSON). At most 2,000
  characters a value, 8 KB per package per device.
- Synced both ways like today's shared keys: newest wins (`updated_ms`), raise the ping `rev` (device_ping and
  device_sync compute it the same way; `scripts/check-ping-keys.py` checks the prefix too).
- Removed when the package is uninstalled (the device drops them; the server deletes them at the next sync that
  no longer lists the package).
- JS reads them with `settings.get(key)` and hears changes with `app.onSettings(changed)`; it never writes another
  app's values. A secret reads as `{set: true}` only.

### 1.3 Validation, three times with one rule set

| Where | How |
|---|---|
| SDK (`espos check`) | `schemas/settings.schema.json` + checks (unique keys, defaults valid for their type, `send_to` hosts covered by `perms` `net:<host>`) |
| Server | `put_device_settings` and `device_sync` accept `app.<id>.<key>` only when the package is installed on that device and its schema (from the new table `app_manifests`, filled by the publish script, dev uploads and user packages) has that key; the value is checked by type. Everything else is refused, as today |
| Device | the same checks in C++ before a value is stored (it may be set on the device itself) |

This replaces hard-coded allowlists for third-party apps without weakening anything: an unknown key is still
refused, and the rules come from a manifest the store signed.

### 1.4 Secrets

Two kinds, because two real apps need them differently (the WooCommerce POS keeps its keys on the device only;
server-side fetch rules need a key on the server):

| | `where: "device"` | `where: "server"` |
|---|---|---|
| Typed on | the device (keyboard), or from a computer on the same Wi-Fi (the device shows a QR and a PIN; a one-time local page) | the web |
| Stored | the device's secret store (NVS namespace `sec`), never synced | the server, encrypted (`app_secrets`), never returned by any read |
| Used by | the OS, which adds it to the app's requests through a named binding of the manifest's `auth` (`basic`, `bearer`, `header`, an optional `query` fallback, and the addresses it may go to in `send_to`; app-slots.md §15.1). JS says `net.request({..., auth: "store"})` | the server's fetch rules (2.2); and, when an `auth` binding uses it, delivered to that device (sync `secrets`, acked) and used as on the left |
| The app sees | `{set, hint}` (the last 4 characters) | `{set, hint}` |
| The web shows | "Set on the device" or "Not set" (the device reports which are set) | "Set, ends in 4F2A" + Replace / Remove |

A binding's address placeholder (`{shop_url}`) names a url or text field; changing that field clears the binding's
secrets on the device and on the server, so keys never follow a re-pointed address. The full contract (sync fields,
routes, codes, the JS calls) is app-slots.md §15.

Limits, said plainly: the S3 has no flash encryption today, so a device secret is readable by someone who takes the
board and reads its flash. Encryption at rest on the hosted server needs AES in our code nodes (HMAC works there;
AES is untested): until then `app_secrets` is a table no route ever returns.

---

## 2. Server-side needs, declared

```json
"server": {
  "feeds": ["weather", "markets"],
  "fetch": [{"id": "rates", "every_min": 30, "max_kb": 20,
             "url": "https://api.example.com/rates?city={settings.city.name}&key={secret.api_key}",
             "pick": {"usd": "$.rates.USD", "eur": "$.rates.EUR", "at": "$.time"}}],
  "hooks": [{"id": "door", "popup": {"title": "Someone at the door", "priority": "high"}}],
  "storage_kb": 64
}
```

### 2.1 Feeds

The server already keeps weather (per city), markets, news and calendars (`weather_cache`, `market_cache`,
`news_cache`, `ics_cache`) for the dashboards. An app lists the ones it reads (with the matching `data:*`
permission); the device gets them through the existing data sync, and JS reads `data.weather`, `data.markets` and the others

### 2.2 Fetch rules

- The server fetches the URL for each device that has the app, every `every_min` (at least 15), with the device's
  settings and server secrets filled in, keeps what `pick` selects (a small JSONPath subset: `$.a.b`, `$.a[0]`,
  `$.a[*].b`), at most 4 KB, and sends it in the next sync as `app_data.<id>.<rule>`. JS reads
  `data.app("rates")`.
- Safety: https only, public addresses only, no redirects, 10 s, `max_kb` (≤ 100) read, the host shown at install
  ("This app's server part reads api.example.com"), and per-account caps (e.g. 500 fetches a day).
- Built by one scheduled workflow (`app_fetch_tick`) and one table (`app_data`).

### 2.3 Hooks

`POST https://api.espos.dev/v1/hooks/<token>` with a JSON body: the token is made per device, app and hook, shown
on the app's web page ("Your doorbell address"). A hook can raise the app's pop-up on that device (app-slots.md 6,
with its rate limits) or store a small value in `app_data`. Revoking it is one button.

### 2.4 Assistant providers and storage

- An assistant app's brain is declared in `assistant.provider` (app-slots.md 5; the protocol in
  assistant-providers.md).
- `storage_kb` caps `app_data` per device (default 64).

---

## 3. The web side of an app

| Level | What | When |
|---|---|---|
| 0 | the settings form, drawn from the schema, on the device page › Apps › the app | phase 1 |
| 1 | a declarative page: the form plus status cards bound to `app_data` and the feeds, in the same layout format as the device's declarative faces (profiles.md §4: `text`, `bar`, `list`, `icon`, `image` items), drawn by the web | after phase 6 |
| 2 | a sandboxed iframe page (`"web": {"page": "web/index.html"}`, `sandbox="allow-scripts"`, no same-origin), with a small postMessage API: `getSettings`, `setSettings`, `getData`, `hookUrl` | later, for advanced apps |

Declarative first: most apps need a form and a few numbers.

---

## 4. Our own settings, moved to schemas

| Setting | Becomes | Why / order |
|---|---|---|
| JS sample apps (water, focus, vocab) and new apps (the POS) | schema + `app.<id>.*` | first: no old keys to keep |
| Aquarium settings (night light, sounds) | schema on `vn.pawboop.tank` | second |
| Assistant switches (`s3.assist`: wake, ack, follow, led) | schema whose fields point at the existing keys (`"store_as": "s3.assist#wake"`) | third: no data moves, the web's custom card becomes the generic form |
| Weather city, markets lists, news sections, calendars | schema fields on the built-in widgets, pointing at today's `dash.*` / `news_prefs` | last: shared by widgets and the assistant |
| Home editor (`s3.home`), saver designer (`s3.saver_pages`), alarms, WiFi, pairing, OTA, photos, the Bot's screen picker | stay custom | their editors are the product; a form would be worse |

`store_as` is for built-in packages only (the server allows it for OS-owned ids), so our apps use the same renderer
without moving anyone's data.

---

## 5. Effort and risks

| Piece | Effort | S3 flash | Risks |
|---|---|---|---|
| SDK: `settings.schema.json`, validator, tests, docs page | 1 to 2 days | 0 | low |
| Server: `app_manifests`, the `app.*` namespace in put_device_settings / device_sync / device_ping, refusals | 3 to 4 days | 0 | the busiest workflows; others edit them too: small, separate functions called from them |
| Server secrets: `app_secrets`, routes, delivery | 2 to 3 days | 0 | AES in code nodes untested |
| Web: `SchemaForm`, the app settings page, secrets UI, i18n | 2 to 3 days | 0 | low |
| Device: the LVGL form, the value store, `settings.get`, `app.onSettings` | 3 to 4 days | about 15 KB | the keyboard and list rows on 320 px; 2.8" scaling |
| Device: `net.request` + Promise jobs + secrets added by the OS (with woo-pos) | 3 days | about 10 KB | the TLS queue is single; timeouts |
| Device: secrets typed from a computer (local page, QR, PIN) | 2 days | about 6 KB | a web server on the device (only while the page is open) |
| Server fetch rules + `app_data` + `data.app()` | 3 to 4 days | about 3 KB | SSRF, cost; caps |
| Hooks → pop-ups | 2 days | about 1 KB | abuse: rate limits |
| Declarative web pages; iframe pages | 3 days each | 0 | sandbox rules |
| Moving our settings (section 4) | per item | ±0 | keep old keys (`store_as`) |
