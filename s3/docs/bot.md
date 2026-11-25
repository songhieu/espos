# EspOS Bot firmware (env `bot128`)

The Xiaozhi-style ESP32-S3 N16R8 board with a 128x64 SSD1306 OLED (I2C SDA 41 / SCL 42), an I2S MEMS mic (WS 4,
SCK 5, DIN 6), an I2S amp (DOUT 7, BCLK 15, LRCK 16) and one button (BOOT). The firmware lives in `src/bot/`.

- There is no LVGL. The S3 envs leave `src/bot` out (`build_src_filter`), so the S3 image is byte-identical apart
  from build times. This was checked on 2026-11-20: 3,884,032 B both ways.
- It reuses the S3's wake-word core (`src/wake/wake_fe.c`, `wake_rt.c`), the licence (`src/lic.cpp`), the trace
  and the cues (`src/cues.h`).
- The wake model is the package's own `wake/pkg/dev.espos.wake.gau/gau.ewm`, built in through `embed_files`.

```
pio run -e bot128            # the firmware (1.7 MB of a 6.25 MB slot, default_16MB.csv, OTA)
sim/bot/run.sh               # host: tests (logic, wrap, QR, dates), the turn (sim/bot/flow.cpp: talk.cpp on a fake clock),
                             # every screen -> tools/bot/out/ui.html
python3 tools/bot/gen.py     # bot-art's art2 (tools/bot/art2) -> src/bot/art.{h,cpp}: looks, 21 standby screens, fonts
```

## Setup, pairing, buttons

**Way 1 (a new bot, by phone):**
- The OLED shows the bot's WiFi (`EspOS-XXXX`) with a QR code, its WPA2 password (8 digits, the device's own) as text.
- The phone page (`portal.cpp`) uses plain forms. "Kết nối" answers at once with "Đang kết nối WiFi…"; the page then refreshes `/status` every 2 s.
- The join starts 0.8 s later. The result comes from the WiFi reason code:
  - "Đã kết nối!", or
  - "Sai mật khẩu WiFi", "Không thấy mạng WiFi" or "Chưa vào được WiFi", with the form again.
- The join happens on the router's own channel: the setup WiFi moves there first, so the radio doesn't hop between
  two channels during the handshake.
