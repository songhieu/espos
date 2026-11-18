# EspOS server (self-hosted)

The server side of [EspOS](../README.md): the web app and the API the Xteink X4 reader and
the ESP32-S3 colour desk talk to. One Go binary, one account, all data in one folder. No cloud
service, no sign-up. The reader's firmware is in [../firmware](../firmware/README.md), the
desk's in [../s3](../s3/README.md).

> **Tiếng Việt:** Máy chủ EspOS tự host cho máy đọc Xteink X4. Một file chạy duy nhất,
> một tài khoản (đặt bằng `ESPOS_USER` / `ESPOS_PASSWORD`), dữ liệu nằm trong một thư
> mục. Đủ tính năng như bản hosted: gửi sách (như Send to Kindle), giao diện widget cho màn
> chính và màn chờ, Báo sáng từ VnExpress, thời tiết, lịch âm dương, lịch Google/iCloud,
> giá vàng, chứng khoán, tỷ giá, tiền số, thống kê đọc, thông báo qua API, cập nhật
> firmware qua WiFi và cài firmware ngay trên trình duyệt. Xem "Quick start" bên dưới.

## Features

- **Send books** (EPUB, TXT). The reader downloads them at its next sync; once it reports a
  full copy, the server deletes its stored original and keeps only the send history.
- **Widget screens**: drag widgets onto the reader's home screen and its landscape or
  portrait sleep screen: clock, solar and lunar date, month calendar, today's events, weather,
  countdown, reading progress and statistics, recent books, the morning paper, gold, stocks,
  exchange rates, crypto, notes, battery, photos. The web preview is drawn with the reader's
  own fonts and layout code, so it matches the e-ink screen.
- **Sleep screen** choices: widgets, ready-made layouts, your pictures, the cover of the book
  you are reading.
- **Morning paper**: a daily EPUB built from VnExpress sections you pick (off by default).
- **Reading statistics** per day and per book, with goals and streaks.
- **Device settings** edited on the web or on the reader; the latest change wins.
- **Notifications**: `POST /v1/notify` with a key puts a line on the reader (CI, Home
  Assistant, scripts).
- **Firmware**: install it from the browser over USB (Web Serial), then update over WiFi.

## Quick start

### Docker

```sh
cp .env.example .env          # set ESPOS_PASSWORD
mkdir -p data && sudo chown 10001 data   # the container runs as uid 10001
docker compose up -d --build
```

Open http://localhost:8080 and sign in.

### From source

Needs Go 1.26+ and Node 22+. Run these in this `server/` folder.

```sh
make                          # builds the web app, then ./espos
ESPOS_PASSWORD='a long password' ./espos
```

## Configuration

| Variable | Default | |
|---|---|---|
| `ESPOS_PASSWORD` | (required) | The account's password, 8 characters or more. |
| `ESPOS_USER` | `admin` | The account's user name. |
| `ESPOS_ADDR` | `:8080` | Where the server listens. |
| `ESPOS_DATA` | `./data` | Database, files and the link-signing key. |
| `ESPOS_PUBLIC_URL` | (none) | The address the browser and the reader use, e.g. `https://espos.example.com`. Set it behind a reverse proxy. Without it the server uses the Host of each request. |
| `ESPOS_PKG_CATALOG` | (none) | The package catalog (a v2 `apps/catalog.json`) the X4 gets its sleep faces and widgets from. Any public URL works, e.g. the catalog of the hosted EspOS store. Without it the X4 is offered no packages; `.espk` files copied to its card still install. |

## Connecting the X4

1. **Get a firmware bundle.** Build the firmware in [../firmware](../firmware/README.md);
   `scripts/espos_bundle.sh` collects `bootloader.bin`, `partitions.bin`, `boot_app0.bin`
   and `firmware.bin` in a folder with a `manifest.json`:

   ```json
   {"version": "espos-x4-0.4.0", "chip": "esp32c3", "flash_size": "16MB",
    "parts": [{"file": "bootloader.bin", "offset": 0}, {"file": "partitions.bin", "offset": 32768},
              {"file": "boot_app0.bin", "offset": 57344}, {"file": "firmware.bin", "offset": 65536}]}
   ```

   Then add it to the server (it is also offered to linked readers as an update):

   ```sh
   ./espos firmware add path/to/bundle -notes "What changed"
   ```

2. **Install from the browser.** Open the web app in Chrome or Edge on a computer, go to
   **Thiết bị** and follow the steps. The installer backs up the reader's original firmware,
   flashes EspOS and gives the reader this server's address, a device token and your WiFi.
   Web Serial only works on a secure page: open the app at `http://localhost:8080` on the
   computer that runs the server, or serve it over HTTPS.

