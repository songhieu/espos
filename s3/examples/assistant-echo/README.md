# Echo assistant

An EspOS **assistant app**: `"provides": ["assistant"]` in `manifest.json`. The person picks it in Settings, Default apps,
Assistant; after that, every question goes to **your provider** as a signed EspOS Assistant Protocol v1 request, and
the device says your answer in the voice of the dog the person chose. The guide: espos.dev/docs/guides/assistant.

## Try it in two minutes

```
espos ai secret u.dev.espos.ai.echo --endpoint https://your-host/espos/turn   # after espos upload; shown once
export ESPOS_SIGNING_SECRET=esps_...        # the secret (or any esps_ value, to try it locally)
node provider/server.mjs                    # answers on http://localhost:8787/espos/turn
espos ai test http://localhost:8787/espos/turn
```

`espos ai test` sends signed example turns (weather, an alarm, nothing heard, a yes for something held, a wrong
signature) and checks the answers with the server's own rules.

## What is where

| file | what |
|---|---|
| `manifest.json` | `provides`, `assistant.provider` (your endpoint), and `perms`: what the server may send your provider and do for it (shown at install) |
| `main.js` | a small screen on the device (the brain is not here) |
| `provider/server.mjs` | a working provider: checks the signature, answers from the request, sets alarms; replace `answer()` with your model |
| `espos.json`, `icon.jpg` | what `espos build` packs (the provider folder is not packed) |

Permissions in this template: `location:city` and `data:weather` (the city and its weather), `assist.alarms`
(sees the alarms), `alarms.write` (sets them). The full list: espos.dev/docs/guides/assistant#permissions.

## Commands

```
espos check                 # the manifest, files, icon and permissions vs the APIs the code calls
espos build                 # dist/dev.espos.ai.echo-1.0.0-core.espk
espos sim --live            # the simulator in a window, the app opened (mouse = finger)
espos sim --headless        # a headless run: screenshots in .espos/shots, fails on a script error
espos sim --live --watch    # rebuilds and restarts the simulator when a file changes
espos qemu --install dist/dev.espos.ai.echo-1.0.0-core.espk   # the real firmware in QEMU, the package on its card
espos device app dev.espos.ai.echo    # open it on a board (after installing it from the store)
```

## Files

| file | what |
|---|---|
| `manifest.json` | the package's manifest (s3/docs/packages.md): id, name, version, code (raise it every release), perms, ... |
| `espos.json` | what `espos build` puts into the package |
| `icon.jpg` | 120x120 baseline JPEG (a placeholder: replace it; the device rounds the corners) |
| `sim/smoke.txt` | simulator commands run after the app opens (tap X Y, wait MS, shot NAME, ...) |
