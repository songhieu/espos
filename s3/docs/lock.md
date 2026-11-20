# Paid content and the trial (device side)

2026-11-19. The code is open; paid content and the AI come from espos.dev. A new account gets a trial of 7 days or 50
AI turns. When the trial ends, the paid features lock, and the device says so on its screen.

The code is in `src/lock.h` and `src/lock.cpp`. It reads the server's `lic`, which `src/lic.h` keeps (wakeword's
module, AI side; see docs/assistant-ai.md §4b).

## What the server says

- `device_sync` → `"lic": {"plan": "free|trial|owned|sub", "trial_end_ms", "ai": "on|trial|off|byok", "ai_left", "ent": [...], "buy_url"}`.
- **No `lic` means no licensing**: nothing locks, and the device behaves exactly as before. This covers self-hosted
  and older servers.
- A `lic` that comes while the catalog marks nothing `"paid"` locks nothing either.
- The plans:
  - `trial`, `owned` and `sub` may use everything.
  - `free` may use what the catalog leaves free, plus the ids in `ent`.
  - A plan word this firmware does not know counts as `owned`.
- The trial's end is the server's decision. When `trial_end_ms` passes on the device's clock, the device syncs early.
  It never locks on its own, so a purchase always stays a purchase.
- `free` with `trial_end_ms` 0 means this device gets no trial (the account or the board already had one): nothing
  says "trial", and no end card is shown.
- `ai_left` is -1 when the turns are unlimited; the "turns left" notice needs 1..10.
- `ent` is an opaque list (the server expands its own "*" before answering).
- On a server fault the sync answers without `lic` ("fail open"): nothing locks, as with no licensing at all.
- Every API request carries `X-Espos-Hwid` (SHA-256 of `"espos-hw:"` + the base MAC as 12 lowercase hex) and
  `X-Espos-Mac4` (its last 4 hex digits), so the trial is counted per board (desk.cpp `hwHeaders`). The MAC itself
  is never sent, and storage links never get these headers.

## The catalog (`apps-s3/catalog.json`)

- `"paid": true` marks a paid package.
- `"free_ids": [...]` marks a package that has a free part. The package itself stays usable, and the ids name what is
  free inside it.
- A part of a package is written as `<package>+<thing>`. `<package>+full` (or the package id itself in `ent`)
  unlocks all of it.
- Every part of a paid package (also one with `free_ids`) comes through `GET /v1/device/pkg-link?id=&part=`, asked
  just before the download with the device token: `200 {"url": signed for an hour, "size", "sha256"}`,
  `403 {"locked": "trial_ended" | "not_owned", "buy_url"}`, `404 {"error"}`. The public `url` such a part may still
  carry is for firmware 0.5.6 only (until those copies are retired); a part may also have only a private `"key"`.
  - The job shows as "Waiting" until the link comes.
  - `403 {"locked": …}` opens the lock sheet.
  - Installs the web asks for through `s3.pkgs` are not tried while the package is locked, and are tried at most
    3 times per boot.
- The marks are kept in NVS (`lock/marks`, as `;id=,free,free,;`), so locks still hold offline after a restart.

The free subsets proposed on 2026-11-19 (the catalog decides; the firmware only follows):

