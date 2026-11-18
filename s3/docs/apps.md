# Writing apps for EspOS (S3)

Apps are installed over WiFi from the store, without new firmware. An app is a folder in
`s3/apps/<name>/` with `manifest.json` and `main.js` (and an optional `icon.jpg`). Since 0.3.0 an app is a
**package** (`.espk`, kind `js`): `docs/packages.md` explains the package file, parts, where it is installed
(Máy or Thẻ nhớ), the store and how to try it in the simulator. `apps/pack.py` turns every folder into both the
old `.espk` (firmware 0.2.3) and the `.espk`, and writes `catalog.json` with both lists; they are published to `apps-s3/` in the site's public storage (temporary bbk_ key, revoked after). The device checks
each part's SHA-256 and each file's CRC32 before it commits an install.

## manifest.json

```json
{"id": "dev.espos.focus", "name": "Tập trung", "version": "1.0.0",
 "desc": "Hẹn giờ Pomodoro 25/5 phút, có tiếng chuông",
 "icon": {"bg": ["#ff6b5a", "#e0383b"], "glyph": "timer"},
 "permissions": ["sound", "notify"]}
```
(pack.py adds the package fields: `code` from the version, `kind`/`engine` "js", `min_fw` 300, `category`,
`author`, and `perms` from `permissions`; set them in the manifest to choose them yourself.)

- `glyph`: timer, alarm, bell, list, drop, flag, globe, sun, moon, photo, edit, font, home, search,
  speaker, play, lap, pin, leaf, power, grid.
- `permissions`: `sound` (sound.*), `notify` (notify()). Without one, the call throws.

## main.js

The screen is a tree of plain objects returned by `app.render`; it is built again after every tap,
timer or `app.refresh()`. Text uses the device's fonts (and the language's font package) and the system colours
(light by day, dark at night). `app.lang` is the language the device shows ("en", "vi", "de"... docs/i18n.md):
write the app in English and keep other languages in a table, as the sample apps in `apps/` do.

```js
const TEXT = {vi: {'Focus': 'Tập trung', 'Start': 'Bắt đầu', 'Reset': 'Làm lại'}};
const t = (s) => (TEXT[app.lang] || {})[s] || s;
app.title = t('Focus');
app.render = () => ui.column([
  ui.big('25:00', {color: '#e0383b'}),
  ui.row([ui.button(t('Start'), start), ui.button(t('Reset'), reset, {bg: '#9ca3af'})], {center: true}),
]);
```

| Call | What |
|---|---|
| `ui.column(children, {gap, center})`, `ui.row(...)`, `ui.card(children, {bg, center})` | layout |
| `ui.title(text, opts)`, `ui.text(text, {size 12-24, bold, dim, color, center})`, `ui.big(text, {color, size})` | text (big = digits) |
| `ui.button(text, fn, {bg, color, wide})`, `ui.chips(names, selected, fn(i))`, `ui.list(items, fn(i))` | input; items `{title, sub, right}` |
| `ui.progress(0..1, {color})`, `ui.spacer(h)` | misc |
| `timer.every(ms, fn)`, `timer.after(ms, fn)` -> id, `timer.cancel(id)` | at most 8 timers, while the app is open |
| `sound.chime()`, `sound.beep()`, `sound.click()` | permission `sound` |
| `notify(title, body, {arg, tag, sound, badge})` -> true/false | a notification (permission `notify`): banner, Notification Center, count on the icon; false when the user turned the app's notifications off. Tapped: the app opens with `app.arg` = `arg`. Same `tag` replaces the older one |
| `notify.schedule(at, title, body, opts)` -> true/false | fires at `at` (a `Date` or ms since 1970, e.g. `Date.now() + 25 * 60000`; a number under 10^9 counts as ms from now) even when the app is closed; kept across restarts; same `tag` replaces; at most 32 waiting |
| `notify.cancel(tag)` | removes the waiting and the shown notification of that tag; `notify.cancel()` removes every waiting one of the app |
| `app.badge(n)` | the number on the app's icon (0 hides it); `app.badge()` goes back to counting unread notifications |
| `app.screen` | `{w, h, landscape}`: the room the app draws in (480 x 320 in landscape). After the screen turns, the OS opens the app again |
| `ui.card(children, {half: true})` | half the line in landscape (two of them side by side in a `ui.row`), the whole line in portrait |
| `app.arg` | the `arg` of the notification that opened the app (`undefined` when opened from Home) |
| `toast(text)` | a short message at the top |
| `store.get(key, default)`, `store.set(key, value)` | JSON values per app, keys 1-14 chars, values ≤ 3000 chars |

Limits: QuickJS-ng (ES2023), 1.5 MB heap in PSRAM, 1.5 s per call (3 s to start), 11 KB of stack (QuickJS runs
on its own 16 KB stack in internal RAM while an app is open: the UI thread's 8 KB overflowed and the board restarted
when Xúc xắc opened; a deeper script throws "Maximum call stack size exceeded"; when internal RAM is short, e.g. a
sync's TLS handshake right after boot, the app does not open and says "Máy đang bận"; `JSSTACK bytes [under]` on the
dev console). A throw or a timeout
shows "Ứng dụng lỗi: …" and the OS keeps running. Apps run only while open (no background yet).

Try one on a computer first: `make -C sim -j10`, then either `python3 tools/espk.py --install-sim
sim/data-me app.espk --vol flash` and `app <id>` in a script, or put the package and catalog under
`$SIM_DATA/net/` (named like `_storage_bb_149_apps-s3_*`) and use the store (`SIM_FW_CODE=300`); docs/packages.md §7.
On the board, the dev console's `JS <code>` evaluates a line.
