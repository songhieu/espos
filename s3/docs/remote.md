# Remote control

Settings › Điều khiển từ xa (src/remote.cpp) lets the owner see the screen and touch it from a browser: from anywhere
through the EspOS server, or faster from the same network straight to the device. Off until turned on; each time
it is turned on it makes a new 6-digit PIN (the home network's way).

## Over the internet (through the EspOS server)

- **The device**, while the switch is on and it is linked, calls `POST /v1/device/remote` (workflow device_remote, the
  device token):
  - It calls every 4 s while nobody watches, back to back while someone does.
  - It uses desk's one kept TLS connection (`desk::remoteSend`), taking turns with sync and downloads.
  - Body: `on`, `ack` (the last gesture done), and a frame when the screen changed: `frame` (JPEG, base64) with the screen's `w`, `h`.
  - Frames are 160x240 at quality 60 while the screen moves, and 320x480 at quality 50 once it has stood still 0.6 s.
  - Reply: `watch` (someone watches), `events` (the gestures still to do).
- **Gestures:** `tap X Y`, `long X Y MS`, `drag X0 Y0 X1 Y1 MS`, `key home|back`, in screen coordinates. The device plays them one at a time with touch::inject's timing.
- **The browser** (web EspOS, device page, "Mở điều khiển", n2/RemoteViewer.tsx):
  - `GET /v1/devices/:id/remote?after=SEQ` (remote_view) about 4 times a second. Each call keeps the device sending for 12 s (`watch_until`).
  - Mouse or finger gestures go to `POST /v1/devices/:id/remote/input` (remote_input). Only the four gesture forms are queued.
- **Tables:** `remote_sessions` (one row per device: switch, seen_at, watch_until, the newest frame) and `remote_events` (the gesture queue).
- **list_devices** gives `remote.on` when the device called within the last minute with the switch on.
- **Speed:** the board's TCP send window (5.7 KB) at ~0.2 s to the server gives about 25 KB/s up. A small frame (5-8 KB, ~11 KB in base64) means roughly 1-3 frames a second, plus the server's own time. Not measured yet.

## In the home network

- The device serves port 80 with esp_http_server (one task on core 0, stack 6 KB):
  - `GET /`: the page (4 KB, in the firmware).
  - `GET /frame`: one JPEG of the screen.
  - `POST /touch`: `d X Y` (finger down or moved), `u X Y` (lifted), in screen coordinates.
  - `POST /key`: `home` or `back`.
- Every request but the page carries the PIN in the `X-Pin` header. Ten wrong PINs lock it for a minute.
- **Frames:**
  - While someone watches (a frame asked for in the last 3 s), `lcd::mirror` keeps a copy of everything sent to the panel in PSRAM: LVGL's bands and the run game's rows (landscape: the shadow frame it has anyway).
  - A frame is that copy at one moment, JPEG-encoded by Espressif's encoder (lib/esp_new_jpeg, quality 70): about 22 KB.
  - In QEMU a frame took 45 ms. The board is not measured yet.
- **Touches** go into the touch task's queue (`touch::feedScreen`): the gestures, the slop and the click are the board's.
- **The web EspOS:**
  - While it is on, the device sends `rc_url` and `rc_pin` in its sync's diag.
  - `GET /v1/devices` (list_devices) gives them as `remote` (null when off, or when that sync is over 3 hours old).
  - The device page's side panel then has "Mở điều khiển". It opens `URL/#pin=PIN` in a new tab; the fragment never reaches a server, and the page drops it at once.
  - Turning remote control on or off syncs at once.
  - Live since 2026-11-17: list_devices version 4, web app v61.
- **QEMU:** port 80 is forwarded to http://127.0.0.1:8080 (tools/qemu/run.sh), which is what it reports as its address.
- **Dev console:** `REMOTE [1|0]` turns it on or off and prints the address and the PIN.
- **Cost on the board:**
  - +72 KB of flash (encoder and server).
  - While watched: a 300 KB PSRAM copy, a JPEG encode on core 0, and the frames over WiFi.
- **Not yet:** control from outside the home network (that needs a relay through the server).
