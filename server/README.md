# InkDesk server (self-hosted)

The server side of [InkDesk](../README.md): the web app and the API the Xteink X4 reader
talks to. One Go binary, one account, all data in one folder. No cloud service, no sign-up.
The reader's firmware is in [../firmware](../firmware/README.md).

> **Tiếng Việt:** Máy chủ InkDesk tự host cho máy đọc Xteink X4. Một file chạy duy nhất,
> một tài khoản (đặt bằng `INKDESK_USER` / `INKDESK_PASSWORD`), dữ liệu nằm trong một thư
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
cp .env.example .env          # set INKDESK_PASSWORD
mkdir -p data && sudo chown 10001 data   # the container runs as uid 10001
docker compose up -d --build
```

Open http://localhost:8080 and sign in.

### From source

Needs Go 1.26+ and Node 22+. Run these in this `server/` folder.

```sh
make                          # builds the web app, then ./inkdesk
INKDESK_PASSWORD='a long password' ./inkdesk
```

## Configuration

| Variable | Default | |
|---|---|---|
| `INKDESK_PASSWORD` | (required) | The account's password, 8 characters or more. |
| `INKDESK_USER` | `admin` | The account's user name. |
| `INKDESK_ADDR` | `:8080` | Where the server listens. |
| `INKDESK_DATA` | `./data` | Database, files and the link-signing key. |
| `INKDESK_PUBLIC_URL` | (none) | The address the browser and the reader use, e.g. `https://inkdesk.example.com`. Set it behind a reverse proxy. Without it the server uses the Host of each request. |

## Connecting the X4

1. **Get a firmware bundle.** Build the firmware in [../firmware](../firmware/README.md);
   `scripts/inkdesk_bundle.sh` collects `bootloader.bin`, `partitions.bin`, `boot_app0.bin`
   and `firmware.bin` in a folder with a `manifest.json`:

   ```json
   {"version": "inkdesk-0.3.3", "chip": "esp32c3", "flash_size": "16MB",
    "parts": [{"file": "bootloader.bin", "offset": 0}, {"file": "partitions.bin", "offset": 32768},
              {"file": "boot_app0.bin", "offset": 57344}, {"file": "firmware.bin", "offset": 65536}]}
   ```

   Then add it to the server (it is also offered to linked readers as an update):

   ```sh
   ./inkdesk firmware add path/to/bundle -notes "What changed"
   ```

2. **Install from the browser.** Open the web app in Chrome or Edge on a computer, go to
   **Thiết bị** and follow the steps. The installer backs up the reader's original firmware,
   flashes InkDesk and gives the reader this server's address, a device token and your WiFi.
   Web Serial only works on a secure page: open the app at `http://localhost:8080` on the
   computer that runs the server, or serve it over HTTPS.

3. **No cable?** A reader that is already on InkDesk can be linked with the code it shows
   (Thiết bị → "Liên kết lại bằng mã"). It asks the server it was built for, so build the
   firmware with your server's address: see "Server address" in the
   [firmware README](../firmware/README.md#server-address).

The reader talks to the server over HTTPS or plain HTTP. HTTPS is recommended whenever the
server is reachable from outside your network.

## Data and backups

Everything is in `INKDESK_DATA`:

- `inkdesk.db`: the database (SQLite; every table is a set of JSON documents).
- `files/`: books waiting for the reader, pictures, morning papers.
- `firmware/`: install bundles.
- `secret.key`: signs file links. Keep it private.

Stop the server (or use `sqlite3 inkdesk.db ".backup backup.db"`) and copy the folder.

## Services it calls

Only what the features you use need: [Open-Meteo](https://open-meteo.com) for weather and
place search, VnExpress RSS and articles for the morning paper (personal use), PNJ for gold
prices, VPS for Vietnamese stock quotes, Vietcombank for exchange rates, CoinGecko for crypto,
and the calendar links you add. The morning paper sources are fetched only while the paper is
switched on.

## How it is built

- `cmd/inkdesk`: the binary (server and `firmware add`).
- `internal/server`: the web app, sign-in, the `/v1` API, signed file links.
- `internal/backend`: routes, tables and workflow graphs (JSON). Each route runs a workflow:
  a small graph such as http_in → code → branch → respond.
- `internal/engine`: runs those graphs and the scheduled ones (cron).
- `internal/flows`: the code nodes, one Go package per node.
- `internal/wm`: what the nodes call: tables on SQLite, files on disk, outbound HTTP.
- `internal/jobs/newsjob`: the morning paper builder, run as a background job.
- `web/`: the React web app, embedded into the binary at build time.
- `tools/importer`: re-imports the workflows from the hosted InkDesk backend
  (`make import FROM=…`), so both stay the same.

```sh
make test     # Go tests
cd web && npm run dev    # the web app with hot reload, using a server on :8080
```

## Security

- One account. Sign-in sessions last 60 days; signing out ends one, and changing
  `INKDESK_USER` or `INKDESK_PASSWORD` ends all of them.
- After a failed sign-in the address waits 1 s, then 2 s, 4 s and so on (up to 15 minutes).
  Open routes (pairing, notifications) are limited per address per minute.
- Device tokens and notification keys are stored as SHA-256 hashes; the data folder is
  readable by the server's user only.
- Files are reached only through short-lived signed links.
- Calendar links you add are fetched by the server (https only, no redirect to plain http).
- Put the server behind HTTPS (Caddy, nginx, Traefik) before exposing it to the internet,
  and set `INKDESK_PUBLIC_URL`.

## License

MIT, see [LICENSE](../LICENSE). Bundled fonts keep their own licenses (OFL, UFL).
