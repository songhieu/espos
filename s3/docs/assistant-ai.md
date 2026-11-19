# The dog assistant (AI): protocol for the firmware

The person holds a button (or says the wake word "gâu gâu"), speaks, lets go; their chosen PawBoop dog answers out
loud, in its own character, about their own data (gold, coins, stocks, FX, news, weather, calendar, alarms,
reminders, notes, reading), and does things for them: sets, changes and deletes alarms, adds and edits notes and
reminders, changes a few settings (volume, brightness, Do Not Disturb, theme, saver, city, quiet hours, language).
Anything else it turns down, in its own way. It calls the person by the name they gave (Tài khoản on the web, or
told to a dog), remembers important things, and a conversation keeps its thread. Server side:
`web/backend/workflows/assist_reply` + `assist_apply` (2026-11-18). This page is what the device does.

```
 hold button      release                      ONE request: the answer IS the voice
 ──record──▶ POST /v1/device/assist?stream=1 ─────────────────────────────────────────▶ speaker
             (WAV 16 kHz mono)        headers first: X-Assist (heard, reply, mood, cid, n…), X-Sample-Rate
                                      then the body: 16-bit mono PCM, streamed while the TTS speaks
```

**Use the streamed answer** (`?stream=1`, since 2026-11-18): one request, the dog's words and mood in the
`X-Assist` header (draw them the moment the headers arrive), then its voice as the body. It saves the second
round trip of the older two-step way (about 1-1.5 s). The two-step way still works and is the fallback:
`POST /v1/device/assist` without `stream` answers JSON, then `GET /v1/device/assist/say` streams the voice (§3).
Every request carries the device's token (`Authorization: Device <token>`, as `/v1/device/sync`); keep the TLS
connection to `api.espos.dev` open.

## 1. Recording (push to talk)

| | |
|---|---|
| Format | 16-bit signed little-endian PCM, mono, **16 kHz** (8-48 kHz accepted; more only costs upload) |
| Length | 0.3 s to 20 s (the server refuses outside). Stop recording at 12 s: the dog answers short questions |
| Container | a RIFF WAV with the usual 44-byte header (`Content-Type: audio/wav`), or raw samples (`Content-Type: audio/L16`, rate in `?rate=16000`). A header written before the length is known (size `0xFFFFFFFF`) is fine |
| Level | remove the DC offset of the I2S microphone; no AGC needed; do not clip. Leading and trailing silence may be trimmed (smaller upload) but is not required |
| Size | 16 kHz x 2 bytes = 32 KB a second; a 4 s question is 128 KB |

Open the TLS connection when the button goes down (or keep the sync's one), so the upload starts the moment it
is released.

### 1b. A turn started by the wake word

When the wake-word detector ("gâu gâu") fires, the device starts a turn by itself:

- Record from **300 ms before** the detection (a pre-roll ring buffer) so the start of the sentence is not cut;
  the wake words themselves may stay in the recording.
- Stop on **end of speech**: about 800 ms below the noise floor + 6 dB after speech was heard, or **8 s** in all;
  if nothing above the floor came within 4 s of the wake word, drop the turn without sending anything.
- Send it like a push-to-talk turn with `wake=1` in the query. The server then removes "gâu gâu" from what it
  heard, and when nothing else was said answers `{"silent": true, "reply": ""}`: say nothing, keep nothing (a
  false wake: a dog on the TV).
- While recording: the `alert` clip (ears up), as for push to talk; the same thinking/answering states after.

## 2. The question: `POST /v1/device/assist`

```
POST https://api.espos.dev/v1/device/assist?cid=<cid>&dog=<key>
Authorization: Device <token>
Content-Type: audio/wav
Content-Length: <bytes>

<the WAV>
```

- `cid` (optional): the conversation to continue: the `cid` of the last answer. Without it the server continues
  this device's conversation of the last 30 minutes, if any. `cid=new` starts a new conversation (a "new topic"
  button, or after the dog was changed if you want it to start fresh).
- `dog` (optional): the chosen dog's key (`bao momo boop leaf volt byte shiba corgi husky golden`). Without it the
  server uses the device's `s3.pet_dog` (synced both ways since 2026-11-18, checked like the web's save), then
  `corgi`. Send it when the dog in use differs from the setting (`s3.pet_dog` "" = the first dog installed, which
  the server cannot know).
