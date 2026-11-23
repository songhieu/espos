# The dog's AI as a provider: EspOS Assistant Protocol v1 (design draft)

Status: design, 2026-11-21; the owner accepted all 11 recommendations (section 7). Owner corrections the same day: a
custom AI is an assistant app a developer builds (sections 2.10 and 3 rewritten: no end-user form), and our own AI apps
are removable like any app (app-slots.md §16). Part of the app-slots architecture: [app-slots.md](app-slots.md).

The owner's words (2026-11-21):

> "nên làm phần AI lại theo kiểu handle bằng app ngoài để user có thể tick chọn AI là kiểu của mình xài pawpop hay
> cũng có tài liệu cho họ tự dev 1 version tương tự nghĩa là dữ liệu có cấu trúc đó và có thể dev riêng con AI của
> họ cái này cũng phải cải thiện code của mình á"
>
> "mình hướng tới là mọi nước luôn ko phải việt nam đâu nha"

In short: rebuild the dog's AI so it is handled by an outside "app". A user ticks which AI their dog uses (ours, PawBoop
AI, or another), and there are docs so anyone can build their own: the data is structured, and they write their own
AI against it. Our own code must improve to work the same way. And it is for every country, not only Vietnam.

What this document gives:

1. Today's flow on one page (section 1).
2. A draft of the contract between the EspOS server and any AI: **EspOS Assistant Protocol v1** (section 2).
3. The provider model: where the choice lives, how an AI is added, what the device shows, what happens when it is
   down, hosted and self-hosted (section 3).
4. How our code moves behind the same contract, so PawBoop AI is just the first provider (section 4).
5. The outline of the docs site's new "AI" section, in English (section 5).
6. The mockups to review before any coding (section 6).
7. The decisions the owner must make (section 7).

Words used here: **device** = an S3 desk screen, an EspOS Bot, an X4 reader, or the web chat panel. **Server** = the
EspOS backend (hosted at api.espos.dev, or a self-hosted server). **Provider** = the AI that decides what the dog
says and does; **PawBoop AI** is ours. **Turn** = one question and its answer.

---

## 1. Today's flow (one page)

```
 Device (S3 / Bot / web)                     EspOS server (hosted workflows)                       Outside
 ─────────────────────────                   ──────────────────────────────────                    ───────
 push to talk, follow-up, "gâu gâu", typed
   │  POST /v1/device/assist                  device_assist
   │  WAV 16-bit mono 0.3–20 s, or JSON text   ├─ require_device (token) ─ admin_ai_gate (trial: 402/403)
   │  ?stream=1&say_rate=16000&cid&dog&wake    ├─ take: WAV clean-up, 422 empty/too long
   │                                           ├─ assist_reply (function, talk.go, about 3,700 lines)
   │                                           │    1 one database round trip (13 reads)
   │                                           │    2 weather cache (open-meteo if > 90 min) ───────────► open-meteo
   │                                           │    3 prompt: persona + rules + actions + data + memory
   │                                           │    4 ONE model call: hears the WAV and answers JSON ───► the model router
   │                                           │      (gemini-3.5-flash-lite, JSON mode)                (Gemini)
   │                                           │    5 checks: nothing heard, unclear, claim guard,
   │                                           │      actions vs lists, holds, memory filter/merge,
   │                                           │      summary/compaction, retention; one batch write
   │                                           ├─ assist_apply: settings → put_device_settings,
   │                                           │                computer → desk_queue (waits ≤ 2 s)
   │                                           └─ answer: JSON, or voice: say node ────────────────────► tts.espos.dev
   │  ◄── PCM stream + X-Assist (base64 JSON)        (vieneu-turbo, the dog's voice, PCM)               (vieneu)
   ▼
 shows reply, plays voice, mood clip; on done/saved it syncs at once (talk.cpp); listens on if "asks"
```

The data at each step:

| Step | Data in | Data out |
|---|---|---|
| Device → server | `Authorization: Device <token>`, WAV or `{text, cid, dog, intent}`, query `rate`, `stream`, `say_rate`, `wake`, `voice` | |
| device_assist / chat_device_dog | the request; trial state (admin_ai_gate) | `{device_id, user_id, source, text, audio (base64 WAV), cid, dog, wake, intent, desk_on}` |
| talk.go reads | devices row; device_settings (language, time zone offset, city + coordinates, alarms, to-dos, notes, dog, assistant switches); news_prefs; market_cache; assist_memories; the current session and the last 7 days of sessions; ics_cache; reading_days; user_profiles (call name); desktop + desk_state | |
| prompt | persona (dogs.json + profile), Vietnamese rules, actions allowed on this kind of device, DATA (time and lunar date, place, weather, markets, news, calendar, alarms, to-dos, notes, reading, computer), name, up to 15 memories, up to 3 earlier session summaries, this session's summary and its last 6 to 9 turns | about 19,000–21,500 characters, 9,000–10,000 prompt tokens |
| model answer | | JSON `{heard, unclear, reply, mood, topic, keep, guess, remember[], update[], forget[], actions[], ask_confirm, confirm, summary, prev_summary}` |
| server checks | the answer and the lists it just read | cleaned reply; `changes` (settings to save); held deletes; memory writes; the turn saved |
| assist_apply | `changes`, desk actions | `saved` true/false; on failure the dog's own "couldn't do it" line |
| voice | the spoken text (capitals lowered), the dog's voice id | PCM 16-bit mono at `say_rate`, streamed; or later via `GET /v1/device/assist/say?cid&n` |
| device after | X-Assist `{heard, reply, mood, topic, cid, n, dog, done, saved, asks, ...}` | sync at once if done/saved; follow-up listening |

On a self-hosted server, the importer swaps the model call for `wm.AIChat` (`ESPOS_AI_*`), adds an optional
speech-to-text step (`ESPOS_STT_*`) and a `tts` node (`ESPOS_TTS_*`); everything else is the same code.

**What is tied together today, and has to come apart:**

1. One code node does everything: gathering data, writing the prompt, calling the model, checking the answer and
   saving. There is no place to plug in another AI.
2. The model's answer is an internal shape. Actions point at list positions ("alarm #2"), which only work because the
   same code wrote the list a second earlier.
3. It is Vietnam-first throughout: the prompt, rules, persona lines and refusal lines are Vietnamese; the voices
   (vieneu) speak only Vietnamese; the markets are Vietnamese (PNJ gold, VN stocks, Vietcombank rates); the lunar
   date is always given; time zones are a minute offset, not a named zone; the claim guard and the memory checks read
   Vietnamese words only.

---

## 2. EspOS Assistant Protocol v1 (draft)

### 2.1 Who does what

The device keeps talking to the EspOS server exactly as today. The new contract is between the server and a provider:
**one HTTPS request per turn**.

| The EspOS server always does | The provider decides |
|---|---|
| device sign-in, plan and trial, rate limits | what the dog says |
| recording the voice; speech-to-text when the provider wants text only | the mood (animation) |
| gathering the person's data, only within the scopes the person granted | which actions to propose |
| checking every proposed action, holding risky ones for a yes, then applying them through the same code the web uses | what to remember or forget (when EspOS keeps memory) |
| keeping memories and sessions (unless the provider keeps its own) | whether to keep listening |
| the dog's voice (unless the provider sends its own audio) | its own model, prompt and tools |
| logs, retention, the kill switch, fallback when the provider is down | |

A provider can never reach the device, the person's data or their other devices except through what the server sends
it and what the server agrees to apply.

### 2.2 Discovery: the provider's manifest

`GET https://<provider>/.well-known/espos-assistant.json` (fetched when the person adds the provider, and once a day):