3. **No cable?** A reader that is already on EspOS can be linked with the code it shows
   (Thiết bị → "Liên kết lại bằng mã"). It asks the server it was built for, so build the
   firmware with your server's address: see "Server address" in the
   [firmware README](../firmware/README.md#server-address).

The reader talks to the server over HTTPS or plain HTTP. HTTPS is recommended whenever the
server is reachable from outside your network.

## Connecting the S3 desk

The desk (firmware in [../s3](../s3/README.md)) pairs with a code like a reader that has no
cable: build its firmware with this server's address (`ESPOS_API` and `ESPOS_WEB`, see the
[S3 README](../s3/README.md#server-address)), then enter the code it shows under **Thiết bị**.
It syncs its settings, widgets, news, markets and packages with this server. The self-hosted
web app has no pages for the desk's Home screens yet: edit them on the desk itself (every
setting has a screen there).

## Data and backups

Everything is in `ESPOS_DATA`:

- `espos.db`: the database (SQLite; every table is a set of JSON documents).
- `files/`: books waiting for the reader, pictures, morning papers.
- `firmware/`: install bundles.
- `secret.key`: signs file links. Keep it private.

Stop the server (or use `sqlite3 espos.db ".backup backup.db"`) and copy the folder.

A server set up before the project was renamed keeps working: its database is renamed to
`espos.db` on the first start, and settings still under their former `*_` names are read
(the log says which to rename).

## Services it calls

Only what the features you use need: [Open-Meteo](https://open-meteo.com) for weather and
place search, VnExpress RSS and articles for the morning paper (personal use), PNJ for gold
prices, VPS for Vietnamese stock quotes, Vietcombank for exchange rates, CoinGecko for crypto,
and the calendar links you add. The morning paper sources are fetched only while the paper is
switched on.

## How it is built

- `cmd/espos`: the binary (server and `firmware add`).
- `internal/server`: the web app, sign-in, the `/v1` API, signed file links.
- `internal/backend`: routes, tables and workflow graphs (JSON). Each route runs a workflow:
  a small graph such as http_in → code → branch → respond.
- `internal/engine`: runs those graphs and the scheduled ones (cron).
- `internal/flows`: the code nodes, one Go package per node.
- `internal/wm`: what the nodes call: tables on SQLite, files on disk, outbound HTTP.
- `internal/jobs/newsjob`: the morning paper builder, run as a background job.
- `web/`: the React web app, embedded into the binary at build time.
- `tools/importer`: re-imports the workflows from the hosted EspOS backend
  (`make import FROM=…`), so both stay the same. Two kinds are left out on purpose: routes for
  the hosted store's API keys, and the workflows that send e-mail on the hosted sign-up's
  account events (`user.registered`, `user.verification_requested`,
  `password.reset_requested`, through `event_trigger` and `smtp_send`). This server has one
  account set by `ESPOS_USER` / `ESPOS_PASSWORD`: nobody signs up, checks an address or resets
  a password by mail, so there is no SMTP setting. Any other `event_trigger` stops the import
  until the engine supports it. Workflows that answer HTTP but that no route runs (drafts and
  temporary probes on the hosted site), and tables nothing here uses yet, are left out too.
  So is the dog assistant (`/v1/device/assist`, `/v1/devices/:id/assist`, `/v1/assist/memories`):
  it needs the hosted service's AI model and speech service (workflows that read a hosted secret,
  the ones that call them, and its own memory workflows). The S3 shows no answer from the dog on a
  self-hosted server.

```sh
make test     # Go tests
cd web && npm run dev    # the web app with hot reload, using a server on :8080
```

## Security

- One account. Sign-in sessions last 60 days; signing out ends one, and changing
  `ESPOS_USER` or `ESPOS_PASSWORD` ends all of them.
- After a failed sign-in the address waits 1 s, then 2 s, 4 s and so on (up to 15 minutes).
  Open routes (pairing, notifications) are limited per address per minute.
- Device tokens and notification keys are stored as SHA-256 hashes; the data folder is
  readable by the server's user only.
- Files are reached only through short-lived signed links.
- Calendar links you add are fetched by the server (https only, no redirect to plain http).
- Put the server behind HTTPS (Caddy, nginx, Traefik) before exposing it to the internet,
  and set `ESPOS_PUBLIC_URL`.

## License

GNU Affero General Public License v3.0, see [LICENSE](LICENSE). If you run a changed copy as a
service for others, offer them its source. Bundled fonts keep their own licenses (OFL, UFL).
Copies up to commit `fac0c54` (2026-11-18) were published under the MIT License and stay MIT.
