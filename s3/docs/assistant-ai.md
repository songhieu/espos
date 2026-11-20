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
- As built (firmware 0.5.7, src/pet/talk.cpp): a pause after the call brings the dog's ack (setting `s3.assist`
  `"ack"`: 0 nothing, 1 a beep, 2 a word in its own voice, default 2). The question must begin within **6 s of the
  ack's end** (floor + 7 dB for 0.24 s, the ack itself not counted), or the dog says it is here and the turn ends
  with nothing sent. End of speech (owner, 2026-11-19): speech = within 10 dB of the question's own peak; it goes
  2 s after the last speech, 1.2 s when the room sits at its floor + 3 dB, 8 s after the question began at most.
  The "no question" log line gives the window's loudest level, the floor, how long it was loud and how much was
  the dog's own sound.
- The device's own sound is never a question: the mic task ignores the ack's mute (on the mic clock) and anything
  the speaker played in the last 400 ms but a game's tune (sound::outputSince; the dog's voice comes back at
  -30..-35 dBFS, peaks -12, plus the room's echo). The live sim reproduces the board's path with SIM_ECHO=dB
  (sim/live.cpp simEcho): without the gate, 3 of 5 dry calls took the ack's tail for a question at +6 dB.
- Speech is counted in the mic task, every 10 ms (wake::vadTake / vadSpeechMs / vadMute), not by the UI loop: a
  game drawing 6 pictures a second or a 2 s TLS handshake used to hide the question from a UI loop that sampled the
  level only when it ran (build 4 on the board: three calls in the aquarium, "no question" each time). The dog's
  ack is muted on the mic's own clock.
- The dog's pop-up pauses what is under it: the pet game, Coin Run (onScreen() is false while pet::summoned()),
  J2ME games (pause/resume posts 7/8, as for their menu); the aquarium the same way (tank-game). Closing the pop-up
  resumes them where they were.
- Game music (PawBoop's tunes, pawboop-all's src/pet/sfx): the wake word keeps listening through a tune or an
  ambience alone (pet::sfx::bedOnly), with both bars 0.05 higher; the dog's lines, effects and the AI's voice still
  stop it. Measured with wake/host/wakecli on the 9 tunes under a -62 dBFS floor: 1-2 false calls per 10 min at
  the normal bar, 0-1 at +0.05; a tune within ~20 dB of the call at the mic masks most calls, so tunes stay quiet.
  Once a minute the trace logs the mic level while the speaker played ("wake: mic while the speaker played").
- The warm-up at the call keeps the connection desk hands over (warm from its 45 s ping) and connects only when
  there is none; no second handshake. While it runs the dog moves at 5 pictures a second (each picture is ~50-60 ms
  of TJpgDec on core 0; on the board the pictures now use Espressif's decoder, TJpgDec as the fallback; DOGBENCH
  compares the two).
