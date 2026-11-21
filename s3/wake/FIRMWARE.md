# "Gâu" wake word on the S3: device flow and firmware plan

Status (2026-11-19): built in firmware 0.5.5 (sim tested; board test below). The design as it is in the code:
`src/mic.cpp` (the mic), `src/wake/` (front end, runtime, listener), `src/pet/talk.cpp` (the AI turn),
`src/pet/summon.cpp` (the pop-up's mic button and states), `src/pet/book.cpp` (settings rows), `src/os.cpp` (status-bar
dot, Control Center tile), `src/desk.cpp` (`rawSend`: the turn on the kept TLS connection).

## 1. What the person sees

| state | screen | dog | mic audio |
|---|---|---|---|
| off (default) | nothing | as now | mic off |
| waiting for "Gâu" | an orange dot right of the clock in the status bar | as now | stays in the device: 2 s ring in PSRAM, overwritten all the time |
| heard "Gâu gâu" | the dog's pop-up (summon.cpp) opens over any screen (the saver wakes): "I'm listening / Say what you need" (vi: "Đang nghe đây / Bạn cần gì cứ nói"), the mic button red with a ring that follows the level, hint "Tap when done" | `alert` (ears up) and, when the person paused after the call, a short soft "bloop" at once (`sound::blip`); none when they are already saying the question | recording: the last 1.2 s (the "gâu gâu" itself) + what follows |
| no question within 5 s | "Mình đây. Gọi lại, hoặc giữ nút rồi nói nha" | `plead` | dropped: "gâu gâu" + silence is never sent |
| the question (loud 0.28 s, the bloop not counted), then 0.8 s under the noise floor + 9 dB, 10 s at most, or a tap on the mic | "Thinking" | `look_up` | `POST /v1/device/assist?stream=1&wake=1` |
| the server made nothing out of it (`silent`) | the dog's own "say again" line, each dog its way (Bao: "Chú nghe chưa rõ, nói lại nghen bây.") | `alert` + its recorded alert line | |
| push to talk (hold the round mic button in the pop-up) | "Listening / Let go to send" | `alert` | from 0.1 s before the press to the release (12 s at most; under 0.3 s: "Hold the button while you speak") |
| answer | the bubble: the reply, what it heard small under it | the answer's `mood` | the voice plays as it streams (1 s buffered first); the wake word is held during the turn and 300 ms after the speaker |
| follow-up (after a voiced answer) | the answer stays, "Listening" under it | `alert` | 6 s of listening, no call needed (its own voice not counted); then back to waiting for "Gâu gâu", the pop-up stays ~20 s |

The wake word works on every screen, in the dog's pop-up and the pet game too; a second "Gâu gâu" while it waits for the
question starts the call again. The screen saver never shows over the dog's pop-up.

The RGB LED (`src/led.cpp`, boards with CAP_RGB_LED; `s3.assist` "led" 0/1/2, "led_bright" 1-3): wake = white-cyan
flash, listening = cyan-blue breathing (1.6 s), thinking = blue-to-violet pulse (0.8 s), speaking = amber following the
voice, didn't catch it = two orange blinks, done = 400 ms fade; something arriving (a web change, a notification) =
one soft green glow. 50 Hz task, gamma, 8/20/40 % caps, 25 % in quiet hours.

- "Gâu, mấy giờ rồi?" said in one breath works because of the pre-roll: the recording starts 1.2 s before the
  detection, so the request right after "gâu" is never cut. The server strips the leading "gâu"/"gâu gâu" from
  `heard` (already done by ai-assist: `?wake=1`, `talk.go`) and answers nothing when nothing follows it.
- No earcon when it wakes: a beep would land on top of "mấy giờ rồi" in the recording and the speaker gate would
  blind the listener. The reaction is visual (ears up, pop-up). An optional soft tick can be a setting later.
- A real dog barking, the TV saying "gâu gâu" or the dog's own voice: see section 5.

## 2. Settings

`s3.assist` (synced, already the dog assistant's prefs) gets three keys (written only once changed from the
defaults); the server checks them (`checkAssist` in put_device_settings and device_sync) and the web's Trò chuyện tab
shows the same rows. On the device they are in the dog's book (PawBoop › book › ASSISTANT).

