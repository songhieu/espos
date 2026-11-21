<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/logo-horizontal-white.svg">
  <img alt="EspOS" src="../assets/logo-horizontal.svg" height="40">
</picture>

# EspOS server (self-hosted)

The server side of [EspOS](../README.md): the web app and the API the ESP32-S3 desks and the
EspOS Bot talk to. One Go binary, one account, all data in one folder. No cloud service, no
sign-up. Their firmware is in [../s3](../s3/README.md).

> **Tiếng Việt:** Máy chủ EspOS tự host cho máy S3 (màn cảm ứng màu) và EspOS Bot. Một file
> chạy duy nhất, một tài khoản (đặt bằng `ESPOS_USER` / `ESPOS_PASSWORD`), dữ liệu nằm trong một
> thư mục: ghép máy bằng mã, đồng bộ cài đặt hai chiều, thời tiết, lịch âm dương, lịch
> Google/iCloud, tin tức VnExpress, giá vàng, chứng khoán, tỷ giá, tiền số, ảnh, thông báo qua
> API, ứng dụng của bạn và cập nhật firmware qua WiFi. Xem "Quick start" bên dưới.
> Chó AI trên máy S3 và EspOS Bot dùng khoá AI và giọng đọc của bạn: xem
> [cấu hình chó AI](#tiếng-việt-cấu-hình-chó-ai).

## Features

- **Pairing with a code**: a desk or a Bot shows a code; enter it on the web and it is linked.
- **Settings both ways**: change them on the web or on the device; the latest change wins.
- **Data for the widgets**: weather (Open-Meteo), solar and lunar date, calendar links (Google,
  iCloud), news from VnExpress with pictures, gold, stocks, exchange rates and crypto, your photos.
  The server fetches and caches them; devices never call outside sources.
- **Notifications**: `POST /v1/notify` with a key puts a line on your devices (CI, Home Assistant,
  scripts).
- **Apps**: this server has no store catalog of its own yet: apps go onto a device from a memory card
  or as your own apps (next line), and a new upload updates them.
- **Your own apps on your devices**: make a developer key (`POST /v1/dev-keys`, the hash of a key
  the client makes), then `espos login` and `espos upload` in the SDK (`../s3/sdk`) send a
  JavaScript app or a Java game to your own devices (`/v1/dev/*`); they install it at their next sync.
- **The dog assistant** with your own AI, speech-to-text and voice services (below).
- **Firmware updates over WiFi**: add a release with `./espos firmware add`; devices are offered it.

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
| `ESPOS_PUBLIC_URL` | (none) | The address the browser and the devices use, e.g. `https://espos.example.com`. Set it behind a reverse proxy. Without it the server uses the Host of each request. |
| `ESPOS_AI_*`, `ESPOS_STT_*`, `ESPOS_TTS_*` | (none) | The dog assistant's AI model, speech-to-text and voice: see [The dog assistant](#the-dog-assistant-ai-and-voice). |
| `ESPOS_PKG_CATALOG` | (none) | Store catalogs (`apps-s3/catalog.json`, the v2 `apps/catalog.json`), separated by commas. Every 15 minutes the server reads the apps' settings schemas from them and checks the apps' settings against those; e-ink readers (paused) are also offered the v2 one. Without it, only your own uploads have settings. |

## Connecting a desk or a Bot

A desk or a Bot (firmware in [../s3](../s3/README.md)) pairs with a code: build its firmware with
this server's address (`ESPOS_API` and `ESPOS_WEB`, see the
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

Any API that speaks OpenAI's `POST /chat/completions` works. Providers differ in small ways (one
refuses JSON mode, another a temperature, a third thinks until the answer is cut), so
`ESPOS_AI_PROVIDER` sets the right values for each; any variable you set yourself wins.