- The dog's pictures never hold up the UI loop: a picture the background decoder has not finished is replaced by
  the one already shown (dog.cpp; a TLS handshake on core 0 used to freeze the UI ~2 s with the dog on screen). After a spoken answer the dog listens on
  (**follow-up**, setting `s3.assist` `"follow"`, default 1): the next question must start within 2.5 s and be close
  to the device (15 dB over the room's floor and above -42 dBFS), or the dog goes back to waiting for "gâu gâu".
- The wake word turned on (web or device) while `dev.espos.wake.gau` is not installed: the device fetches it from the
  store by itself and posts "Getting the "Gâu" listener" (0.5.7; 0.5.6 waited for "Download" in the dog's book).

## 2. The question: `POST /v1/device/assist`

```
POST https://api.espos.dev/v1/device/assist?cid=<cid>&dog=<key>
Authorization: Device <token>
Content-Type: audio/wav
Content-Length: <bytes>

<the WAV>
```

- `intent` (optional, JSON text turns only): `"news"` asks for the news summary whatever the words (the pop-up's
  "Today's news" chip, talk.cpp askNews): `topic` is `news` and the reply may run to about 130 words.
- `cid` (optional): the conversation to continue: the `cid` of the last answer. Without it the server continues
  this device's conversation of the last 45 minutes, if any. `cid=new` starts a new conversation (a "new topic"
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
| `reply` | the bubble text: 1-3 short sentences, Vietnamese unless the person spoke another language. Numbers are written Vietnamese style (137,5; 64.652). A news summary (`topic` `news`) is the one long reply: up to about 130 words, 3-5 stories, plain sentences (no list marks); scroll it |
| `mood` | the pet clip to play while it talks: one of `happy giggle high_five levelup plead sulk yuck alert look_up stand no_tired sleep` (all are moments of every dog's pet pack) |
| `topic` | `gold crypto stock fx news weather time calendar alarm todo note reading memory chat refuse`: a card to show beside the bubble if you want one (`refuse` = the question was out of scope) |
| `cid`, `n` | keep `cid` for the next question; `cid` + `n` fetch this reply's voice |
| `remembered`, `forgot` | facts saved / removed in this turn ("Bao đã nhớ: …" toast, optional) |
| `done` | what the dog did for the person, e.g. `["đặt báo thức 06:30 một lần"]` (empty: nothing). The change is already saved for this device as a web save is: the device gets it with its next ping/sync (within about a minute); a device that wants it at once runs a sync now |
| `saved` | present when the dog tried to do something: `true` done, `false` refused by the settings check (`error` says why, `reply` is the dog owning up) |
| `asks` | the reply ends with a question to the person ("Mệt vì việc hả?", a riddle, the guessing game): a good moment to listen a little longer for the answer. In the JSON reply and (from device_assist v-next, 2026-11-20) in the streamed `X-Assist` meta |
| `unheard` | nothing was heard (silence, noise, "...", a hum; push to talk, a follow-up or typed): `reply` is the dog's own "say that again" line and nothing of the model's answer is used: no action, no memory, a held delete keeps waiting (2026-11-20). Typed noise makes no model call. A wake turn with nothing after "gâu gâu" stays `silent`. "ừ", "ờ", "ừm" are a yes, not noise |
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
| 402 `{"locked": "trial_ended" \| "no_ai", "buy_url"}` | the AI is not allowed (the trial is over, or the account has none) | §4b |
| 403 `{"error": "account_disabled", "state": "blocked" \| "deleting" \| "deleted"}` | the account is blocked (every device route says so) | keep the token; a calm "Tài khoản đang bị khoá" / "Xem chi tiết tại espos.dev" in the pop-up, no retry, no error look (the OS's screen for it: tank-game's lock) |

### 4b. Talking is locked (the trial, 2026-11-19)

The account's licence comes with every sync: `"lic": {"ai": "on" | "trial" | "off" | "byok", "ai_left": n,
"buy_url": "…", …}` (src/lic.h keeps it in NVS; tank-game's src/lock.h reads its other fields for the store and the
pickers). **No `lic` = no licensing** (self-hosted or older server): nothing locks, the device behaves as before.

- 402 from `POST /assist`: `lic::locked(why, buy_url)`; the dog says, in its own voice, that the free trial of talking
  is over (`ai_trial_end`) or that talking is not on (`ai_locked`), its own words in the bubble (talk.cpp
  `lockLineOf`, in every language), and the pop-up shows a QR code of `buy_url` (the web when none was sent) with
  "Scan to keep talking" in place of the mic button and the chips; it stays a minute. Never silent.
- `"ai": "off"`: the wake word still works and brings the dog, which says the same; the mic button does the same;
  nothing is recorded or sent. Each time, the device asks for a sync (at most every 30 s), so an unlock on the phone
  reaches it. Without that too: every licence change (a grant, an extension, a revoke, the trial's end, each AI turn)
  raises the server's `lic_ms`, and with it `settings_rev` and the ping's `rev`, so the 60 s ping brings the new
  `lic` within about a minute (admin-users, 2026-11-19). `buy_url` is used exactly as sent (a full URL such as
  `https://espos.dev/app#buy=<device id>`; the admin may change its base), never built on the device.
- `"ai": "trial"` with `ai_left` 10 or fewer: a small "Còn N lượt" chip on the pop-up (one less after each answer,
  until the next sync tells the real count).
- `"byok"`, `"on"`, no `lic`: as before.
- The voiced lines are in each dog's `assist.pba` from `vn.pawboop.dog.<dog>` 1.0.1 (the same ElevenLabs voice and
  settings as its other lines, -16 LUFS; wake/tools/lock_lines.py makes them, tools/pawboop/dog_lines.py appends
  them to the 1.0.0 file, every old clip unchanged). With an older dog package the dog shows the words only.
- Tests: `LOCK lic {"ai":"off"}` / `LOCK lic none` on the console (tank-game's lock::dev); the simulator's
  `SIM_FAKE_ASSIST=trial_ended` or `no_ai` answers 402.

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

A long `reply` is never cut (firmware 0.5.7, summon.cpp `fitBubble`): the bubble grows to the room the card has
(portrait: the card up to 8 px under the screen's top; landscape: its column), then its words scroll in a window of
whole lines with a thin bar on the right; a finger drags them, and reading keeps the pop-up open.

**Chips** (2 x 2 under the dog): "What time is it?" and "Weather?" (the dog's own voice pack, no AI), "Today's
news" (AI, shown when linked), "Play with {dog}". "Today's news" is a typed turn (`talk::askNews`): the same
`POST /v1/device/assist?stream=1` with a JSON body `{"text": "<the question in the device's language>",
"intent": "news", "dog": "<key>"}` (vi "Hôm nay có tin gì mới?"); the server sums up today's news (ai-assist). The
lock (402), a blocked account (403) and AI off are handled before sending, as for voice; a turn in progress stops.

## 6. What the dog knows and refuses

The server puts the person's own data into the model's prompt every turn (one database round trip): gold (PNJ
board), crypto (BTC, ETH, BNB, SOL, XRP, DOGE, TON), VN indexes and the cached stocks, Vietcombank FX, the news
sections they follow (titles), the weather of the device's city (`dash.city`, the dashboards' cache; refreshed when
over 3 hours old), the device's calendar cache (7 days), `s3.alarms` (and the next ring), `s3.todo`, `s3.notes`
(titles), reading on their EspOS readers, today's date and lunar date, the time in `sys.tz_min`. It must say when the
numbers were updated and never use a number that is not there. A figure older than its limit (weather forecast
24 h, current conditions 2 h, gold 24 h, coins 3 h, FX 36 h, stocks 4 days, news 24 h) is not given to the model
at all: the dog says it has no new figure. Weather answers name the city (the device's `dash.city`: a device paired
again takes the person's other device's city since 2026-11-19, not Hà Nội). A garbled or half sentence gets "say
that again" in the dog's voice, never a guess. News: "tóm tắt báo" gets a spoken summary of the most important
stories of the sections the person follows (fresh ones only, up to 15 titles and 8 leads in the prompt).

Where it is (2026-11-20): every turn carries the device's name as the owner set it, its kind (S3 3.5"/2.8" colour
touch screen on the owner's desk, or the X4 reader), the city and the time zone. "Mày đang ở đâu?" gets "in the
device on your desk, in <city>"; "ở đó trời sao?" gets that city's weather. It never makes up an address and never
claims to see outside. With no city set it says it does not know yet and that the city is set on the web (Thời tiết)
or in Settings; no weather is fetched then.

Chat (2026-11-20): it answers what the person says, not only questions: sympathy for "mệt quá"/"buồn quá" (sometimes
a question back), teasing back, taking compliments, made-up stories of its own day in the yard, riddles and a
guess-the-number game (the answer is kept in the turn, unseen by the person; the server corrects a hint that points
the wrong way). Opinions are its taste, never facts. Chat replies stay 1-3 sentences. General knowledge, homework, translation, code,
politics, religion, medical/legal/financial advice, price predictions: a short refusal in the dog's voice plus what
it can do. Someone in danger: it stops joking and tells them to call family, 115 or 113.

Itself (2026-11-20): each dog knows its own profile (breed, coat, age, favourite food, games, dislikes, place in the
yard, habit) from web/scripts/assist-dog-profiles.json, written into talk.go by gen-assist-dogs.py.
Asked about itself, it answers from that only; anything not there ("nặng mấy ký", "ba mẹ là ai") gets a playful,
vague answer in its voice, never a new fact and never the refusal line.

What it says it did (2026-11-20): a reply that says the dog changed something ("Đã đặt báo thức 7 giờ rưỡi", "Chú
đặt báo thức rồi nghen") when the turn made no change is replaced by the dog's own "nothing done yet, say it again"
line, so `reply` always matches `done`. When something was done, the device syncs at once (talk.cpp) and the alarm
rings from alarms::tick.

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

- **Memories** (`assist_memories`, per account, all dogs share them, at most 60, kept until deleted): the model
  proposes them when the person tells something lasting (birthdays, likes, family and pets, work, goals) and the
  server filters out anything that looks secret (6+ digit runs, passwords, OTP, PIN, card or account numbers, ID
  numbers, home address). Since 2026-11-20 a fact said again is not stored twice, and a changed one replaces the old
  ("đổi giờ dậy thành 6 giờ": the model names it by id, or the server sees the same words with other numbers, a
  fuller version, or the opposite). Each turn shows the model at most 15: those sharing words with what is being
  talked about, then the most recent. "Quên chuyện X đi" removes it, shown or not. The web lists and deletes them
  (device page, tab Trò chuyện; `GET /v1/assist/memories`, `POST /v1/assist/memories/delete`).
- **Conversations** (`assist_convos`, one row per session; 2026-11-20, "like ChatGPT"): 45 minutes of silence starts
  a new session. A session keeps its last 6 to 9 turns word for word (text only: no audio is kept anywhere); when it
  holds 9, the same model call also writes a one or two sentence summary of the oldest 4, added to the session's
  summary (past about 90 words the summary is rewritten shorter), and those 4 go. No extra call, no extra wait.
  The first turn of a new session also summarizes the previous one's unsummarized turns. Every turn sees this
  session's summary and up to 3 earlier sessions' summaries (7 days), so "lúc nãy mình nói gì?" works. The words go
  after 24 hours (a session never summarized keeps only what it was about), the summaries after 7 days (assist_reply
  for the device it talks to, assist_sweep every hour for the rest). The web shows the recent sessions with their
  summaries and deletes one or all.
- **Prompt budget**: the system prompt stays under 24,000 characters (about 11,000 prompt tokens with the router's
  own); over it, fewer memories and earlier sessions are shown. Measured on a 30-turn chat: 9,000 to 9,900 tokens.
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
