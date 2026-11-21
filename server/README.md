<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/logo-horizontal-white.svg">
  <img alt="EspOS" src="../assets/logo-horizontal.svg" height="40">
</picture>

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
> Chó AI trên máy S3 và EspOS Bot dùng khoá AI và giọng đọc của bạn: xem
> [cấu hình chó AI](#tiếng-việt-cấu-hình-chó-ai).

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
- **Your own apps on your S3s**: make a developer key (`POST /v1/dev-keys`, the hash of a key the client
  makes), then `espos login` and `espos upload` in the SDK (`../s3/sdk`) send a JavaScript app or a Java
  game to your own devices (`/v1/dev/*`); they install it at their next sync.

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
| `ESPOS_AI_*`, `ESPOS_STT_*`, `ESPOS_TTS_*` | (none) | The dog assistant's AI model, speech-to-text and voice: see [The dog assistant](#the-dog-assistant-ai-and-voice). |
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

## The dog assistant: AI and voice

The dog on the S3 desk and the EspOS Bot is an AI assistant: you speak (or type on the web),
the server sends the words to an AI model, and the dog answers on the screen and out loud. The
self-hosted server uses **your own** AI and voice services, set in the environment. Nothing is
sent anywhere until you set them, and you pay those services directly (most have a free tier).

One turn goes like this:

```
device ── recording (WAV) ──▶ server ──▶ [speech-to-text, optional] ──▶ AI model (one call)
device ◀── the voice (PCM, 16 kHz) ◀── speech service ◀── the dog's reply
```

There are three parts. Only the first one is required:

| Part | Variables | Without it |
|---|---|---|
| **AI model**: hears and answers | `ESPOS_AI_*` | The dog says it is too tired to answer. |
| **Speech-to-text**: turns the recording into words first | `ESPOS_STT_*` | The AI model hears the recording itself, so it must accept audio (Gemini does). |
| **Voice**: reads the reply out loud | `ESPOS_TTS_*` | The dog shows the reply on the screen only, with no sound. |

The device needs nothing new: build its firmware with this server's address
([Connecting the S3 desk](#connecting-the-s3-desk)) and pair it.

### 1. The AI model (required)

Any API that speaks OpenAI's `POST /chat/completions` works.

| Variable | Default | |
|---|---|---|
| `ESPOS_AI_KEY` | (none) | The API key, sent as `Authorization: Bearer`. |
| `ESPOS_AI_URL` | `https://generativelanguage.googleapis.com/v1beta/openai` | The API's base, the part before `/chat/completions`. |
| `ESPOS_AI_MODEL` | `gemini-3.5-flash-lite` | The model, the same one the hosted service uses. |
| `ESPOS_AI_REASONING` | `low` on Gemini, not sent elsewhere | Sent as `reasoning_effort`. `off` never sends it. Most models other than Gemini and OpenAI's reasoning models refuse it. |
| `ESPOS_AI_JSON` | `1` | The dog asks for a JSON answer (`response_format: json_object`). Set `0` for an API that refuses it; the server still reads JSON from plain text. |
| `ESPOS_AI_PROMPT` | (full) | `short`: a shorter prompt, ordered so that a model on your own machine can reuse most of the last turn's prompt from its cache (below). |
| `ESPOS_AI_TIMEOUT` | `25` | Seconds to wait for the model's answer. |

**Recommended: Google Gemini.** It has a free tier, hears recordings directly (no speech-to-text
needed) and understands Vietnamese well. Get a key at https://aistudio.google.com/apikey, then:

```sh
ESPOS_AI_KEY=AIza...your-key...
```

The rest are already the defaults. Check that the key works (this calls Google once):

```sh
curl -s https://generativelanguage.googleapis.com/v1beta/openai/chat/completions \
  -H "Authorization: Bearer $ESPOS_AI_KEY" -H "Content-Type: application/json" \
  -d '{"model":"gemini-3.5-flash-lite","messages":[{"role":"user","content":"Xin chào"}]}'
```

**Other services:**

| Service | `ESPOS_AI_URL` | `ESPOS_AI_MODEL` (example) | Hears recordings? |
|---|---|---|---|
| Google Gemini | (default) | `gemini-3.5-flash-lite`, `gemini-3.8-flash` | yes |
| OpenRouter | `https://openrouter.ai/api/v1` | a Gemini model there, e.g. `google/gemini-…` | only models that list audio input |
| OpenAI | `https://api.openai.com/v1` | `gpt-4.1-mini` | no: add speech-to-text |
| Ollama (on your own machine) | `http://localhost:11434/v1` (Docker: `http://host.docker.internal:11434/v1`) | `qwen2.5:7b` | no: add speech-to-text; leave `ESPOS_AI_KEY` empty |

Model names change often: take the current one from the service's own list. A model that
cannot hear needs speech-to-text (part 2), or every spoken question fails.

**A model on your own machine.** A small model on a laptop or a Mac mini (for example Gemma 4 E2B) reads the prompt
much more slowly than a hosted one: the dog's full prompt (about 8,000 tokens) takes 20 s or more the first time.
Set `ESPOS_AI_PROMPT=short`: the same rules in fewer words, with what changes every turn (the time, the alarms and
notes an action just changed) moved to the end, so a server with a prompt cache (llama.cpp-based servers do this)
only reads the new part of each turn. Measured with Gemma 4 E2B on an M1 Pro: a turn in about 4 s instead of
8.5 s, with as many questions answered right. Also raise `ESPOS_AI_TIMEOUT` (60): the first turn after the model
starts still reads the whole prompt.

### 2. Speech-to-text (optional)

Set it when your AI model cannot hear audio, or to save on audio tokens. Any API with OpenAI's
`POST /audio/transcriptions` works. With it set, the recording becomes words on this server and
the model only reads text. If the service fails, the model gets the recording as before.

| Variable | Default | |
|---|---|---|
| `ESPOS_STT_URL` | (none) | The API's base, the part before `/audio/transcriptions`. |
| `ESPOS_STT_KEY` | (none) | Its key. |
| `ESPOS_STT_MODEL` | `whisper-1` | The model. |
| `ESPOS_STT_LANG` | (the service guesses) | The language you speak, ISO 639-1: `vi`, `en`. Setting it is faster and more accurate. |

Groq has a free, fast Whisper (key at https://console.groq.com/keys):

```sh
ESPOS_STT_URL=https://api.groq.com/openai/v1
ESPOS_STT_KEY=gsk_...
ESPOS_STT_MODEL=whisper-large-v3-turbo
ESPOS_STT_LANG=vi
```

OpenAI: `ESPOS_STT_URL=https://api.openai.com/v1`, model `whisper-1` or `gpt-4o-mini-transcribe`.
A local [faster-whisper-server / speaches](https://github.com/speaches-ai/speaches) works the
same way at its own address.

### 3. The voice (optional)

| Variable | Default | |
|---|---|---|
| `ESPOS_TTS_URL` | (none: no voice) | An OpenAI-style base (the part before `/audio/speech`), or with `ESPOS_TTS_API=espos` the full URL of a `/v1/tts` service. |
| `ESPOS_TTS_KEY` | (none) | Its key, sent as `Authorization: Bearer`. |
| `ESPOS_TTS_API` | `openai` | `openai` or `espos` (below). |
| `ESPOS_TTS_MODEL` | `gpt-4o-mini-tts` (`vieneu-turbo` for `espos`) | The model. |
| `ESPOS_TTS_VOICE` | `alloy` (for `espos`: each dog's own voice) | The voice. With `openai`, every dog uses this one voice. |
| `ESPOS_TTS_FORMAT` | `pcm` | What to ask for: `pcm` (raw 16-bit mono) or `wav`. Use `wav` when you do not know the service's sample rate: the WAV header says it. |
| `ESPOS_TTS_RATE` | `24000` | The sample rate of a `pcm` answer (OpenAI's is 24000). |

The server converts the voice to the device's 16 kHz as it streams, so the dog starts talking
before the whole reply is read.

**Free and good at Vietnamese: Microsoft Edge voices** through
[openai-edge-tts](https://github.com/travisvn/openai-edge-tts), a small proxy you run yourself:

```sh
docker run -d --name edge-tts -p 5050:5050 -e API_KEY=any-text travisvn/openai-edge-tts:latest
```

```sh
ESPOS_TTS_URL=http://localhost:5050/v1     # Docker: http://host.docker.internal:5050/v1
ESPOS_TTS_KEY=any-text
ESPOS_TTS_MODEL=tts-1
ESPOS_TTS_VOICE=vi-VN-HoaiMyNeural         # or vi-VN-NamMinhNeural
ESPOS_TTS_FORMAT=wav
```

**OpenAI:**

```sh
ESPOS_TTS_URL=https://api.openai.com/v1
ESPOS_TTS_KEY=sk-...
ESPOS_TTS_MODEL=gpt-4o-mini-tts
ESPOS_TTS_VOICE=alloy
```

**Your own speech server (`espos`).** The hosted service's own voices answer this request; a
server of yours that does the same works here:

```
POST <ESPOS_TTS_URL>          Authorization: Bearer <ESPOS_TTS_KEY>
{"model": "...", "voice": "...", "text": "Gâu! ...", "format": "pcm", "sample_rate": 16000, "stream": true}
→ 200, raw 16-bit little-endian mono PCM at sample_rate, streamed
```

### A complete example

Gemini hears and answers, Edge reads the reply in Vietnamese. In `.env`:

```sh
ESPOS_AI_KEY=AIza...
ESPOS_TTS_URL=http://host.docker.internal:5050/v1
ESPOS_TTS_KEY=any-text
ESPOS_TTS_MODEL=tts-1
ESPOS_TTS_VOICE=vi-VN-HoaiMyNeural
ESPOS_TTS_FORMAT=wav
```

On Linux, `host.docker.internal` needs `extra_hosts: ["host.docker.internal:host-gateway"]` under
the `espos` service in `docker-compose.yml`, or run both containers on one Docker network and
use `http://edge-tts:5050/v1`. Then `docker compose up -d` again.

### Checking it without a device

Sign in, pick a paired device, ask the dog in text, then fetch its voice:

```sh
S=http://localhost:8080
T=$(curl -s $S/auth/login -H 'Content-Type: application/json' \
  -d '{"email":"admin","password":"your-password"}' | jq -r .token)
curl -s $S/v1/devices -H "Authorization: Bearer $T" | jq '.devices[] | {id, name}'
A=$(curl -s $S/v1/devices/123/assist -H "Authorization: Bearer $T" \
  -H 'Content-Type: application/json' -d '{"text":"Mấy giờ rồi?"}')
echo "$A" | jq '{reply, cid, n}'
curl -s "$S/v1/devices/123/assist/say?cid=$(echo $A | jq -r .cid)&n=$(echo $A | jq -r .n)&rate=24000" \
  -H "Authorization: Bearer $T" -o reply.pcm
ffplay -f s16le -ar 24000 -ch_layout mono reply.pcm
```

(`123` is the device id from the list. On older ffplay use `-ac 1` instead of `-ch_layout mono`.)

### When something is wrong

The server log says what failed, one line per failed call:

| Log line | Meaning |
|---|---|
| `no AI model set` | `ESPOS_AI_KEY` (or `ESPOS_AI_URL`) is missing. |
| `AI model answered 400/401/404 …` | A wrong key or model name, or an option the API refuses: try `ESPOS_AI_REASONING=off`, then `ESPOS_AI_JSON=0`. |
| `model failed: …` after a spoken question | The model cannot hear audio: use Gemini, or set speech-to-text. |
| `speech to text: …` | The speech-to-text service failed; the model got the recording instead. |
| `device_assist / say: no speech service set` | No voice is set: the dog answers on the screen only (expected). |
| `speech service answered …` | The voice service refused: check its key, model and voice. |
| `the WAV must be 16-bit mono` | Ask that service for `pcm` with the right `ESPOS_TTS_RATE`, or a mono voice. |

### Tiếng Việt: cấu hình chó AI

Chó trên máy S3 và EspOS Bot là trợ lý AI. Trên server tự host, bạn dùng **dịch vụ AI và
giọng đọc của chính bạn**, đặt bằng biến môi trường trong file `.env`. Chưa đặt thì server
không gửi gì đi đâu cả.

Có 3 phần, chỉ phần 1 là bắt buộc:

1. **Model AI** (`ESPOS_AI_*`), bắt buộc: nghe câu hỏi và trả lời. Thiếu thì chó báo "mệt quá".
2. **Chuyển giọng nói thành chữ** (`ESPOS_STT_*`), tuỳ chọn: chỉ cần khi model không nghe được file ghi âm.
3. **Giọng đọc** (`ESPOS_TTS_*`), tuỳ chọn: thiếu thì chó chỉ hiện chữ trên màn hình, không có tiếng.

**Bước 1: model AI (nên dùng Gemini, có gói miễn phí, nghe được tiếng Việt).**

1. Vào https://aistudio.google.com/apikey, đăng nhập Google, bấm **Create API key**, chép khoá.
2. Mở `.env`, thêm một dòng:
   ```sh
   ESPOS_AI_KEY=AIza...khoá-của-bạn...
   ```
   Các biến khác để mặc định: `ESPOS_AI_URL` là API Gemini, `ESPOS_AI_MODEL` là
   `gemini-3.5-flash-lite` (giống bản hosted).
3. Muốn dùng dịch vụ khác (OpenRouter, OpenAI, Ollama chạy trên máy nhà), đặt thêm
   `ESPOS_AI_URL` và `ESPOS_AI_MODEL` theo bảng "Other services" ở trên. OpenAI và Ollama
   không nghe được ghi âm, nên phải làm thêm bước 2.

**Bước 2 (tuỳ chọn): chuyển giọng nói thành chữ bằng Groq, miễn phí.**

Lấy khoá ở https://console.groq.com/keys rồi thêm vào `.env`:

```sh
ESPOS_STT_URL=https://api.groq.com/openai/v1
ESPOS_STT_KEY=gsk_...
ESPOS_STT_MODEL=whisper-large-v3-turbo
ESPOS_STT_LANG=vi
```

**Bước 3 (tuỳ chọn): giọng đọc tiếng Việt miễn phí bằng giọng Microsoft Edge.**

1. Chạy bộ chuyển đổi trên cùng máy với server:
   ```sh
   docker run -d --name edge-tts -p 5050:5050 -e API_KEY=chu-gi-cung-duoc travisvn/openai-edge-tts:latest
   ```
2. Thêm vào `.env`:
   ```sh
   ESPOS_TTS_URL=http://host.docker.internal:5050/v1
   ESPOS_TTS_KEY=chu-gi-cung-duoc
   ESPOS_TTS_MODEL=tts-1
   ESPOS_TTS_VOICE=vi-VN-HoaiMyNeural
   ESPOS_TTS_FORMAT=wav
   ```
   Giọng nữ là `vi-VN-HoaiMyNeural`, giọng nam là `vi-VN-NamMinhNeural`. Chạy server không
   qua Docker thì dùng `http://localhost:5050/v1`. Trên Linux xem ghi chú
   `host.docker.internal` ở mục "A complete example".
3. Muốn dùng OpenAI thì đặt `ESPOS_TTS_URL=https://api.openai.com/v1`, `ESPOS_TTS_KEY=sk-...`,
   `ESPOS_TTS_VOICE=alloy`.

**Bước 4: khởi động lại và thử.**

```sh
docker compose up -d        # chạy từ mã nguồn: tắt ./espos rồi chạy lại
```

Gọi chó trên máy (nói "Gâu gâu" hoặc bấm nút nói), hoặc thử bằng lệnh ở mục "Checking it
without a device". Không chạy thì xem log (`docker compose logs -f espos`) và đối chiếu bảng
"When something is wrong".

**Lưu ý:**

- Khoá API chỉ nằm trong `.env` trên máy bạn. Đừng đưa file này lên GitHub.
- Tiền dịch vụ AI và giọng đọc do bạn trả thẳng cho nhà cung cấp. Bản tự host không giới hạn
  số lượt và không có gói trả phí.
- Bản tự host không có giọng riêng của từng chú chó như bản hosted: mọi chú chó dùng chung
  giọng `ESPOS_TTS_VOICE`, trừ khi bạn có server giọng đọc riêng (`ESPOS_TTS_API=espos`).

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
and the calendar links you add. The dog assistant calls only the AI, speech-to-text and voice
services you set (`ESPOS_AI_*`, `ESPOS_STT_*`, `ESPOS_TTS_*`). The morning paper sources are fetched only while the paper is
switched on.

## How it is built

- `cmd/espos`: the binary (server and `firmware add`).
- `internal/server`: the web app, sign-in, the `/v1` API, signed file links.
- `internal/backend`: routes, tables and workflow graphs (JSON). Each route runs a workflow:
  a small graph such as http_in → code → branch → respond.
- `internal/engine`: runs those graphs and the scheduled ones (cron). `publish` (the hosted platform's
  realtime push) has no listener here and only lets the run go on: clients see the change at their next sync.
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
  The dog assistant is imported with two changes: its AI model call reads `ESPOS_AI_*` (and,
  with `ESPOS_STT_URL` set, the recording is turned into words first), and its requests to the
  hosted speech service become `tts` nodes that read `ESPOS_TTS_*` and fall back to a text-only
  answer (`tools/importer/assistant.go`). A workflow that needs any other hosted secret is left out.
  The hosted service's administration (`admin_*`: users, roles, licences, trials, device stock) is
  left out as well, and its guards are dropped from the routes: here nothing is blocked or
  licensed, and a device that sees no licence in its sync locks nothing.

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