| Thing | Free | Paid |
|---|---|---|
| Dogs (`vn.pawboop.dog.*`) | bao, momo | the other 8 |
| Run maps (`vn.pawboop.map.*`) | bazaar (the ring's first, where the run starts) | the other 7 |
| Aquarium (`vn.pawboop.tank`, `free_ids`) | `planted`, guppy, molly, swordtail, neon, cory, snail; plants, decor, machines | other species, looks, the sea tank (`marine`) |

## Nothing is ever lost

- A locked package stays installed, its saves stay as they are, and its update waits.
- The chosen dog (`s3.pet_dog`) keeps naming a locked dog. Meanwhile the pet, the run and the assistant use the first
  free installed dog, so the old dog comes back as it was when it is bought.
- The aquarium:
  - Fish of a locked kind keep living, growing, breeding and earning. Only buying more of them is closed.
  - A locked look is shown as the free look, and a locked sea tank is shown as the fresh tank.
  - `TNK1` still holds the real look and the real current tank.
- The run: an installed locked map shows on the start card with "Locked now" over "Tap to start". A tap on it opens
  the lock sheet instead of starting the run.

## What the device shows

| When | What |
|---|---|
| Trial | A chip ("Trial · 3 days left", "Trial · ends today") in several places: the store's title bar, the pet stage's lower left, under the aquarium's top bar, and above the run's start card. The aquarium and the run draw past LVGL, so they get a picture of the chip made once by LVGL (`lock::chipPicture`), which shows every language's letters. |
| A day before the end | A notification scheduled with notif, kept across restarts: "Your trial ends tomorrow" (or "…today" when less than a day was left when it was first heard of). |
| 10 AI turns left or fewer | One notification ("9 AI turns left"). |
| The plan turns `free` after a trial or a subscription | One full-screen card listing what stays free and what is locked now, with the QR to `buy_url` and "Later". It shows on Home when nobody is touching the screen, and only once per trial end. |
| After that | A padlock on locked items: the store's icon and its "Buy" button, Home icons and the dock, the dog chooser ("Locked now" or "Buy · 6.4 MB"), the aquarium shop (a dark padlock pill in place of the price), and the run's start card. |
| A locked item opened | The lock sheet: "<name> is locked". The chosen dog says kindly that nothing is lost, with the QR and "Later". It uses the game's own colours: OS, PawBoop's cream (pet, run) or the aquarium's glass. |
| Store, paid items | "Buy" (locked), "Try" (installs during the trial), "Owned · 6.4 MB" (bought). |
| `owned` / `sub` | No trial UI at all. |

## A blocked account

Every device route answers `403 {"error": "account_disabled", "state": "blocked" | "deleting" | "deleted"}` when an
admin blocks the owner or the account is being deleted.
- desk.cpp `handle()` reports it with `lock::account(state)`. The token is kept, syncs go on at the normal pace (no
  back-off), and the first 200 of a sync or a ping ends it with `lock::account(nullptr)`. wakeword's talk.cpp calls the
  same function for the assistant's 403.
- `lock::accountBlocked()` tells whether the account is blocked.
- While blocked, a calm full screen covers Home: a padlock, then "Your account is locked" or "Your account is being
  deleted", then "Contact support@espos.dev". It hides while an app, an alarm or the saver is in front, and Home stays
  out of reach under it. It is kept in RAM only, so after a restart the next sync tells again.
- Tests: `LOCK account blocked|deleting|ok` (sim: `lock account …`).

## Publishing the marks

`tools/paid_marks.py build-pkgs/espos/s3/catalog.json` adds them (`--off` removes them). Publish them only once the
server's pkg-link is live: firmware that knows the marks fetches every paid part through it. Order: the server
(lic + pkg-link + the private copies) → the catalog with the marks. The firmware may ship before or after: without
`lic` or without marks nothing locks. A rebuild of the catalog (`apps/pack.py`, `tools/i18n.py catalog`) keeps
`paid`, `free_ids` and a part's `key` (the key only for the same version); `tools/test_catalog.py` case 6.

## Size

`-DESPOS_NO_LOCK=1` builds the image without any of this (lock.h's inline stand-ins: nothing locks). Measured
2026-11-19 on the same tree: 3,879,632 B with, 3,861,216 B without = 18,416 B; the blocked-account screen added
1,072 B (3,880,704 B).

## Tests

- Dev console (board): `LOCK lic {json}` (as a sync's "lic"), `LOCK lic none`, `LOCK card`.
- Simulator: `lock lic …` / `lock card`, and also `lock sheet [pet|tank] NAME`, `lock install ID` (the store's install
  as a tap would, so paid parts go through pkg-link), `lock close` and `lock state`.
- Sim data `sim/data-lic` (everything installed) and `sim/data-lic2` (a few dogs; shiba's part comes only through
  pkg-link, and golden's link answers locked). Both have a catalog with these marks and `dev.espos.focus` marked
  paid (for the Home lock).