- `s3.assist` (the assistant's switches, `{"on","greet","weather","alarm","meal"}` 0/1 and `{"water","move","eyes"}`
  0-600 minutes) is synced and checked the same way; it does not change the AI turn.
- `wake=1`: the turn began with the wake word (1b).
- `stream=1` (or `Accept: audio/pcm`): answer with the voice (2b). `say_rate=` its sample rate (16000; 8000-48000).
- Text instead of audio (tests, a keyboard): `Content-Type: application/json`, `{"text": "…", "cid": "…", "dog": "…"}`.
- Rate limit: 20 questions a minute per device (429 past it).

Answer `200`, JSON (about 0.5-1 KB):

```json
{
  "heard": "Chú Bao ơi, giá vàng SJC hôm nay bao nhiêu vậy chú?",
  "reply": "Vàng SJC bán 137,5 triệu, mua 134,5 triệu đó bây. Giá lúc 11 giờ đêm qua nghen.",
  "mood": "stand",
  "topic": "gold",
  "cid": "7f3a9c0d12e45b68",
  "n": 3,
  "dog": "bao", "dog_name": "Bao", "voice": "adam",
  "remembered": [], "forgot": 0,
  "ms": {"db": 120, "weather_fetch": 0, "llm": 1450, "save": 30, "total": 1620},
  "tokens": {"in": 4100, "out": 70}
}
```

| Field | Use |
|---|---|
| `heard` | what the dog understood (show it small above the reply). `""` = it heard nothing clear; `reply` then asks to say it again |
| `reply` | the bubble text: 1-3 short sentences, Vietnamese unless the person spoke another language. Numbers are written Vietnamese style (137,5; 64.652) |
| `mood` | the pet clip to play while it talks: one of `happy giggle high_five levelup plead sulk yuck alert look_up stand no_tired sleep` (all are moments of every dog's pet pack) |
| `topic` | `gold crypto stock fx news weather time calendar alarm todo note reading memory chat refuse`: a card to show beside the bubble if you want one (`refuse` = the question was out of scope) |
| `cid`, `n` | keep `cid` for the next question; `cid` + `n` fetch this reply's voice |
| `remembered`, `forgot` | facts saved / removed in this turn ("Bao đã nhớ: …" toast, optional) |
| `done` | what the dog did for the person, e.g. `["đặt báo thức 06:30 một lần"]` (empty: nothing). The change is already saved for this device as a web save is: the device gets it with its next ping/sync (within about a minute); a device that wants it at once runs a sync now |
| `saved` | present when the dog tried to do something: `true` done, `false` refused by the settings check (`error` says why, `reply` is the dog owning up) |
| `asks_confirm` | the dog asked "xóa hết luôn hả?": nothing was deleted yet; the next question ("ừ", "xóa đi") does it |
| `name_saved` | the person's name went to the account (Tài khoản › Tên của bạn) |
| `silent` | a wake-word turn with nothing after the wake word: show and say nothing |
| `ms` | server timings, for the dev console |

## 2b. The streamed answer (`?stream=1`): the faster path

```
POST https://api.espos.dev/v1/device/assist?stream=1&cid=<cid>&dog=<key>
Authorization: Device <token>
Content-Type: audio/wav

<the WAV>
```

Read the answer's `Content-Type` first (the body's pieces are not sample-aligned either: §3's rule applies):

- `audio/pcm` (status 200): the dog answered. Headers: `X-Sample-Rate` (16000 unless `say_rate`), `X-Assist` = the
  answer's JSON (the same fields as §2: heard, reply, mood, topic, cid, n, dog, dog_name, voice, done, saved,
  asks_confirm, name_saved, remembered, forgot, ms) in standard base64 (decode, then parse; about 1-1.5 KB). Show
  the bubble and start the mood clip at once, then play the body as §3 says (buffer about 1 s, chunked, the end of
  the body is the end of the speech).
- `application/json`: no voice this time; it is §2's JSON or an error of §4. Same handling as the two-step way.
  This is what comes back when the dog has nothing to say (`silent`, a wake word with nothing after it), when the
  model failed (502 `ai_failed` + the recorded clip to play), and for every 4xx.
