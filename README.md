<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo-horizontal-white.svg">
    <img alt="EspOS" src="assets/logo-horizontal.svg" height="64">
  </picture>
</h1>

EspOS is an open operating system for ESP32-S3 devices:

- **The S3 desks**: colour touch displays (the 3.5" ES3C35P and the 2.8" ES3C28P; one firmware
  finds its board). A desk OS with Home pages of apps and widgets, a saver, notifications, themes,
  languages, a dog assistant you call by voice, and a store of apps installed without new firmware
  (JavaScript apps, J2ME phone games, widgets).
- **The EspOS Bot**: a small ESP32-S3 board with a 0.96" OLED, a microphone and a speaker: the dog
  assistant in your hand.

They sync with an EspOS server: the hosted one at **[espos.dev](https://espos.dev)**, or your own,
self-hosted from this repository (one Go binary, one account, your data in one folder).

Docs for app developers and board porters: **[espos.dev/docs](https://espos.dev/docs)**.

> **Tiếng Việt:** EspOS là hệ điều hành mở cho thiết bị ESP32-S3: màn cảm ứng màu (màn chính
> ghép ứng dụng và widget, màn chờ, thông báo, giao diện, ngôn ngữ, chó trợ lý gọi bằng giọng
> nói, kho ứng dụng cài không cần nạp lại firmware) và EspOS Bot (màn OLED nhỏ, có mic và loa).
> Máy đồng bộ với máy chủ EspOS: bản hosted ở espos.dev hoặc tự host từ repo này.

Support for the Xteink X4 e-ink reader is paused: its firmware is in this repository's history up
to commit `9059678` (folder `firmware/`).

## What is in this repository

| Folder | | Licence |
|---|---|---|
| [`s3/`](s3/README.md) | The ESP32-S3 firmware for the desks and the EspOS Bot, their simulators (host build and QEMU), tools, the SDK (`espos` CLI, typings, schemas), sample apps, translations, the wake word's training code and the developer docs site. | Apache-2.0 |
| [`server/`](server/README.md) | The self-hosted server: Go API, the React web app (embedded in the binary), SQLite storage, background jobs (weather, calendars, market prices, news), the dog assistant with your own AI and voice services. | AGPL-3.0 |
| [`ai-kit/`](ai-kit/README.md) | The AI kit: the dog assistant's model, speech-to-text and voices on your own Mac (llama.cpp, whisper.cpp, VieNeu-TTS and Piper behind one gateway), for the self-hosted server. | AGPL-3.0 |
| [`docs/device-api.md`](docs/device-api.md) | The contract between the devices and the server. | Apache-2.0 |

## Self-hosting quick start

Docker, or Go 1.26+ and Node 22+:

```sh
cd server
cp .env.example .env            # set ESPOS_PASSWORD
mkdir -p data && sudo chown 10001 data
docker compose up -d --build    # or: make && ESPOS_PASSWORD='…' ./espos
```

Open http://localhost:8080 and sign in. Settings, backups, the dog assistant's AI and voice, and
how devices connect: [server/README.md](server/README.md).

| Variable | Default | |
|---|---|---|
| `ESPOS_PASSWORD` | (required) | The account's password, 8 characters or more. |
| `ESPOS_USER` | `admin` | The account's user name. |
| `ESPOS_ADDR` | `:8080` | Where the server listens. |
| `ESPOS_DATA` | `./data` | Database, files and the link-signing key. |
| `ESPOS_PUBLIC_URL` | (none) | The public address, e.g. `https://espos.example.com`. Set it behind a reverse proxy. |
| `ESPOS_AI_*`, `ESPOS_STT_*`, `ESPOS_TTS_*` | (none) | The dog assistant's AI model, speech-to-text and voice. |

## Build the firmware

```sh
cd s3
pio run -e s3desk                 # the desks (both boards); flash with -t upload
pio run -e bot128                 # the EspOS Bot
make -C sim -j8                   # the desk simulator (after `pio pkg install -e s3desk`)
cd sdk && npm install && npm test # the SDK and the espos CLI
```

The firmware talks to `https://api.espos.dev` unless you build it with your server's address
(`ESPOS_API`, `ESPOS_WEB`): see [s3/README.md](s3/README.md#server-address).

## Development

```sh
cd server && make test                     # Go tests
cd server/web && npm run dev               # web app with hot reload against a server on :8080
cd s3 && make -C sim -j8                   # desk simulator
cd s3/site && npm install && npm run dev   # developer docs site
```

CI builds the server and its Docker image on every push and pull request.

## Credits and licences

- `s3/`: Apache-2.0, see [s3/LICENSE](s3/LICENSE); vendored libraries and fonts (QuickJS-ng,
  libopus, Espressif drivers, Ubuntu and Noto fonts) are listed in
  [s3/THIRD_PARTY.md](s3/THIRD_PARTY.md).
- `server/`: GNU AGPL-3.0, see [server/LICENSE](server/LICENSE).
- `ai-kit/`: GNU AGPL-3.0, see [ai-kit/LICENSE](ai-kit/LICENSE); what it downloads keeps its own licence
  ([ai-kit/THIRD_PARTY.md](ai-kit/THIRD_PARTY.md)).
- Everything else: Apache-2.0, see [LICENSE](LICENSE) and [NOTICE](NOTICE).
- Earlier versions, up to commit `fac0c54` (2026-11-18), were published under the MIT License and
  stay under it.

Weather and place search from [Open-Meteo](https://open-meteo.com). Bundled fonts keep their own
licences (OFL, UFL). EspOS is not affiliated with the makers of the boards it runs on.
