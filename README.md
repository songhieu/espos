<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo-horizontal-white.svg">
    <img alt="EspOS" src="assets/logo-horizontal.svg" height="64">
  </picture>
</h1>

EspOS is an open operating system for ESP32 screens. It runs on two kinds of device today:

- **ESP32-S3 colour touch displays** (the 3.5" ES3C35P and 2.8" ES3C28P boards): a desk OS with
  Home pages of apps and widgets, a saver, notifications, themes, languages, and a store of apps
  installed without new firmware (JavaScript apps, J2ME phone games, widgets).
- **The Xteink X4 e-ink reader**: a reader plus a small desk display, with widget home and sleep
  screens, books sent from the browser and reading statistics.

Both sync with an EspOS server: the hosted one at **[espos.dev](https://espos.dev)**, or your own,
self-hosted from this repository (one Go binary, one account, your data in one folder).

Docs for app developers and board porters: **[espos.dev/docs](https://espos.dev/docs)**.

> **Tiếng Việt:** EspOS là hệ điều hành mở cho màn hình ESP32: màn cảm ứng màu ESP32-S3 (màn
> chính ghép ứng dụng và widget, màn chờ, thông báo, giao diện, ngôn ngữ, kho ứng dụng cài
> không cần nạp lại firmware) và máy đọc Xteink X4 (đọc sách kiêm màn hình để bàn). Máy đồng
> bộ với máy chủ EspOS: bản hosted ở espos.dev hoặc tự host từ repo này.

## What is in this repository

| Folder | | Licence |
|---|---|---|
| [`server/`](server/README.md) | The self-hosted server: Go API, the React web app (embedded in the binary), SQLite storage, background jobs (weather, calendars, market prices, the morning paper). | AGPL-3.0 |
| [`s3/`](s3/README.md) | The ESP32-S3 firmware, its simulators (host build and QEMU), tools, the SDK (`espos` CLI, typings, schemas), sample apps, translations and the developer docs site. | Apache-2.0 |
| [`firmware/`](firmware/README.md) | The Xteink X4 firmware, a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for its ESP32-C3. | Apache-2.0 for EspOS's changes; CrossPoint's code MIT |
| [`docs/device-api.md`](docs/device-api.md) | The contract between the devices and the server. | Apache-2.0 |

## Self-hosting quick start

Docker, or Go 1.26+ and Node 22+:

```sh
cd server
cp .env.example .env            # set ESPOS_PASSWORD
mkdir -p data && sudo chown 10001 data
docker compose up -d --build    # or: make && ESPOS_PASSWORD='…' ./espos
```

Open http://localhost:8080 and sign in. Settings, backups and how devices connect:
[server/README.md](server/README.md).

| Variable | Default | |
|---|---|---|
| `ESPOS_PASSWORD` | (required) | The account's password, 8 characters or more. |
| `ESPOS_USER` | `admin` | The account's user name. |
| `ESPOS_ADDR` | `:8080` | Where the server listens. |
| `ESPOS_DATA` | `./data` | Database, files and the link-signing key. |
| `ESPOS_PUBLIC_URL` | (none) | The public address, e.g. `https://espos.example.com`. Set it behind a reverse proxy. |
| `ESPOS_PKG_CATALOG` | (none) | The package catalog the X4 is offered sleep faces and widgets from. |

## Build the S3 firmware

```sh
cd s3
pio run -e s3desk                 # both boards; flash with -t upload
make -C sim -j8                   # the simulator (after `pio pkg install -e s3desk`)
cd sdk && npm install && npm test # the SDK and the espos CLI
```

The firmware talks to `https://api.espos.dev` unless you build it with your server's address
(`ESPOS_API`, `ESPOS_WEB`): see [s3/README.md](s3/README.md#server-address).

## Build the X4 firmware

```sh
git submodule update --init --recursive
cd firmware
pio run -e default
scripts/espos_bundle.sh default ../bundle
cd ../server && ./espos firmware add ../bundle -notes "First install"
```

Then open **Thiết bị** in the web app in Chrome or Edge on a computer, plug the reader in over
USB and follow the steps: the installer backs up the reader's original firmware, then gives it
your server's address, a device token and your WiFi. See [firmware/README.md](firmware/README.md).

**Your server's domain.** The default X4 build has no server address in it (the USB installer
gives it one). To link readers with a code, without a cable, and to show your address and QR
code on the reader, build with your domain in `firmware/platformio.local.ini` (git ignores it):

```ini
[env:default]
build_src_flags =
  '-DESPOS_DEFAULT_API="https://espos.example.com"'
  ; only if the web app has a different address than the API:
  ; '-DESPOS_DEFAULT_WEB="https://app.example.com"'
```

## Development

```sh
cd server && make test                 # Go tests
cd server/web && npm run dev           # web app with hot reload against a server on :8080
cd s3 && make -C sim -j8               # S3 simulator
cd s3/site && npm install && npm run dev   # developer docs site
cd firmware && pio run -e default      # X4 firmware
```

CI builds the server, its Docker image and the X4 firmware on every push and pull request.
X4 firmware code follows CrossPoint's conventions (`firmware/.agents/rules`); run
`firmware/bin/clang-format-fix` before sending C++ changes.

## Credits and licences

- `server/`: GNU AGPL-3.0, see [server/LICENSE](server/LICENSE).
- `s3/`: Apache-2.0, see [s3/LICENSE](s3/LICENSE); vendored libraries and fonts (QuickJS-ng,
  libopus, Espressif drivers, Ubuntu and Noto fonts) are listed in
  [s3/THIRD_PARTY.md](s3/THIRD_PARTY.md).
- `firmware/`: built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
  (MIT, [firmware/LICENSE](firmware/LICENSE)) and the
  [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (MIT); EspOS's changes are Apache-2.0
  ([LICENSE](LICENSE)).
- Everything else: Apache-2.0, see [LICENSE](LICENSE) and [NOTICE](NOTICE).
- Earlier versions, up to commit `fac0c54` (2026-11-18), were published under the MIT License and
  stay under it.

Weather and place search from [Open-Meteo](https://open-meteo.com). Bundled fonts keep their own
licences (OFL, UFL). EspOS is not affiliated with Xteink or with the makers of the S3 boards.