- A 502 JSON `{"error", "upstream_status"}` with no `X-Assist`: the TTS refused AFTER the turn was saved. Ask
  `GET /v1/device/assist/say?n=last&format=json` (§3) for the words, show them, and try the voice once with
  `GET /v1/device/assist/say?n=last`.
- The connection broke during the voice: the words are already in `X-Assist`; `GET /say?cid=<cid>&n=<n>` plays it
  again if wanted.

## 3. The voice: `GET /v1/device/assist/say`

```
GET https://api.espos.dev/v1/device/assist/say?cid=<cid>&n=<n>&rate=16000
Authorization: Device <token>
```

The body is raw 16-bit little-endian mono PCM at `rate` (default 16000; 8000-48000 accepted), sent chunked as the
TTS speaks it (`Content-Type: audio/pcm`, header `X-Sample-Rate`). No length is known in advance; the end of the
body is the end of the speech. A 3-sentence reply is 6-10 s of sound, 200-320 KB.

- **Chunks are not sample-aligned.** The voice reaches the device in pieces cut wherever the network cut them;
  through Cloudflare most pieces have an ODD number of bytes (measured 2026-11-18: 62 of 93 pieces). A 16-bit
  sample can be split across two reads. Never decode a read on its own: keep the odd last byte of a read and put it
  in front of the next one, decode only whole 2-byte little-endian samples, and drop a lone byte left at the very
  end. Decoding each piece alone shifts every later sample by one byte: the speech becomes white noise (spectral
  flatness 0.84 instead of 0.11 on the same bytes). The web player does exactly this (`web/app/src/n2/pcm.ts`,
  tested by `web/scripts/test-pcm.sh` on 500 random splits).
- Play it as it arrives through `sound::stream` (as the pet voice does): read into a ring of about 1 s in PSRAM,
  start playing once about 1 s is buffered (the TTS can run slower than real time when its machine is busy, §9), then keep reading. Underrun: pause the dog's talking animation, keep
  waiting while bytes come.
- The same `cid` + `n` can be fetched again ("Nói lại").
- `n=last`: the last turn of `cid`, or without `cid` the last turn of this device's newest conversation.
  `format=json`: that turn as JSON `{heard, reply, mood, cid, n, dog, dog_name, voice}` instead of its voice.
- The voice is the dog's own (table below); the text is the `reply` with `[cười]` / `[thở dài]` cues that make the
  voice laugh or sigh (the bubble text has them removed).

## 4. Timeouts and errors

| Step | Timeout | |
|---|---|---|
| connect + TLS | 6 s | |
| `POST /assist`: upload, then the answer | 25 s from the end of the upload | typical 1.5-3 s; the server retries the model once |
| `GET /say`: first byte | 10 s | the TTS has 2 workers; a busy one queues |
| `GET /say`: between bytes | 6 s | then stop, keep the bubble |

| Answer | Meaning | What the device does |
|---|---|---|
| 401 `unauthorized` | token revoked | as sync: show "Ghép lại máy" |
| 413 `audio_too_long`, 422 `audio_too_short`, 422 `empty` | recording outside 0.3-20 s | play the dog's own recorded line for a repeat (e.g. its `alert` clip) and show "Nói lại nha" |
| 415 `bad_wav`, `wav_not_pcm16_mono`, `bad_rate` | wrong format | firmware bug: log it |
| 429 | over 20 questions a minute | the dog's `no_tired` clip, "Đợi chút rồi hỏi tiếp nha" |
| 502 `ai_failed` + `reply`, `mood`, `clip` | the model failed twice | show `reply` and play the recorded clip named in `clip` (`no_tired_2`) from the dog's own voice pack: that clip says exactly `reply` |
| 500 | server error | as 502 without the body: the dog's `no_tired` clip |
| `say`: 404 `not_found` | the conversation was deleted (1 day old, or the person cleared it) | keep the bubble, no sound |
| `say`: 502 | the TTS refused or was busy | retry once after 1 s; then keep the bubble, no sound |

## 5. What the dog shows

| State | Dog | Screen |
|---|---|---|
| idle | its normal pet behaviour | a mic button |
| listening (button held) | `alert` (ears up), looped | a level meter from the mic; release to send; under 0.3 s = cancel |
| thinking (upload + until the JSON) | `look_up`, looped (or `stand`) | "Đang nghĩ" under the dog; tap to cancel (close the connection) |
| answering | the answer's `mood` clip, looped while sound plays | the bubble: `reply`; `heard` small above it |
| done | back to idle 2 s after the sound ends | the bubble stays until the next question or 10 s |