| key | values | default | row (Cài đặt › Trợ lý, and PawBoop's settings) |
|---|---|---|---|
| `wake` | 0 off, 1 only on the charger, 2 always | 0 | "Gọi bằng 'Gâu'": Tắt / Khi cắm sạc / Luôn luôn |
| `wake_word` | `gau2` ("Gâu gâu", fewer mistakes), `gau` ("Gâu" alone also works) | `gau2` | "Cách gọi" |
| `wake_sens` | 1 low ("gâu gâu" >= 0.98), 2 normal, the easy mode (>= 0.8, or any "gâu" >= 0.95), 3 high (>= 0.6 or 0.85); overridable by the package's wake.json | 2 | "Độ nhạy" |
| `calls` | which calls wake it, bits: 1 "Gâu gâu", 2 "Ki ki", 4 "Lu lu", 8 "Cún ơi", 16 "Milu" (or "Milu ơi"); never 0 | 31 (all five) | "Tiếng gọi" (several chips on at once) |

**More calls (2026-11-21).** The owner asked for "ki ki" next to "gâu gâu", then "lu lu", "cún ơi" and "milu". They
are more outputs of the same model (one network, the same CPU: only the last layer grows), named `kiki`, `lulu`,
`cun`, `milu` in the `.ewm`. Their thresholds come only from the package's wake.json (`"thr": {"kiki": [low, normal,
high], ...}`); a model without an output, or a wake.json without its thresholds, leaves that call off, so an older
listener package keeps working with this firmware. A model with more than four outputs uses `.ewm` version 2 (the
names and thresholds of outputs 5-8 right after the header); firmware from before refuses it and keeps the model it
has, so the package's `min_fw` goes up with it. The EspOS Bot hears "Ki ki" too when its built-in model has it
(`src/bot/ear.cpp` `THRK`). Training: `train/texts.py` WAKE2 / CONF2, `gen_tts.py --words2`, `build_sets.py
--words2`, `train.py --nout 6 --init m64d`, `evaluate_calls.py` (README section 3).

Turning it on the first time shows one screen: "Máy sẽ nghe liên tục để chờ tiếng 'Gâu'. Âm thanh chỉ xử lý trong
máy; chỉ sau tiếng 'Gâu' máy mới gửi câu bạn nói để chú chó trả lời." with "Bật" / "Để sau". When the package is
missing the row offers "Tải giọng gọi 'Gâu' (40 KB)". The Control Center gets a mic tile (tap = pause listening
until the next restart or until it is tapped again) so the person can mute it without digging into settings.

## 3. The package `dev.espos.wake.gau`

```
manifest.json   {"id": "dev.espos.wake.gau", "kind": "native", "engine": "wake", "type": "pack",
                 "version": "1.0.0", "code": 10000, "min_fw": 505, "part": "core", "i18n": {"vi": ...}}
