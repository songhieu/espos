# Bubbles

An EspOS **screen saver app**: `"provides": ["saver"]` in `manifest.json`. The person picks it in Settings, Default
apps, Screen saver; after that, the device runs `saver.js` whenever the saver shows, and a touch comes back. The
guide: espos.dev/docs/guides/saver-apps. Needs firmware 0.6.1 (`min_fw` 601).

## What is where

| file | what |
|---|---|
| `manifest.json` | `provides`, `entry` (`saver.js` for the saver, `main.js` for the app on Home) and the settings (colours, how many bubbles, speed, the date) |
| `saver.js` | the saver: `app.onStart(info)`, then `app.onFrame(g, dt)` 12 times a second |
| `main.js` | the app on Home: what it is and a button to its settings |
| `espos.json`, `icon.jpg` | what `espos build` packs (`saver.js` is listed in `files`) |

## The saver in four lines

```js
app.onFrame = (g, dt) => {
  g.gradient(0, 0, g.w, g.h, '#03263b', '#0b4f6c');
  g.circle(160, 240, 30, '#7dd3fc', {a: 0.4});
  g.text(app.time('HH:mm'), g.w / 2, 180, {size: 96, color: '#ffffff', align: 'center', bold: true});
};
```

## Commands

```
espos check                 # the manifest, files and icon
espos build                 # dist/dev.espos.bubbles-1.0.0-core.espk
espos upload                # to your own devices (espos.dev/docs/guides/my-apps); then pick it in Default apps
```

In the simulator (from the s3desk tree): `python3 tools/saver_sim_test.py` installs this package and checks it frame
by frame (see the guide's "Test it" part).