Never speak a recorded pet line over the AI voice (`pet::voice::hush()` first). Keep the thinking clip moving: the
wait is the part the person feels.

## 6. What the dog knows and refuses

The server puts the person's own data into the model's prompt every turn (one database round trip): gold (PNJ
board), crypto (BTC, ETH, BNB, SOL, XRP, DOGE, TON), VN indexes and the cached stocks, Vietcombank FX, the news
sections they follow (titles), the weather of the device's city (`dash.city`, the dashboards' cache; refreshed when
over 3 hours old), the device's calendar cache (7 days), `s3.alarms` (and the next ring), `s3.todo`, `s3.notes`
(titles), reading on their EspOS readers, today's date and lunar date, the time in `sys.tz_min`. It must say when the
numbers were updated and never use a number that is not there. General knowledge, homework, translation, code,
politics, religion, medical/legal/financial advice, price predictions: a short refusal in the dog's voice plus what
it can do. Someone in danger: it stops joking and tells them to call family, 115 or 113.

## 6b. What the dog does

The model answers with `actions` beside its reply (one call, no second round trip): `alarm_add`, `alarm_edit`
(`h m days label on`), `alarm_delete`, `todo_add`, `todo_done`, `todo_edit`, `todo_delete`, `note_add`,
`note_edit` (or `append`), `note_delete`, `set` (a short list of settings, per board), `name`. The server checks
each one against the lists it just read (the item exists, hours 0-23, days digits, lengths), builds the new
`s3.alarms` / `s3.todo` / `s3.notes` JSON exactly as the web writes it (alarms `on` as 1/0 for `alarm.cpp`, a new
note first, a new reminder last), and saves through `assist_apply`, which calls `put_device_settings`: the one
function the web's own settings save (`save_device_settings`) calls too (same validation, same `device_settings`
rows, `source: web`, `updated_ms` now: the ping rev rises and the device syncs). Rules:

- Ambiguous ("đặt báo thức giùm" with no time, "xóa ghi chú" with several): nothing is done, the dog asks one short
  question.
- Deleting two or more things in one go is held: the dog asks, and only the next turn's yes does it (if the list
  did not change meanwhile).
- Never reachable: WiFi, pairing, the account, passwords, firmware, installing or removing apps, Home layouts.
- If the save refuses (a value out of range, an S3 setting on an X4), the dog says so in its own words
  (`saved: false`).

## 7. Memory and privacy

- **Memories** (`assist_memories`, per account, all dogs share them, at most 40): the model proposes them when the
  person tells something lasting (name, what to be called, birthdays, likes, family and pets, work, goals) and the
  server filters out anything that looks secret (6+ digit runs, passwords, OTP, PIN, card or account numbers, ID
  numbers, home address). "Quên chuyện đó đi" removes one. The web lists and deletes them (device page, tab Trò
  chuyện; `GET /v1/assist/memories`, `POST /v1/assist/memories/delete`).
- **Conversations** (`assist_convos`): the last 12 turns, text only (no audio is kept anywhere), deleted after one
  day; 30 minutes of silence starts a new one. The web can delete them all.
- **The name** (`user_profiles.call_name`): the web's Tài khoản › "Tên của bạn (chó sẽ gọi bạn như vậy)", or told
  to a dog ("Tên mình là Lan"); every dog uses it with its own way of addressing (Bao: "bây Lan", Boop: "sếp
  Lan").
- **Language**: the dog understands any language but answers in the device's language (`sys.lang`, Vietnamese by
  default). The voices are Vietnamese: a reply in another language is read with a Vietnamese accent.

## 8. The dogs' voices (vieneu-turbo at tts.espos.dev)

