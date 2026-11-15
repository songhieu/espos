# InkDesk

InkDesk turns the **Xteink X4** e-ink reader into a reader plus a small desk display: a home
screen and a sleep screen made of widgets, books sent from your browser, and settings that
stay in sync between the reader and a web app. It is self-hosted: one Go binary, one account,
your data in one folder.

> **Tiếng Việt:** InkDesk biến máy đọc Xteink X4 thành máy đọc sách kiêm màn hình để bàn:
> màn chính và màn chờ ghép từ widget (đồng hồ, lịch âm dương, thời tiết, sách đang đọc,
> Báo sáng, giá vàng, chứng khoán, tỷ giá, ảnh…), gửi sách từ trình duyệt như Send to
> Kindle, cài đặt đồng bộ hai chiều giữa máy và web, thống kê đọc, cập nhật firmware qua
> WiFi. Tự host: một file chạy, một tài khoản, dữ liệu nằm trong một thư mục.

## What is in this repository

| Folder | |
|---|---|
| [`server/`](server/README.md) | The server: Go API, the React web app (embedded in the binary), SQLite storage, background jobs (weather, calendars, market prices, the morning paper). |
| [`firmware/`](firmware/README.md) | The reader firmware, a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for the ESP32-C3 in the X4. |
| [`docs/device-api.md`](docs/device-api.md) | The contract between the reader and the server. |

## Features

- **Send books** (EPUB, TXT) from the web. The reader downloads them at its next sync; once
  it has a full copy, the server deletes its stored original and keeps only the history.
- **Widget screens** for the home screen and the landscape or portrait sleep screen: clock,
  solar and lunar date, month calendar, today's events (Google / iCloud calendar links),
  weather, countdown, reading progress and statistics, recent books, the morning paper,
  gold, stocks, exchange rates, crypto, notes, battery, your photos. Ready-made layouts and
  frame styles; the web preview uses the reader's own fonts and layout code.
- **Sleep screen** that wakes on a timer to refresh its data and keep a minute clock without
  WiFi, with quiet hours and a battery-saving level below which it stops waking.
- **Morning paper**: a daily EPUB built from the VnExpress sections you pick (off by default).
- **Reading statistics** per day and per book, with goals and streaks.
- **Settings both ways**: change them on the web or on the reader; the latest change wins.
- **Notifications**: `POST /v1/notify` with a key puts a line on the reader.
- **Firmware**: install from the browser over USB (Web Serial), then update over WiFi.

## Quick start

1. **Run the server** (Docker, or Go 1.26+ and Node 22+):

   ```sh
   cd server
   cp .env.example .env            # set INKDESK_PASSWORD
   mkdir -p data && sudo chown 10001 data
   docker compose up -d --build    # or: make && INKDESK_PASSWORD='…' ./inkdesk
   ```

   Open http://localhost:8080 and sign in. Details and settings:
   [server/README.md](server/README.md).

2. **Build the firmware** and add it to the server:

   ```sh
   git submodule update --init --recursive
   cd firmware
   pio run -e default
   scripts/inkdesk_bundle.sh default ../bundle
   cd ../server && ./inkdesk firmware add ../bundle -notes "First install"
   ```

   See [firmware/README.md](firmware/README.md) for the toolchain.

3. **Install it on the X4**: in the web app open **Thiết bị** in Chrome or Edge on a
   computer, plug the reader in over USB and follow the steps. The installer backs up the
   reader's original firmware first, then gives the reader your server's address, a device
   token and your WiFi.

## Configuration

**Server** (environment variables, see [server/README.md](server/README.md#configuration)):

| Variable | Default | |
|---|---|---|
| `INKDESK_PASSWORD` | (required) | The account's password, 8 characters or more. |
| `INKDESK_USER` | `admin` | The account's user name. |
| `INKDESK_ADDR` | `:8080` | Where the server listens. |
| `INKDESK_DATA` | `./data` | Database, files and the link-signing key. |
| `INKDESK_PUBLIC_URL` | (none) | The public address, e.g. `https://inkdesk.example.com`. Set it behind a reverse proxy. |

**Firmware: your server's domain.** The USB installer always tells the reader where the server
is, so the default firmware build has no server address in it. To link readers with a code
(without a cable) and to show your address and QR code on the reader, build the firmware with
your domain. Create `firmware/platformio.local.ini` (git ignores it):

```ini
[env:default]
build_src_flags =
  '-DINKDESK_DEFAULT_API="https://inkdesk.example.com"'
  ; only if the web app has a different address than the API:
  ; '-DINKDESK_DEFAULT_WEB="https://app.example.com"'
```

| Flag | Default | |
|---|---|---|
| `INKDESK_DEFAULT_API` | empty | The server the reader calls. |
| `INKDESK_DEFAULT_WEB` | same as `INKDESK_DEFAULT_API` | The web app's address, for QR codes and pairing links. |

## Development

```sh
cd server && make test                 # Go tests
cd server/web && npm run dev           # web app with hot reload against a server on :8080
cd firmware && pio run -e default      # firmware build
```

CI builds the server, its Docker image and the firmware on every push and pull request.
Firmware code follows CrossPoint's conventions (`firmware/.agents/rules`); run
`firmware/bin/clang-format-fix` before sending C++ changes.

## Credits and license

MIT, see [LICENSE](LICENSE). The firmware is built on
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) and the
[FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (MIT). Weather and place search from
[Open-Meteo](https://open-meteo.com). Bundled fonts keep their own licenses (OFL, UFL).

InkDesk is not affiliated with Xteink.