| `ESPOS_AI_PROVIDER` | Address and default model | What the preset sets |
|---|---|---|
| `gemini` (the default) | Gemini's API, `gemini-3.5-flash-lite` | JSON mode, `reasoning_effort: low`, 2048 tokens (thinking counts), hears recordings |
| `openai` | `https://api.openai.com/v1`, `gpt-5.4-mini` | JSON mode, no temperature, `max_completion_tokens` 2048 |
| `anthropic` | `https://api.anthropic.com/v1`, `claude-haiku-4-5-20251001` | the answer's JSON schema (it refuses JSON mode), no temperature, 2048 tokens |
| `groq` | `https://api.groq.com/openai/v1`, `openai/gpt-oss-20b` | JSON mode, `reasoning_effort: low`, `max_completion_tokens` 2048 |
| `deepseek` | `https://api.deepseek.com`, `deepseek-flash` | JSON mode, `reasoning_effort: none` (DeepSeek thinks unless told not to) |
| `mistral` | `https://api.mistral.ai/v1`, `mistral-small-latest` | JSON mode |
| `xai` | `https://api.x.ai/v1`, `grok-4.3` | JSON mode, `reasoning_effort: none`, `max_completion_tokens` 2048 |
| `openrouter` | `https://openrouter.ai/api/v1`, `google/gemini-3.5-flash-lite` | JSON mode, 2048 tokens, hears recordings (that model does) |
| `azure` | set `ESPOS_AI_URL=https://<resource>.openai.azure.com/openai/v1` and `ESPOS_AI_MODEL=<deployment>` | JSON mode, no temperature, `max_completion_tokens` 2048 |
| `local` | `http://127.0.0.1:8080/v1` (the AI kit, `../ai-kit`), `gemma-4-e2b` | JSON mode, the short prompt, 60 s timeout, no key |

Only `gemini` and `openrouter` hear a recording; with every other preset a spoken question goes
through speech-to-text (part 2) and never reaches the model as audio. Model names change often:
take the current one from the provider's list.

