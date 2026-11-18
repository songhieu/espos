# EspOS as an app platform: landscape and recommendations

> Date: 2026-11-17. Scope: ESP32-S3 desk (N16R8, 320x480 and 240x320 LCD, LVGL 9) and the ESP32-C3 e-ink device.
> Revised the same day for the wider goal of a full IoT OS: §6 (hardware and peripherals) is new and is folded into §7-§9.
> **Convention.** A sentence with a link is sourced from that link. Text marked **[I]** is our inference or recommendation.
> "Our code" refers to files in this repo.

## 0. The short version

1. Every successful small-device platform lets third parties ship **non-native code**, or native code under hardware protection. Pebble ran C apps unprivileged under the MPU and is now adding JavaScript (Moddable XS). Garmin uses a VM (Monkey C). Zepp, Fitbit and Bangle.js use JavaScript. Toit and Moddable use VMs with sandboxing. The platforms that load native ELF files with no protection (Flipper, Tactility) offer no sandbox at all. **[I]** EspOS should keep third-party code in memory-safe interpreters: QuickJS now, WASM later. Native "engines" (pet, run, j2me) should stay first-party and compiled into the firmware.
2. Permissions are declared in the manifest, accepted at install time and revocable later (Fitbit, Zepp). APIs check them at the binding layer. **[I]** EspOS already has the right shape (`perms`, and calls throw without them). It needs `net:<host>`, storage quotas, a consent screen and per-app switches.
3. API compatibility is a separate number from the firmware version: Garmin `minApiLevel` and the `has` check, Zepp `apiVersion.minVersion`, Flipper's semver `api_symbols.csv`. **[I]** Add `api.min/target` to the manifest and freeze the current `ui.*` API as level 1.
4. Watch faces, widgets and settings pages are **separate app types** with smaller budgets on every platform studied: Garmin watch face, data field and glance; Zepp `watchface`, `app-widget` and `setting`; Bangle `clock`, `widget` and `clkinfo`. **[I]** EspOS needs `app`, `widget`, `face`, `theme` (data only), `service`, `driver` and a declarative `settings` schema.
5. Hosted plus self-host works when **the protocol is the product**. Bitwarden clients have a "Self-hosted" server field, and Vaultwarden is a compatible server written by someone else. Tailscale clients accept `--login-server` (Headscale). Rebble re-pointed Pebble phones to new servers through a "boot" step. **[I]** Write an EspOS protocol spec, build a small Docker server against it, and give the device one server setting backed by a boot document.
6. Funding: projects are funded by a paid hosted convenience tier that is optional (Home Assistant Link, Ghost(Pro)). Paywalling features is what triggers community forks (HashiCorp → OpenTofu). **[I]** Licenses: firmware GPL-3.0 or MPL-2.0, server AGPL-3.0, SDK and templates MIT or Apache-2.0, plus a trademark policy.
7. Hardware access uses two layers almost everywhere. Underneath are bus drivers: Zephyr devicetree drivers, RIOT SAUL, NuttX lower halves, ECMA-419 IO classes. On top are typed sensors and actuators with fixed units: Zephyr channels, ECMA-419 sensor classes, Matter clusters, Home Assistant device classes. Phones and browsers ask the user per device (Android USB, Web Serial, Web Bluetooth). **[I]** EspOS should do the same:
   - native bus primitives
   - signed **driver packages** that match devices and expose typed channels
   - apps that see only channels, with a per-device prompt before they control anything
   - an OS-owned "Devices" page, sampling, history, rules, and MQTT/Home Assistant sharing
   - first buses: BLE sensors, then Qwiic I2C, with USB host last

## 1. Where EspOS is today (for mapping)

- `.espk` = header + manifest JSON + file table with CRC32 (`docs/ecosystem.md` §2). There are two kinds: `js` (QuickJS, `ui.*` API, `docs/apps.md`) and `native` (data for engines compiled into the firmware). `perms`: `sound`, `notify`, with `net` and `bg` planned. A call without the permission throws (`docs/packages.md`).
- Per-app QuickJS runtime with a 1.5 MB memory limit, a stack limit and an interrupt handler (`src/js/jsapp.cpp:615-618`). Limits: 8 timers per app and `store.*` values of at most 3000 characters (`docs/apps.md`).
- The catalog is static JSON on public storage. Parts carry `sha256` but the catalog itself is not signed. Install and remove are synced through the setting `s3.pkgs`.
- Constraints that matter: the firmware takes **3.68 MB of a 3.75 MB app slot** (`docs/ecosystem.md` §1), and only one TLS session can run at a time (project notes).
- The hosted server runs on a workflow platform (Go workflow nodes, tables, routes) and the web app is React. It is not portable. The repo `github.com/songhieu/espos` is already public under MIT (firmware snapshot and server).

## 2. App platforms on small devices

### 2.1 Comparison