- It tries up to 4 times in 30 s. It says "wrong password" only after 2 password-type failures (reason 2, 14, 15,
  202 or 204: some routers refuse the first join after a reboot), and "not found" only after 2 not-found results
  (201, 210–212). Reasons 8 (our own leave) and 36 (the Arduino core's own retry) don't end a try.
- The hardware headers X-Espos-Hwid / X-Espos-Mac4 go on every request: pair/start, whoami, sync, ping, and assist.
  The AI trial is counted per board.
- A network is saved only when the join worked. The bot's WiFi stays up until the phone has seen "connected" (+15 s, or 3 minutes).
- The OLED mirrors each step.

**Then, still way 1:**
- A unit with a claim code (Kho máy) shows its claim code and QR.
- Otherwise it pairs by itself, the way the S3 does:
  - `POST /v1/pair/start` with a 6-digit code and the hash of a token made on the device;
  - the OLED shows the code and a QR of `https://espos.dev/app#/pair?code=`;
  - `GET /v1/device/whoami` with that token every 5 s, until the web confirms;
  - then "Xong! Chào bạn nha".
- A test build with `-DBOT_PAIR_PAUSED` makes no pairing call (a server without the bot's routes).

**Way 2:** `ESPOS:PROVISION` over USB.

**Lost WiFi** (a router that is off, a move, a changed password):
- **Offline more than 20 s:** "Mất WiFi <tên> · Đang thử lại · bấm nút để cài WiFi". Standby and the night clock show a small crossed-out WiFi mark.
- **The setup WiFi opens by itself** when:
  - the bot has been offline 2 minutes;
  - or, at boot, two full rounds of the saved networks failed;
  - or a saved network refused its password 3 times;
  - or nothing is saved.
- **It never opens** while online or during a turn. A press of BOOT or Vol+ while offline opens it at once.
- **While it's up:** the reason shows for 4 s of every 12 ("Mất WiFi <tên> · Cài WiFi mới trên điện thoại" / "Sai mật khẩu WiFi <tên> · Cài lại trên điện thoại").
- **The saved networks are still tried** every 30 s, only when one is in the scan and no phone is on the setup WiFi. When one comes back the bot joins it, and the setup WiFi closes 20 s later.

**Buttons** (active low):

| button | short press | held 5 s | twice quickly |
|---|---|---|---|
| BOOT (GPIO 0, inside the case) | talk | forget the WiFi | |
| Vol+ (GPIO 40, the head pat) | talk | forget the WiFi | volume +10 |
| Vol− (GPIO 39) | volume −10 | | |

The console's `PINS` shows the three as they are read.

## Art (art2)

- **Looks:** B "Mắt to" (`bot.style` eyes, the default), C "Chó pixel" (pixel), D "Nét mảnh" (line). Each has 9 scenes, and its reply box takes 13-px lines.
- **Standby screens:** picked by id in `bot.screens`: clock_bold, clock_thin, clock_7seg, clock_dots, clock_analog, clock_flip, date_page, month, tet, weather, kennel_night, butterfly, newspaper, coffee, window_rain, aquarium, eyes, gold, proverb.
- **The weather screen:** `weather` shows the real sky from the server's icon: rain, storm or snow → weather_rain; cloud or fog → weather_cloud; cloudsun → weather_cloudsun; sun → weather_sun. Between the city's sunset and sunrise (`weather.rise` / `weather.set`, "HH:MM", from the server since 2026-11-25) clear skies show `night`. Without those times it goes by the icon (moon = night), then 18:00–06:00.
- **Older ids:** weather_sun, weather_rain and night were separate screens until 2026-11-25; in `bot.screens` they now mean `weather`, once. The console's `UI screen <id>` still shows any one picture exactly. The first firmware's `clock` / `date` / `dog` map to clock_bold / date_page / eyes.
- **Default rotation:** clock_bold 20 s, date_page 8, weather 8, eyes 20, aquarium 15. Coffee replaces the calendar page 06:00–09:00.
- **Waiting for data:** a screen waits for what it shows (weather, the gold price from `dashboard.gold`/`fx`, the news from `dashboard.news.top`).
- **Live fields:** `ui.cpp` draws digits, text (a marquee or wrapped and scrolled, never "…"), hands, the seconds run and the month grid, following `standby2.draw_live`.
- **Drift:** screens marked drift move ±2 px every 3 minutes.
- **Lunar date:** the device's own maths (`src/lunar.cpp`, the S3's).

## Simulator

`tools/bot/sim.sh` runs the board's own code from `src/bot` in a window. Only the hardware parts are swapped:

- `sim/bot/sim.cpp`: the OLED, the button and the sound.
- `net_sim.cpp`: WiFi and the setup page.
- `transport_curl.cpp`: HTTPS.
- `shim/`: the Arduino and FreeRTOS stand-ins.

```
tools/bot/sim.sh                    the window: Space or the round button = BOOT (5 s held: forget the WiFi), ↑ = Vol+,
                                    ↓ = Vol-,
                                    S = a picture of the panel and of the window (sim/bot/out), Q = quit
tools/bot/sim.sh --provision        linked to the stand-in server first, as the USB installer would
tools/bot/sim.sh --reset            a new bot: the setup WiFi screen, then the page at http://localhost:8099
tools/bot/sim.sh --headless sim/bot/scripts/turn.txt   no window: the script in, sim/bot/out/turn/ui.html out
BOT_SIM_TRACE=1                     the trace in the terminal as it happens
BOT_SIM_LIVE=1                      the real api.espos.dev (live AI/TTS calls: only with an OK)
```

- **The terminal is the USB line:** `ESPOS:INFO`, `ESPOS:PROVISION {...}`, `ESPOS:FACTORY_RESET`, and the console (`HELP`).
- **Sound:**
  - The Mac's microphone goes into the same front end and wake network. The model is read from `wake/pkg/dev.espos.wake.gau/gau.ewm`.
  - The voice and the cues play on the Mac's speaker.
- **Server:** `tools/bot/fixture_server.py`, at 127.0.0.1:8098. It answers the bot's routes in the server's shapes, with canned replies and the Mac's own `say` voice.
  - Every request goes there, whatever api a provisioning line names, unless `BOT_SIM_LIVE=1`.
  - Its `/_fixture/*` knobs play the web side: a setting changed, the next reply, the claim entered, the trial's turns left.

**Scripts** (`sim/bot/scripts/*.txt`), one command per line:

| line | what |
|---|---|
| `wait MS` | wait |
| `press` | a short BOOT press |
| `volup`, `voldown` | a press of a volume switch |
| `curl ARGS` | a phone or the web, in the background |
| `page` | prints the setup page's state |
| `hold MS` | hold BOOT |
| `say FILE.wav` | plays into the mic (`${VAR}` reads the environment) |
| `serial LINE` | a USB line |
| `shot NAME` | a PNG of the panel |
| `gif NAME MS` | a GIF of the panel |
| `window NAME` | a picture of the window (not headless) |
| `expect ui STATE` | fails the run when the screen is another one |
| `expect talk active\|idle` | fails the run when the turn is in another state |
| `expect portal on\|off`, `expect online yes\|no` | the setup WiFi, the network |
| `quit` | ends the run |

Headless runs also write what the speaker played: `speaker.wav`.

- `boot.txt`: setup → WiFi → "set me up" → provision → standby.
- `turn.txt`: BOOT → question → think → reply with voice → follow-up → standby.
- `wake.txt`: needs `BOT_SIM_CALL`, a recording of a real "gâu gâu". The Mac's synthetic voice does not wake the model.
- `setup.txt`: the phone page with a wrong password, then the right one → pairing code → the web confirms → hello → standby.
- `buttons.txt`: Vol−, Vol+ twice, Vol+ once (talk).
- `lostwifi.txt`, `newpass.txt`, `boot_offline.txt` (with `BOT_SIM_ROUTER=down`): the router played over the console
  (`NETSIM down|up|newpass`). A drop and a return, a changed password, a boot with no network.
- `coffee.txt`: with `BOT_SIM_TIME=HH:MM` (a fake local clock, +7), the morning coffee and its greeting.
- `setup_chars.txt`: a network name with spaces and a password with signs, saved exactly as typed (the page's
  form encoding).

Headless runs use the setup page on port 8199, so a window can keep 8099.

## Modules

| file | what |
|---|---|
| `app.cpp` | The main loop. It picks the screen: update, setup WiFi, claim, "set me up", night, standby. It also runs the BOOT button, alarms and brightness, and starts updates after a quiet minute (never at night). |
| `ui.cpp`, `gfx.cpp` | Every screen, drawn into a 1 KB framebuffer in SSD1306 order. Pure C++, so the host build draws the same pixels. Replies wrap and move up a whole line at a time, never cut and never "…". |
| `art.cpp` | Generated by `tools/bot/gen.py`: looks B "eyes", C "pixel", D "line", the fonts F11 (Ubuntu 11, every Vietnamese letter) and F16B (ASCII), and the digit sets. |
| `logic.cpp` | The server's settings read into values (style, screens, night hours, alarms, local time). Host-tested. |
| `store.cpp` | NVS "bot": token, api, claim code/url, settings plus the dirty keys, saved WiFi (5 at most), mic gain. |
| `net.cpp` | WiFi: the saved networks in turn. Its own open WiFi `EspOS-XXXX` with a captive page (network + language) when none is saved, after BOOT is held 5 s, or after 3 minutes offline. NTP. |
| `link.cpp` | HTTPS on its own task: device_sync (45 s ping, 15 min sync), claim (401 not_linked), assist, OTA (sha256-checked, marked valid after the first good sync). |
| `ear.cpp` | The mic task (10 ms blocks, 24-bit, gain 16x by default), the levels/VAD, "Gâu gâu", and the question's recording (WAV). |
| `spk.cpp` | The amp: an 8 s PCM ring, the streamed voice, the cues, the alarm tone, and `outputSince()` for the echo gate. |
| `talk.cpp` | The turn: listen (6 s for the question to start, it ends 2 s after speech, 9 s at most) → assist → reply → a follow-up, or back to standby after 20 s. |
| `usb.cpp` | The USB lines (below). |

## Server contract

The server side belongs to admin-users: `web/scripts/bot-keys.go.txt`, `web/scripts/test-bot.py`.

- **Sync body:** `fw` "espos-bot-x.y.z", `board` "bot128", `kind` "bot", `settings_rev`, dirty `settings` [{k,v,age_s}], `wifi` {saved, acks}, `diag`.
- **Sync answer:** `settings`, `settings_rev`, `lic`, `weather` {city,t,icon,desc,hi,lo,hum,rise,set}, `dashboard.lunar`, `wifi_ops`, and `fw_update` (by `bot.ota`).
- **Keys:**
  - `bot.style` eyes|pixel|line
  - `bot.screens` [{id: clock|date|weather|dog, s}]
  - `bot.bright` 5-100
  - `bot.flip` "1" = the panel turned 180° (the SSD1306's own remap A0/C0, applied live), default "0"
  - `bot.night` "HH:MM-HH:MM"
  - `bot.wake` 0 off / 1-3
  - `bot.follow`
  - `bot.voice` 1 (default: the owner wants it to always speak, as if a speaker is fitted) or 0
  - `bot.volume`
  - `bot.alarms` [{h,m,days,on,label,date}]
  - `bot.ota` stable|beta|off
  - `bot.dog`
  - `sys.lang`
  - `sys.tz_min`
- **Assist:** `POST /v1/device/assist` with the WAV.
  - `bot.voice` 1: `?stream=1&say_rate=16000`. The words come in the X-Assist header first and show at once, then the voice streams to the amp.
  - `bot.voice` 0: `?voice=0`, a JSON answer.
  - Either way the query also carries `&wake=1&cid=&dog=`.

## USB lines (115200, the S3's provisioning protocol)

```
ESPOS:HELLO <epoch ms>   -> ESPOS:READY {"fw","kind":"bot","board":"bot128","caps":["claim","softap"],"linked"}
ESPOS:INFO               -> ESPOS:INFO {fw,kind,board,mac4,hwid,token_sha8,claim_tail,linked,wifi[]}
ESPOS:PROVISION {token,api,now_ms,wifi[],settings,settings_rev,claim,claim_url,device}   -> ESPOS:OK | ESPOS:ERR why
ESPOS:CLAIM {claim,claim_url}   ESPOS:FACTORY_RESET (settings, WiFi, language go; token and claim stay)
CMD:WIFIADD ssid<TAB>pass
```

**Test console:**

| command | what |
|---|---|
| `SHOT` | 2048 hex digits of the framebuffer |
| `STATE` | the bot's state |
| `UI listen\|think\|happy\|sad\|talk <w>\|claim\|ap\|note <t>\|alarm\|night\|screen <id>\|look <n>\|lang vi\|en\|auto` | show a screen |
| `FAKE <status> <json>` | an assist answer without the network |
| `MIC` | the mic's levels |
| `MICGAIN <q8>` | the mic's gain |
| `WAKEREC <s>` | a WAV over serial |
| `SET <k> <v>` | change a setting here (sent on the next sync) |
| `SYNC` | sync now |
| `TRACE` | the trace |
| `BOOT short\|long` | press the button |
| `REBOOT` | restart |

## Not done yet / to check on the board

- Mic level against the S3's codec mic. The wake model uses absolute log-mel levels, so set the gain with `MIC` /
  `WAKEREC`. A short fine-tune on this mic may be needed.
- The 11 px Vietnamese letters with two marks (ồ, ẩ...) are hard to tell apart at this size.
