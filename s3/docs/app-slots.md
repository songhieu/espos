# App slots: savers, assistants, pop-ups, on every EspOS S3-family device (design)

Status: design, approved by the owner on 2026-11-21 ("ok làm hết toàn bộ và hiện tại cái này quan trọng nha"). The
implementation plan is [app-platform-plan.md](app-platform-plan.md). Two companion documents:

- [assistant-providers.md](assistant-providers.md): the assistant slot's server side, the **EspOS Assistant
  Protocol v1** (the dog's brain as a provider anyone can build).
- [manifest-driven.md](manifest-driven.md): one package runs on the device and is configured from the server and
  the web, everything declared in the manifest (settings schema, `app.<id>.*` values, secrets, server-side fetch
  rules, web pages).

The owner's words, which set the goals:

> "mấy cái AI dạng này hay cái hồ cá là app thôi tùy chọn cắm vào. kiến trúc app cho phép dev các app làm thay màn
> hình chờ như hồ cá và gọi popup lên giống AI chó..."
>
> "mục tiêu là xây 1 kiến trúc cho mọi người tự dev đơn giản có ví dụ đơn giản về phần này và thêm phần ref tới code
> app trong repo của mình"
>
> "còn việc đóng gói app để bán như mình thì nó là 1 tầng cao hơn để handle -> cái này làm sao cho chuẩn nha ko để
> mình tách xa kiến trúc của github đó."
>
> "cái màn oled kia cũng trở thành 1 phần được support bởi hệ thống này luôn"
>
> "x4 giờ mày tạm thời bỏ nó ra đi chưa làm tới nó được trên github bỏ nó ra đi cũng đc mình cố gắng support dòng
> esp32-s3 trước"

In short: the aquarium and the AI dog are apps you plug in or not. Anyone can build an app that replaces the screen
saver, or an assistant that the wake word calls up as a pop-up, or an app that raises a pop-up. It must be simple,
with simple examples and links to our real code in the public repo. What we sell is a layer on top of the same open
architecture, never a separate one. The EspOS Bot (128×64 OLED) is a first-class device. The ESP32-S3 family comes
first; **the X4 (e-ink, ESP32-C3) is paused** and left out of this design.

---

## 0. The whole idea on one page

| Slot | What an app in it does | Picked by the user | Built-in today, moved behind the slot |
|---|---|---|---|
| **Screen saver** (`saver`) | draws the full screen while the device rests; reacts to touch (or the lever) | Settings › Default apps › Screen saver | the saver pages (clock, photos, widgets) and the aquarium; the Bot's 21 standby screens |
| **Assistant** (`assistant`) | answers when the wake word or push-to-talk calls it, in a pop-up over whatever is on screen; its brain is a server provider | Settings › Default apps › Assistant | the PawBoop dog (pet engine + PawBoop AI) |
| **Pop-ups** (`popup` permission, not a slot) | any app raises a short card over the current screen: "timer done", "someone at the door" | per app, in Settings › Apps (like notifications) | the alarm screen, the dog's pop-up, notification banners |

Three rules keep it simple:
1. **One manifest.** A package says what it `provides` (slots), what it `requires` (capabilities), what it may do
   (`perms`), and its `settings`. The store, the web and the device all read the same file.
2. **The OS does the hard parts.** Microphone, wake word, streaming, playback, the assistant pop-up's frame, pop-up
   rules, sleep, locks and trials are the OS's. An app only draws, answers, or asks.
3. **Our apps use the same door.** The aquarium is a saver app, the dog is an assistant app, the Bot's standby
   screens are saver screens. No special path for our own package ids.

What is left out on purpose (to keep the first version small): background code for third-party apps (scheduled
notifications and pop-ups cover "wake me at 7"), third-party native code (the ESP32-S3 cannot isolate it), custom
assistant pop-up layouts (the OS draws the frame; the app gives the picture, name and voice), WASM (planned, not
first), third-party selling (out of scope for now, not blocked), the X4.

---

## 1. Devices: profiles, capabilities and size classes

### 1.1 The S3 family today

| | S3 3.5" (es3c35p) | S3 2.8" (es3c28p) | EspOS Bot (bot128) |
|---|---|---|---|
| Chip | ESP32-S3, 16 MB flash, 8 MB PSRAM | same | ESP32-S3 N16R8 (16 MB, 8 MB PSRAM) |
| Screen | 320×480 colour IPS, turns (portrait/landscape) | panel 240×320; the OS draws its 360×480 UI at 2/3 | 0.96" SSD1306, **128×64, 1-bit** |
| Input | capacitive touch | capacitive touch | **one button (BOOT) and a hidden talk lever; no touch** |
| Audio | ES8311 codec: speaker + mic | ES8311: speaker + mic | I2S MEMS mic + I2S amp |
| Firmware | `env:s3desk` (LVGL 9), 3,578,736 B of a 3,932,160 B slot (about 350 KB free) | the same image (`board::detect`) | `env:bot128` (no LVGL, `src/bot/`), 1.7 MB of a 6.25 MB slot |
| Runtimes | QuickJS (JS level 1), engines (pet, run, tank, j2me), decl (format exists; S3 renderer still to port) | same | none yet: built-in screens only |

The Bot shares the wake-word core (`src/wake/wake_fe.c`, `wake_rt.c`), the licence (`src/lic.cpp`), the trace and the
cues with the S3, but has its own UI, network, store and talk code (`src/bot/*`). Bringing it onto the same OS core is
phase 5 of the plan.

### 1.2 Capabilities (the existing list, plus three)

Packages say what they need in `requires`; the device reports what it has in the sync body (`caps`, `screen`,
`rt`); the store and web show only packages that fit. Today's flags (docs/profiles.md §1.1) stay; three are added
for the Bot:

| New flag | Meaning | S3 | Bot |
|---|---|---|---|
| `screen.mono` | 1-bit pixels (black/white only) | | yes |
| `input.button` | at least one press button (the Bot's lever counts: press, long press) | | yes |
| `screen.tiny` | the short side is under 160 px | | yes |

`input.touch`, `screen.color`, `screen.fast`, `audio.in`, `audio.out`, `mem.psram` keep their meaning (the Bot has
`audio.in`, `audio.out`, `mem.psram`, not `input.touch` or `screen.color`).

### 1.3 Size classes

A screen's class decides which layout an app draws, so one package can serve the Bot and the 3.5" board:

| Class | Short side | Devices today | Layout rule |
|---|---|---|---|
| `tiny` | under 160 px | the Bot (128×64) | text and icons only; 1-bit; 2 to 4 lines |
| `small` | 160 to 319 px | the 2.8" panel (drawn as `medium` at 2/3 by the OS) | |
| `medium` | 320 to 479 px | the 3.5" board (320×480 / 480×320) | the reference class for colour apps |
| `large` | 480 px and more | future 4.3"/7" boards (480×800, 800×480) | |
| `round` | a round panel | future | |

- Declarative files carry one `layout` per class or size (the rule of docs/profiles.md §4.2: same size, else the
  closest ratio, scaled).
- JS gets `g.w`, `g.h`, `g.cls` (`"tiny"`, `"medium"` and so on) and `g.mono`, so one script can branch once at start.
- The OS's own screens stay hand-made for the 320 class today (section 10 says what that means for bigger screens).

---

## 2. Runtimes: what code an app can be

| Runtime | Who | On the S3 | On the Bot | Notes |
|---|---|---|---|---|
| `decl` (JSON layouts with data bindings) | anyone | port the existing level-1 renderer to LVGL (planned in platform.md) | **first**: a 1-bit renderer, the same file format | the simplest app: a saver face in 30 lines of JSON |
| `js` (QuickJS-ng, ES2023) | anyone | exists (level 1: `ui`, `app`, `timer`, `sound`, `notify`, `toast`, `store`); level 2 adds `gfx`, `popup`, `net`, lifecycle, Promises | later (QuickJS fits: ~250 KB of a 4.5 MB free slot); no `ui.*` (no LVGL), `gfx` only | 1.5 MB heap in PSRAM, 1.5 s per call, 16 KB stack |
| `wasm` (WAMR) | anyone | planned (platform.md: after freeing flash) | later | for speed and other languages; the same API names |
| `engine` (native C++) | **first-party only** | pet, run, tank, j2me, lang, font | the Bot's own screens | the S3 cannot isolate native code, so third parties never ship it (platform.md decision); an engine's package carries data only |

**Can third parties do what the aquarium does?** Not with native speed: the aquarium draws photo-like fish with
per-pixel blending at about 29 frames a second past LVGL (`lcd::native`, docs/tank.md). A JS or decl saver can:

| Saver kind | Realistic on the S3 | On the Bot |
|---|---|---|
| decl face (clock, date, weather, quotes, slides) | redrawn each minute or on data, no cost | yes, 1-bit |
| JS `gfx` with 20 to 60 shapes, text and small sprites | 15 to 20 frames a second (the OS draws the pixels in C; JS only issues the calls: a 30 ms JS budget per frame) | 10 frames a second |
| JS per-pixel effects (plasma, particles by the thousand) | no: too slow in an interpreter | no |
| WASM (later) | per-pixel effects at small sizes; sprites at full speed | later |

The design answer: **give JS and decl drawing primitives that run in C** (rect, circle, line, text, image, sprite
sheet with frames, gradient, blit with alpha), batch them per frame, and let the OS push the frame with the same fast
path the aquarium uses. That is enough for most savers (clocks, slides, bouncing logos, simple animated scenes). For
anything heavier the answer is WASM later, or a first-party engine.

---

## 3. The manifest: what a slot app declares

Only the new fields; everything else is the existing manifest (reference/manifest.mdx) and manifest v2
(platform.md).

```json
{
  "id": "com.example.starfield",
  "name": "Starfield",
  "version": "1.0.0", "code": 10000,
  "type": "app",
  "runtime": "js",
  "provides": ["saver"],
  "entry": {"saver": "saver.js", "app": "main.js"},
  "requires": ["screen.color"],
  "screens": ["medium", "large"],
  "perms": ["screen.awake"],
  "settings": [{"key": "speed", "type": "number", "label": "Speed", "min": 1, "max": 10, "default": 4}]
}
```

| Field | Values | Meaning |
|---|---|---|
| `provides` | `saver`, `assistant` (more later) | the slots it can fill; it then appears in Settings › Default apps |
| `entry` | `{"app", "saver", "assistant", "settings"}` | the file each part starts from; a package can be both an app and a saver |
| `screens` | size classes (`tiny`, `small`, `medium`, `large`, `round`) or exact sizes | where it can draw; the store hides it elsewhere |
| `requires` | capability flags (1.2) | what the device must have |
| `perms` | new: `overlay` (raise pop-ups), `mic` (assistant apps only, granted to the OS's recorder), `screen.awake` (keep the screen on while it runs as an app) | shown in plain words in the store and accepted at install |
| `assistant` | `{"provider": "<https url>" or "pawboop", "picture": "file", "voice": "<voice id>", "languages": ["*"]}` | an assistant app's brain and look (5) |
| `settings` | a schema | one form drawn by the device and the web (manifest-driven.md) |

What the store shows from it: a badge per slot ("Replaces the screen saver", "Assistant"), "Shows pop-ups" for
`overlay`, the permissions in plain words, "Works on this device" or why not (from `requires` and `screens`), and the
paid/trial chip from the catalog (section 11).

---

## 4. The saver slot

### 4.1 What the OS does

- Decides **when**: the existing saver timer (`s3.saver`), night hours, charging rules, wake on touch or the lever.
- Starts the chosen saver app with the screen's size, class and scale; stops it when the person comes back.
- **Pauses** it while an OS layer is on top (a notification, the assistant pop-up, Control Center) and resumes it.
- Lowers its frame rate when dimmed; stops it after `stop_after` (night) and shows the OS's dark clock instead.
- **Fallback**: an app that throws, misses its frame budget badly (3 frames over 200 ms in a row) or crashes 3 times
  in 10 minutes is stopped; the built-in saver takes over and the person gets one notification saying why.

### 4.2 The JS API for a saver (level 2)

```js
// saver.js: a ball that bounces and a clock (the "hello saver", section 9)
let x = 40, y = 40, dx = 2, dy = 3;
app.onFrame = (g, dt) => {                 // called up to 20 times a second; g is the frame
  g.clear("#081020");
  g.circle(x, y, 18, "#ffcc33");
  g.text(app.time("HH:mm"), g.w / 2, g.h - 60, {size: 48, color: "#ffffff", align: "center"});
  x += dx; y += dy;
  if (x < 18 || x > g.w - 18) dx = -dx;
  if (y < 18 || y > g.h - 18) dy = -dy;
};
app.onTouch = (e) => { x = e.x; y = e.y; };  // the S3; the Bot sends {key: "press"} instead
```

| Name | What it is |
|---|---|
| `app.onFrame(g, dt)` | draw one frame into `g`; `dt` is ms since the last one |
| `app.fps(n)` | ask for 1 to 30 frames a second (default 15); the OS may give fewer |
| `app.onStart(info)` / `app.onStop()` / `app.onPause()` / `app.onResume()` | lifecycle; `info = {as: "saver"|"app", w, h, cls, mono, scale}` |
| `app.onTouch(e)` / `app.onKey(e)` | `{x, y, kind: "down"|"move"|"up"|"tap"}`; `{key: "press"|"long"}` on button devices |
| `g.clear(c)`, `g.rect(x,y,w,h,c,{r})`, `g.circle(x,y,r,c)`, `g.line(x1,y1,x2,y2,c,{w})`, `g.text(s,x,y,{size,color,align,font})`, `g.image(name,x,y,{w,h,frame,alpha})`, `g.gradient(x,y,w,h,c1,c2)` | drawing primitives, done in C; colours `#rrggbb` (on a mono screen: light or dark by luminance) |
| `g.w`, `g.h`, `g.cls`, `g.mono` | the frame's size, class and depth |
| `app.time(fmt)`, `data.*` (with `data:*` perms) | the clock and the OS data feeds (weather, markets, news, calendar) |

### 4.3 A decl saver (no code)

A `face` file in the existing declarative format (profiles.md §4), with `provides: ["saver"]` and `runtime: "decl"`.
It redraws when its data changes (`refresh: "minute"`). One file can carry a `tiny` layout for the Bot and a
`medium` one for the S3.

### 4.4 The built-ins behind the slot

| Built-in | Becomes |
|---|---|
| The saver pages (s3.saver_pages: clock, photos, widgets, text bands) | the built-in saver app "Clock and photos" (`dev.espos.saver`, part of the OS), the default |
| The aquarium (`vn.pawboop.tank`, engine `tank`) | a package that `provides: ["saver", "app"]`; desksaver asks the slot registry instead of the hard-coded background type `tank` (desksaver.cpp:740) |
| The Bot's 21 standby screens (art2) | the Bot's built-in saver "Standby screens", still picked and timed by `bot.screens`; third-party decl screens join the same rotation |

---

## 5. The assistant slot

### 5.1 What an assistant app is

An assistant app is mostly **declarations**: a name, a picture (or an engine that animates one), a voice, and a
brain. The OS does the rest:

| The OS | The assistant app |
|---|---|
| wake word and push-to-talk (S3: hold the talk chip; Bot: the lever) | its name and picture (`assistant.picture`), or an engine that animates it (the dog) |
| recording, sending the turn to the server, streaming the voice back, the mic ring, the pop-up frame, "heard" text, the reply bubble | its brain: a provider URL (assistant-providers.md), or `pawboop` |
| the trial and lock (402/403 from the server), Do Not Disturb, follow-up listening when the reply asks something | its voice id (or the provider sends audio) |
| pausing the saver or the app underneath, and resuming it | optional: JS hooks to show a card for a reply (`app.onAssistantReply(r)`, later) |

The **server side** is the provider protocol: the device keeps calling `POST /v1/device/assist` as today; the server
routes the turn to the provider of the device's assistant app (the PawBoop AI for the dog). No firmware change is
needed for a new assistant app that uses the OS pop-up.

### 5.2 The tiny assistant (the whole package)

```json
{"id": "com.example.tiny", "name": "Tiny", "version": "1.0.0", "code": 10000, "type": "app", "runtime": "decl",
 "provides": ["assistant"], "requires": ["audio.in"], "screens": ["tiny", "medium"],
 "assistant": {"provider": "https://tiny.example.com", "picture": "tiny.png", "languages": ["*"]}}
```

Plus `tiny.png` (and `tiny-mono.png` for the Bot). Its provider is the 50-line toy server of section 9.

### 5.3 The dog behind the slot

The dog becomes the package `vn.pawboop.pet` with `provides: ["assistant", "app"]`, `assistant.provider:
"pawboop"` and engine `pet` (it animates the dog in the pop-up, as summon.cpp does today). talk.cpp's turn
(record, send, stream, play, follow-up) moves into the OS's assistant client, shared by every assistant app and by the
Bot. The pop-up frame (summon.cpp) becomes the OS's; the dog's clip area is the engine's part of it.

---

## 6. Pop-ups (the `overlay` permission)

Pop-ups reuse the notification system (notif.cpp: per-app switches, Do Not Disturb, quiet hours, history), with one
more style: a card over the current screen with up to 2 buttons.

```js
// "timer done" (section 9): a pop-up now, or at a time even when the app is closed
popup.show({title: "Tea is ready", text: "4 minutes are up", icon: "timer", buttons: ["Stop", "+1 min"],
            sound: "chime", timeout: 60}).then((b) => { if (b === "+1 min") start(60); });
popup.schedule(Date.now() / 1000 + 240, {title: "Tea is ready", buttons: ["Stop"]});   // opens the app on a tap
```

| Rule | Value |
|---|---|
| Who | apps with `overlay`, allowed per app in Settings › Apps; the server can send one for an app (a doorbell webhook, manifest-driven.md) |
| From the server (built: `src/popnote.cpp`) | a dashboard note with `app` (an installed package) becomes that app's notification; with `acts` and the app's `overlay` permission also a card (priority high, one card per app with "+N more waiting"). Note fields: `pop {head, title, mono, text}`, `acts [{id: approve \| deny \| ok, post, body, risky}]`, `exp_ms`, `arg`, `ref`. A button posts `body` + a nonce (+ `ack_risky` after a second tap on a risky one) to `{espos}/v1/device/app/<app>/<post>` with the device credential the OS adds for the app's own path (§15.1); labels come from an OS vocabulary translated on the device |
| Priority | `normal` (waits behind the assistant and other pop-ups), `high` (shows over a running app or the saver, never over an assistant turn or the alarm screen); `critical` is the OS's only (alarms) |
| Dismiss | always: a tap outside, a swipe, a button, the timeout (default 30 s); on the Bot a press = first button, a long press = dismiss |
| Rate | at most 1 every 10 s and 12 an hour per app; more go to the Notification Center |
| Quiet hours and Do Not Disturb | no pop-up, no sound: it goes to the Center (alarms excepted) |
| Size | S3: a card up to 300×220 (landscape 420×200); the Bot: the whole screen, 3 lines of text + a button hint |

---

## 7. Choosing the default apps

| Where | What |
|---|---|
| Device settings keys (per device, synced both ways like `s3.*`) | `slot.saver` (a package id, or `builtin`), `slot.assistant` (a package id, or `none`) |
| Device | Settings › Default apps: Screen saver, Assistant; each opens a list of installed apps that provide it (mockup: `default-apps.html`) |
| Web | the device page › Settings › Default apps (the same list; the assistant row opens the provider and consent card of assistant-providers.md) |
| Defaults | saver: `builtin` (the saver pages; the Bot: its standby screens); assistant: `vn.pawboop.pet` when installed, else `none` |
| Removed or broken | uninstalling the chosen app, or 3 crashes in 10 minutes, sets the slot back to its default and posts one notification; the server refuses a value that is not installed and fitting (device_sync, put_device_settings) |

---

## 8. Device APIs: what exists and what is missing

| Need | Exists today | Missing (phase) |
|---|---|---|
| JS app runtime | QuickJS level 1 (`src/js/jsapp.cpp`): retained `ui.*` tree, timers, store, notify, sound | lifecycle hooks, Promise jobs, `console` (2) |
| Full-screen drawing for JS | none (`ui.*` only) | `gfx` frame with C primitives, pushed by the OS (2) |
| Fast native drawing | `lcd::native()` strips past LVGL (tank, run) | shared by the `gfx` frame on the S3 (2) |
| Declarative drawing | the level-1 format and its X4 renderer (`Decl.cpp`), the SDK's checker (`checkDeclFile`) | an LVGL renderer for the S3, a 1-bit renderer for the Bot (2, 5) |
| Saver hosting | the saver pages (`desksaver.cpp`), the tank background type | the slot registry, lifecycle, fallback (2, 4) |
| Mic capture | `mic.cpp` (one listener: `wake.cpp`) | nothing for apps: the OS records for the assistant only (by design) |
| Streaming to the server, playing a stream | `pet/talk.cpp` (HTTPS POST of the WAV, PCM stream with X-Assist) | moved into an OS assistant client used by every assistant app (4) |
| Wake events | `wake.cpp` fires the dog's turn | the event goes to the default assistant app (4) |
| Pop-up over any screen | the dog's pop-up on LVGL's top layer (`summon.cpp`), notification banners (`notif.cpp`), `lock::sheet` | `popup.*` for apps, the queue and rules (2) |
| Net for JS | none (level 1) | `net.request` through the OS network queue with `net:<host>` perms and secrets added by the OS (1, with woo-pos) |
| Settings per app | `store.get/set` (app-private) | the schema form + `app.<id>.*` synced values + secrets (1) |
| Locks and trials | `lic.cpp`, `lock.cpp` (generic `may(id)`, the sheet, the end card), the catalog's `paid` / `free_ids` | remove the hard-coded ids (7) |

---

## 9. Simple examples, and links to the real code

Each example is small enough to read in one sitting, lives in the repo next to the existing samples
(`s3/apps/`, published as `s3/apps/` on GitHub), and runs in the simulator.

| Example | Folder | What it shows | Runs on |
|---|---|---|---|
| Hello saver (JS) | `apps/hello-saver/` | a bouncing ball and a clock with `app.onFrame` (4.2), about 25 lines | S3 (sim, QEMU, board) |
| Hello standby (decl) | `apps/hello-standby/` | a clock face in JSON with a `tiny` layout and a `medium` one: one file for the Bot and the S3 | Bot and S3 |
| Tiny assistant | `apps/tiny-assistant/` | the manifest of 5.2 and a picture; the pop-up shows what it heard and the answer | S3 and Bot |
| Toy provider | `apps/tiny-assistant/provider/` | a 50-line Python server (standard library only): checks the signature, answers the time or echoes; `test.sh` signs a request with curl | any computer |
| Timer pop-up | `apps/timer-popup/` | sets a timer, raises "Tea is ready" with Stop and +1 min (6) | S3 (and the Bot, 1-bit) |
| Settings form | `apps/hello-settings/` | a `settings` schema (text, number, enum, bool, secret) shown on the web and on the device | S3, web |

**From zero to your own saver in 15 minutes** (docs page `guides/savers.mdx`):
1. `espos create my-saver --template saver` (copies hello-saver).
2. `espos sim` opens the simulator with the saver running; edit `saver.js`, it reloads.
3. Change the colours, add a `g.image` (put a PNG in the folder).
4. `espos run --device <ip>` on your S3; Settings › Default apps › Screen saver › My saver.
5. `espos publish` (or keep it as your own app, guides/my-apps.mdx).

**From zero to your own assistant in 15 minutes** (docs page `ai/build/quick-start.mdx`):
1. `espos create my-assistant --template assistant` (manifest + picture + `provider/app.py`).
2. `python3 provider/app.py` on your computer; `provider/test.sh` checks it with curl.
3. Expose it (a tunnel, or a self-hosted EspOS server on the same network).
4. Install the package on your device; on the web: Default apps › Assistant › My assistant, paste the provider's
   address, press Test (assistant-providers.md 3.2).
5. Say the wake word.

**The full references** (github.com/songhieu/espos, branch main):

| Real code | Where |
|---|---|
| The aquarium, a native saver engine | `s3/src/tank/` (`screen.cpp` frame loop and saver background, `water.cpp`, `gfx.cpp`), `s3/docs/tank.md` |
| The saver pages (the built-in saver) | `s3/src/desksaver.cpp`, `s3/src/saveredit.cpp` |
| The PawBoop dog as an assistant (device) | `s3/src/pet/talk.cpp` (the turn), `s3/src/pet/summon.cpp` (the pop-up), `s3/src/wake/` (the wake word) |
| The dog's brain (server) | `server/internal/flows/assist_reply/talk/talk.go`, `server/internal/flows/device_assist/`, `server/internal/wm/ai.go` |
| The EspOS Bot | `s3/src/bot/` (`talk.cpp`, `ui.cpp`, `art.cpp`), `s3/docs/bot.md` |
| JS apps today | `s3/apps/` (dice, focus, vocab, water), `s3/src/js/jsapp.cpp` |
| Notifications (what pop-ups build on) | `s3/src/notif.cpp` |

---

## 10. Hardware flexibility, honestly

**Today:**
- A HAL with board descriptors (`src/hal/`, `src/boards/` es3c35p, es3c28p, qemu, `_template`, `docs/porting.md`),
  one firmware that detects its board, `CAP_*` flags mapped to the capability names, screen size and rotation, the
  2.8" UI drawn at 2/3, portrait and landscape.
- The S3 boards share one OS (LVGL). The Bot is a separate small firmware in the same tree (no LVGL).
- The X4 (CrossPoint-based, e-ink) is paused.

**The path to "as flexible as it can sensibly be":**
1. **Size classes** (1.3) for decl layouts and the JS `gfx` frame, with helpers (`g.cls`, `fit`, `scale`).
2. **Board descriptors as data** (pins, panel, touch, codec, buttons) so a new S3-family board is mostly a
   descriptor plus, when needed, a driver package (platform.md, BSP).
3. **One OS core for the S3 boards and the Bot**: packages and the installer, settings sync, the slot registry,
   the network queue, the assistant client, pop-up rules, the decl renderer with a 1-bit back end. Each device keeps
   its own shell (LVGL on the S3, the OLED UI on the Bot).
4. **WASM later** for app code that moves to other ESP32 chips (P4, C6) unchanged.
5. **A CI matrix**: build `s3desk` and `bot128`, run the host tests, take QEMU screenshots per size class
   (`tools/qemu`, `sim/bot/run.sh`).

**The limits, said plainly:**
- The OS's own screens (Home, Settings, Store, the saver designer) are hand-made for the 320 class. A 480×800 board
  would show them centred or scaled until they are redone with size classes.
- The Bot cannot show LVGL apps (`ui.*`); it runs decl screens first, JS `gfx` later.
- Another MCU family (P4, C6) is real porting work: drivers, memory, the build. Apps in decl, JS or WASM move; native
  engines (tank, pet) need work.

---

## 11. Selling apps: the commercial layer

### 11.1 The split

| Open and identical for everyone (GitHub) | Only the seller's (hosted espos.dev) |
|---|---|
| the firmware, runtimes, slots, OS UI for locks and trials (`lock.cpp`: chip, sheet, end card) | the paid content itself (dog packs, maps, the full aquarium) |
| the SDK, the manifest, the `.espk` format, signing (SHA-256 per file, Ed25519 later) | the store's signing keys and the catalog's prices |
| the store protocol (catalog, `pkg-link` download links), the licence object `lic` and the check `lock::may(id)` | accounts, entitlements, trials (admin_trials), purchases |
| the assistant protocol | PawBoop AI's model costs and routing |
| the self-host server and its importer | admin, Kho máy (stock), claim codes |

### 11.2 A paid app is a normal package plus an entitlement

- The **package** is built like any other with the public SDK. It may name the things a store can sell separately
  (`"unlocks": ["full", "guppy", "map.bazaar"]`), so the OS can lock parts generically (today's "id+thing" form).
- The **catalog** (the store's, not the package's) says what is paid, what is free, whether a trial applies:
  `"paid": true, "free_ids": [...]` (exists). The same package can be free on a self-hosted store.
- The **device** checks `lock::may("<id>+<thing>")`. The licence `lic` (from sync, kept in NVS, so it holds
  offline) and the catalog marks decide. No `lic` (a self-hosted or older server) = nothing locks (exists).
- The **OS** shows the trial chip, the lock sheet with the QR to buy, the end card. An app never draws its own
  paywall. A locked package stays installed; nothing is deleted.
- **AI turns** are the server's business: the trial counts turns of the PawBoop AI (assistant-providers.md decision
  4); a third-party provider's turns are not ours to sell.

### 11.3 Special cases in today's code, and the clean-up

| Where | Special case | Clean-up |
|---|---|---|
| `src/lock.cpp:30-32` | `TANK`, `DOG`, `MAP` id constants; `:331` and `:699` check `vn.pawboop.tank+full`, dog ids | the engine asks `lock::may(myId + "+full")`; the end card lists locked packages from the catalog |
| `src/tank/game.cpp:169-182` | `vn.pawboop.tank+<thing>` ids | the engine's own package id (passed at `open`) |
| `src/pet/app.cpp:739-750`, `src/run/run.cpp:876, 972, 1191` | `vn.pawboop.run`, `vn.pawboop.map.*` | add-ons found by `addon_of` (exists in the manifest), not by prefix |
| `src/apps.cpp:60`, `src/app_more.cpp:502`, `src/pkg/pkg.cpp:306-322` | `vn.pawboop.pet` as a built-in record on the partition | an installed package like any other; the partition record becomes a generic "preinstalled" list in the board descriptor |
| `src/pkg/pkg.cpp:343-345` | categories of three sample apps by id | read `category` from their manifests |
| server `admin_*`, `device_sync` | to audit for hard-coded ids | the catalog decides |

Order (no visible change for current customers): (1) add the generic paths beside the special ones; (2) switch each
engine; (3) delete the constants; (4) a CI test fails if a firmware file names a paid package id.

### 11.4 Third-party selling

Out of scope for now (owner, 2026-11-21: "tạm thời ko handle"). Nothing above blocks it later: catalog marks are per
package, entitlements are package ids, the lock UI is generic.

### 11.5 Self-hosters

They get everything open and the free content. A self-hosted store can mark its own packages paid and issue its own
`lic` with its own keys: the device logic is the same. The OSS guard (`scripts/sync_oss_s3.sh` and its private-string
check) keeps our paid content, keys and private data out of GitHub.

### 11.6 Guardrails that keep the repo and the product in step

- Our paid packages are built with the published SDK version only (`espos check` + `espos build` in CI).
- One firmware for both: no `#ifdef` for hosted features; hosted-only behaviour comes from the server's answers.
- The CI test of 11.3 (no paid ids in firmware).
- `sync_oss_s3.sh`'s guard stays the gate for every push.

---

## 12. The EspOS Bot in this architecture

| Slot | On the Bot |
|---|---|
| Saver | its standby screens are the built-in saver; third-party decl screens (a `tiny` layout) join the rotation `bot.screens` |
| Assistant | the same protocol and server route; the OS pop-up is the whole screen: picture or eyes, then the reply, scrolling |
| Pop-ups | full-screen, 3 lines + "press: Stop"; long press dismisses |

API on a mono, no-touch device: the same decl format (1-bit renderer, `tiny` layouts), and later the same JS `gfx`
(colours become light or dark), `app.onKey({key: "press"|"long"})` instead of touch, no `ui.*`. The Bot's firmware
has the room (16 MB flash, 8 MB PSRAM, 4.5 MB free in its app slot); decl first because it needs no runtime and the
21 built-in screens already are data (`tools/bot/gen.py` from art2).

Examples on the Bot: `apps/hello-standby/` (its `tiny` layout) and `apps/tiny-assistant/` (works unchanged).
Real code: `s3/src/bot/`.

---

## 13. For every country

- Every OS string and every sample in English first, translated into the device's 11 languages (the existing i18n,
  docs/i18n.md); manifests carry `i18n` for name and description (exists).
- Settings schemas carry labels in `i18n` too (manifest-driven.md).
- Assistant apps declare `languages`; the store says which languages an assistant speaks.
- Time zones as IANA names, units and currency from the locale (assistant-providers.md 4.6).

---

## 14. Decisions taken with the recommended option (the owner can still change them)

| # | Decision | Taken |
|---|---|---|
| 1 | Pop-ups as a permission on top of notifications, not a third slot | yes |
| 2 | The assistant pop-up frame is the OS's; apps give a picture, name, voice and brain | yes (custom layouts later) |
| 3 | Third parties: decl and JS now, WASM later, never native | yes (platform.md) |
| 4 | Defaults per device (`slot.*` keys), set on the device and the web | yes |
| 5 | The Bot runs decl first, JS later | yes |
| 6 | Paid status lives in the store's catalog, not in the package | yes |
| 7 | The saver pages stay the built-in default saver; the aquarium is an option | yes |
| 8 | The assistant provider decisions of assistant-providers.md section 7 | the recommendations there |

---

## 15. The agreed contract for phase 1 (frozen 2026-11-21, with woo-pos, fw-appset, web-schemaform, claude-term-app)

Names below are final for phase 1; a change needs all four owners. Builders: SDK + server + web form (ai-assist,
web-schemaform), device settings form + values + device secret store (fw-appset), JS net/fs/crypto/UI (woo-pos),
dictation and terminal pieces (claude-term-app).

### 15.1 Manifest

| Field | Shape |
|---|---|
| `settings[]` secret field | `{"key", "type": "secret", "label", "where": "device" \| "server", "help", "i18n"}`; no default. `device`: typed on the device or through the OS's LAN page (QR + PIN), kept in the device's secret store, never synced, never on the web. `server`: typed on the web, kept by the server, never returned |
| `auth` (new) | named bindings of secrets to requests: `{"<name>": {"basic": ["<user key>", "<pass key>"]} \| {"bearer": "<key>"} \| {"header": {"<Name>": "<key>"}} \| {"device": true}, "query": {"<param>": "<key>"} (optional fallback, tried after a 401), "send_to": ["https://host/path*" \| "{<url field>}/path*" \| "{espos}/v1/device/app/<id>/*"]}`; keys name secret fields; `{espos}` needs `net:espos`. `{"device": true}` (send_to only `{espos}/v1/device/app/<this app's id>/*`): the device's own credential, added by the OS (src/js/jsnet.cpp `ownPath`: no `..`, `//` or percent-escapes in the path); a binding of that name with a secret that is set goes instead (the later app-scoped token) |
| `perms` (v2) | `net:<host>`, `net:setting:<url field>`, `net:lan` (RFC1918 and loopback, http allowed), `net:espos` (the device's own EspOS server through the URL prefix `{espos}`, which is `desk::apiBase()`, so a self-hosted server works; the OS adds the device credential `Authorization: Device <token>` only to `{espos}/v1/device/app/<the calling app's own id>/*`, JS never sees it, and the server's guard `desk_app_auth` checks that the app is installed on that device; an app-scoped token through an `auth` binding is a later upgrade, built and switched off: desktop-protocol.md §9.0), `storage:<KB>` (`data/`, kept), `cache:<KB>` (`cache/`, the OS may evict, SD first else LittleFS ≤ 1 MB), `overlay`, `screen.awake`, `mic` (assistant apps: the OS records for the assistant), `dictate` (speech to text through the OS; the app never gets audio), `data:*`, `location:city` |
| `api` | `{"min": 2}` for packages that use any of the level-2 calls below |
| `requires`, `screens` | as §1; a touch-only app says `["input.touch", "screen.color"]` so the Bot never gets it |

Rules: an `auth.send_to` placeholder names a `url` or `string` field; changing that field clears the binding's
secrets (device ones on the device, server ones on the server), so keys never follow a re-pointed address.
Redirects are followed within the same origin only, and never for a request that carries the device credential; auth is never sent to another host; https only, except `net:lan`.

### 15.2 Sync (device ⇄ server)

| Direction | Field | Meaning |
|---|---|---|
| both | `app.<id>.<key>` in the existing `settings` (device `{k, v, age_s}`, server answers every `app.*` row) | values; newest wins; `""` = not set (kept as `""` so every device hears the reset) |
| device → server | `"app_secrets": {"<pkg id>": ["ck", "cs"]}` | which secret keys the device's store holds (every sync; `{}` when none); the web shows "Set on the device" |
| server → device | `"secrets": [{"app", "key", "v", "ms"}]` | server-kept secrets that an `auth` binding uses on the device; `"v": ""` = removed; re-sent until acked |
| device → server | `"secret_acks": [{"app", "key", "ms"}]` | the server marks it delivered when `ms` equals the row's `updated_ms` |
| server | uninstall | `app.<id>.*` rows go at the first sync whose package report no longer lists the package (and the store's wanted list neither) |

### 15.3 Web routes (end_user, device owner)

| Route | Answer |
|---|---|
| `GET /v1/devices/:id/apps[?pkg=]` | `{"apps": [{"id", "name", "version", "code", "settings": [...], "secrets": {"<key>": {"where", "set", "hint", "ms"}}}]}`; `set: null` for a device secret not reported yet; `{"apps": []}` when none |
| `POST /v1/devices/:id/settings` | as today; `app.*` refusals: 422 `{"error", "code", "key", "why"}` with `code` `invalid_setting`, `app_not_installed` or `unknown_app_key` |
| `POST /v1/devices/:id/secrets` `{"pkg", "key", "value"}` | `{"ok": true, "set": true, "hint": "<last 4>", "ms"}`; 422 codes `secret_device_only`, `unknown_app_key`, `app_not_installed`, `secret_invalid` (empty, over 2,000 characters, control characters, spaces at either end) |
| `DELETE /v1/devices/:id/secrets?pkg=&key=` | `{"ok": true, "set": false}` |

### 15.4 JS, API level 2 (the calls phase 1 needs)

| Call | Contract |
|---|---|
| `net.request({method, url, headers, body, keep, timeout, auth, save})` → Promise | `url` is an `https://` address or `{<url field>}/path` (filled by the OS). `auth: "<binding>"`. `headers` ≤ 6 (Authorization, Cookie, Host refused). `body` a string or an object (sent as JSON), ≤ 32 KB. `keep` ≤ 4 response header names. `timeout` 20 s by default, 60 s at most. `save: "cache/<name>"` writes the body to the app's files (≤ 512 KB) and answers `{status, size}`. **Resolves for any HTTP status** `{status, ok, headers, body}` (body ≤ 256 KB); **rejects only without a response**: `Error` with `code` (`offline`, `timeout`, `dns`, `tls`, `too_big`, `denied`, `busy`) and `sent` (whether the body may have reached the server). One request in flight per app, 4 queued |
| `net.online` | bool |
| `settings.get(key)` / `app.onSettings(changed)` | values; a secret reads as `{set, hint}` |
| `fs.read / write (atomic) / remove / list / free` | per-app `data/` (quota `storage:<KB>`) and `cache/` (`cache:<KB>`); wiped on uninstall |
| `crypto.sha256(text)`, `crypto.random(n)` | |
| UI nodes | `ui.grid`, `ui.split`, `ui.input` (the OS keyboard: text, number, decimal, password, search; also used for `ui.field` needs), `ui.qr`, `ui.line`, `ui.sheet`, `ui.image(src, {w, h, radius, band, fallback})`, `ui.nav(tabs, sel, onPick, {badges})`, keyed nodes that survive a re-render, button `w`/`h`/`onDown`/`onUp`, `{fill: true}`; `ui.term` (native terminal view) is a level-2 candidate with its own flash budget |
| `app.full`, `app.onBack` | no nav bar; the back gesture goes to the app first |
| `voice.dictate({lang, max_ms})` → Promise `{text}`, `voice.stop()` | `dictate` permission; the OS draws the mic ring and records; the server route `POST /v1/device/dictate` is metered like assistant turns; a 402 shows the OS lock sheet |

The device token is sent with an app's request only as the device credential the OS adds for `net:espos` to the app's own path `{espos}/v1/device/app/<its id>/*`; JS never sees it, and every other request (other hosts, other paths, other apps' paths) never carries it or the hardware headers. The
trace keeps method, host and path only (no query, headers or body).

---

## 16. Every slot app is removable, ours included (owner, 2026-11-21)

> "mấy cái AI của mình cũng có thể xóa đi như xóa 1 app bình thường thôi"

Nothing we ship as a slot app is undeletable: the PawBoop dog and its AI (`vn.pawboop.pet`), the aquarium
(`vn.pawboop.tank`), the Bot's packaged screens, the wake listener (`dev.espos.wake.gau`). Only the OS core stays: Home,
Settings, the store and installer, notifications, the built-in saver pages ("Clock and photos").

| Question | Answer |
|---|---|
| The assistant slot after the dog is removed | `slot.assistant` = `none`. The wake word and push to talk do nothing: the listener stops listening (it only listens for the slot's app), and the talk chip shows "No assistant installed · Get one" (opens the store's Assistants). The listener package can be removed too |
| How | the same flow as any app: "−" on the device, Remove on the web; the person sees what goes |
| Device data | the app's own files and settings go (`app.<id>.*`, its secrets, its save: the dog's `/littlefs` state, `s3.pet_dog` reset) as for any app |
| Account data | kept: what the dogs remember (`assist_memories`) is the person's, shared by devices; the conversation history follows its normal retention (24 h words, 7 days summaries). The web's removal sheet offers "Also forget what the dogs remember" (unticked) |
| Reinstall | from the store, like any app; paid dogs and the aquarium stay owned (entitlements are on the account) and come back as they were |
| The saver slot | removing the aquarium sets `slot.saver` back to `builtin` |

Places that assume the dog exists today, and the fix (phase 4):

| Where | Assumption | Fix |
|---|---|---|
| `src/os.cpp:601-629` | the Control Center tile shows `pet::name()` and summons the dog | the tile shows the assistant slot's app (name, picture) or "No assistant" |
| `src/os.cpp:241, 760`, `src/tank/screen.cpp:250-448`, `src/popnote.cpp:285`, `src/js/jsvoice.cpp:332`, `src/pkg/store.cpp:896` | "is the dog's pop-up up / is a turn running" through `pet::summoned()`, `pet::talk::busy()` | ask the OS assistant client (`assist::open()`, `assist::busy()`), whatever the app |
| `src/wake/wake.cpp:308`, `src/led.cpp:133` | the listener and the light read the dog's prefs (`pet::assist::prefs()`, `s3.assist`) | the slot's prefs; nothing listens when the slot is `none` |
| `src/js/jsvoice.cpp:81` | the mic icon from `pet::talk` | an OS icon |
| `src/lock.cpp:238, 297` | the lock sheet and end card speak through the chosen dog, list dogs | the sheet's voice from the assistant app if any, else plain text; lists from the catalog (§11.3) |
| `src/pkg/pkg.cpp:306-322`, `src/apps.cpp:60`, `src/app_more.cpp:502` | `vn.pawboop.pet` as a built-in record on the partition | a preinstalled package like the rest (removable; the partition record becomes the preset's preinstall list, §17) |
| `src/wid.cpp:163` | the "pet" Home widget | shown only while the app is installed; a removed app's widget leaves the page |
| onboarding (`src/onboard.cpp`) | a dog step | only when the preset or the installed apps include an assistant (§17) |
| server `assist_reply` | a missing dog falls back to `corgi` | no assistant app on the device: 409 `{"error": "no_assistant"}`; the device shows its own note |
| server `device_assist`, `device_sync` | dog defaults (`s3.pet_dog`) sent to every S3 | sent only when the assistant app is installed |

### 16.1 Built (2026-11-22, not flashed or deployed)

The slot on the device is `src/assistant.{h,cpp}` (namespace `assistant`): `id()` (`slot.assistant`, else PawBoop when
installed, else `none`; a slot naming an app the device cannot run counts as `none` until the OS client of §5.3),
`any()`, `dog()`, `name()`, `listens()`, `open()`, `busy()`, `summon(reason)`, `noneCard()`, `micIcon()`, and the
store's two hooks `removed(id, name, addonOf)` / `installed(package)` (store.cpp `afterJob`).

| Where | What it does now |
|---|---|
| Control Center (`os.cpp`) | the tile is `assistant::name()` or "No assistant"; a tap is `assistant::summon("cc")`; the mic tile only while `listens()`; the closed Control Center is built again when the slot changes (`ccFollowAssistant`, once a second) |
| `os.cpp` gestures and saver, `tank/screen.cpp`, `j2me/app_j2me.cpp`, `pkg/store.cpp` (busy), `popnote.cpp`, `js/jsvoice.cpp` | `assistant::open()` / `assistant::busy()` |
| `wake/wake.cpp` | `readPrefs` sets the mode off while `!assistant::listens()` (and re-reads on package changes); `talk.cpp` `onWake` ignores a call too. `led.cpp` keeps `s3.assist` (now the OS's assistant settings key: wake, LED); nothing lights for a turn when there is none |
| `js/jsvoice.cpp` | the OS's mic glyph (`assistant::micIcon`; `pet::talk::micIcon` forwards to it) |
| `lock.cpp` | the sheet's portrait is the dog only in PawBoop's own games or while the dog is the assistant, else a padlock; the end card lists PawBoop's dogs and maps from the catalog (`addon_of`), only while PawBoop is installed; "The AI assistant" only with an assistant |
| `pkg.cpp`, `apps.cpp`, `app_more.cpp` | already a removable package: removal erases the adopted record's headers (tombstones), so it is never adopted again; the preinstall list of §17 is a later step. The Store has an "Assistants" chip (`apps::open("store", "assistant")`: catalog `provides` "assistant", or the dog's engine) |
| `wid.cpp`, `home.cpp` | `wid::installed(kind)`: Home drops a "pet" widget once PawBoop is gone, the gallery leaves it out, a saver page's slot stays empty |
| `onboard.cpp` | the dog, name and calls steps and the dog tips only with an assistant; "All set" without the dog rows |
| server `assist_reply` (talk.go) | `slot.assistant` = `none`: 409 `{"error": "no_assistant"}` before any model call or write (device and web chat); unset or PawBoop as before. The device says "Setting up the assistant. Ask again in a moment." and syncs at once; the web chat says no assistant is installed |
| server `device_sync`, `device_ping`, `put_device_settings` | `slot.assistant` stored from the device and the web (colour desks only, `"" \| none \| <package id>`; the web's must be installed on the device, 422 `app_not_installed`); `s3.pet_dog` not sent to a desk whose packages report lacks PawBoop (its time still counts in the rev, as device_ping counts it) |

Removal and reinstall, as built:

- Removing PawBoop (the app first, then its add-ons): the slot becomes `none` (written, so the server knows), one
  note in the Notification Center (no banner, no sound); the dog's screens and pop-up close, its packs are let go (no
  greeting, habit line, alarm words or needs notification after that), NVS "pet" is cleared, `s3.pet_dog` = "", its
  notifications go; the run's best (NVS "run") goes with the run. Every removal also drops a Java game's record
  stores (`data/<id>/`) and the aquarium's save. Home's removal sheet now says the data goes.
- The account keeps `assist_memories` (the web's removal sheet with "Also forget what the dogs remember" is a web
  follow-up; the existing memory page already forgets them).
- Reinstalling PawBoop while the slot is `none` makes it the assistant again; the add-ons removed with it that were
  bought (`lock::bought`) and the dog that was chosen come back by themselves (NVS "asst": "back", "chose"); others
  wait in the Store. A paid dog downloads through the server's `pkg-link` as any purchase.
- Proof: `sim/scripts/assist-remove.txt` (data: a copy of sim/data-calls with the catalog and the PawBoop, Bao and
  Momo files in net/, Momo marked paid): remove, the Control Center, the card, the Store's Assistants, `WAKEFIRE` on
  Home and over the saver (nothing), the lock sheet and end card (no dog), `app pet` (nothing), reinstall (slot back,
  Bao and the bought Momo back). The onboarding without an assistant: sim built with `-DESPOS_ONBOARD=1`.

Built after (2026-11-22, the same lane):
- **The OS assistant client (§5.3, lane 4b).** `src/pet/talk.cpp` is `src/turn.{h,cpp}` (namespace `turn`;
  `src/pet/talk.h` keeps `pet::talk` as an alias): the recording, the request, the streamed voice, follow-ups and locks
  for whichever app is in the slot. It shows a turn through the slot app's **face** (`assistant::Face`: open, show,
  level, button, touched, lock, trial, say, speaking, hush, quietMs, voice): PawBoop's is the dog's pop-up
  (`pet::face()`, summon.cpp: its clips, its recorded words for the ack, "say again" and the lock lines, its voice
  key); any other assistant app (a JavaScript app whose manifest says `"provides": ["assistant"]`) gets the OS pop-up
  in `assistant.cpp` (its icon and name, the words, the round mic button, the unlock code, a trial's talks left; a
  beep for the ack; the server's voice). The wake word listens for any assistant in the slot. The dog's pop-up shows
  its mic and news chip only while the dog is the assistant.
- **Settings › Default apps (§7, mockup `default-apps.html`)**: `src/slots.{h,cpp}`. Rows for the screen saver and
  the assistant (picture, name, maker, when the saver starts); the saver picker ("Clock and photos" built in, the
  installed saver apps, the Store's new "Screen savers" chip, Preview) and the assistant picker (the installed
  assistant apps, None, the Store's Assistants). `slot.saver`: "builtin" or a saver app; the aquarium chosen draws the
  whole saver (its tank, the time and the date) in place of the saver pages, which are kept; the saver editor says so
  instead of opening; removing the chosen app brings the built-in saver back with one note (Center only).
- The setup's dots count only the steps a unit shows. The web's removal sheet of an assistant app has "Also forget
  what the dogs remember" (unticked; `POST /v1/assist/memories/delete {"all": true}`).

Still open: push to talk inside the dog's pop-up while the web set the slot to `none` asks the server and gets the
409; JavaScript saver apps (`provides: ["saver"]`, lane 4a's API) are listed by the Store but not run as savers yet;
the web's own Default apps card.

---

## 17. Product presets and the delivery bundle (owner, 2026-11-21)

> "cái quan trọng là bộ đóng gói của mình để sau mình bán giao cho khách thì đủ hết... sau này mình sẽ nạp nhiều config
> cho các khách khác nhau như khách muốn làm AI chó nhưng cũng có khách chỉ muốn nuôi cá trên màn hình"

A **preset** is the starting point of one product (one SKU): data, versioned, made of the same building blocks as any
install. It never locks anything: the customer can change every part later.

### 17.1 The format (open, in the repo: `presets/<id>.json`)

```json
{"preset": 1, "id": "aquarium-screen", "version": 3, "name": "Aquarium screen",
 "boards": ["es3c35p", "es3c28p"],
 "packages": [{"id": "vn.pawboop.tank", "parts": ["core", "bg35"]}],
 "slots": {"saver": "vn.pawboop.tank", "assistant": "none"},
 "settings": {"s3.saver": "120", "s3.theme": "dark", "app.vn.pawboop.tank.sounds": "0"},
 "home": {"template": "aquarium-clock"}, "saver": {"template": "tank-full"},
 "locale": {"lang": "ask", "region": "ask", "timezone": "ask"},
 "orientation": "landscape",
 "onboarding": {"skip": ["dog", "wake"], "first": "tank"},
 "entitlements": ["@aquarium-full"],
 "branding": {"name": "EspOS Aquarium", "boot_logo": "logo-aqua.png"}}
```

| Field | Meaning |
|---|---|
| `packages` | preinstalled packages and parts (the board's own background part picked automatically) |
| `slots` | the slot defaults (`saver`, `assistant`), and later pop-up rules |
| `settings` | OS keys and app settings (`app.<id>.<key>`, checked by the same schema as any value) |
| `home`, `saver` | the Home and saver templates (the existing template lists) |
| `locale` | language, region, time zone: fixed, or `ask` at first boot (global) |
| `orientation` | `portrait`, `landscape` or `ask` |
| `onboarding` | the first-boot steps to skip or put first (`src/onboard.cpp`, ESPOS_ONBOARD) |
| `entitlements` | references (`@name`) resolved only by the seller's server: owned dogs, the full aquarium, trial terms. The open format carries names, never keys |
| `branding` | optional boot name and logo |

The open repo carries the format, its schema, the tooling and one or two sample presets ("Clock desk", "Hello
saver"). Our real presets, paid content and entitlement definitions stay private (hosted only).

### 17.2 Where a preset is applied (recommended: both halves, each doing what it can)

| When | What | Why |
|---|---|---|
| **Factory** (Kho máy prepares a batch, the web installer or `tools/make_bundle.py --preset`) | the firmware, the preset's packages (app area or SD), and the preset file in LittleFS (`/sys/preset.json`); first boot applies its device-side parts | the unit works offline before WiFi: the aquarium swims out of the box |
| **Claim** (the claim code is bound to the preset at Kho máy) | the first sync applies the account-side parts: entitlements, server-kept settings, the account defaults | entitlements belong to the account; a unit given away is claimed again |

A unit flashed without a preset (a self-built one) behaves as today.

### 17.3 Updates never overwrite the customer

The device keeps `preset.id`, `preset.version` and, per key, the value the preset set. Preset v2 changes a key only
where the device still has the old preset's value (a three-way merge); packages are added, never removed; slot
defaults change only if the person never chose. Kho máy can push a new version to a batch; the customer's own choices
stay.

### 17.4 The delivery bundle ("đủ hết")

Per preset and board: `tools/make_bundle.py --preset aquarium-screen --board es3c35p` writes one folder for the web
installer and the factory: the firmware image, the partition table, the preset's packages, `preset.json`, the
first-boot assets, and a checksum list. Kho máy records which bundle each unit got. The web installer flashes it and
sends `ROT n` as today.

### 17.5 Tests

CI builds every preset's bundle for every board it names, boots each in QEMU (`tools/qemu/run.sh --bundle`), and
takes first-boot screenshots: the aquarium unit must show no dog step, the AI dog unit the dog's step. A schema test
checks every preset file; a merge test checks that v2 never overwrites a changed key.

### 17.6 Built (2026-11-22, not flashed or deployed)

| Part | Where |
|---|---|
| Format | `sdk/packages/sdk/schemas/preset-v1.schema.json` (`preset`: the file people write; `device`: the compiled file a unit keeps); samples `sdk/presets/clock-desk.json`, `photo-frame.json`. A product's own presets live outside the open repo |
| Checker and compiler | `espos preset check FILE... [--catalog] [--saver-templates]` (P001-P014: shape, the keys a preset may set and their values, `app.<id>.*` only for its packages, slots naming its packages, an assistant that is one, templates, a fixed city's coordinates, a key set twice); `espos preset compile FILE --board B`: the board's parts (a group's part for the other board swapped), `slots` as `slot.*`, the saver template as `s3.saver_pages`, `orientation` as `s3.rot` (landscape is 1, or 3 on the 2.8" case), a fixed `locale` as `sys.lang`, `sys.tz` (+ `sys.tz_min` now), `dash.city/lat/lon`; what is asked goes in `ask`, the setup steps to leave out in `skip` (`src/preset.ts`, tests `cli/test/preset.test.mjs`) |
| Device | `src/preset.{h,cpp}`, `src/preset_merge.{h,cpp}`. Sources: `/sys/preset.json` in LittleFS (applied before Home is built), else the card's `/espos/preset.json` (copied in once the card was looked at), USB `PRESET {json}` (also `PRESET`, the state as `ESPOS:PRESET {...}`), the sync answer's `"preset"`; the sync body reports `"preset": {"id", "v"}`. The board must match. `/sys/preset-state.json` keeps `{id, v, base, pkgs, skip}`. The factory's first apply (no state, not from a sync) sets every key (values the firmware itself stored at its first start, such as `s3.rot`, do not count); after that the three-way merge of §17.3 per key (`sdk/packages/sdk/schemas/preset-merge-vectors.json`, host test `test_host/preset_test.cpp`: 32 checks). Packages new to the preset and not installed are wanted in `s3.pkgs` (the store installs them when online), never removed. After Kho máy's `PROVISION` the preset's values go back over the server's defaults (`desk::touchSetting` sends them up). The setup (`ESPOS_ONBOARD`) leaves out the `skip` steps and the questions the preset answered (language, city); USB `ESPOS:READY` lists the `preset` capability |
| Bundle | `tools/make_bundle.py --preset FILE --board B [--out DIR]`: the firmware parts (never PawBoop 1.0's packs), `littlefs.bin` (only `/sys/preset.json`: a blank unit), `preset.json`, `card/` (the packages as a card keeps them, and `espos/preset.json`), `manifest.json` (`factory: true`, the preset, the card's files with SHA-256), `SHA256SUMS`. Packages must be the catalog's files (SHA-256 checked) |
| QEMU and the simulator | `tools/qemu/mkimage.py --bundle DIR`, `tools/qemu/preset_boot.py BUNDLE OUT [--board 28]` (headless, its own port and socket: `PRESET`, then Home, the Control Center, the saver); `tools/preset_sim_test.py OUT BUNDLE... [--onb SIM] [--card-only]` (every key, the card's packages, the slot, a v2 over USB: the person's own change stays, an untouched key follows, new keys and a new package come; with the setup build, the step after Wi-Fi: the dog's for an assistant product, the city otherwise) |
| Hosted | the unit is bound to a preset when Kho máy prepares it (the page sends `PRESET` over USB); its claim grants what the product comes with; its syncs carry the product's newer versions |

Open: the web installer flashing a delivery bundle (`littlefs.bin`, the card) for a flashing station; `branding` and
`onboarding.first` are kept but not used by firmware 0.6; a USB `PRESET` line is at most 8 KB.