| Platform | App code runs as | Isolation | Permissions | API versioning | Distribution / review | Dev tools |
|---|---|---|---|---|---|---|
| [Pebble / PebbleOS](https://github.com/coredevices/pebbleos/blob/main/docs/sdk/how_it_works.md) | Native C. JS ([Alloy](https://moddable.com/blog/pebble/), Moddable XS) on new models | Own task, unprivileged, MPU-confined; syscalls check pointers | `capabilities` (location, configurable, health) | Fixed jump table `g_pbl_system_tbl` | Appstore, now on Rebble services | `pebble` CLI, QEMU emulator, in-browser CloudPebble |
| [Garmin Connect IQ](https://developer.garmin.com/connect-iq/articles/connect-iq-basics/App_Types.html) | Monkey C bytecode in a VM | VM; APIs limited by app type | Manifest permissions (Background and Complications need one) | `minApiLevel`, runtime `has` | Store with approval, developer key | VS Code extension, simulator, profiler |
| [Zepp OS](https://docs.huami.com/docs/reference/app-json/) | JS "mini programs" | VM | `app.json` `permissions`, install-time dialog | `runtime.apiVersion.minVersion` | Zepp store, QR install | Zeus CLI, simulator, bridge |
| [Fitbit OS](https://dev.fitbit.com/build/guides/permissions/) | JS (device and phone companion) | VM | `requestedPermissions`, install prompt, revocable, runtime `granted()` | SDK versions | Gallery with review | CLI, simulator |
| [Flipper Zero](https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html) | Native ELF (`.fap`) copied to RAM. Also [mJS scripts](https://developer.flipper.net/flipperzero/doxygen/js_about_js_engine.html) | None documented, only a restricted symbol table | None | Semver `api_symbols.csv`, major must match | [Catalog by PR](https://github.com/flipperdevices/flipper-application-catalog/blob/main/documentation/Contributing.md), built from a source commit | uFBT, gdb script, JS SDK on npm |
| [Toit](https://docs.toit.io/getstarted) | Toit bytecode in "containers" | VM runs containers "safely side by side" | n/a | n/a | Artemis fleet service or self-hosted broker | Jaguar live reload over WiFi |
| [Moddable](https://moddable.com/documentation/xs/mods) | JS "mods" (XS archive, runs in place from flash) | SES `Compartment`, frozen primordials | Host chooses the modules and globals a mod sees | Per host and per XS version | Host-defined | xsbug, mcsim |
| [Bangle.js](https://github.com/espruino/BangleApps) | Espruino JS | None (one interpreter) | None | `requires_firmware` | Web App Loader over Web Bluetooth, GitHub-hosted | Web IDE, emulator flag |
| [Tactility](https://github.com/TactilityProject/Tactility) | Native ELF via Espressif's ELF loader | None documented | None documented | Manual symbol exports | `.app` tar files from SD | SDK, simulator |
| [ESP-Brookesia](https://docs.espressif.com/projects/esp-brookesia/en/latest/index.html) | C/C++ apps compiled in | n/a | n/a | n/a | n/a | ESP-IDF components |
| [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime/issues/1262) | C++ compiled into one binary | n/a | n/a | n/a | Rebuild and reflash | Simulator |

### 2.2 What each one teaches

**Pebble and Rebble.** PebbleOS runs each SDK app "as its own task, unprivileged, with the MPU limiting it to its own memory". SDK calls go through a jump table into applib, and kernel work goes through `sys_*` syscalls that "check that buffers and strings lie in the app's memory, and a failed check terminates the app rather than the firmware" ([how_it_works](https://github.com/coredevices/pebbleos/blob/main/docs/sdk/how_it_works.md), [syscalls](https://github.com/coredevices/pebbleos/blob/main/docs/kernel/syscalls.md)). App RAM (code plus heap) is 24 KB to 128 KB depending on the model ([hardware info](https://developer.repebble.com/guides/tools-and-resources/hardware-information/)). Settings pages run on the phone. [Clay](https://github.com/pebble/clay) turns a JSON definition into a config page whose values are sent to the watch ([app configuration](https://developer.repebble.com/guides/user-interfaces/app-configuration/)). Tooling: `pebble install --emulator`, `pebble logs` and `pebble screenshot` ([pebble tool](https://developer.repebble.com/guides/tools-and-resources/pebble-tool/)), plus CloudPebble with an in-browser emulator that shows logs and takes screenshots ([CloudPebble](https://developer.repebble.com/guides/tools-and-resources/cloudpebble)). In 2026 Pebble added **Alloy**: Moddable XS built into PebbleOS, ES2025 and TypeScript, with `fetch`, `WebSocket` and `localStorage` ([Moddable blog, 2026-02-20](https://moddable.com/blog/pebble/)). Their stated reason is that JS is "a more approachable, forgiving language than C".
History: when Fitbit switched off Pebble's servers in 2018, Rebble rebuilt them. Weather and dictation became paid (around $2/month) and everything else stayed free ([Rebble 2018](https://rebble.io/2018/02/15/rebble-web-services.html)). Phones are re-pointed by a **boot** step: log in at boot.rebble.io and tap "Switch to Rebble" ([help](https://help.rebble.io/boot-ios)). In 2025 Rebble's services powered the relaunched Pebble Appstore ([Migicovsky](https://ericmigi.com/blog/re-introducing-the-pebble-appstore/)). After a public falling-out, Rebble stated that apps "belong to the community, not to Rebble" and released a monthly archive ([Rebble 2025-11-24](https://rebble.io/2025/11/24/rebble-in-your-own-world.html)). **[I]** Lesson: say in writing from day one that store content belongs to its authors, and keep it exportable.

**Garmin Connect IQ.** The VM limits the APIs available to each app type. Watch faces "cannot access the compass, GPS, or other sensors" and update once a minute in sleep. Widgets time out. Glances "run in a limited runtime space, with reduced memory and privileges and do not accept any input". Background and Complications need a permission ([App Types](https://developer.garmin.com/connect-iq/articles/connect-iq-basics/App_Types.html)). Memory budgets depend on the device and the app type. For example, data fields get about 28 KB on a Fenix 5 and 128-256 KB on newer models ([forum](https://forums.garmin.com/developer/connect-iq/f/discussion/418612/device-memory-limits/1960173)). The `has` operator checks at runtime whether an API exists on the device, and the gradual or strict Monkey Types checker adds no runtime cost ([System 7](https://forums.garmin.com/developer/connect-iq/b/news-announcements/posts/welcome-to-system-7), [CIQ 4.0](https://forums.garmin.com/developer/connect-iq/b/news-announcements/posts/welcome-to-connect-iq-4-0)). Updates must be signed with the original developer key ([forum](https://forums.garmin.com/developer/connect-iq/f/discussion/360712/developer-verification/1748226)).

**Zepp OS.** `app.json` holds `appType` (`app` or `watchface`), `permissions`, `runtime.apiVersion.minVersion` ("`v <= minVersion` cannot be upgraded or installed"), per-device `targets` keyed by asset folder, and modules: `page`, `app-widget`, `secondary-widget`, `watchface`, `setting` (phone settings page), `app-side` (phone-side service), and in v3 `app-service` (background) and `app-event` ([app.json](https://docs.huami.com/docs/reference/app-json/)). Users see a consent dialog at install. Calling a protected API without the permission throws an error that names the permission ([permission](https://docs.huami.com/docs/guides/framework/device/permission/)). Tooling: Zeus CLI with `zeus dev` (simulator plus watch for changes), `zeus preview` (QR code to the phone) and bridge mode with screenshots ([CLI](https://docs.zepp.com/docs/guides/tools/cli/overview/)). Watch faces can be built without code in the browser-based Watch Face Maker ([watchface tools](https://docs.zepp.com/docs/guides/tools/watchface/overview/)).

**Fitbit OS.** 11 permissions (e.g. `access_internet`, `run_background`, `access_heart_rate`). Users accept them at install and can revoke them "at any time", and code checks with `appbit.permissions.granted()` ([permissions](https://dev.fitbit.com/build/guides/permissions/)).

**Flipper Zero.** A `.fap` is an ELF that the loader copies into RAM, relocates and links against a symbol table generated from `api_symbols.csv`. Only an allow-listed subset of the firmware is exported. The API is semver: removing or changing a symbol is a major bump, and the loader checks that the major version matches ([apps on SD](https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html)). The catalog holds only a manifest that points to a public source repo and a `commit_sha`. Apps must be open source and buildable with uFBT, and pull requests are reviewed in about 1-2 business days ([Contributing](https://github.com/flipperdevices/flipper-application-catalog/blob/main/documentation/Contributing.md)). Flipper also offers mJS scripts with an npm SDK whose typings carry each API's version history ([fz-sdk](https://unpkg.com/@flipperdevices/fz-sdk@0.3.0/README.md)). **[I]** That is the same native-plus-script split EspOS has.

**Toit.** Multiple "containers" run "safely side by side" in the VM. They can be installed without a firmware update. Jaguar pushes code "over your local WiFi in two seconds". Artemis fleet management is free for up to ten devices, "and you can host your own broker" ([getstarted](https://docs.toit.io/getstarted), [containers](https://docs.toit.io/tutorials/containers)). **[I]** That is a good model for both live reload and a hosted plus self-host split.

**Moddable mods.** A mod is JS plus assets compiled to an XS archive. It executes in place from flash (256 KB partition on ESP32). The host decides which modules and globals a mod gets through a `Compartment`. Secure ECMAScript freezes the primordial objects, "so a mod cannot monkey-patch built-ins to attack the host" ([mods](https://moddable.com/documentation/xs/mods), [Compartment](https://www.moddable.com/documentation/xs/XS%20Compartment)). The runtime is LGPL-3.0, the tools GPL-3.0, and a commercial license is available ([licenses](https://github.com/Moddable-OpenSource/moddable/blob/public/licenses/readme.md)).

**Bangle.js.** `metadata.json` declares a `type` (`app`, `clock`, `widget`, `module`, `bootloader`, `settings`, `clkinfo`, `launch`, `notify`, ...), `supports` (device ids), `dependencies` and `provides_widgets`. Two optional pages run inside the web App Loader: `custom.html` (customize before upload) and `interface.html` (talk to the installed app) ([BangleApps README](https://github.com/espruino/BangleApps)). The loader is a static site that anyone can fork and host, as long as it is served over HTTPS for Web Bluetooth ([App Loader customisation](https://www.espruino.com/Bangle.js+App+Loader+Custom)). It has no permission model.

**Tactility (ESP32 OS).** External apps are `.app` tar files containing `manifest.properties` (sdk, platforms, id, versionCode), one ELF per platform and assets. The lifecycle is `onCreate/onShow/onHide/onDestroy/onResult` ([fundamentals](https://docs.tactilityproject.org/v0.7.0/app-development/fundamentals/)). Symbols are exported by hand, and no ABI policy is documented ([symbols](https://docs.tactilityproject.org/v0.7.0/app-development/symbols/)). The OS is GPL-3.0 and the SDK Apache-2.0 ([LICENSE.md](https://github.com/TactilityProject/Tactility/blob/main/LICENSE.md)).

**ESP-Brookesia** is Espressif's C/C++ phone-style HMI framework, with no downloadable apps ([docs](https://docs.espressif.com/projects/esp-brookesia/en/latest/index.html)). **InfiniTime** is one monolithic binary, and its request for installable apps is still open ([#1262](https://github.com/InfiniTimeOrg/InfiniTime/issues/1262)). **[I]** Brookesia is a reference for structure, not a platform. InfiniTime is where EspOS was before 0.3.0.

### 2.3 Runtimes for third-party code on ESP32-class chips

| Runtime | Code size | RAM | Speed vs native | Isolation | Notes |
|---|---|---|---|---|---|
| QuickJS (current) | "367 KiB of x86 code" for hello world ([site](https://bellard.org/quickjs/)) | We cap each app at 1.5 MB (our code) | No reliable ESP32 benchmark found | Memory-safe interpreter, plus memory, stack and interrupt limits (our code) | MIT. Full modern JS |
| MicroQuickJS | ~100 KB ROM (Thumb-2) | "as little as 10 kB of RAM" | "comparable to QuickJS" | Same | ES5-like subset, stdlib in ROM, MIT ([repo](https://github.com/bellard/mquickjs)) |
| Moddable XS | Bytecode runs from flash | Low (runs in place) | n/a | Compartments + SES | LGPL/commercial. Now in PebbleOS |
| WAMR | 58.9 K fast interpreter, 56.3 K classic, 29.4 K AOT runtime (Cortex-M4F) ([README](https://github.com/bytecodealliance/wasm-micro-runtime)) | Linear memory is at least one 64 KB page unless truncated to `__heap_base` ([memory tune](https://github.com/bytecodealliance/wasm-micro-runtime/blob/main/doc/memory_tune.md)) | AOT ≈ 0.79× native CoreMark on x86 ([wiki](https://github.com/bytecodealliance/wasm-micro-runtime/wiki/Performance)) | Bounds-checked linear memory, imports only | Apache-2.0. ESP-IDF component lists esp32s3 ([registry](https://components.espressif.com/components/espressif/wasm-micro-runtime)). XIP AOT mode ([XIP](https://wamr.gitbook.io/document/wamr-in-practice/features/xip)) |
| wasm3 | Small | ~10.6 KB stack for fib(24) on ESP32 ([perf](https://github.com/wasm3/wasm3/blob/main/docs/Performance.md)) | "~12x slower than native" (README). 11× on ESP32-C6 bubble sort | Same | "minimal maintenance phase" ([README](https://github.com/wasm3/wasm3)) |
| Berry | Very small by design | Small | "not comparable to mainstream languages" ([manual](https://berry.readthedocs.io/en/latest/source/en/Reference.html)) | Interpreter | MIT. Tasmota + LVGL |
| Native ELF (`elf_loader`) | n/a | Code loaded to PSRAM or IRAM ([component](https://components.espressif.com/components/espressif/elf_loader)) | 1× | **None** | Needs a stable C ABI |

On an ESP32-C6, an arXiv study measured bubble sort at 577.5 µs native vs 6,358 µs on wasm3. Firmware footprint was 156 KB with wasm3 and "almost 480 KB" with WAMR (interpreter only, no AOT tested) ([arXiv 2512.00035](https://arxiv.org/html/2512.00035)). **[I]** No source gives ESP32-S3 numbers for QuickJS or for WAMR AOT on Xtensa. We have to measure them ourselves (Phase 3).

## 3. Themes and watch faces

- **Tokens and roles.** In Material 3, one source colour becomes five key colours (primary, secondary, tertiary, neutral, neutral variant). Each gets a tonal palette, and tones fill semantic roles such as primary / on-primary / surface ([Android colour](https://developer.android.com/design/ui/mobile/guides/styles/color)). Material describes tokens and roles as what "allow apps to seamlessly accept any dynamic color scheme" ([Material blog](https://material.io/blog/start-building-with-material-you)). Apps **opt in** with `DynamicColors.applyToActivitiesIfAvailable` and a dynamic-colour theme overlay. Widgets can use dynamic colour from Android 12 ([dynamic colors](https://developer.android.com/develop/ui/views/theming/dynamic-colors)).
- **A file format for tokens.** The W3C Design Tokens Community Group published its first stable format, 2025.10, plus a Resolver module for light/dark contexts ([format](https://www.w3.org/community/reports/design-tokens/CG-FINAL-format-20251028/), [resolver](https://www.w3.org/community/reports/design-tokens/CG-FINAL-resolver-20251028/)).
- **LVGL support.** A theme is an `apply_cb` that adds styles per widget class, and themes chain with `lv_theme_set_parent` ([LVGL themes](https://docs.lvgl.io/9.5/common-widget-features/styles/themes.html)).
- **A failure case.** On Bangle.js, system widgets follow the light/dark setting, but apps that clear the screen to black make the widgets draw wrong ([forum](https://forum.espruino.com/conversations/371320)). **[I]** If apps paint their own colours, the system theme breaks. Components must take colours from tokens by default.
- **Faces as their own type.** Garmin (watch face type with a reduced API), Zepp (`appType: watchface`, editable components, no-code Watch Face Maker) and Bangle (`clock` and `clkinfo` providers) all separate faces from apps (sources in §2.2).
- **[I] Safe themes are data only.** A theme package should contain a token JSON (a DTCG subset), wallpapers, optional icon glyph sets and fonts, and **no code**. Validate it against a schema. Then themes can skip code review, can come from any source, and also work on the e-ink device. For "dynamic colour", compute the roles from the wallpaper on the web or server when the user picks it, and send the device a finished token set.

## 4. Hosted plus self-host in open-source device ecosystems

| Project | Server / service | How clients find the server | Funding | Licence |
|---|---|---|---|---|
| Home Assistant | Runs locally. **Home Assistant Link** (renamed from Cloud in Oct 2026) is optional: remote access, voice, backups. "your data is encrypted, and only you have the key" ([blog](https://home-assistant.io/blog/2026/10/02/big-tech-ruined-the-cloud-so-were-renaming-ours/)) | Local by default | "the majority of profits from subscriptions now fund the Open Home Foundation" (same post) | Apache-2.0 (core) |
| ESPHome | Device firmware generated from YAML | Through HA | Open Home Foundation ([structure](https://www.openhomefoundation.org/structure/)) | GPLv3 C++ runtime + MIT Python ([LICENSE](https://github.com/esphome/esphome/blob/dev/LICENSE)) |
| Meshtastic | Public MQTT broker `mqtt.meshtastic.org` for general use and testing | Change the server address to self-host ([MQTT](https://meshtastic.org/docs/software/integrations/mqtt/)) | Community | GPL-3.0 firmware |
| Bitwarden / Vaultwarden | Official server, or [Vaultwarden](https://github.com/dani-garcia/vaultwarden), "Unofficial Bitwarden compatible server" | Login screen region picker with "Self-hosted" + server URL ([help](https://bitwarden.com/help/change-client-environment/)) | Paid plans | Server AGPL-3.0 + proprietary "Bitwarden License" directory, and trademark guidelines ([LICENSE](https://github.com/bitwarden/server/blob/main/LICENSE.txt)) |
| Tailscale / Headscale | [Headscale](https://github.com/juanfont/headscale), "self-hosted implementation of the Tailscale control server" | Client flag `--login-server` | Tailscale paid tiers | BSD-3 |
| Ghost | Ghost(Pro) hosting "generates revenue for the non-profit foundation", with no outside funding ([about](https://ghost.org/about), [docs](https://docs.ghost.org/product)) | n/a | Hosted tier | MIT + trademarks |
| Immich | Self-host only | Server URL in the app | Purchases that "never" unlock features. AGPL with no CLA ([blog](https://immich.app/blog/2024/immich-licensing)) | AGPL-3.0 |
| Toit Artemis | Hosted fleet service | "you can host your own broker" ([docs](https://docs.toit.io/getstarted)) | Free ≤10 devices | |
| Rebble | Community-run replacement services | Boot URL flow (§2.2) | Small subscription for costly features (weather, voice) | Mixed |

Other findings:
- **Push notifications without a central vendor.** The HA companion app's free relay is capped at 500 notifications per device per day "to keep the notification service free" ([HA docs](https://companion.home-assistant.io/docs/notifications/notification-details)). UnifiedPush and ntfy let users pick their own push server on Android ([F-Droid](https://f-droid.org/2022/12/18/unifiedpush.html)). **[I]** EspOS devices already hold their own connection to the server, so device push needs no FCM. Phone or web alerts could use Web Push or ntfy, which are optional.
- **Licence changes cause forks.** HashiCorp's 2023 switch from MPL to BSL produced the OpenTofu fork under the Linux Foundation ([OpenTofu](https://opentofu.org/blog/opentofu-announces-fork-of-terraform/)). **[I]** Don't move features behind a non-open licence later. Charge for running costs (data feeds, voice, backups, managed hosting) instead.
- **Two common patterns:** a **single open protocol** with several servers (Bitwarden/Vaultwarden, Tailscale/Headscale), and **a default server plus an override** (Meshtastic, Bitwarden, Rebble boot).

## 5. Security on an ESP32-S3 app platform

**Hardware limits.** The S3 has Permission Control (PMS) for memory protection ([ESP-IDF security](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/security/security.html)). NuttX's protected build on the S3 uses "the World Controller (WC) and Permission Control (PMS) peripherals for implementing isolation between Kernel and Userspace" ([NuttX](https://nuttx.apache.org/docs/latest/platforms/xtensa/esp32s3/boards/esp32s3-devkit/index.html), [source](https://github.com/apache/nuttx/blob/master/arch/xtensa/src/esp32s3/esp32s3_userspace.c)). Espressif's own ESP Privilege Separation splits the firmware into a protected app and a user app ("two separate and independent binaries"), but it targets the ESP32-C3, is labelled beta ([blog](https://developer.espressif.com/blog/introducing-esp-privilege-separation/)), and the repo was last pushed in Dec 2022 ([repo](https://github.com/espressif/esp-privilege-separation)). **[I]** These mechanisms give two worlds (kernel vs user) with coarse region boundaries. They cannot give each app its own address space the way Pebble's per-task MPU regions do. Third-party native code therefore cannot be sandboxed on EspOS in practice: one bad ELF can read WiFi keys or corrupt the heap. Flipper and Tactility accept that risk. A store open to anyone should not.

**[I] Isolation inside the interpreter (what we can enforce):**
- **Capability bindings.** An app's global scope contains only the API objects its permissions allow; no `std` or `os` modules. Each binding checks the permission again, the way Pebble syscalls validate arguments. For store apps, consider leaving out `eval`/`Function` (QuickJS lets you choose the intrinsics added to a context) so that no code can be downloaded at runtime.
- **Quotas.** Memory per runtime (`JS_SetMemoryLimit`, already 1.5 MB). CPU per slice through the interrupt handler (already present). A watchdog that closes an app which blocks the UI longer than N ms. Storage per app (KB). Timers (already 8). Notification rate. Network requests per minute and bytes.
- **Secrets.** WiFi credentials, account tokens and server keys stay in encrypted NVS, and no binding returns them. `wifi.info` returns SSID and signal only. App storage is namespaced per app id, and package files are read-only.
- **Network.** `net:<host>` allowlist, HTTPS only, certificate check on, and private/LAN addresses blocked unless `net:lan`. Requests go through one OS queue, because only one TLS session fits on the device.
- **Recovery.** A JS exception or OOM closes the app with a message. If the device crash-loops right after an install, boot into safe mode with third-party packages off.
- **Engines are trusted code with untrusted input.** Pet, run and j2me are first-party, but `j2me` will run JARs downloaded by users. The VM must verify bytecode and bounds-check everything, because a VM bug is a firmware bug.

**Signing.**
- Secure Boot V2 on the S3 is RSA-3072 (RSA-PSS), with up to three revocable key digests in eFuse ([ESP secure boot](https://docs.espressif.com/projects/esp-iot-solution/en/latest/security_solution/secure_boot.html)). Use it for firmware.
- **[I] Packages:** use Ed25519. Signatures are 64 bytes, verification is cheap, and the code is small. Espressif publishes a libsodium component ([libsodium](https://components.espressif.com/components/espressif/libsodium)). ESPHome's trimmed build covers "Ed25519 detached verification" only ([esphome/libsodium](https://components.espressif.com/components/esphome/libsodium/versions/1.10021.12/readme)).
- **[I] Two signatures, after TUF's role split** ([TUF roles](https://theupdateframework.io/metadata/)):
  - The **developer** signs the package. An update must use the same key, as in Garmin's model.
  - The **store or repo** signs the catalog index, with an expiry date, so stale or swapped catalogs are rejected.
  - Keep the root key offline.
  - Replace CRC32 in `.espk` with SHA-256 per file and add a signature block. CRC catches corruption, not tampering.

**[I] Store review.** For JS packages:
- Automatic checks: manifest schema, permissions vs APIs used (static scan of `main.js`), size, and a headless simulator run that captures screenshots and fails on exceptions.
- Human review: required for `net`, `bg` and new authors.
- Source availability: Flipper's rule (an open-source repo and a pinned commit) is cheap and builds trust. JS is shipped as source anyway.

## 6. Hardware and peripherals: EspOS as an IoT OS

### 6.1 How platforms let apps use hardware

| Platform | Who talks to the bus | What apps see | Consent |
|---|---|---|---|
| [Android Things](https://developer.android.com/reference/com/google/android/things/pio/PeripheralManager) | Apps, through Peripheral I/O (GPIO, PWM, I2C, SPI, UART). "User drivers" feed sensors and input into Android | Raw buses, or framework sensors | Listing is open. Opening a peripheral needs `USE_PERIPHERAL_IO` ([DP8 notes](https://www.developer-tech.com/news/android-things-developer-preview-8-apis/)) |
| [Android USB host](https://developer.android.com/develop/connectivity/usb/host) | Apps, through `UsbManager` | Raw USB device | A manifest filter by vendor/product id makes Android offer to open the app when a matching device is attached. Otherwise `requestPermission()` shows a dialog per device ([UsbManager](https://developer.android.com/reference/android/hardware/usb/UsbManager)) |
| Web Serial / Web Bluetooth | Pages, through the browser | Raw port / GATT services | A device chooser opened by a user gesture, with filters. Granted ports come back from `getPorts()` ([Serial](https://developer.chrome.com/docs/capabilities/serial)). Pages can use only services listed in `filters`/`optionalServices`, and a GATT blocklist applies ([Bluetooth](https://developer.chrome.com/docs/capabilities/bluetooth)) |
| Garmin, Zepp, Fitbit | The OS | Typed sensors (heart rate, position) | Manifest permissions: Garmin `Sensor` and `Positioning`; Zepp `device:os.bg_service`, which can also be asked at runtime with `requestPermission` ([Zepp](https://docs.huami.com/docs/reference/device-app-api/newAPI/app/requestPermission/)); Fitbit `access_heart_rate` |
| [Toit](https://docs.toit.io/tutorials/hardware/bme280) | App code opens `i2c.Bus` with pin numbers | Driver packages from pkg.toit.io. A driver takes a generic `serial.Device`, so the same driver works over I2C or SPI | None: the owner is the developer |
| [ECMA-419](https://419.ecma-international.org/) / Moddable | Native IO classes | IO Class Pattern (Digital, I2C, SPI, Serial). Sensor Class Pattern (`sample()`, `configure()`, `close()`, 23 sensor kinds from temperature to CO2 and PM) ([sensor class](https://embedded.js.org/api/sensor-class/)). A `device` host provider with named pins, default buses and IO, sensor and display constructors ([host provider](http://embedded.js.org/api/host-provider/)) | The host decides what to expose |
| [ESPHome](https://esphome.io/components/external_components/) | C++ components compiled into the firmware from YAML. External components come from git | Entities in Home Assistant | Owner rebuilds the firmware |
| [Tasmota](https://tasmota.github.io/docs/I2CDEVICES/) | Compiled-in I2C drivers, switched on and off by index (`I2CDriver<n>`). Berry drivers via `tasmota.add_driver` ([cookbook](https://tasmota.github.io/docs/Berry-Cookbook/)) | Readings in telemetry | Owner config |
| [Zephyr](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor.html) | Drivers bound by devicetree `compatible` | `sensor_sample_fetch` + `sensor_channel_get`. Channels have fixed units (ambient temperature in °C) | Build time |
| [RIOT SAUL](https://doc.riot-os.org/drivers_2include_2saul_8h.html) | Drivers register with SAUL | Device classes (actuators 0x40.., sensors 0x80..). Values are `phydat` with a unit and scale | Build time |
| [NuttX](https://nuttx.apache.org/docs/latest/components/drivers/special/sensors/sensors_uorb.html) | Lower-half drivers | The upper half provides a char device per topic, ring buffers, multi-user access and downsampling | File permissions |
| [Particle](https://docs.particle.io/reference/device-os/api/cloud-functions/particle-variable/) | Firmware | Cloud `variable`, `function`, `publish`. Publish payloads are about 1024 B, at about 1 event/s ([publish](https://docs.particle.io/reference/device-os/api/publish/particle-publish-publish/)) | Account |
| [Arduino Cloud](https://docs.arduino.cc/arduino-cloud/cloud-interface/variables) | Sketch | Typed cloud variables such as `CloudTemperatureSensor`, synced both ways | Account |
| [Matter](https://handbook.buildwithmatter.com/howitworks/datamodel) | Device | Node → endpoints → clusters with attributes, commands and events. Temperature Measurement is 0x0402 (0.01 °C units), On/Off is 0x0006 ([Nordic](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/protocols/matter/getting_started/adding_clusters.html)) | Commissioning |
| [Home Assistant](https://www.home-assistant.io/integrations/sensor.mqtt/) | Integrations | Entities with `device_class`, `state_class` and `unit_of_measurement` (also through MQTT discovery) | Admin |

**[I] Patterns:**
- **Two layers almost everywhere.** Bus-level drivers (trusted, usually native) sit underneath. Typed sensors and actuators with fixed units sit on top: Zephyr channels, SAUL classes, NuttX topics, ECMA-419 sensor classes, Matter clusters, HA device classes. Apps rarely need a raw bus.
- **Consent depends on who wrote the code.** Phones and browsers ask per device because apps are written by strangers. Wearables ask per capability. Maker firmware asks nothing because the owner built it. An IoT OS with a public store is in the phone situation.
- **Sampling belongs to the OS.** Garmin's `Sensor` permission is valid only for some app types ([forum](https://forums.garmin.com/developer/connect-iq/f/discussion/2088/watch-and-sensor-permission-allow-in-manifest)). A bug report says that just declaring it costs about 1.5 %/h of battery on one watch ([bug](https://forums.garmin.com/developer/connect-iq/i/bug-reports/forerunner-970-fw-18-32-open-device-app-with-the-sensor-permission-drains-1-5-h-extra-even-with-no-sensor-calls)). Apps should subscribe; they should not each poll.
- **ECMA-419 fits EspOS best.** It is standard JS naming for IO and sensors, and Moddable ships it. We can copy its shapes without full compliance, and developers get documented patterns.

### 6.2 Plug-and-play on ESP32-S3 hardware

- **Free pins are the first limit.**
  - Octal PSRAM occupies GPIO33-37. GPIO19/20 are USB. GPIO0/3/45/46 are strapping pins ([RIOT S3 notes](https://api.riot-os.org/group__cpu__esp32__esp32s3.html)).
  - On the ES3C35P, the display, touch and codec I2C (38/39), I2S, SD (2-7), amplifier, LED and battery ADC use almost every other pin (board notes).
  - **[I]** On today's boards, expansion means the shared I2C bus, BLE, UART0 or USB. An external device that holds the shared I2C bus low also breaks touch (0x55) and audio (0x18). Future boards should give sensors their own Qwiic I2C port, or a buffer or mux.
- **Connectors.** Qwiic and STEMMA QT use the same 4-pin, 1.0 mm JST SH (GND, V+, SDA, SCL), and Qwiic runs at 3.3 V. Grove is a 2.0 mm connector with a different pin order, and it also carries UART, analog and digital signals ([Adafruit](https://learn.adafruit.com/introducing-adafruit-stemma-qt), [Bus Pirate](https://docs.buspirate.com/docs/overview/qwiic-stemma-breakout-garden-grove-adapter/)).
- **I2C devices have no identity.**
  - There is no standard detection command, and probing "can confuse your I2C bus, cause data loss and worse" ([i2cdetect](https://manpages.debian.org/i2cdetect)).
  - Addresses collide. AHT2x shares 0x38 with VEML light sensors, so Tasmota makes users switch drivers by index ([AHT2x](https://tasmota.github.io/docs/AHT2x/)).
  - **[I]** Probe only the addresses in the known-driver table, using the safest read for each. Confirm with chip-ID registers. Ask the user when the result is ambiguous, and remember the answer.
- **1-Wire, BLE and USB devices do have identity.** A 1-Wire ROM id carries a family code in its low 8 bits, and the search algorithm enumerates every device on the wire ([Analog AN187](https://www.analog.com/en/resources/app-notes/1wire-search-algorithm.html)). BLE sensors can expose the standard Environmental Sensing service 0x181A, with temperature at 0x2A6E ([card10 ESS](https://firmware.card10.badge.events.ccc.de/bluetooth/ess.html)). USB devices report vendor, product and class ids.
- **USB host on the S3.**
  - ESP-IDF's host library has class drivers for CDC-ACM, HID, MSC, UAC and UVC. Hub support comes with the newer esp-usb component, which is a managed component from IDF 6.0 ([esp-usb](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host/usb_host_notes_arch.html)).
  - USB-OTG and USB-Serial/JTAG share one PHY, so only one works at a time. Host stacks take the PHY when they start ([ESP-IDF USB](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/peripherals/usb_device.html)).
  - **[I]** While USB host is active we lose the USB console and the flashing port, and the board must supply 5 V VBUS. That needs a "USB mode" setting with BOOT-button recovery, so it comes last.
- **[I] UART, ADC and PWM have no discovery**, so the user picks the device type. **Hot-plug:** USB and BLE give attach and detach events. I2C and 1-Wire need a slow rescan plus error counting. **Power:** sensors share the 3.3 V rail with the backlight, so drivers should declare their typical current and the Devices page should warn.

### 6.3 Permission design for hardware [I]

Three layers, mirroring §6.1:
1. **Bus primitives (native, in the firmware):** I2C, SPI, UART, 1-Wire (RMT), GPIO, ADC, PWM, BLE scan/GATT, USB host. Timing-critical protocols stay native, like ECMA-419 IO classes.
2. **Drivers (`type: driver` packages):** JS that runs on the bus primitives.
   - A driver declares `match` rules: I2C address + chip-ID value, 1-Wire family, USB VID/PID/class, BLE service UUID or advert pattern, or `manual` for UART and GPIO.
   - It declares the typed channels it `provides`.
   - Only drivers may hold `hw:*` permissions (`hw:i2c`, `hw:uart`, `hw:gpio:<pin>`, `hw:ble`, `hw:usb:<vid>:<pid>`). Drivers need review and a signature, and run in the OS driver runtime, never inside an app.
3. **Apps see typed channels only.**
   - Reading environment sensors (`sensor:temperature`, `sensor:co2`, ...) is an install-time permission.
   - Controlling anything (`actuator:switch`, `actuator:light`) needs the install-time permission **plus** a per-device prompt the first time, like Android USB ("Tưới cây muốn bật/tắt Relay 1 — Cho phép?").
   - Raw access to a bus has the same rule as controlling.
   - The system **Devices** page lists and revokes every grant.

**Devices page:** every device with its name, room, bus, driver and version, channels, last value and battery; the apps that use it; sharing switches. Actions: rename, forget, rescan, and "add by hand" for UART and GPIO devices.

**Services owned by the OS:**
- **Sampling and history.** One sampler per channel, at the fastest rate any subscriber asked for (capped). A ring buffer in PSRAM and minute aggregates on flash or SD, which is the NuttX upper-half idea (ring buffers, multi-user access, downsampling).
- **Rules.** Declarative "if channel/condition/time → notify, set an actuator, call a webhook", edited in the web app. Apps can add new actions. No app has to run in the background just to watch a threshold.
- **Sharing.** A per-device switch syncs readings to the EspOS server. An optional MQTT publisher sends Home Assistant discovery fields (`device_class`, `unit_of_measurement`, `state_class`). A Matter bridge is a later idea, and a heavy one.
- **Driver updates (OTA).** Drivers update through the store like apps. A driver that faults N times is switched off and its device marked "needs attention", which is safe mode for drivers.
- **Actuator safety.** Each actuator gets a fail-safe state (relays off after reboot unless a rule says otherwise) and a manual override on the Devices page.

### 6.4 The EspOS sensor framework [I]

Channel types, with fixed units that map one-to-one to HA device classes for export:

| Channel | Unit | Channel | Unit |
|---|---|---|---|
| temperature | °C | light | lx |
| humidity | %RH | motion, contact, presence | bool |
| pressure | hPa | power / energy | W / kWh |
| co2 | ppm | battery | % |
| voc_index | index | button | event (press, double, long) |
| pm25, pm10 | µg/m³ | switch, light (actuators) | on/off, 0-100 %, colour |

A sketch of the JS API, using ECMA-419 shapes where they exist:

```js
const t = sensors.find("temperature")[0];                 // {id, name, room, unit}
sensors.on(t.id, s => ui.update(s.value), {every: 60});   // s = {value, unit, ts, quality}
const day = await sensors.history(t.id, {since: Date.now() - 864e5, step: 600});
const relay = devices.find("switch")[0]; await relay.set(true);   // per-device consent first time
// driver: export default class SHT4x { constructor(o) { this.io = new device.io.I2C(o.sensor); } sample() { /* ... */ } }
```

- **Which buses, in order:**
  1. BLE sensors: no wiring, no pin conflicts, standard ESS.
  2. External I2C (Qwiic), with first-party drivers for a few common temperature/humidity, CO2 and light sensors.
  3. GPIO inputs and relays, and 1-Wire, on boards with free pins.
  4. UART devices.
  5. USB host.
- **Simulator: a fake bus layer, not electrical emulation.**
  - `sim/devices.json` declares devices: bus, address or BLE id, driver, and a value script (constant, sine, random walk, or CSV replay).
  - The dev console sets values and plugs or unplugs devices (`dev sensor add sht4x@0x44`, `dev sensor set temp1 23.5`).
  - The same layer can sit under QEMU builds, so drivers' `match` logic and the apps run unchanged.
  - Driver authors get an I2C register-map mock.
- **Memory budget:**
  - First-party and reviewed drivers share one QuickJS runtime, one context each (target ≤256 KB PSRAM for all of them). Third-party drivers get their own runtime.
  - History rings live in PSRAM. One channel at one sample per minute for a day is about 1,440 × 8 B ≈ 11 KB.
  - Measure the BLE host stack's RAM and WiFi/BLE coexistence on the board before enabling continuous scanning. Keep the framework's share of internal SRAM to a few tens of KB.

## 7. Recommendations for EspOS

### 7.1 Runtimes

| Runtime | Use | Decision **[I]** |
|---|---|---|
| QuickJS | All third-party apps, widgets with logic, services | **Keep as the platform runtime.** Freeze the API, add TS typings and capability globals. Measure RAM and CPU per app on the S3 |
| Declarative templates (JSON + bindings, native renderer) | Widgets, faces, themes, settings pages | **Add first.** Fast, safe, no code review, works on the e-ink C3 too |
| WAMR (interpreter first, AOT later) | Heavy compute (emulators, physics, image work) in C, Rust or Zig | **Experiment in Phase 3.** Gate: firmware cost under ~100 KB, AOT on Xtensa proven, and a real app that needs it |
| Native engines (pet, run, j2me) | First-party only. Packages carry data | **Keep.** No third-party native code. New engines only through the main repo |
| MicroQuickJS | Possible lite JS on the C3 e-ink device (10 KB RAM) | Evaluate later. The ES5 subset means a separate API profile |

Why not more runtimes now: flash is 98% full. Each runtime is another API binding layer to keep stable. JS already covers the developer audience Pebble chose Alloy for.

### 7.2 App types (manifest `type`)

| type | What it is | Budget **[I]** | Platform analogues |
|---|---|---|---|
| `app` | Full-screen, foreground, with lifecycle `onOpen/onShow/onHide/onClose` | 1.5 MB heap, full `ui.*` | Garmin device app, Zepp page |
| `widget` | Home-screen card, size classes S/M/L, tap opens the owning app | Declarative template; optional `render()` with a 50 ms budget and a small heap; refresh limits | Garmin glance, Zepp app-widget, Bangle clkinfo |
| `face` | Clock face and screen saver | Declarative; updates at most once per minute when idle | Garmin watch face, Zepp watchface |
| `theme` | Tokens, wallpapers, glyphs, fonts | Data only, schema-validated | Material tokens |
| `service` | Background job: `every`/`at` triggers, fetch, `notify`, write app storage | Short runs (e.g. ≤2 s, 256 KB), queued with system sync | Garmin Background (needs a permission), Zepp app-service, Fitbit `run_background` |
| `pack` | Data for a native engine (today's `kind: native`) | n/a | |
| `driver` | Matches a device and provides typed channels (§6.3) | Runs in the OS driver runtime, small heap, signed and reviewed | ECMA-419 sensor classes, Toit driver packages, Tasmota Berry drivers |
| `settings` (part of any type) | JSON schema rendered by the web desk **and** the device | n/a | Pebble Clay, Zepp `setting` |
| `web` (optional) | A page shown in a sandboxed iframe on the web desk that may read and write only this app's settings and storage | n/a | Bangle `custom.html`/`interface.html`, Zepp app-side |

### 7.3 Permissions

Rules: declared in the manifest, shown in plain words in the store, accepted at install, switchable per app in Settings (next to today's notification switches), and checked in each binding. Code can test them with `app.can('net')`.

| permission | Grants | Notes **[I]** |
|---|---|---|
| `notify` | `notify*`, `app.badge` | Exists. Add a per-day cap |
| `sound` | `sound.*` | Exists |
| `net:<host>` (repeatable), `net:*`, `net:lan` | `fetch` to those hosts | `net:*` and `net:lan` get human review |
| `bg` | `service` entry points | Shown as "runs in the background" |
| `storage:<KB>` | App store quota (default 64 KB) | Larger quotas are shown at install |
| `data:weather`, `data:calendar`, `data:markets`, `data:news` | Read the OS's cached dashboard data | Calendar shows events only, never ICS URLs (already masked in the web) |
| `location:city` | The city/coordinates from settings | No GPS hardware |
| `wifi.info` | SSID and RSSI | Never credentials |
| `remote` | Receive messages from the app's web page or server | |
| `screen.awake` | Keep the screen on while open | |
| `sensor:<channel>` | Read typed channels and their history (§6.4) | Install-time only |
| `actuator:<channel>` | Control switches, lights, etc. | Install-time + per-device prompt, revocable on the Devices page |
| `hw:i2c`, `hw:uart`, `hw:gpio:<pin>`, `hw:ble`, `hw:usb:<vid>:<pid>` | Raw bus access | **Drivers only.** Human review + signature |

Contacts are left out: the device has no contact data.

### 7.4 Manifest v2 and API levels

```json
{"id": "vn.you.timer", "name": "Hẹn giờ", "version": "1.2.0", "code": 10200,
 "type": "app", "runtime": "js", "api": {"min": 1, "target": 2}, "min_fw": 300,
 "screens": ["320x480", "240x320"], "theme": "system",
 "perms": ["notify", "sound", "net:api.open-meteo.com", "storage:128"],
 "entry": {"app": "main.js", "widget": "widget.json", "settings": "settings.json"}}
```

**[I]** Notes on the fields:
- `api` is a separate integer from `min_fw`. Adding an API raises the level. Nothing is removed without a deprecation period. Apps can feature-test (`if (ui.chart)`), like Garmin's `has`.
- The store hides apps whose `api.min` is above the device's level and shows "needs update".
- `screens` follows Zepp `targets` and Bangle `supports`.
- `theme: "system"` (the default) means components use tokens. `"custom"` means the app paints its own colours, and review checks the contrast in light and dark.

### 7.5 SDK and tools

**[I] The SDK:**
- **`@espos/sdk` (npm, MIT)**: `espos.d.ts` per API level with `@since` tags (as in Flipper's fz-sdk), JSON schemas for the manifest, widgets, faces, themes and settings, templates, and an esbuild-based bundler so apps can be written in TS.
- **CLI `espos`**, wrapping `tools/espk.py`:

| Command | What it does |
|---|---|
| `create --template app\|widget\|face\|theme\|service` | New project from a template |
| `build` (with `check`) | Bundle, lint the manifest, compare perms with APIs used |
| `sim` | Host SDL simulator with the package mounted |
| `sim --web` | Browser simulator |
| `run --device <ip>` | Push over WiFi plus live reload, like Jaguar or `zeus dev` |
| `logs`, `shot` | Stream the console, take screenshots (like `pebble logs`/`screenshot`) |
| `sign` | Ed25519 developer key |
| `publish` | Upload to a store, or open a catalog pull request |

- **Device developer mode.** Settings → Developer → enable the LAN dev server. The screen shows a one-time code. Unsigned packages are allowed and marked "dev". There is a log WebSocket.
- **Simulators:**
  - Keep the SDL host simulator.
  - Compile it to WebAssembly with Emscripten, which LVGL supports ([LVGL browser](https://docs.lvgl.io/9.5/integration/pc/browser.html)). That gives a browser playground in the web desk and the docs, like CloudPebble's in-browser emulator.
  - Keep QEMU for firmware-level tests and CI.
  - Wokwi is an optional third route: it simulates the S3 with octal PSRAM and has CI and screenshot support ([ESP-IDF Wokwi](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/third-party-tools/wokwi.html)).
- **Docs site.** API reference generated from the typings, guides per app type, an example gallery, and the protocol spec.

**[I] MVP vs later:**
- **MVP:** typings for API level 1, `create/build/sim/run/logs`, manifest v2, the consent screen, `net:<host>`, signing, the declarative widget and theme types.
- **Later:** the web simulator, services, the `web` type, WAMR, a developer portal with review queues.

### 7.6 Server: hosted and self-hostable

**[I] What to build:**
1. **An EspOS Protocol v1 spec** (OpenAPI + JSON schemas), covering:
   - pairing and tokens
   - settings sync (`s3.*`, last writer wins)
   - data feeds (weather, ICS, markets, news)
   - catalog and package download
   - device events and notifications (one long-lived connection)
   - remote control
   - OTA manifest
   - The web app talks only to this API.
2. **A reference server** built against the spec: one Go binary, SQLite by default (Postgres optional), serving the web app and package files from disk or S3. Ship it as a Docker image (`docker compose up`). Self-hosters provide their own API keys for feeds. Add a **conformance test suite** that runs against both the reference server and the hosted service, so the two never drift. (Vaultwarden exists because Bitwarden's client protocol was usable by others.) Option: move the hosted service onto the reference server once it reaches parity.
3. **How a device picks its server:**
   - The default host is built in.
   - Setup and Settings both have a "Server" field. Scanning a QR code from the self-hosted web app fills it in.
   - The device fetches `https://<server>/.well-known/espos.json`: endpoint URLs, server name, protocol version, catalog sources with their keys, OTA channel. One URL configures everything, like Rebble's boot step.
   - The device remembers the server's public key at pairing (trust on first use) and lets the user add a custom CA for private servers.
4. **The store across servers:**
   - A catalog is a **signed static index plus files**, which is what EspOS already hosts on public storage.
   - A device can subscribe to several sources: the official store plus the self-hoster's own. Each source has its own key.
   - A self-hosted server can mirror the official catalog; package signatures stay valid on any mirror.
   - Store content belongs to its authors, and the archive is exportable (the Rebble lesson).
5. **Identity.** Hosted: Google sign-in plus email. Self-hosted: local accounts or OIDC. Device tokens are scoped per device and revocable.
6. **OTA.** Firmware is signed (Secure Boot V2 + an app-level signature check). The OTA manifest comes from the configured server. Self-hosters can mirror GitHub releases. Channels: stable and beta.
7. **The hosted tier pays for running costs:** managed server, feed API keys, backups, store hosting, voice or AI later. All device features also work against a self-hosted server.

### 7.7 Migration: what moves to packages

**[I]** The rule: anything a third party could have written becomes a package. Anything that defines the platform stays in the firmware.

| Move to packages (in order) | Why |
|---|---|
| Markets, news, notes, focus timer, vocabulary | Simple UIs that test the JS API. Frees flash |
| Clock faces / screen-saver styles | Become `face` templates. Proves the declarative renderer |
| Built-in colour themes, wallpapers | Become `theme` packages |
| Weather and calendar *screens* | Later, once `data:*` permissions exist. The feeds stay in the OS |
| PawBoop, Run, J2ME games | Already `pack` + engine. Stay that way |

**Stays in the base firmware:** home screen and launcher, widget and face renderer, theme engine, settings, store and installer, notifications, data-feed sync, network/TLS queue, OTA, power, input, the JS runtime and engines, and safe mode. For IoT, also the bus primitives, the driver runtime, the sensor sampler and history, the rule engine, the Devices page and the MQTT publisher. Sensor drivers themselves are packages, including first-party ones.

### 7.8 Licensing

**[I]** The current public repo is MIT; past releases stay MIT. Going forward:
- **Firmware**: MPL-2.0 (file-level copyleft, friendly to board makers) or GPL-3.0 (as Flipper, Meshtastic and Tactility do). Choose before outside contributions arrive. Either way, the SDK headers and JS typings that apps link against should be MIT or Apache-2.0, as in Tactility's GPL OS + Apache SDK split.
- **Server**: AGPL-3.0 (Immich, Vaultwarden, Bitwarden core), so a closed hosted fork must share its changes, while self-hosting stays free.
- **Protocol spec, SDK, templates, examples**: MIT or Apache-2.0 / CC-BY.
- **Contributions**: use a DCO rather than a CLA (Immich promised no CLA), and a trademark policy for the "EspOS" name and logo so that forks rename (Bitwarden, Ghost).
- **Avoid open core**: no paid-only device features (see OpenTofu).

## 8. Risks

| Risk | Mitigation **[I]** |
|---|---|
| Flash is full (3.68/3.75 MB). New APIs or WAMR do not fit | Move apps to packages first. Review the partition table (OTA slot size vs 7.5 MB app area). Set a size budget per release |
| RAM/PSRAM with several JS contexts (foreground app + widgets + services) | One foreground app. Widgets declarative by default. Services serialized. Measure each step |
| One TLS session: apps' `fetch` competes with system sync | A single OS network queue with priorities, or an optional server-side fetch proxy for widgets (adds server dependence) |
| API freeze is forever | Level 1 kept small. Typings and tests as the contract. Deprecation windows |
| j2me runs untrusted JARs; copyright of old phone games | Hardened VM. The store only lists free or homebrew titles; users load their own JARs |
| Review and moderation cost | Automated checks + source required. Human review only for `net`/`bg` |
| Hosted and self-hosted drift; the hosted backend is not portable | Spec first + conformance suite. Reference server |
| Governance and trust (the Rebble/Core dispute) | Written content-ownership terms, exportable archive, public roadmap |
| Board fragmentation (320x480, 240x320, e-ink) | `screens` in the manifest, size classes, declarative widgets that adapt |
| Few free pins; external I2C shares the touch/codec bus | BLE first. Bus-stuck detection and recovery. A dedicated Qwiic port on the next board |
| BLE + WiFi + TLS together exceed internal RAM | Measure first. Scan in windows, not continuously. Keep BLE optional |
| Actuators switching real loads (relays, heaters) | Fail-safe states, per-device consent, rate limits on rules, a clear warning for mains devices |
| I2C probing confuses devices; address clashes | Probe only addresses with known drivers. Confirm chip-ID. Ask the user |
| USB host takes the console/flash port | USB mode setting with BOOT recovery. Ship it last |

## 9. Roadmap

1. **Phase 0 — Contract (2-4 weeks).**
   - Manifest v2 schema, API level 1 (today's `ui.*`, `timer`, `sound`, `notify`, `store`).
   - Protocol v1 draft. `espos` CLI with create/build/check/sim.
   - SHA-256 in `.espk`. Licensing decision and trademark note.
   - Reserve the names now: channel types and units, `sensor:`/`actuator:`/`hw:` permissions, and the `driver` type (§6.4), so API level 1 does not paint us into a corner.
2. **Phase 1 — Safe extensibility (1-3 months).**
   - Consent screen and per-app permission switches; `net:<host>`, `storage:<KB>`.
   - Ed25519 developer and catalog signatures.
   - Declarative `widget`, `face`, `theme` types.
   - Developer mode with push, live reload and logs.
   - TS typings and the docs site.
   - Migrate markets, news, notes, focus and the faces.
3. **Phase 2 — Self-host (3-6 months).**
   - Reference server (Go + SQLite, Docker) and conformance tests.
   - Server field, `.well-known` boot document, multi-source store.
   - OTA manifest from the server; OIDC/local accounts.
   - **Sensors v1:**
     - The sampler, history and Devices page, with first-party drivers built in.
     - BLE ESS sensors and the board's own sensors (battery, plus mic level from the codec).
     - `sensor:*` for apps, and fake devices in the simulator.
     - Readings shown on the server and in widgets.
4. **Phase 3 — Power features (6-9 months).**
   - `service` type with budgets, the settings schema rendered on web and device, the optional `web` iframe type.
   - Browser simulator (Emscripten).
   - WAMR spike: measure flash, RAM, interpreter and AOT CoreMark on the S3. Keep it or drop it.
   - **Sensors v2:**
     - The `driver` package type with `match`/`provides`, and Qwiic I2C with safe probing.
     - GPIO buttons and relays, plus `actuator:*` with per-device consent and fail-safe states.
     - The rule engine (edited in the web app) and the MQTT publisher with Home Assistant discovery.
5. **Phase 4 — Ecosystem (9-12+ months).**
   - Developer portal with review queues, other S3 boards via board profiles, an e-ink C3 profile (declarative widgets, maybe MicroQuickJS).
   - Hosted tier launch, public governance (roadmap, budget, store terms).
   - **Sensors v3:**
     - Community drivers through review.
     - 1-Wire, UART devices, then USB host (CDC/HID/MSC) behind a USB-mode setting.
     - A sensor-node build profile: a headless S3/C3 with no display, running drivers, rules and sharing only.
     - Evaluate a Matter bridge.