gau.ewm         the int8 model (header + weights, CRC-checked), 37.6 KB
wake.json       {"thr": {"gau2": [0.99, 0.98, 0.95], "gau": [0.995, 0.99, 0.98]}}   (low / normal / high sensitivity)
```

- A `type: "pack"` with engine `wake`: installed on "Máy" in LittleFS like a language pack (38 KB), copied to PSRAM
  when the listener starts (`pkg::loadAll`); on the card the same. `tools/s3.py pkg` puts it on a board over USB
  before it is in the store. Another word later ("meo meo" for a cat) is another
  package with the same engine; the firmware does not change as long as the model uses the same layer types.
- The `.ewm` header carries the front end's quantisation, the network shape, default thresholds, smoothing and
  the refractory time; `ewm_init` refuses a file with a wrong magic, size, CRC or shape.

## 4. Firmware changes (as built)

| file | what | flash (measured) |
|---|---|---|
| `src/wake/wake_fe.c,h`, `wake_rt.c,h` | front end + runtime, plain C, no allocations; `wake/host` builds the same files | 4.0 KB |
| `src/sound.cpp` | TX and RX made together (`i2s_new_channel(&chan, &tx, &rx)`, I2S0 full duplex), ADC registers as the board self-test (0x17 0xC8, 0x14 0x1A, 0x16 0x04), 4 DMA buffers of 10 ms each way, `micChannel()`, `outputSince(ms)` | ~0.5 KB |
| `src/mic.cpp` (new) | the only reader of the mic: a task on core 0 (prio 6, 4 KB stack in PSRAM), 10 ms blocks, the slot that carries the mic picked from the first second's levels, DC blocker, lost-block counter | 1.3 KB |
| `src/wake/wake.cpp` (new) | the listener: 2 s ring, the wake word (model from the package, thresholds from its wake.json), speaker gate, recordings for turns (pre-roll from the ring), level + noise floor, console WAKE / WAKEREC / WAKEPAUSE / PKGPUT / SETTING / RESTART | 5.2 KB |
| `src/pet/talk.cpp` (new) | the turn: push to talk and wake turns, end of speech, `POST /v1/device/assist?stream=1` with its own HTTP/1.1 (chunked, odd byte carried), the voice ring (4 s, PSRAM) played through `sound::stream`, errors and the `/say?n=last` fallback, console ASK / TALK | 8.7 KB |
| `src/desk.cpp` | `rawSend(fn, done, ctx)`: a job that gets the kept TLS connection, ahead of the other requests | ~0.5 KB |
| `src/pet/summon.cpp`, `book.cpp`, `os.cpp`, `assist.cpp`, `main.cpp`, `devtools.cpp`, `pkg/` | mic button and states, the three settings rows + first-use screen, status-bar dot, CC tile, `s3.assist` keys, hooks, engine `wake` (LittleFS like a language pack) | ~10 KB |
| **firmware.bin** | 3,859,504 B before, **3,896,736 B** after (+37 KB; limit 3,932,160 B, 35 KB left) | |

Internal RAM: the RX DMA (4 x 640 B) and 0.6 KB more for TX (4 buffers instead of 3): ~3.2 KB. Everything else is in
PSRAM: ring 64 KB, front end 10 KB, model 38 KB (LittleFS package copied to PSRAM), net state 3.5 KB, voice ring
128 KB (first turn), a recording up to 12 s = 384 KB while it exists.

CPU (core 0, where WiFi and the sound task are; LVGL stays on core 1):
- front end: one 256-point complex FFT + 40 bands + 40 `logf` per 10 ms: ~30-40 k cycles, ~1.5 % of a 240 MHz core;
- network: 32.6 k MAC per 30 ms step (1.09 M MAC/s). The compiled inner loop is 7 instructions per MAC
  (Xtensa zero-overhead loop, checked in the `-O2` assembly): ~8 M cycles/s, ~3.5 %; plus cache refills for the
  37 KB of weights (~1.5 %). Total about 6-7 % of core 0. If needed, 4-at-a-time loads or the S3's SIMD (esp-nn
  style) bring the network under 1.5 %.
- nothing runs on core 1, so LVGL's frame time does not change except for the shared cache and PSRAM bus.

To confirm on the board in phase 2: `WAKE` prints µs per frame; the target is under 400 µs per 10 ms frame on
average and LVGL's FPS in `tools/qa.py` within 1 fps of the build without it.

## 5. Mistakes and how each is handled

| cause | handling |
|---|---|
| its own speaker (the dog's TTS answer, pet lines, chimes, alarms, taps) | the mic frames are ignored while `sound` outputs anything and 300 ms after; then the network state is reset and detections are held for its 1.4 s receptive field. No echo canceller needed (and ES8311 has none). |
| TV, radio, people talking | the model is trained against 60+ h of Vietnamese/English speech and music; numbers in `reports/`. "Gâu gâu" (two barks) is the default because one syllable alone is easy to say by accident in Vietnamese (câu, cao, đâu, sau, ngâu...). |
| someone reads "con chó kêu gâu gâu" | it wakes (it is the word). The server hears no request after it and the dog stays silent (`?wake=1`, empty `heard`): the cost is one short upload. |
| a real dog barking | trained as a negative where the licence allows; real barks from ESC-50 are used to measure it (eval only, NC licence). A false wake from a bark sends at most ~2 s of audio and gets no answer. Trade-off: people who bark very realistically ("woof") instead of saying "gâu" are less likely to wake it. |
| the person's own voice not recognised | `wake_sens` 3, then `WAKEREC` to collect a few of their "gâu gâu" and retrain (the package updates through the store). |

Privacy: before a detection nothing is stored except the 2 s ring in RAM, and nothing leaves the device; the
status-bar dot shows whenever the mic is open. After a detection only that one recording is sent, and the server
keeps no audio (`assistant-ai.md` §7). A false wake is visible (the pop-up opens) and can be cancelled with a tap.

## 6. Low power

The listener keeps I2S and the codec ADC clocked and costs ~6 % of core 0, so it is not free on battery. "Khi cắm
sạc" (`wake` 1) listens only while `power::usb()`; in "Luôn luôn" it also runs on battery. It does not stop for the
saver or standby: a desk device should answer when the screen is dark (the pop-up wakes the screen). It stops in
OTA install mode, during a J2ME game that uses the sound, and while push-to-talk records (REC owns the audio then).

## 7. Order of work in phase 2

1. `mic.cpp` + duplex I2S + ES8311 ADC registers, `WAKEREC` to record raw audio from the board: check the level
   (speech at 1 m should peak around -20..-10 dBFS), the noise floor and the DC offset.
2. Feed those recordings through `wake/host/wakecli` on the Mac: real mic, real room, your voice. If recall
   is low, add the recordings to training (positives from `WAKEREC`, negatives from a few hours of the room).
3. `wake.cpp` with the speaker gate and end-of-speech, `WAKE` stats; then the pop-up states and settings rows.
4. Board QA: 30 min of TV/radio next to it (count wakes), 50 calls from 1 m and 3 m, while the dog talks, while
   music plays, on battery; FPS and internal RAM before/after (`tools/qa.py`, `MEM`, `TASKS`).
