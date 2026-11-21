# A unit for sale: claim code, first boot, Kho máy over USB

2026-11-19.
Code: `src/claim.h`, `src/claim.cpp`.

## How a unit is prepared (Kho máy, the web)

The factory flash writes only the bootloader, the partitions, boot_app0 and the firmware. The packages come on a
pre-imaged microSD card. The page then talks over USB:

| Line | Answer |
|---|---|
| `ESPOS:HELLO <ms>` | `ESPOS:READY {"fw","kind":"s3","board","caps":["claim"],"linked"}`. The page stops if `claim` is missing |
| `ESPOS:PROVISION {api, token, device:{id:0,name,user:""}, wifi, settings, settings_rev, claim, claim_url}` | `ESPOS:OK`. The token is saved as usual (desk.cpp), and `claim` / `claim_url` go to NVS `claim` |
| `ESPOS:INFO` | `ESPOS:INFO {"fw","board","mac4","hwid","token_sha8","claim_tail","sd":{"present","free"},"pkgs":[ids]}` |
| `ESPOS:CLAIM {"claim","claim_url"}` | `ESPOS:OK`, or `ESPOS:ERR bad claim`. A new code (a lost label, or a returned unit); the first-boot step is kept |
| `ESPOS:FACTORY_RESET` | `ESPOS:OK`, then a restart. It clears the person's data and WiFi and keeps the packages and the identity |
| `ESPOS:ATTEST <hex challenge>` | `ESPOS:ATTEST {"hmac"}`, or `ESPOS:ERR attestation off` |
| `ESPOS:FACTORY {…}` | `ESPOS:ERR unsupported` (the optional network install is not built: cards come pre-imaged) |

The INFO fields:
- `hwid` is SHA-256 of `"espos-hw:"` + the base MAC as 12 lowercase hex. Every API request also sends it as
  `X-Espos-Hwid`, with `X-Espos-Mac4`.
- `token_sha8` is the first 8 hex digits of SHA-256 of the token.
- `claim_tail` is the last 4 characters of the code.

What `FACTORY_RESET` clears and keeps:
- It clears the NVS namespaces `desk` (display, sound, saver), `wifi`, `pet`, `run`, `lock`, `lic`, `news`, `remote`
  and `wake`.
- It deletes `/settings.json`, `/dash.json`, `/sys/notif.json`, `/tank.sav` and the photos in `/ph`.
- It keeps `link` (the token), `claim` and the package index. The first-boot steps start again.

## First boot of an unclaimed unit

Since 2026-11-21 the first boot is the setup of `src/onboard.cpp` (docs/onboarding-plan.md): language, WiFi, the dog,
the person's name, the wake word, the city, then at step 7 "Activate this device" with the code `XXXX-XXXX` (Crockford
base32, the 8 characters kept without the dash) and a QR of `claim_url` (`https://espos.dev/app#claim=<code>` when none
was given), then tips and "All set". claim.cpp keeps the code (`claim::code()`, `claim::url()`), the USB lines and the
reset; its own language list, step machine and Home welcome card are gone (NVS `claim/step` is no longer used).
- **Polling.** The sync asks every 5 s while the server answers `401 not_linked` (desk.cpp), from the moment the
  device has a network: a code entered early (from the box label) is seen during any step.
- **Claimed.** The first 200 erases the code from NVS and makes the welcome due (`claim::takeWelcome`): step 7 shows
  "Hello <name>!" (the name from step 4, else the account's name, never an e-mail address), "This device is yours
  now." and the trial chip. Claimed after "Later": the same screen opens by itself at Home.
- ESPOS:INFO also has `"onb"`: the setup version done (0 never), 100 + the step while it runs, -1 a demo unit.
- `ESPOS:FACTORY_RESET` also clears NVS `onb`, so the setup starts again from step 1 (the account link stays).

## eFuse (v2): OFF

- Only the read side of a device attestation exists, and since 2026-11-21 it is built only with `-DESPOS_ATTEST=1`
  (about 1 KB; without it ESPOS:ATTEST answers "attestation off"): HMAC-SHA256 of a challenge with the key in eFuse BLOCK_KEY5
  (purpose HMAC_UP), through `esp_hmac_calculate`.
- It answers only when NVS `claim/attest` is 1, and nothing writes that yet.
- This firmware contains **no** code that burns an eFuse: no `esp_efuse_write_*` call. Burning, with the whitelist and
  the dry runs in QEMU and `espefuse --virt`, comes only after the owner approves a one-board test.

## Tests

- Simulator commands:
  - `claim set CODE [URL]` and `claim done` (stands in for the first 200);
  - `onb reset`, `onb step N[.SUB]`, `onb done`, `onb state`, `onb demo 1 PIN` (the setup, built with `-DESPOS_ONBOARD=1`:
    docs/onboarding-plan.md; without the flag the old `claim step N` screens);
  - `claim ESPOS:…` (the USB lines).
- `SIM_WIFI_OFF=1` starts the simulator without WiFi, and the WiFi page's connect turns it on.
- QEMU: `ESPOS:` lines go through `tools/qemu/console.py`. See the gallery for a full run, including the restart after
  `FACTORY_RESET`.

## Size

`-DESPOS_NO_CLAIM=1` builds the image without any of this. Measured on the same tree on 2026-11-19: 10,016 B, of
which about 1.5 KB is Espressif's HMAC driver for the attestation.