| Dog | Character | Voice | Why |
|---|---|---|---|
| bao (Bao) | ông chú khó tính, miền Nam | `adam` (Adam, nam, Nam) | the owner's pick for Bao was ElevenLabs "Adam"; this is the vieneu Adam, Southern like his "hông, nghen" |
| momo (Momo) | dân chill lười | `thai-son` (Thái Sơn, nam, Nam, kể chuyện) | slow storyteller, warm |
| boop (Boop) | trẻ trâu tăng động | `adam-bua` (Adam bựa, nam, Bắc) | the goofy one |
| leaf (Leaf) | bánh bèo ngọt ngào | `ngoc-huyen` (Ngọc Huyền, nữ, Bắc) | soft natural girl |
| volt (Volt) | ngôi sao tự phong | `hai-dang` (Hải Đăng, nam, Bắc) | casual young man |
| byte (Byte) | robot tổng đài | `mai-anh` (Mai Anh, nữ, Bắc, tin tức) | announcer = switchboard |
| shiba (Rocket) | trai phố lạnh lùng | `minh-triet` (Minh Triết, nam, Nam, tin tức) | flat, cool |
| corgi (Biscuit) | công chúa miền Nam | `my-duyen` (Mỹ Duyên, nữ, Nam, đọc truyện) | Southern, storybook princess |
| husky (Frost) | nữ hoàng drama cải lương | `thuc-doan` (Thục Đoan, nữ, Nam, kể chuyện) | cải lương is Southern theatre |
| golden (Sunny) | huấn luyện viên nhiệt huyết | `quang-son` (Quang Sơn, nam, Trung) | a voice no other dog has |

Chosen from the voice labels (nobody could listen while choosing): try them on the web (tab Trò chuyện, "Giọng"
list) and change a dog's voice in `web/scripts/gen-assist-dogs.py` (`EXTRA[dog]["voice"]`), then run it and push
`assist_reply`.

## 9. Measured (2026-11-18, real model, real data, real TTS through the platform's test runner; 19 turns)

| Step | Time |
|---|---|
| upload 4 s of 16 kHz WAV (128 KB) | Wi-Fi dependent, typically 0.2-0.5 s |
| server reads everything (two `QueryMany`) | 25-47 ms (+0.3-0.6 s when the weather cache is over 3 hours old) |
| model, typed question (prompt ~6k tokens, answer ~80) | 0.9-1.6 s |
| model, recorded question (hears and answers in one call) | 1.8 s; a separate speech-to-text call alone took 1.6 s, so it was merged |
| saving the turn and memories (one batch) | 15-170 ms |
| doing an action (the web's save, `assist_apply`) | 130-170 ms |
| whole answer on the server (`ms.total`) | 1.0-1.9 s |
| speech: first sound after `GET /say` | ~0.3-0.5 s (vieneu-turbo streams; tts.espos.dev README: ~0.3 s) |
| speech: whole reply | 7.3 s of speech streamed in 4.3 s, 6.6 s in 3.0 s (faster than it plays) |

Live, over real HTTP from Vietnam (2026-11-18, after the release; curl, a new TLS connection each time):

| Request | Time |
|---|---|
| `POST /v1/device/assist`, a 3-4 s WAV | answer after 3.0-3.6 s (the run itself 1.9 s: guard 12 ms, reading the upload 14 ms, the turn 1.84 s) |
| `POST /v1/device/assist`, text | 2.3-2.7 s |
| `GET /v1/device/assist/say` | first sound after 1.8 s (6 s once, the TTS cold), 7.1 s of speech in 4.2 s, in 98 pieces |
| the same, the TTS's Mac busy (load 58, 2026-11-18 17:00) | first sound after 3.7-9.5 s, 8.1 s of speech in 11-22 s: SLOWER than it plays |
| `POST /v1/device/assist?stream=1`, a 3-4 s WAV (17:30, TTS down to 1 worker, load 18) | headers + first sound after 4.9-6.2 s (once 16.8 s while the TTS was being reconfigured); `GET /say` for the same turn 1.3-2.2 s |

The TTS (tts.espos.dev, 2 vieneu-turbo workers) runs on one machine: when that machine is busy the voice comes
slower than real time. The device must handle it: buffer at least 1 s before playing, and on an underrun pause the
talking animation and wait (never drop the rest); past 6 s without bytes, stop and keep the bubble.

So from releasing the button to the first sound: about 5 s today, of which the model is 1.6-1.8 s and the trip to the
server and back (Cloudflare, the platform in Europe, the TTS at home) most of the rest. Keep the TLS connection
open between the two requests, start the `say` request the moment the JSON arrives, and show the reply and the
mood at once: they come about 2 s before the sound.