| Variable | Default | |
|---|---|---|
| `ESPOS_AI_PROVIDER` | `gemini` when `ESPOS_AI_URL` is unset or Gemini's, else none | A preset (table above). |
| `ESPOS_AI_KEY` | (none) | The API key, sent as `Authorization: Bearer`. |
| `ESPOS_AI_URL` | the preset's | The API's base, the part before `/chat/completions`. |
| `ESPOS_AI_MODEL` | the preset's (`gemini-3.5-flash-lite`) | The model; on Azure, the deployment's name. |
| `ESPOS_AI_JSON` | the preset's (`object`) | `object` (or `1`): `response_format: json_object`. `schema`: the answer's JSON schema, strict. `off` (or `0`): none; the server still reads JSON from plain text. |
| `ESPOS_AI_REASONING` | the preset's | Sent as `reasoning_effort` (`none`, `minimal`, `low`...). `off` never sends it. |
| `ESPOS_AI_TEMPERATURE` | the dog's (0.7, 0.4 on a retry) | A temperature of your own, or `off` to send none (reasoning models, Claude 4.7 and later). |
| `ESPOS_AI_TOKENS_PARAM` | `max_tokens` | `max_completion_tokens` for APIs that want that name (OpenAI's reasoning models). |
| `ESPOS_AI_MAX_TOKENS` | the preset's (the dog asks 700) | The answer's limit. A model that thinks spends part of it thinking: an answer cut at the limit is named in the log. |
| `ESPOS_AI_AUDIO` | the preset's (`1` without one) | `1`: the model hears a recording itself. `0`: it cannot; a spoken question needs speech-to-text. |
| `ESPOS_AI_PROMPT` | (full) | `short`: a shorter prompt, ordered so that a model on your own machine can reuse most of the last turn's prompt from its cache (below). |
| `ESPOS_AI_TIMEOUT` | `25` (`60` for `local`) | Seconds to wait for the model's answer. |

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
| Service | Settings |
|---|---|
| A provider above | `ESPOS_AI_PROVIDER=openai` (and so on) and `ESPOS_AI_KEY` |
| The EspOS AI kit (Gemma 4, Whisper and voices on your Mac, `../ai-kit`) | `ESPOS_AI_PROVIDER=local`, `ESPOS_AI_KEY` = a key from its `config.yaml` |
| Ollama or another local server | `ESPOS_AI_PROVIDER=local`, `ESPOS_AI_URL=http://localhost:11434/v1` (Docker: `http://host.docker.internal:11434/v1`), `ESPOS_AI_MODEL=` one of its models |
| Any other OpenAI-style API | `ESPOS_AI_URL`, `ESPOS_AI_KEY`, `ESPOS_AI_MODEL`, and the variables above for its quirks |

**A model on your own machine.** A small model on a laptop or a Mac mini (for example Gemma 4 E2B) reads the prompt
much more slowly than a hosted one: the dog's full prompt (about 8,000 tokens) takes 20 s or more the first time.
Set `ESPOS_AI_PROMPT=short`: the same rules in fewer words, with what changes every turn (the time, the alarms and
notes an action just changed) moved to the end, so a server with a prompt cache (llama.cpp-based servers do this)
only reads the new part of each turn. Measured with Gemma 4 E2B on an M1 Pro: a turn in about 4 s instead of
8.5 s, with as many questions answered right. Also raise `ESPOS_AI_TIMEOUT` (60): the first turn after the model
starts still reads the whole prompt.

### 2. Speech-to-text (optional)

Set it when your AI model cannot hear audio, or to save on audio tokens. Any API with OpenAI's
`POST /audio/transcriptions` works, and ElevenLabs, Google Cloud and Azure have their own adapters.
With it set, the recording becomes words on this server and the model only reads text. If the
service fails, the model gets the recording as before.

| Variable | Default | |
|---|---|---|
| `ESPOS_STT_API` | `openai` | `openai` (any `/audio/transcriptions`: OpenAI, Groq, a local Whisper), `elevenlabs`, `google` or `azure`. |
| `ESPOS_STT_URL` | (none) | `openai`: the API's base, the part before `/audio/transcriptions`. The others: optional, the vendor's own address is used. |
| `ESPOS_STT_KEY` | (none) | Its key: Bearer (`openai`), `xi-api-key` (`elevenlabs`), `X-Goog-Api-Key` (`google`), `Ocp-Apim-Subscription-Key` (`azure`). |
| `ESPOS_STT_MODEL` | `whisper-1` (`scribe_v2` for `elevenlabs`) | The model. Google and Azure take none. |
| `ESPOS_STT_LANG` | each device's language | The language you speak, ISO 639-1 (`en`, `vi`, `de`...), for every device. Unset: each device's own language (its Settings) is the hint; without one, OpenAI-style APIs and ElevenLabs guess, and Google and Azure use `en-US`. |
| `ESPOS_STT_REGION` | (none) | `azure` only: the Speech resource's region (`southeastasia`, `eastus`...), unless `ESPOS_STT_URL` is set. |

Groq has a free, fast Whisper (key at https://console.groq.com/keys):

```sh
ESPOS_STT_URL=https://api.groq.com/openai/v1
ESPOS_STT_KEY=gsk_...
ESPOS_STT_MODEL=whisper-large-v3-turbo
```

OpenAI: `ESPOS_STT_URL=https://api.openai.com/v1`, model `whisper-1` or `gpt-4o-mini-transcribe`.
A local Whisper (the AI kit's, `../ai-kit`, or [speaches](https://github.com/speaches-ai/speaches))
works the same way at its own address.

```sh
# ElevenLabs Scribe
ESPOS_STT_API=elevenlabs
ESPOS_STT_KEY=...your-xi-api-key...
# Google Cloud Speech-to-Text (an API key with the Speech-to-Text API enabled)
ESPOS_STT_API=google
ESPOS_STT_KEY=AIza...
# Azure AI Speech (short-audio REST API)
ESPOS_STT_API=azure
ESPOS_STT_KEY=...your-speech-key...
ESPOS_STT_REGION=southeastasia
```

### 3. The voice (optional)

| Variable | Default | |
|---|---|---|
| `ESPOS_TTS_API` | `openai` | `openai`, `espos` (below), `elevenlabs`, `google` or `azure`. |
| `ESPOS_TTS_URL` | (none: no voice) | `openai`: the base, the part before `/audio/speech`. `espos`: the full URL of a `/v1/tts` service. `elevenlabs`, `google`, `azure`: optional, the vendor's own address is used. |
| `ESPOS_TTS_KEY` | (none) | Its key: Bearer (`openai`, `espos`), `xi-api-key` (`elevenlabs`), `X-Goog-Api-Key` (`google`), `Ocp-Apim-Subscription-Key` (`azure`). |
| `ESPOS_TTS_MODEL` | `gpt-4o-mini-tts`; `vieneu-turbo` (`espos`); `eleven_flash_v2_5` (`elevenlabs`) | The model. Google and Azure take none. |
| `ESPOS_TTS_VOICE` | `alloy`; each dog's own voice (`espos`); none (`elevenlabs`: required); a voice for the reply's language (`google`, `azure`) | The voice. Except with `espos`, every dog uses this one voice. A Google or Azure voice name carries its language (`en-US-...`). |
| `ESPOS_TTS_LANG` | (guessed from the reply) | The language the dog speaks, `en`, `vi`, `de`...: it picks the Google and Azure voice and ElevenLabs' `language_code`. Unset, the reply's letters decide; English when nothing marks another language. |
| `ESPOS_TTS_REGION` | (none) | `azure` only: the Speech resource's region, unless `ESPOS_TTS_URL` is set. |
| `ESPOS_TTS_FORMAT` | `pcm` | `openai` only: `pcm` (raw 16-bit mono) or `wav`. Use `wav` when you do not know the service's sample rate: the WAV header says it. |
| `ESPOS_TTS_RATE` | `24000` | `openai` only: the sample rate of a `pcm` answer (OpenAI's is 24000). |

The device plays 16-bit mono PCM at 16 kHz. ElevenLabs and Azure are asked for raw PCM at that
rate and Google for LINEAR16 at that rate; anything else is converted as it streams, so the dog
starts talking before the whole reply is read.

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

**ElevenLabs** (16-bit PCM at 16 kHz; `eleven_flash_v2_5` speaks 32 languages):

```sh
ESPOS_TTS_API=elevenlabs
ESPOS_TTS_KEY=...your-xi-api-key...
ESPOS_TTS_MODEL=eleven_flash_v2_5
ESPOS_TTS_VOICE=...a-voice-id...            # required: a voice from your ElevenLabs voices (each account has its own)
```

**Google Cloud Text-to-Speech** (an API key with the Text-to-Speech API enabled):

```sh
ESPOS_TTS_API=google
ESPOS_TTS_KEY=AIza...
# ESPOS_TTS_VOICE=en-US-Neural2-F          # optional: unset, en-US-Neural2-F for English, vi-VN-Neural2-A for
                                           # Vietnamese, and Google's own pick for other languages
```

**Azure AI Speech** (raw-16khz-16bit-mono-pcm):

```sh
ESPOS_TTS_API=azure
ESPOS_TTS_KEY=...your-speech-key...
ESPOS_TTS_REGION=southeastasia             # the region of your Speech resource
# ESPOS_TTS_VOICE=en-US-JennyNeural        # optional: unset, vi-VN-HoaiMyNeural for Vietnamese and the
                                           # multilingual en-US-AvaMultilingualNeural for every other language
```

**Your own speech server (`espos`).** The hosted service's own voices answer this request, and so
does the AI kit in `../ai-kit` (VieNeu-TTS and Piper on a Mac); a server of yours that does the same
works here:

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
| `speech service answered …` | The voice service refused: check its key, model, voice (and region for Azure). |
| `azure speech: set ESPOS_TTS_REGION` | Azure needs the Speech resource's region (or `ESPOS_TTS_URL`). |
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
3. Muốn dùng dịch vụ khác, đặt `ESPOS_AI_PROVIDER` (`openai`, `anthropic`, `groq`, `deepseek`,
   `mistral`, `xai`, `openrouter`, `azure`, `local`) và khoá của dịch vụ đó: server tự đặt địa chỉ,
   model và các chỉnh riêng của từng bên (bảng `ESPOS_AI_PROVIDER` ở trên). Trừ Gemini và
   OpenRouter, các dịch vụ khác không nghe được ghi âm, nên phải làm thêm bước 2.
   Chạy hết trên máy Mac ở nhà (Gemma 4, Whisper, VieNeu): xem `../ai-kit`.

**Bước 2 (tuỳ chọn): chuyển giọng nói thành chữ bằng Groq, miễn phí.**

Lấy khoá ở https://console.groq.com/keys rồi thêm vào `.env`:

```sh
ESPOS_STT_URL=https://api.groq.com/openai/v1
ESPOS_STT_KEY=gsk_...
ESPOS_STT_MODEL=whisper-large-v3-turbo
```

Không cần đặt ngôn ngữ: server lấy ngôn ngữ của từng máy (trong Cài đặt) làm gợi ý.
Cũng dùng được ElevenLabs, Google Cloud hay Azure (`ESPOS_STT_API`, xem mục 2 ở trên).

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
   `ESPOS_TTS_VOICE=alloy`. ElevenLabs, Google Cloud, Azure: `ESPOS_TTS_API=elevenlabs|google|azure`
   và khoá (mục 3 ở trên); Google và Azure tự chọn giọng theo ngôn ngữ của câu trả lời.

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
- `files/`: pictures, packages and the news pictures.
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
- `internal/engine`: runs those graphs and the scheduled ones (cron). There is no realtime stream here:
  `publish` (the hosted platform's push) only lets the run go on, and `stream_token` fails into its error
  edge; clients see changes at their next sync or poll.
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
  Dictation (`/v1/device/dictate`) and the sessions app's AI turns use the same `ESPOS_AI_*` model, which
  must hear audio for dictation.
  The hosted service's administration (`admin_*`: users, roles, licences, trials, device stock) is
  left out as well, and its guards are dropped from the routes: here nothing is blocked or
  licensed, and a device that sees no licence in its sync locks nothing. The licence check other
  workflows call (`admin_lic_core`) is replaced by a stand-in that allows the AI; a route whose own
  guard is left out is left out with it, never left open. EspOS Desktop (the Mac companion) is not
  public yet: its routes are imported but answer `desktop_disabled` here.

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