```json
{
  "protocol": ["1"],
  "name": "Home AI",
  "developer": "Sam Lee",
  "description": "My own assistant running at home.",
  "icon": "https://ai.example.com/icon-256.png",
  "homepage": "https://ai.example.com",
  "privacy_url": "https://ai.example.com/privacy",
  "endpoint": "https://ai.example.com/espos/turn",
  "auth": {"hmac": true, "bearer": "optional"},
  "input": {"text": true, "audio": ["wav16k"]},
  "output": {"audio": ["pcm16"], "stream": true},
  "languages": ["*"],
  "persona": "espos",
  "memory": "espos",
  "scopes": {
    "required": ["turn.text", "locale"],
    "optional": ["profile.name", "place", "weather", "calendar", "alarms", "todo", "notes", "memories", "history"]
  },
  "actions": ["alarm.add", "alarm.update", "alarm.delete", "todo.add", "todo.done", "note.add", "memory"]
}
```

| Field | Meaning |
|---|---|
| `protocol` | versions it speaks; the server picks the highest both know |
| `endpoint` | where turns go; must be `https` on the hosted service |
| `auth` | `hmac`: it checks the server's signature (strongly advised); `bearer`: it wants the key the person pastes |
| `input.audio` | it wants the recording itself; without it the server sends text only (it transcribes first) |
| `output.audio` | it can send its own voice; `stream`: it can answer as a stream (2.7) |
| `languages` | BCP 47 tags it answers in, or `*` |
| `persona` | `espos`: it speaks as the dog the person chose, from the persona the server sends; `own`: it has its own character (the dog's picture still shows) |
| `memory` | `espos`: the server keeps memories and sessions and sends them (scopes `memories`, `history`); `own`: the provider keeps its own; `none` |
| `scopes` | data it needs (`required`) or can use (`optional`); the person approves them (2.10) |
| `actions` | action types it may ask for; anything else is refused |

### 2.3 The turn request

`POST <endpoint>` with these headers:

| Header | Value |
|---|---|
| `Content-Type` | `application/json` |
| `Accept` | `application/json`, or `text/event-stream` when the manifest says `stream` |
| `EspOS-Protocol` | `1` |
| `EspOS-Request-Id` | unique per turn, e.g. `req_01JDA7Q3...` (also in the body) |
| `EspOS-Timestamp` | Unix seconds |
| `EspOS-Signature` | `t=<timestamp>,v1=<hex HMAC-SHA256>` (2.9) |
| `Authorization` | `Bearer <the provider's key>`, if the person pasted one |

Body (one example, with every scope granted; a field whose scope is not granted is simply missing):

```json
{
  "v": "1",
  "type": "turn",
  "request_id": "req_01JDA7Q3V8K2",
  "sent_ms": 1795300000000,
  "deadline_ms": 1795300015000,
  "turn": {
    "id": "t_9c1e", "seq": 7,
    "source": "voice",
    "intent": null,
    "input": {
      "text": "Set an alarm for 6:30 tomorrow",
      "text_from": "espos_stt",
      "audio": {"format": "wav", "rate": 16000, "channels": 1, "seconds": 2.4, "data": "UklGRi..."}
    }
  },
  "locale": {"language": "en-US", "timezone": "Europe/Berlin", "utc_offset_min": 60, "units": "metric",
             "clock": "24h", "currency": "EUR", "calendar": "gregorian"},
  "now": "2026-11-21T09:41:00+01:00",
  "persona": {
    "id": "pawboop/bao", "name": "Bao", "species": "dog",
    "character": "The grumpy old uncle of the street who secretly cares about everyone.",
    "speech": "Short, gruff sentences. Denies caring, then cares. Never rude, never swears.",
    "address": {"self": "", "user": "kid"},
    "profile": {"breed": "village dog, short and stout", "coat": "red-orange with a cream belly", "age": "about 7",
                "food": "rice and bones", "play": "guarding the yard, fetch, tug of war",
                "dislikes": "baths, vegetables in the bowl", "spot": "the gate", "habit": "sniffs everything twice"},
    "lines": ["Oh, you're here. I wasn't waiting. Just sitting."],
    "voice": "pawboop/bao"
  },
  "user": {"ref": "u_7f3c2a91", "name": "Sam"},
  "device": {"ref": "d_19aa04be", "kind": "s3", "model": "es3c35p", "name": "Desk",
             "screen": {"w": 320, "h": 480, "touch": true}, "speaks": true,
             "say_max_chars": 500, "say_max_chars_long": 950},
  "scopes": ["turn.text", "turn.audio", "locale", "profile.name", "place", "weather", "calendar", "alarms",
             "todo", "notes", "memories", "history"],
  "context": {
    "place": {"city": "Berlin", "country": "DE"},
    "weather": {"fetched_ms": 1795299400000, "now": {"temp_c": 4, "text": "light rain", "is_day": true},
                "today": {"min_c": 2, "max_c": 6, "rain_chance": 70},
                "days": [{"date": "2026-11-22", "min_c": 1, "max_c": 5, "text": "cloudy", "rain_chance": 30}]},
    "calendar": {"fetched_ms": 1795298000000,
                 "events": [{"start": "2026-11-21T14:00:00+01:00", "end": "2026-11-21T15:00:00+01:00",
                             "title": "Dentist", "all_day": false}]},
    "alarms": {"snapshot": "h_3a9c", "items": [{"ref": "al_1", "time": "07:00", "days": "12345", "on": true,
                                                "label": "Work", "next": "2026-11-24T07:00:00+01:00"}]},
    "todo": {"snapshot": "h_77e0", "items": [{"ref": "td_1", "text": "Buy milk", "done": false}]},
    "notes": {"snapshot": "h_c1d2", "items": [{"ref": "nt_1", "title": "Shopping", "body": "eggs, bread"}]},
    "memories": {"total": 42, "items": [{"ref": "m_12", "fact": "Sam's birthday is 12 March", "kind": "date"}]},
    "history": {"session": {"id": "s_5b20", "started": "2026-11-21T09:30:00+01:00",
                            "summary": "Sam said the dentist moved to 2 pm.",
                            "turns": [{"seq": 6, "user": "Is it cold out?", "assistant": "Four degrees, kid. Take a coat."}],
                            "note": ""},
                "earlier": [{"started": "2026-11-20T21:10:00+01:00", "summary": "Sam planned a trip to Munich on Saturday."}]}
  },
  "capabilities": {
    "actions": ["alarm.add", "alarm.update", "alarm.delete", "todo.add", "todo.update", "todo.done", "todo.delete",
                "note.add", "note.update", "note.delete", "setting.set", "profile.name", "memory"],
    "settings": [
      {"key": "volume", "type": "int", "min": 0, "max": 100, "value": 60, "label": "Speaker volume"},
      {"key": "dnd", "type": "enum", "values": ["on", "off"], "value": "off", "label": "Do not disturb"}
    ],
    "moods": ["happy", "giggle", "high_five", "levelup", "plead", "sulk", "yuck", "alert", "look_up", "stand",
              "no_tired", "sleep"],
    "audio_out": {"formats": ["pcm16"], "rates": [16000, 22050, 24000]}
  },
  "last_results": [{"id": "a1", "type": "todo.add", "state": "done"}]
}
```

Notes on the request:

- `turn.source`: `voice` (push to talk), `follow_up` (the device listened on after an answer), `wake` (began with the
  wake word), `typed` (web or keyboard), `chip` (a button on the device). `intent` is set by a button (`news` today).
- `turn.input`: `text` is always there when the provider gets text (typed, or the server's speech-to-text with
  `text_from: "espos_stt"`). `audio` is there only with scope `turn.audio` and a manifest that asks for it. Wake turns
  arrive with the wake word already removed when the server transcribed them.
- `user.ref` and `device.ref` are stable for this assistant app only (an HMAC of the id with the app's salt), so
  two providers cannot match the same person.
- `say_max_chars` is what the device shows and speaks comfortably; `say_max_chars_long` is allowed for a summary
  (news, a story) when the provider says so (`say.long: true`).
- Every data block has `fetched_ms` where age matters. **Stale data is left out**, as today (weather 24 h, current
  conditions 2 h, markets by kind, news 24 h): a provider never gets a number the server would not trust.
- `snapshot` (alarms, to-dos, notes) is a hash of the list as sent. An action that names a `ref` from a snapshot that
  has changed since is refused, so a list edited on the web in the meantime is never changed by mistake.
- Settings are neutral names (`volume`, `brightness`, `dnd`, `theme`, `saver`, `quiet_hours`, `language`, `city`);
  the server maps them to the device's own keys (`s3.volume`, `bot.volume`, ...) and offers only those this device
  has.

The scopes, and the fields they add:

| Scope | Adds | Sensitivity | PawBoop AI default | Third-party default |
|---|---|---|---|---|
| `turn.text` | `turn.input.text` | normal | on (required) | required |
| `turn.audio` | `turn.input.audio` (the recording itself) | high: a voice print | on | off |
| `locale` | `locale`, `now` | low | on (required) | required |
| `profile.name` | `user.name` | normal | on | off |
| `place` | `context.place` (city, country; never coordinates or address) | normal | on | off |
| `weather` | `context.weather` | low | on | off |
| `calendar` | `context.calendar` (titles and times, 7 days) | high | on | off |
| `markets` | `context.markets` (prices for the person's region) | low | on | off |
| `news` | `context.news` (titles and leads of sections followed) | low | on | off |
| `alarms` | `context.alarms` | normal | on | off |
| `todo` | `context.todo` | normal | on | off |
| `notes` | `context.notes` titles; bodies need `notes.body` | high | on | off |
| `reading` | `context.reading` (minutes read, books) | normal | on | off |
| `memories` | `context.memories` (up to 15 relevant facts) | high | on | off |
| `history` | `context.history` (this session and up to 3 earlier summaries) | high | on | off |
| `desk` | `context.desk` (the person's computer: front app, playing, sessions) | high | on (when a computer is paired) | off |
| `act.alarms` `act.todo` `act.notes` | those actions | normal | on | off |
| `act.settings` | `setting.set` within the device's list | normal | on | off |
| `act.desk` | `desk.run` (2.5) | high | on | off |
| `act.memory` | `memory` operations | normal | on | off |

### 2.4 Context shapes

Each block is optional, versioned with the protocol, and documented in the reference with a JSON Schema. **The
schema is the source of truth** (built 2026-11-21): `sdk/packages/sdk/schemas/assistant-v1.schema.json` (entries
`manifest`, `turn`, `response`, `error`, `event`, `action`), its examples `assistant-v1-examples.json` and the shared
cases `assistant-v1-vectors.json`. Where the table below and the schema differ, the schema wins: calendar events are
`{date, time, title, all_day}` (our data has no end times), weather is `{fetched_ms, now {at, temp_c, feels_c, text,
is_day, humidity, wind_kmh}, hours [...], days [{date, min_c, max_c, text, rain_chance, uv, sunrise, sunset}]}` (today
is `days[0]`), reading is `{today_min, goal_min, week_min, last_book}`, markets items carry `buy`, `unit`, `source`,
history's `earlier` items carry `where` (device or web), refs look like `al_1`, `td_2`, `nt_1`, `m_12`.

| Block | Shape (short) |
|---|---|
| `place` | `{city, country}` (ISO 3166-1 alpha-2) |
| `weather` | `{fetched_ms, now {temp_c, text, is_day, humidity, wind_kmh}, today {min_c, max_c, rain_chance}, hours [{at, temp_c, text, rain_chance}], days [{date, min_c, max_c, text, rain_chance}]}`; `units` decides °C or °F only for what the dog says |
| `calendar` | `{fetched_ms, events [{start, end, title, all_day}]}`; no locations, guests or notes |
| `markets` | `{items [{id, name, kind, price, currency, change_pct, fetched_ms}]}`; kinds: `gold`, `crypto`, `index`, `stock`, `fx` |
| `news` | `{fetched_ms, source, items [{section, title, lead}]}` |
| `alarms` | `{snapshot, items [{ref, time "HH:MM", days, on, label, next}]}`; `days` is a string of weekday digits, `0` = Sunday, `""` = once |
| `todo` | `{snapshot, items [{ref, text, done}]}` |
| `notes` | `{snapshot, items [{ref, title, body?}]}` |
| `reading` | `{today_min, goal_min, week_min, books [{title, percent}]}` |
| `memories` | `{total, items [{ref, fact, kind}]}`; kinds `date`, `pref`, `family`, `work`, `other` |
| `history` | `{session {id, started, summary, turns [{seq, user, assistant}], note}, earlier [{started, summary}]}` |
| `desk` | `{online, name, front_app, playing {title, artist}, volume, battery_pct, commands [{cmd, risk}]}` |

### 2.5 Actions

A provider proposes; the server decides. Every action has an `id` (the provider's, unique in the turn), a `type` and
`args`. The server knows each action's **risk**, never the provider:

| Type | Args | Risk |
|---|---|---|
| `alarm.add` | `time` "HH:MM" 24 h, `days` (weekday digits, `""` once), `label` ≤ 40 | safe |
| `alarm.update` | `ref`, any of `time`, `days`, `label`, `on` | safe |
| `alarm.delete` | `ref` | safe for one; **held** when a turn deletes two or more items |
| `todo.add` / `todo.update` / `todo.done` / `todo.delete` | `text` ≤ 120 / `ref`, `text` / `ref`, `done` / `ref` | safe (bulk delete held) |
| `note.add` / `note.update` / `note.delete` | `title` ≤ 60, `body` ≤ 1,000 / `ref`, `title`, `body` or `append` / `ref` | safe (bulk delete held) |
| `setting.set` | `key` from `capabilities.settings`, `value` within its type | safe; some keys `confirm` (e.g. `language`) |
| `profile.name` | `name` ≤ 40, letters only | safe |
| `desk.run` | `cmd` from `capabilities.desk.commands`, `args` per the Desktop protocol (docs/desktop-protocol.md 2.3) | the desktop's own level (`safe` or `confirm`) |
| `memory` | see the response (`memory` list) | safe, filtered (2.11) |

JSON Schema for one of them, as the reference will publish it:

```json
{
  "$id": "https://espos.dev/schemas/assistant/1/action.alarm.add.json",
  "type": "object",
  "required": ["id", "type", "args"],
  "properties": {
    "id": {"type": "string", "maxLength": 32},
    "type": {"const": "alarm.add"},
    "args": {
      "type": "object",
      "required": ["time"],
      "properties": {
        "time": {"type": "string", "pattern": "^([01][0-9]|2[0-3]):[0-5][0-9]$"},
        "days": {"type": "string", "pattern": "^[0-6]{0,7}$", "default": ""},
        "label": {"type": "string", "maxLength": 40, "default": ""}
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

The result of each action comes back in the **next** request's `last_results` (`done`, `held`, `refused` with a
`reason`, `failed`). Within the same turn the server corrects what the dog says (2.11).

### 2.6 The response

```json
{
  "v": "1",
  "request_id": "req_01JDA7Q3V8K2",
  "heard": "Set an alarm for 6:30 tomorrow",
  "say": {"text": "Done, kid. Alarm at 6:30 tomorrow morning.", "lang": "en-US", "long": false,
          "if_not_done": "Hm. I couldn't set that alarm, kid. Try the app."},
  "mood": "happy",
  "topic": "alarm",
  "actions": [{"id": "a1", "type": "alarm.add", "args": {"time": "06:30", "days": "", "label": ""}}],
  "memory": [{"op": "remember", "fact": "Sam gets up at 6:30 on weekdays", "kind": "pref"}],
  "follow_up": {"listen": false},
  "session": {"note": "", "summary": null, "end": false},
  "meta": {"model": "my-model-7b", "ms": 840}
}
```

| Field | Required | Meaning |
|---|---|---|
| `say.text` | yes | what the dog says and shows; plain text, no markup or emoji; `say_max_chars` (or `say_max_chars_long` with `long: true`) |
| `say.lang` | no | the language of `say.text`; the server picks the voice for it |
| `say.if_not_done` | no | said instead of `say.text` when any action is refused, held or fails; else the server says a standard line in the person's language |
| `heard` | when it got audio | what it understood; `""` means nothing was heard (the server then says the "say that again" line and applies nothing) |
| `mood` | no | one of `capabilities.moods` (default `happy`) |
| `topic` | no | `weather`, `calendar`, `alarm`, `todo`, `note`, `news`, `markets`, `memory`, `desk`, `chat`, `refuse` (the device may show a card) |
| `actions` | no | at most 5 (2.5) |
| `memory` | no | at most 3: `{"op":"remember","fact","kind"}`, `{"op":"update","ref","fact"}`, `{"op":"forget","ref"}` (only with `memory: espos`) |
| `follow_up.listen` | no | the dog asked something: the device listens a little longer for the answer |
| `session.note` | no | a private note kept for the next turns of this session (a game's secret number), ≤ 100 characters |
| `session.summary` | no | with `memory: espos`, a new summary of this session to keep (≤ 120 words) |
| `session.end` | no | the provider considers the conversation over: the next turn starts a new session |
| `audio` | no | its own voice: `{"format": "pcm16", "rate": 24000, "data": "<base64>"}`, or sent as a stream (2.7) |

### 2.7 Streaming (optional)

With `Accept: text/event-stream` and `output.stream` in the manifest, the provider may answer as server-sent events:

```
event: plan
data: {"heard": "Set an alarm for 6:30 tomorrow", "actions": [{"id": "a1", "type": "alarm.add", "args": {"time": "06:30"}}]}

event: say
data: {"delta": "Done, kid. "}

event: say
data: {"delta": "Alarm at 6:30 tomorrow morning."}

event: audio
data: {"format": "pcm16", "rate": 24000, "data": "<base64 chunk>"}

event: final
data: { ...the whole response of 2.6... }
```

Rules:

- `plan` comes first and carries every action (an empty list when none). The server checks and applies them before
  speaking, so the dog never says "done" for something that was refused.
- `say` deltas: the server starts speaking at the first full sentence (its own voice), so the first sound comes about
  one sentence after the provider starts writing. `audio` chunks: the server passes them to the device, resampled to
  the device's rate. A provider sends one or the other.
- `final` is the source of truth for everything (memory, follow-up, session). Without a `final` the turn counts as
  failed after what was already spoken.
- The device side does not change: it still gets one PCM stream with the turn in `X-Assist`.

### 2.8 Timeouts and errors

| Limit | Value |
|---|---|
| connect | 3 s |
| JSON answer complete | 12 s (the request's `deadline_ms`) |
| stream: first event | 6 s; whole stream 25 s |
| retries | one, only when the connection failed before anything was sent; the provider must treat a repeated `request_id` as the same turn |

Errors are an HTTP status with a JSON body:

```json
{"error": {"code": "rate_limited", "message": "Too many turns", "retry_after_s": 30}}
```

| Status / code | The server does |
|---|---|
| 400 `bad_request` | logs it; fallback (3.4); shows "your AI said the request was wrong" in the web log |
| 401/403 `unauthorized` | marks the app's provider "needs attention" on its Activity page and for its developer; fallback |
| 429 `rate_limited` | no retry before `retry_after_s`; fallback |
| 5xx, timeout, not JSON | fallback; after 3 failures in 5 minutes the app's provider rests for 2 minutes (calls go straight to fallback) |
| 200 with an invalid body | the invalid parts are dropped (an unknown action, a bad mood); a missing `say.text` is a failure |

### 2.9 Authentication and signing

- Each assistant app has a **signing secret** (32 bytes) and a salt for `user.ref` and `device.ref`, issued to its
  developer (the developer keys page, or `espos ai secret <package id>`), shown once, never to end users. If the
  provider gives each user its own key, the manifest says so and the OS treats it as a secret setting of the app
  (manifest-driven.md §1.4), sent as `Authorization: Bearer`.
- Signature: `v1 = hex(HMAC-SHA256(secret, timestamp + "." + request_id + "." + body))`. The provider rejects a
  request whose timestamp is more than 5 minutes off, or whose `request_id` it saw in the last 10 minutes (replay).
- Rotation: the web makes a new secret; for 24 hours both are valid and the header carries both (`v1=...,v1=...`).
- Hosted: `https` only, no redirects followed, no private or loopback addresses (10/8, 172.16/12, 192.168/16, 127/8,
  169.254/16, ::1, fc00::/7). A self-hosted server may allow `http` and local addresses (a home model) with a
  setting.
- Keys and secrets are stored encrypted, readable only by the workflows that call the provider, never sent to the
  browser after they are saved (the web shows the last 4 characters).
- The provider never gets the device's token, the person's email, account id, device id, coordinates or address.

### 2.10 Privacy and consent: permissions at install

A third-party AI is an **assistant app** (app-slots.md §5): a package whose manifest declares `provides:
["assistant"]`, its endpoint (`assistant.provider`) and, as ordinary permissions, what the server may send it and do
for it. The person approves them when installing the app, like any app's permissions; Settings › Apps › the app ›
Permissions (device and web, the generic screen) shows and revokes them. There is no separate consent form.

| Scope (2.3) | Permission in the manifest | Shown at install as |
|---|---|---|
| `turn.text`, `locale` | always (an assistant app needs them) | "Hears what you say after the wake word" |
| `turn.audio` | `assist.audio` | "Gets the recording of your voice" (with a warning: a voice can identify you) |
| `profile.name` | `assist.name` | "Knows your name" |
| `place`, `weather` | `location:city`, `data:weather` | "Knows your city and its weather" |
| `calendar`, `markets`, `news`, `reading` | `data:calendar`, `data:markets`, `data:news`, `data:reading` | as the data permissions today |
| `alarms`, `todo`, `notes` (read) | `assist.alarms`, `assist.todo`, `assist.notes` (`assist.notes.body` for the bodies) | "Sees your alarms / to-dos / notes" |
| `memories`, `history` | `assist.memory`, `assist.history` | "Remembers things you tell it", "Sees your earlier conversations" |
| `desk` | `assist.desk` | "Sees your computer's state" |
| `act.alarms`, `act.todo`, `act.notes`, `act.settings`, `act.memory` | `alarms.write`, `todo.write`, `notes.write`, `settings.write`, `memory.write` | "Sets your alarms", and so on |
| `act.desk` | `desk.control` | "Controls your computer (the commands you turned on in EspOS Desktop)"; `claude.*` commands never in v1 |

- PawBoop AI is the assistant app `vn.pawboop.pet` with all of these, accepted at install like any app; switching one
  off in its Permissions screen leaves that data out of its turns too.
- The server sends a provider only what the installed app's granted permissions allow; a revoked one stops at the
  next turn.
- The per-app log (the last 50 turns: time, scopes, size, status, time taken) moves to the app's page on the
  web (Settings › Apps › the app › Activity).

### 2.11 What the server keeps checking (so a provider cannot do what the person did not allow)

| Check | Rule |
|---|---|
| Scopes | an action needs its `act.*` scope; data needs its scope; otherwise refused / not sent |
| Shapes | each action against its JSON Schema; `setting.set` against the device's own list and ranges; `desk.run` against the desktop's enabled commands |
| References | `ref` must exist in the snapshot that was sent; the snapshot must still match the current list |
| Bulk deletes | two or more deletes in one turn are held. PawBoop AI: a spoken yes in the next turn (as today). Third-party: see decision 6 |
| Risky settings and computer commands | held, confirmed on the web (or on the Mac, per the Desktop protocol) |
| Nothing heard | `heard: ""` or noise: the dog's "say that again" line, nothing applied, nothing remembered |
| What the dog says | length cap, control characters and markup removed; if any action was refused, held or failed: `say.if_not_done`, else a standard line in the person's language ("I couldn't do that one", "Confirm it in the app"). PawBoop AI keeps its own claim guard on top |
| Memory | at most 3 operations; secrets dropped (long digit runs, password, PIN, card and account words in the 11 languages); duplicates merged; 60 per account; `update` and `forget` only on refs that exist |
| Limits | 5 actions, 3 memory operations, 1,000 characters of speech, 64 KB answer |
| Kill switches | backend variable `ai_providers_enabled`; an app can be paused by its developer or by the store review |

### 2.12 Rate limits

| Limit | Hosted default |
|---|---|
| turns per assistant app per device | 20 a minute, 1,000 a day |
| test turns (web "Test" button) | 10 a minute |
| request size | 1 MB (a 20-second recording is about 640 KB in base64) |
| answer size | 64 KB; streamed audio up to 40 seconds |

### 2.13 Versioning

- `v` in the body and `EspOS-Protocol` in the headers. Within v1 only additive changes: new optional fields, new
  scopes, new action types (offered only to providers whose manifest lists them).
- A breaking change is v2. The server speaks v1 and v2 side by side for at least 6 months; the manifest's
  `protocol` list decides which a provider gets.
- The reference publishes one JSON Schema per message (`request`, `response`, each action, each context block) at
  `https://espos.dev/schemas/assistant/1/...`, and a changelog.

---

## 3. The provider model: assistant apps (revised 2026-11-21)

The owner's correction: "cái AI custom đó nghĩa là user là thằng dev á nó tự làm con bot của nó... mình mở giao thức
cho nó làm chứ ko phải giao diện để user có thể config". A custom AI is something a developer builds and ships as an
app; we open the protocol. End users never type an address, a key or switches.

### 3.1 What an assistant app is

```json
{"id": "com.example.homeai", "name": "Home AI", "version": "1.0.0", "code": 10000, "type": "app", "runtime": "decl",
 "provides": ["assistant"], "requires": ["audio.in"], "screens": ["tiny", "medium"],
 "perms": ["data:weather", "location:city", "assist.alarms", "alarms.write"],
 "assistant": {"provider": "https://ai.example.com/espos/turn", "picture": "home.png", "languages": ["*"]}}
```

- The endpoint is in the manifest; its manifest-at-the-endpoint (2.2) is only a cross-check (protocol versions,
  streaming, audio) that `espos ai test` and the store review read.
- PawBoop AI is the assistant app `vn.pawboop.pet` (`assistant.provider: "pawboop"`: called inside the server, no
  HTTP hop). It is the default on units sold with the dog, and it is **removable like any app** (app-slots.md §16).

### 3.2 Where the choice is stored

The assistant slot (app-slots.md §7): the device setting `slot.assistant` (an installed assistant app, or `none`),
with an account default the web offers for new devices. Decision 1 ("account default + device override") applies
here unchanged.

### 3.3 How developers register and test (no end-user page)

| Step | How |
|---|---|
| Build | `espos create my-assistant --template assistant` (manifest + picture + a toy provider) |
| Test the provider alone | `espos ai test <url>`: recorded turns, schema checks, signing, timing; no device needed |
| Signing secret | issued per app to the developer: the developer keys page on the web (where `espos login` keys live) or `espos ai secret <package id>` (shows it once; rotation keeps the old one 24 hours) |
| Own devices | `espos upload` (guides/my-apps): installs on the developer's own devices, unreviewed, their own risk |
| Everyone | a store submission, reviewed (decision 9): privacy page, region, retention, `espos ai test` passing, uptime check |

The server signs every turn to that app's provider with that app's secret (2.9); `user.ref` and `device.ref` are made
per app.

### 3.4 What the device and the web show

| Where | What |
|---|---|
| Install sheet (device store, web store) | the app's permissions in plain words, the provider's host, the languages it speaks |
| Settings › Default apps › Assistant (device and web) | the installed assistant apps and "None" (app-slots.md §7) |
| Settings › Apps › the app › Permissions | the generic permissions screen: see and revoke |
| Settings › Apps › the app › Activity (web) | the last 50 turns sent to its provider |
| Dog pop-up | the assistant app's name under the picture when it is not the one the device came with |

### 3.5 When the provider is down

Unchanged (decision 3): the assistant says it is not answering, in the person's language; with the slot option "If
it does not answer: use PawBoop AI" ticked, and PawBoop AI installed, PawBoop answers and the reply says so. After 3
failures in 5 minutes the app's provider rests 2 minutes.

### 3.6 Hosted and self-hosted

| | Hosted (espos.dev) | Self-hosted server |
|---|---|---|
| PawBoop AI's model | our router (Gemini) | `ESPOS_AI_*` (any OpenAI-style API) or local with the ai-kit |
| Speech to text, voices | the model hears the recording; tts.espos.dev | `ESPOS_STT_*`, `ESPOS_TTS_*` |
| Third-party assistant apps | yes: https only, public addresses | yes, plus `http` and local addresses if allowed |
| Store listings | reviewed submissions | the server's own store, its own review |
| Plan and trial | the trial counts PawBoop AI turns only (decision 4) | everything free |

## 4. Moving our code behind the protocol

### 4.1 Target shape

```
 device_assist / chat_device_dog          (unchanged routes and device contract)
   └─ assist_turn          orchestrator: session, provider choice, fallback, voice, saving
        ├─ assist_context  builds the v1 request from the database, only within the granted scopes
        ├─ provider:
        │    ├─ pawboop_brain   v1 request → prompt → model → v1 response (called inside, no HTTP hop)
        │    └─ provider_call   v1 request → signed HTTPS → v1 response (manifest, test, stream, rest)
        ├─ assist_policy   checks the response (2.11), holds, memory filter and merge
        └─ assist_apply    unchanged: put_device_settings, desk_queue
```

PawBoop AI becomes `pawboop_brain`: it receives exactly the v1 request a third party would, and answers exactly the
v1 response. The same contract tests run against it and against the reference provider.

### 4.1a The stages as built (2026-11-22): contracts

One source, `web/backend/workflows/assist_reply/nodes/talk.go`; each stage is a function with this contract, so it
can move into a workflow of its own later (size, ownership) without changing behaviour. PawBoop turns run all
stages in one code node; a third-party turn runs `talk` (mode `v1`-like hand-off), `provider_call`, then the same
file in mode `policy`.

| Stage | Function(s) | In | Out | Promises |
|---|---|---|---|---|
| assist_turn | `Run` | `device_id`, `user_id`, `source`, `text` or `audio`, `cid`, `dog`, `wake`, `intent`, `desk_on`, `providers_on`; mode `policy` adds `req`, `internal`, `provider` | `{status, body, changes, n_changes, oops, facts, unclear}`; or `{route: "provider", pkg, req, internal}` | one read round trip, one batch write; the device contract unchanged |
| assist_context | `placeOf`, `eapRequest` (+ `held` set in `Run`) | the turn's rows (device, settings, memories, conversation, caches, desk) | the v1 turn request (schema `turn`) and `internal` (refs → positions, memory refs → ids, neutral setting names → device keys) | no ids, coordinates, tokens or e-mail; stale figures carry only their state; lists by ref with a snapshot |
| pawboop_brain | `pbSystem`, the model call, `pbResponse` | the v1 request, `internal`, `pbExtra` (dog sheets, fold, previous session's turns, the computer's whole state, the small memory pick) | PawBoop's answer as a v1 response | the prompt word for word the same as before the move (golden); PawBoop-only steps stay here (cleaning, shouting case, name prefix, unclear, game hint) |
| assist_policy | `eapClean`, `pbInternal`, `applyActionsL`, the hold and memory code in `Run` | a v1 response (PawBoop's, or a provider's mapped by `pbAnswer` after its raw answer passed the fatal checks) | changes, done, held items, memory writes, the session note and summary | all-or-nothing actions; deletes of two or more held; answers to what waits only through `held`; memory by refs or words; the claim guard only for PawBoop |
| provider_call | workflow `provider_call` (`call.go`) | `pkg`, `req`, `user_id`, `device_id` | `{ok, response, error, status, ms}` | https to a public host; signed exact body (rotation: both secrets 24 h); the app's own refs (salt); no device token, id, e-mail or account id; answers through a redirect or over 64 KB refused; rests after failures; `ai_calls` keeps sizes, the reply's length and a hash, never the words |

Limits of a code node: it cannot resolve host names (the address an endpoint resolves to, DNS rebinding, is the
platform's egress check, applied to every hop) and cannot turn redirects off (an answer that came through one is
refused after the fact). To confirm with the platform before `ai_providers_enabled` goes to 1.

### 4.2 What changes, workflow by workflow

| Workflow | Change |
|---|---|
| `assist_reply` (talk.go) | split: data gathering → `assist_context` (structured JSON, no prose); prompt writing and the model call → `pawboop_brain`; checks → `assist_policy`; the rest (sessions, retention, saving) stays in `assist_turn`. Kept as a thin wrapper during the move, so callers keep working |
| `assist_context` (new) | the 13 reads of today's step 1, the weather and news caches, scopes applied, snapshots and refs made |
| `pawboop_brain` (new) | today's prompt, built from the v1 request instead of the database; the model call; maps today's answer JSON to the v1 response (`reply`→`say.text`, `remember/update/forget`→`memory`, `actions` with list positions → `ref`s) |
| `provider_call` (new) | signing (crypto/hmac already works in our code nodes), SSRF rules, timeouts, stream reading, rest after failures, `ai_calls` log |
| `assist_policy` (new) | today's `applyActions`, hold logic, nothing-heard guard, memory filter and merge, generalised to refs and scopes; the claim guard stays as a PawBoop-only step |
| `assist_apply` | unchanged; `desk_queue` unchanged |
| `device_assist`, `chat_device_dog` | call `assist_turn`; X-Assist gets `provider` |
| `device_assist_say`, `say_device_dog`, the `say` node | voice by language and dog (4.6); pass through a provider's own audio |
| `device_sync`, `device_ping` | `ai.name` for the settings row (check-ping-keys.py) |
| new tables and routes | `assist_app_keys` (per assistant app: secret, salt, state; written from the developer keys page and `espos ai secret`), `ai_calls` (the Activity log, 7 days); `GET /v1/devices/:id/apps/:pkg/activity` (end_user); developer routes under `/v1/dev/ai/*` |
| `assist_sweep` | also trims `ai_calls` after 7 days |

### 4.3 Firmware

Nothing is required: the device contract stays the same. Optional and small:

- S3 Settings: a read-only "Dog's AI" row from `ai.name` (device_sync).
- S3 pop-up and Bot info screen: the AI's name from `X-Assist.provider`.
- The "isn't answering" sentence comes from the server, already in the person's language.

### 4.4 Web

- Default apps › Assistant (the slot picker), the generic Permissions screen, the app's Activity page; the developer keys page gets assistant secrets.
- Account page: the list of AIs and the default.
- Chat panel: "answered by".
- i18n: every new string in en and vi, as the app does today.

### 4.5 Self-hosted server

- The importer maps the new workflows as it maps today's; `pawboop_brain`'s model call becomes `wm.AIChat`, its voice
  the `tts` node.
- `provider_call` runs natively; a self-hosted server can point a device at a model on the same network.
- The README gains "Use another AI" next to "The dog assistant: AI and voice".

### 4.6 For every country

| Today | Needed |
|---|---|
| Vietnamese prompt and rules | an English base prompt; the reply language is `locale.language`; the person may speak any language |
| Personas in Vietnamese (chú, bây, sếp) | each dog described in English (character, speech, profile) plus, per language, how it says "I" and "you" and a few lines; Vietnamese keeps today's tuned version |
| vieneu voices (Vietnamese only) | a voice per dog per language: a multilingual TTS behind tts.espos.dev, or a vendor per language; the dog's voice id stays `pawboop/<dog>` |
| PNJ gold, VN stocks, Vietcombank rates | markets by region (the person's currency and exchange; gold in local currency) |
| `sys.tz_min` offset | an IANA zone (DST-correct), sent as `locale.timezone` |
| the lunar date always | only where it is used (vi, zh, ko) |
| refusal, "say again", "couldn't do it" lines | per language, per dog (generated once, reviewed) |
| claim guard and yes-detection (Vietnamese words) | PawBoop AI asks its own model to classify (one more field in the same answer, no extra call); the standard lines for third parties come from the server's i18n |
| memory secret filter (Vietnamese words) | the same words in the 11 languages the device speaks |

**Built 2026-11-21 (local, not deployed; the global step moved first at main's request):**

- **Vietnamese is untouched.** `scripts/assist-test/golden_test.go` recorded 32 Vietnamese turns from talk.go before
  the work (every dog, the three boards, typed/spoken/woken, the news chip, actions, a held delete and its yes, the
  claim guard, "say again", memories, a long session's summary, earlier sessions, a computer command held for a
  yes, the model failing): the model request and the whole answer and database writes stay byte for byte the same.
  The other languages' turns (13 cases) are recorded after the work, for review.
- **English base prompt** for every `sys.lang` but `vi`: persona, rules, actions, data blocks, memory, history,
  summaries, the computer block, all English; the reply in the device's language ("IN FRANÇAIS: …" carries the
  character over). Rule 3 writes numbers the language's way; rule 6 forbids tags (a stray `[laughs]` is stripped);
  the emergency line says "the local emergency number". The data still says where it comes from (Vietnamese gold,
  VnExpress, Vietcombank, labelled as such) until markets and news by region exist.
- **Dogs in English:** `scripts/assist-dogs-en.json` (hand-written from each Vietnamese sheet, for the owner's
  review) → `dogEN` via `scripts/gen-assist-dogs-en.py`. Fixed lines (say again, could not, not done yet, too tired,
  "just to be sure", the computer's dropped/later lines) per dog in English and per language in de, fr, es, it,
  pt-BR, ru, ja, ko, zh-Hans (`stockLines`; per-dog lines there are a later step). The model failing in another
  language sends the "tired" line and no Vietnamese clip.
- **Locale and zone:** `sys.tz` (IANA, accepted by put_device_settings, read by talk.go with `time.LoadLocation`,
  DST right; checked in the platform's engine) with `sys.tz_min` as the fallback; the weather is asked in that zone;
  the lunar date only for vi, zh-Hans, ko. Not yet: the web sending `sys.tz` (the browser's zone at pairing and in
  Settings) and device_sync keeping `sys.tz_min` in step across daylight saving.
- **Voices per language:** `dogVoices` (piper voices on tts.espos.dev, by dog in English, by man/woman elsewhere;
  none for ja/ko) behind `voiceLangs` (empty: nothing is installed there yet, so every dog keeps its vieneu voice).
  A turn's voice `piper:<voice>` makes the three say routes (device_assist, device_assist_say, say_device_dog) ask
  tts.espos.dev for model `piper`; any other voice stays `vieneu-turbo`.
- **No Vietnam-first guards:** the claim guard reads the model's own `claims_done` in other languages; the secret
  filter has the other languages' words; memory matching has English stop/when/negation words; "long answer" words
  per language; the guessing game's hint fixed in English; assist_apply's computer outcome reasons in English when
  the turn's lines say `en`.

### 4.7 Order of work (each step can ship on its own)

Status 2026-11-21: step 7 (global) built first, see 4.6; step 1 built: the schema, examples and shared vectors
above; the CLI's `cli/src/assistant.ts` (`signTurn`, `verifySignature`, `cleanResponse`: what `espos ai test`
will run) and `cli/src/jsonschema.ts` (a small schema checker, no dependencies); the server's twin
`web/scripts/eap/eap.go` (`eapSign`, `eapClean`, BEGIN/END block for provider_call and assist_policy), both run the
same vectors (`cli/test/assistant.test.mjs`, `web/scripts/test-eap.sh`). Step 2 started: talk.go's `eapRequest`
builds this turn as a v1 request from the same data as the prompt (stale figures left out, refs by position,
memories as shown, no ids or coordinates; `internal` maps refs back), reached by a dry run (input `v1` = "1": no
model call, nothing written) and by the new workflow `assist_context` (a thin wrapper over that dry run until the
split). The request of every golden turn matches the schema (`test-eap.sh`: 45 of 45). Still to do in step 2: the
persona block for third parties (the dog sheets), `last_results`, scopes from the app's permissions.

Step 3 (2026-11-22): **PawBoop AI's prompt is now written from the v1 request** (talk.go `pbSystem`, pawboop_brain's
input side): every data block of the request is turned back into what today's text functions read, plus PawBoop's
own housekeeping (`pbExtra`: its dog sheets, the session's fold, the previous session's unsummarized turns, the
computer's whole state, the smaller memory pick for an over-long prompt) and the request's internal memory ids. To
make that lossless the request gained: weather/markets/news/calendar `state` (and markets `kinds`), figures as the
sources gave them (no rounding), local forecast times, crypto `price_vnd`, news `section_id`, each history turn's
`persona` and `note`, x4's settings. The 45 golden turns (32 Vietnamese) are byte for byte the same, and so are the
platform engine's prompts and answers for the assist_reply fixtures (deployed v12 against the new code). The
output side: `pbResponse` maps PawBoop's answer to a v1 response (refs for positions, memory refs, session note and
summary) and `eapClean` (synced from web/scripts/eap by `scripts/sync-eap.py`) checks it like any assistant's.

**assist_policy decides from the checked v1 response** (2026-11-22, main's decision: PawBoop on the same contract as
third parties): its actions all or nothing (one refused, none done, the dog's own "could not" line), the planner
(applyActions) runs on the v1 actions mapped back (refs → positions, neutral setting names → device keys), deletes of
two or more held for a yes, the person's answers to what waits come from the response's `held`, memories from its
`memory` (forget by ref or by words), the game note and the summary from its `session`; the claim guard stays a
PawBoop-only step on the brain's side. Protocol additions for it (schema, CLI and Go check, shared vectors): at most
30 actions (a whole list deleted at once, held), 20 memory operations, `{"op": "forget", "match": "<words>"}` (the
server finds at most 3), `ask_first`, request `held` / response `held` (`actions`: yes/no; `desk`:
yes/no/unclear/other), note bodies up to 2,000 characters, `desk.run` accepts any command name (the computer's queue
decides and the reason comes back). Result: the 45 golden turns and the platform engine's 8 fixture runs
(assist_reply ×5, device_assist ×3) are the same as deployed v12; the differences, each a golden case
(scripts/assist-test/testdata/golden/edge_*):

| Case | Before | Now |
|---|---|---|
| a computer command and a refused list action in one answer (`edge_mixed_refused`) | the command went to the computer, the dog said it could not | nothing is done, the dog says it could not |
| a setting out of its range, "âm lượng 150" (`edge_setting_out_of_range`) | passed on, put_device_settings refused it later | refused at once, the same "could not" line, nothing changed |
| 31 actions in one answer (`edge_too_many_actions`) | all done | refused (the limit is 30) |
| forgetting a memory by an id the prompt did not show (`edge_forget_unshown_id`) | deleted | ignored (refs only for shown memories; by words still works: `edge_forget_words`) |
| a game note over 100 characters (`edge_long_note`) | cut with "(còn tiếp)" | cut at 100, no marker |

Unchanged and guarded by golden cases: a valid setting, forgetting by words, deleting 12 notes (held), a command
while the computer is offline (it still goes; the computer's queue says why), an unknown action. The code nodes'
interpreter panics on a `switch` over an interface value (found by the engine run, fixed; test-eap.sh now refuses
one).

**provider_call and the third-party route** (2026-11-22, built locally, behind `ai_providers_enabled` = 0):
- Shape (instead of four separate workflows: a code node cannot call a workflow, so separate nodes would each carry
  the shared helpers and pass all of the turn's state as JSON, 3 to 4 parses per PawBoop turn): **one source,
  talk.go, with the stages as functions** (`eapRequest` = assist_context, `pbSystem` + the model + `pbResponse` =
  pawboop_brain, `eapClean` + the policy = assist_policy, `Run` = assist_turn), wired by assist_reply's graph:
  `talk` → branch on `talk.route` → (PawBoop: `out`, unchanged, one node) or (`prov` = call_workflow
  **provider_call** → `pol` = the same talk.go with mode `policy` → `outp`). The policy run re-reads the turn's state
  (one more database round trip) and takes the provider's answer through the same checks and saving
  (`pbAnswer` maps it into the answer shape; its raw answer is first checked for a fatal error).
- Chosen by the device setting `slot.assistant` (an app id that is not `vn.pawboop*` and not `none`) and the
  variable `ai_providers_enabled` = 1.
- **provider_call** (workflow, code node call.go with the EAP block): the app's row in `assist_app_keys` (endpoint,
  signing secret and the previous one for 24 h, salt, state, health) → https only to a public host
  (`eapEndpointOK`) → the request as the exact JSON it signs, with EspOS-Protocol / Request-Id / Timestamp /
  Signature → an answer through a redirect refused, 401/403 → `unauthorized` (needs attention), 429 → rests for
  `retry_after_s`, 3 failures in 5 minutes → rests 2 minutes, every call logged in `ai_calls`. Out
  `{ok, response, error, status, ms}`.
- Not answering (§3.5): the "isn't answering" line in the device's language (`noAnswerLines`), status 502
  `provider_failed` with `provider_error`.
- Third-party turns skip PawBoop's own steps (shouting case, the name prefix, the claim guard, the game hint); a
  refused action says the provider's `if_not_done` when it gave one.
- Tests: Go (`provider_test.go`: the two runs, an alarm set from a provider, its own "could not" line, not answering
  for a failed call and for an answer to another request); the engine (test-eap.sh: provider_call against nine fake
  providers, and the whole route in a scratch copy with the switch on). PawBoop's path is unchanged (golden 55;
  engine prompts and answers identical to deployed v13).
- Built since (2026-11-22): per-app salts (provider_call refuses an app without one), `ai_calls` with sizes and a
  hash only, trimmed after 7 days by assist_sweep; the developer routes `GET /v1/dev/ai` (dev_ai_list: the
  developer's assistant apps, setup and the last day's health; never a secret) and `POST /v1/dev/ai` (dev_ai_set:
  `{pkg, endpoint?, secret?, state?}` for the developer's own private `u.` assistant apps; a new secret keeps the
  previous one 24 hours; the first secret fixes the ref salt); the CLI `espos ai secret | endpoint | status |
  pause | resume | test` (the secret is made on the developer's machine, sent once, shown once; `espos ai test`
  signs the example turns itself and checks the answers with the server's rules, no device and no espos.dev).
- Built 2026-11-22 (later): **scopes from the app's permissions**: the SDK's `assistant-v1-scopes.json` (permission
  → scopes, scope → actions) mirrored in the EAP block (`eapScopesFor`, `eapFilter`; eap_test.go compares), the
  provider route filters the request by the app's `app_manifests.perms` (new column, written by confirm_user_pkg
  and app_manifests_tick): blocks, the name, the recording, notes' bodies, actions and computer commands not
  granted are left out, and the answer check then refuses actions outside them; the validator accepts the
  assistant permissions only in assistant apps (M049) and needs an https provider (M050); private uploads keep an
  assistant app's permissions (parse.go, the same codes; user-pkgs-check.mjs 33/33). **For developers**: `espos
  create --template assistant` (a small screen plus `provider/server.mjs`, a working provider that `espos ai test`
  passes), the example `examples/assistant-echo` made from it, and the docs page "Build your own assistant"
  (site/src/content/docs/guides/assistant.mdx: the flow, the permission table, the turn and the answer, signing and
  limits, links to the schema, the example and PawBoop's own stage functions in talk.go).
- Not yet: the app's Activity page on the web, PawBoop's own permission switches (it gets every scope), streaming,
  the "use PawBoop AI if it does not answer" slot option, store apps' keys (admin review), and the two platform
  questions below.
- **Blocking before `ai_providers_enabled` = 1:** the engine follows redirects (up to 5) and a code node cannot turn
  that off (no option on `wm.FetchWith` or the `http_request` node), so a provider's endpoint could send the
  request on to another host even though the answer is refused; and a code node cannot check the address a host
  resolves to. The platform's docs say every hop is checked by its egress rules and that workers refuse "private and
  rebinding addresses"; that this holds for code-node fetches must be confirmed with the platform, or a "no redirects" option asked for.

1. **Protocol and schemas**: this draft agreed; JSON Schemas and examples in the repo; the reference provider (4.9)
   written against them.
2. **assist_context**: structured context, the same data as today. Test: today's prompt rebuilt from the structured
   context matches today's prompt for all fixtures.
3. **pawboop_brain behind the contract**: assist_reply calls assist_context → pawboop_brain → assist_policy inside;
   the same answers as today (the fake-model harness of scripts/assist-test runs both paths).
4. **Scopes for PawBoop AI**: the switches on the web; data left out when switched off.
5. **provider_call and assistant apps**: `assist_app_keys`, developer routes, `espos ai test` / `espos ai secret`, signing, rest after failures, fallback; behind
   `ai_providers_enabled` = 0 until reviewed.
6. **Streaming** (say deltas, provider audio).
7. **Global**: English base prompt, personas per language, voices per language, markets by region, IANA zones.
8. **Docs site** (section 5) and the store's AI category.
9. **Self-hosted**: importer, README.

### 4.8 Risks

| Risk | Answer |
|---|---|
| PawBoop AI's replies change when the prompt is rebuilt from structured data | step 2's golden test; the fake-model harness; a few approved live checks |
| An extra hop slows the answer | PawBoop AI is called inside (no HTTP); third parties stream; the first sentence is spoken early |
| A provider says "done" for a refused action | `plan` before `say`; `if_not_done`; the server's standard line |
| A provider is used to read someone's data | permissions at install, opaque refs, no ids or coordinates, the per-app log, uninstall stops everything |
| SSRF through the provider address | https only, public addresses only, no redirects, a short timeout |
| Per-person secrets on the hosted platform | crypto/hmac is available in code nodes; encryption at rest needs crypto/aes, not yet tried there (fallback: a separate table only workflows read) |
| Costs: our voice and speech-to-text for third-party turns | rate limits; a fair-use cap; decision 4 |

### 4.9 Tests

- Schema tests: every example in the docs validates; every PawBoop answer validates.
- Contract tests: the same 30 recorded turns run against `pawboop_brain` (fake model) and the reference provider;
  checks on scopes (a field missing when not granted), refs, holds, `if_not_done`, nothing heard, limits.
- Signing: fixed test vectors (secret, timestamp, id, body → signature) in the docs and in both codebases.
- Failure tests with a fake provider: slow, 500, 429, wrong signature, invalid JSON, half a stream.
- The local harness (section 5) is the same code providers use, so what we test is what they test.

---

## 5. Docs outline: the site's new "AI" section (English)

A new sidebar group **AI** in s3/site (src/content/docs/ai/), also fed to espos.dev/docs.

1. **Choose your dog's AI** (`ai/choose.mdx`)
   - What "the dog's AI" is; PawBoop AI; your own AI; AIs from the store.
   - What each AI hears, knows and can do; the switches; the log.
   - One AI per device; when an AI is down.
2. **How a turn works** (`ai/how-it-works.mdx`): the diagram of 2.1; who checks what; latency.
3. **Build your own AI: quick start** (`ai/build/quick-start.mdx`)
   - A minimal provider in Python (FastAPI, about 60 lines): the manifest, signature check, a fixed reply, one alarm
     action. The same in Node (Express).
   - Run it, expose it (a tunnel or a server), add it on the web, press Test.
   - A `curl` test: a script that signs a sample request so you can try it without a device.
4. **Protocol reference** (`ai/build/reference/*.mdx`): manifest, request, context blocks, actions with schemas,
   response, streaming, errors, signing (with test vectors), scopes, limits, versioning and changelog.
5. **Test harness** (`ai/build/testing.mdx`): `espos ai test <url>` in the existing espos CLI: sends recorded
   turns (each scope, each action, nothing heard, a held delete, a slow-answer check), validates the answers and
   times them. It works on a laptop with no device.
6. **Use a model with PawBoop AI's persona** (`ai/build/personas.mdx`): how to use the `persona` block to speak as
   the chosen dog; voices; moods and what each looks like on the screen.
7. **Publish to the store** (`ai/build/publish.mdx`): the review list (privacy page, region, retention, uptime, the
   harness passing).
8. **PawBoop AI on a self-hosted server** (`ai/self-host/*.mdx`)
   - Cloud models: any OpenAI-style API (`ESPOS_AI_URL`, `_KEY`, `_MODEL`): Google Gemini, OpenAI, OpenRouter, Groq,
     with a table of tested models.
   - Speech from big vendors: speech to text (`ESPOS_STT_*`: OpenAI, Groq Whisper, Google), voices (`ESPOS_TTS_*`:
     OpenAI, Google, Azure, or a hosted-style `/v1/tts`).
   - Everything on your own machine with the ai-kit: a local model, local speech to
     text, local voices; the short prompt; expected speed on common hardware.
   - Pointing a device at your own AI on the same network.
9. **Privacy and security** (`ai/privacy.mdx`): what never leaves EspOS, refs, retention, signing, how to remove an AI.

---

## 6. Mockups (local files, for review before coding)

The mockups of this design (not in this repository):

| File | Shows |
|---|---|
| `default-apps.html` | the slot picker (screen saver and assistant) on the device and the web, with assistant apps and "None" |
| `assistant-install.html` | an assistant app's install sheet with its permissions; the generic permissions screen; the developer side (keys page, `espos ai secret`, `espos ai test`) |
| `device.html`, `bot.html` | the device and Bot screens (which assistant answered, not answering, no assistant installed) |
| `picker.html`, `add.html` | superseded by the corrected model (kept for the record, marked as such) |

---

## 7. Decisions (owner: all 11 recommendations "ok", 2026-11-21), re-checked against assistant apps

| # | Decision | Status under the assistant-app model |
|---|---|---|
| 1 | Account default + device override | holds: it is the assistant slot (`slot.assistant` per device, an account default for new devices) |
| 2 | PawBoop AI inside the server first; an HTTPS endpoint later, for paying accounts | holds |
| 3 | When an assistant does not answer: it says so; PawBoop answers only if the person ticked it | holds, as a slot option; needs PawBoop AI installed |
| 4 | Your own AI free on every plan; the trial counts PawBoop turns only; our voice for third-party turns within a monthly cap | holds: "your own AI" = any third-party assistant app |
| 5 | Raw audio off by default, with a warning | changes form: the permission `assist.audio`, which an app must ask for and the person accept at install, with the warning on the install sheet |
| 6 | Bulk deletes from a third-party assistant refused in v1 | holds |
| 7 | Computer control off by default; no `claude.*` in v1 | changes form: the permission `desk.control`, asked for and accepted at install; `claude.*` never for third parties in v1 |
| 8 | Third parties speak as our paid dogs only for dogs the person owns | holds |
| 9 | Reviewed store listings | holds for store submissions; `espos upload` to one's own devices stays unreviewed |
| 10 | Order: the protocol and pawboop_brain first, then the global work, then third parties | holds (phase 3 of app-platform-plan.md) |
| 11 | The name "EspOS Assistant Protocol" (EAP v1) | holds |

