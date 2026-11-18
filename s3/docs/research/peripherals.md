
# EspOS: peripherals and hardware access for apps

> Date: 2026-11-17. A sentence with a link is sourced from that link. **[I]** marks inference or recommendation. "Board notes" means our own ES3C35P pin map.
> **The owner's decisions so far:**
> - Third-party apps run in **JS and WASM from day one**.
> - The **hosted service** stays as it is. A **Go server** serves self-hosters. The two are kept in step by one conformance test suite.
> - Firmware and SDK are **Apache-2.0**; the server is **AGPL-3.0**.
> - Priority: **OS foundation + SDK**, after the Java games.

## 0. Summary

- Almost every platform separates **bus drivers** (trusted, usually native) from **typed sensors and actuators** with fixed units, which is what apps use. Examples: Zephyr, RIOT SAUL, ECMA-419, Matter, Home Assistant. Apps rarely get a raw bus.
- Consent depends on who wrote the code. Phones and browsers ask **per device** (Android USB, Web Serial, Web Bluetooth). Wearables ask **per capability** (Garmin `Sensor`, Fitbit heart rate). Maker firmware asks nothing (ESPHome, Tasmota, Toit). A public app store puts EspOS in the phone situation.
- On the S3 board, **free pins** and the **shared I2C bus** are the real limits. Devices on I2C cannot be identified reliably. 1-Wire, BLE and USB devices can.
- **[I] Recommendation:**
  - One driver interface in two package forms: **WASM** (vendor C drivers, our own first-party drivers) and **JS** (ECMA-419 style, for community drivers and BLE parsers).
  - **C++** only for bus primitives, timing-critical protocols and the board's own hardware.
  - Apps see typed channels. Controlling a device needs a per-device prompt. The OS owns sampling, history, rules and MQTT/Home Assistant sharing.
- **[I] First buses:** BLE (BTHome and ESS) and Qwiic I2C (SHT4x, BME280, SCD4x, a light sensor). Then GPIO, 1-Wire, UART, and USB host last.

## 1. How platforms let apps use peripherals

| Platform | Who drives the bus | What apps get | Consent |
|---|---|---|---|
| [Android Things](https://developer.android.com/reference/com/google/android/things/pio/PeripheralManager) | Apps, through Peripheral I/O (GPIO, PWM, I2C, SPI, UART). "User drivers" turn devices into framework sensors and input | Raw buses, or framework sensors | Listing is open. Opening a peripheral needs `USE_PERIPHERAL_IO` in the manifest ([DP8 notes](https://www.developer-tech.com/news/android-things-developer-preview-8-apis/)) |
| [Android USB host](https://developer.android.com/develop/connectivity/usb/host) | Apps, through `UsbManager` | Raw USB device | A manifest filter by vendor/product id makes Android offer to open the app when the device is attached, with access until it is unplugged. Otherwise `requestPermission()` shows a dialog for that one device ([UsbManager](https://developer.android.com/reference/android/hardware/usb/UsbManager)) |
| [Web Serial](https://developer.chrome.com/docs/capabilities/serial) / [Web Bluetooth](https://developer.chrome.com/docs/capabilities/bluetooth) | Pages, through the browser | Raw port, or GATT services | A chooser opened by a user gesture, filtered by ids. Granted ports come back from `getPorts()`. Pages can use only the services listed in `filters`/`optionalServices`, and blocklisted GATT UUIDs are refused |
| Garmin Connect IQ | The OS | Typed sensors | Manifest `Sensor`, `Positioning`, etc. `Sensor` is invalid for some app types ([forum](https://forums.garmin.com/developer/connect-iq/f/discussion/2088/watch-and-sensor-permission-allow-in-manifest)). One bug report says declaring it alone cost about 1.5 %/h of battery ([bug](https://forums.garmin.com/developer/connect-iq/i/bug-reports/forerunner-970-fw-18-32-open-device-app-with-the-sensor-permission-drains-1-5-h-extra-even-with-no-sensor-calls)) |
| Zepp OS | The OS | Typed APIs | Permissions in `app.json` with an install dialog ([permission](https://docs.huami.com/docs/guides/framework/device/permission/)). `device:os.bg_service` can also be asked at runtime with `requestPermission` ([API](https://docs.huami.com/docs/reference/device-app-api/newAPI/app/requestPermission/)) |
| [Toit](https://docs.toit.io/tutorials/hardware/bme280) | App code opens `i2c.Bus` with pin numbers | Driver **packages** from pkg.toit.io, installed with `jag pkg install`. A driver takes a generic `serial.Device`, so it works over I2C or SPI | None: the owner is the developer |
| [ECMA-419](https://419.ecma-international.org/) / Moddable | Native **IO classes** (Digital, I2C, SPI, Serial...) | **Sensor Class Pattern**: `sample()`, `configure()`, `close()`, about 23 kinds from temperature and humidity to CO2, PM and proximity ([sensor class](https://embedded.js.org/api/sensor-class/)). **Host provider** `device`: named pins, default buses, IO, sensor and display constructors ([host provider](http://embedded.js.org/api/host-provider/)) | The host decides what to expose |
| [ESPHome](https://esphome.io/components/external_components/) | C++ components compiled into the firmware from YAML. **External components** come from git or a local path | Entities in Home Assistant | Owner rebuilds the firmware |
| [Tasmota](https://tasmota.github.io/docs/I2CDEVICES/) | Compiled-in I2C drivers, each with an index that the user switches with `I2CDriver<n>`. **Berry** drivers registered with `tasmota.add_driver` ([cookbook](https://tasmota.github.io/docs/Berry-Cookbook/)) | Readings in telemetry | Owner config |
| [Zephyr](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor.html) | Drivers bound through devicetree `compatible` strings | `sensor_sample_fetch` + `sensor_channel_get`. Channels have fixed units (ambient temperature in °C) | Build time |
| [RIOT SAUL](https://doc.riot-os.org/drivers_2include_2saul_8h.html) | Drivers register with SAUL | Device classes (actuators 0x40.., sensors 0x80..). Read/write uses `phydat` values with a unit and scale | Build time |
| [Matter](https://handbook.buildwithmatter.com/howitworks/datamodel) | Device firmware | Node → endpoints → clusters (attributes, commands, events). Temperature Measurement is 0x0402 (0.01 °C units), On/Off is 0x0006 ([Nordic](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/protocols/matter/getting_started/adding_clusters.html)) | Commissioning |
| [Home Assistant](https://www.home-assistant.io/integrations/sensor.mqtt/) | Integrations | Entities with `device_class`, `state_class`, `unit_of_measurement` (also through MQTT discovery) | Admin |

**[I] What to take from these:**
1. **Two layers.** Every serious system ends with a vocabulary of typed channels and units: Zephyr channels, SAUL classes, ECMA-419 sensor kinds, Matter clusters, HA device classes. EspOS should define its own vocabulary now and map it to HA and Matter, rather than invent ad-hoc JSON per driver.
2. **Drivers as packages works.** Toit (packages), ESPHome (external components from git) and Tasmota (Berry) all let the community add drivers without forking the core. ESPHome and Tasmota need a firmware rebuild or a scripting VM. Toit and Moddable load drivers at runtime in a VM, which is the model that suits an app store.
3. **ECMA-419 gives JS drivers a ready-made shape**, and its host provider looks like a board profile. The OS should own sampling, as the Garmin battery report shows.

## 2. Plug-and-play on ESP32-S3 hardware

**Pins.**
- With octal PSRAM, GPIO33-37 are taken. GPIO19/20 are the USB pins. GPIO0/3/45/46 are strapping pins ([RIOT S3 notes](https://api.riot-os.org/group__cpu__esp32__esp32s3.html)).
- On the ES3C35P (board notes), the rest are mostly in use:
  - LCD QSPI: 9-14, backlight 41
  - touch and codec I2C: 38/39, touch interrupt and reset 47/48
  - I2S: 15-18 and 21
  - SD: 2-7
  - amplifier 1, LED 40, battery ADC 8
- **[I]** In practice, expansion on this board means four things: the **shared I2C bus**, **BLE**, **UART0** (43/44, if the console moves to USB) and **USB**. An external device that holds SDA low on the shared bus also kills touch (0x55) and audio (0x18). Future boards need a separate I2C port for sensors (the S3 has two I2C controllers ([ESP-IDF I2C](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/i2c.html))) on a Qwiic connector.

**Connectors.**
- Qwiic and STEMMA QT share a 4-pin, 1.0 mm JST SH connector (GND, V+, SDA, SCL), with Qwiic at 3.3 V ([Adafruit](https://learn.adafruit.com/introducing-adafruit-stemma-qt)).
- Grove is 2.0 mm with a different pin order, runs at 3-5 V, and also carries UART, analog and digital signals ([Bus Pirate](https://docs.buspirate.com/docs/overview/qwiic-stemma-breakout-garden-grove-adapter/)).
- **[I]** Put Qwiic on our boards: cables need no soldering, and Grove sensors work through adapters.

**I2C scan and address tables: ambiguous by nature.**
- No standard detection command exists. Probing "can confuse your I2C bus, cause data loss and worse", and some probe methods corrupt certain EEPROMs or lock write-only chips ([i2cdetect](https://manpages.debian.org/i2cdetect)).
- Addresses collide. AHT2x and VEML light sensors both use 0x38, so Tasmota users switch drivers by index ([AHT2x](https://tasmota.github.io/docs/AHT2x/)).
- ESP-IDF's new I2C master driver (IDF 5.4+) has `i2c_master_probe()` for an address-acknowledge check ([Waveshare example](https://docs.waveshare.com/ESP32-ESP-IDF-Tutorials/I2C-Master)).
- **[I]** Detect devices in three steps:
  1. Probe only addresses that some installed or catalog driver claims, and never the addresses of on-board chips.
  2. Let each candidate driver run its `probe()`, which reads a chip-ID or serial register where the chip has one. SHT4x has a serial-number command, BME280 a chip-id register.
  3. If two drivers still claim the device, or the chip has no ID register, ask the user once and remember the answer.

**Buses that identify themselves.**
- **1-Wire:** every device has a 64-bit ROM id with a family code in the low 8 bits, and the search algorithm enumerates every device on one wire ([Analog AN187](https://www.analog.com/en/resources/app-notes/1wire-search-algorithm.html)). Espressif's `onewire_bus` component implements it on RMT or UART, and a separate `ds18b20` component reads the sensor ([onewire_bus](https://components.espressif.com/components/espressif/onewire_bus)).
- **BLE:** standard sensors expose the Environmental Sensing service 0x181A, with temperature at 0x2A6E ([card10 ESS](https://firmware.card10.badge.events.ccc.de/bluetooth/ess.html)). Many cheap sensors instead broadcast **BTHome v2**: an open advertisement format with service UUID 0xFCD2, optional AES-CCM encryption and about 23 bytes of data, which Home Assistant reads ([BTHome parser docs](https://bthomev2.readthedocs.io), [bthome-ble](https://pypi.org/project/bthome-ble/2.2.0)). ESPHome also documents Xiaomi thermometers flashed with BTHome firmware ([ESPHome](https://esphome.io/components/sensor/bthome_mithermometer/)). **[I]** Passive advertisement sensors need no pairing and no GATT connection, which is the cheapest path on RAM.
- **USB host:**
  - ESP-IDF's USB Host Library has class drivers for **CDC-ACM, HID, MSC** (plus UAC and UVC). Hub support comes with the newer esp-usb component, which is a managed component from IDF 6.0 ([esp-usb](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host/usb_host_notes_arch.html)).
  - USB-OTG and USB-Serial/JTAG **share one PHY**, so only one works at a time, and a host stack takes the PHY when it starts ([ESP-IDF USB](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/peripherals/usb_device.html)).
  - **[I]** USB host therefore costs the console and the flashing path. It needs a "USB mode" setting with BOOT-button recovery, plus 5 V VBUS from the board. It comes last.
- **UART, ADC, PWM:** no identity at all. **[I]** The user picks the device type on the Devices page ("add by hand"), and the driver sanity-checks the replies (frame checksums).

**Hot-plug and power [I].**
- USB and BLE give attach and detach events. I2C and 1-Wire need a slow rescan (e.g. every 30 s while the Devices page is open, otherwise only after errors), plus per-device error counters.
- Sensors share the 3.3 V rail with the backlight and the radio peaks. A driver declares its typical and peak current, and the Devices page warns when the total exceeds the board profile's budget.
- On battery boards the OS lowers sampling rates.

## 3. Permission design for hardware

**[I] Three layers.** Apps never touch a bus unless they say so and the user agrees.

| Layer | Who | What it can do | Permission |
|---|---|---|---|
| Bus primitives | Firmware (C++) | I2C, SPI, UART, 1-Wire (RMT), GPIO, ADC, PWM, BLE scan/GATT, USB host | n/a |
| Drivers | `driver` packages (WASM or JS), signed | Talk to the buses they declare, but only to devices the OS has bound to them. Publish typed channels | `hw:i2c`, `hw:uart`, `hw:gpio`, `hw:onewire`, `hw:ble`, `hw:usb:<vid>:<pid>` + review |
| Apps | Ordinary packages | Read channels and history; control actuators | `sensor:<channel>`, `actuator:<channel>`; `hw:*` only as a reviewed exception |

**Consent.**
- **Sensor reads** (`sensor:temperature`, `sensor:co2`, ...) are granted at install. They are low risk, like Fitbit's install-time model.
- **Actuators**, plus any raw `hw:*` access an app asks for, are granted at install **and** confirmed per device the first time: "Tưới cây muốn bật/tắt «Relay 1» — Cho phép?". This follows Android USB's per-device dialog.
- **Attaching a new device** produces one notification ("Đã thấy cảm biến SHT41 — Thêm?"). After the user adds it, the apps that asked for its channels can see it.
- All grants are listed on the Devices page and are revocable.

**Devices page (system settings).**
- **Per device:** name, room, bus and address or id, driver and version, channels with last values, battery, signal, error count, and the apps that use it.
- **Actions:** rename, assign a room, forget, rescan, "add by hand" (UART/GPIO with pin pickers that hide pins the board uses), switch driver when two drivers match, share switches.

**Drivers are trusted, not native.**
- **Bound only:** a driver runs in the driver host and sees only the devices the OS bound to it. An I2C driver cannot address 0x55 (touch) or 0x18 (codec), because the bus primitive refuses unbound addresses.
- **Signed and limited:** drivers are signed by their developer and by the store. Each call has a CPU-time budget and each instance a memory cap.
- **Safe mode:** a driver that faults N times is disabled, and its device shows "cần chú ý".

**Services owned by the OS.**
- **Sampling.** One sampler per channel, at the fastest rate any subscriber asked for, capped by the driver's limits and by battery state.
- **Logging.** A ring buffer in PSRAM plus minute aggregates on flash or SD. This is the NuttX upper-half pattern of ring buffers, multi-user access and downsampling ([NuttX sensors](https://nuttx.apache.org/docs/latest/components/drivers/special/sensors/sensors_uorb.html)).
- **Automations.** Declarative rules: "if channel/condition/time window → notify, set an actuator, call a webhook". They are edited in the web app and run on the device, so they keep working offline. Apps can register new rule actions. Actuators get fail-safe states (relays off after reboot unless a rule says otherwise) and rate limits.
- **Bridges.**
  - A per-device switch syncs readings to the EspOS server (the hosted service or the self-hosted Go server; both pass the same conformance tests).
  - An optional MQTT publisher sends Home Assistant discovery data: `device_class`, `unit_of_measurement`, `state_class` ([HA MQTT sensor](https://www.home-assistant.io/integrations/sensor.mqtt/)).
  - A Matter bridge is a later evaluation. It is heavy, and Matter's cluster units (0.01 °C, etc.) are already mapped in the channel table.
- **Driver OTA.** Drivers update through the store like apps, and the user can pin a version.

## 4. Recommendation for EspOS

### 4.1 Should drivers be JS, WASM or first-party C++?

| | JS (ECMA-419 style) | WASM | First-party C++ |
|---|---|---|---|
| Who writes them | Anyone who writes EspOS apps | C/Rust/Zig authors. **Vendor C drivers port almost as-is**: Bosch BME280 API, Sensirion SHT4x and SCD4x drivers are BSD-3-Clause C ([Bosch](https://github.com/boschsensortec/BME280_SensorAPI), [SHT4x](https://github.com/Sensirion/embedded-i2c-sht4x), [SCD4x](https://github.com/Sensirion/embedded-i2c-scd4x)). Sensirion keeps the platform part in a small `sensirion_i2c_hal.c` | Us only |
| Isolation | Interpreter-safe | Bounds-checked linear memory, imports only | None |
| Memory | A JS context per driver **[I: measure]** | Linear memory can be truncated to `__heap_base` when it never grows ([WAMR memory](https://github.com/bytecodealliance/wasm-micro-runtime/blob/main/doc/memory_tune.md)), so a few KB per small driver **[I]** | Smallest |
| Flash | Package (card or flash area) | Package. The runtime is already paid for, because WASM apps exist from day one | **Firmware slot, which is ~98% full** |
| Speed | Enough for 1 Hz sensors **[I]** | Faster than JS, and AOT is possible later | Native |
| Updates | Store | Store | Firmware OTA only |
| Good for | BLE advert parsers (BTHome, vendor formats), simple register sensors, community drivers | Ported vendor drivers, maths-heavy drivers (gas indices, filters), our first-party drivers | Bus primitives, timing-critical protocols (1-Wire on RMT, WS2812, DHT), on-board chips (touch, codec, battery, display) |

**[I] Decision:**
- **Define one driver interface and accept two package forms, WASM and JS.**
- **Write our own first-party drivers in WASM.** That keeps them out of the full firmware slot, lets them update through the store, and lets us reuse vendor C code.
- **Keep C++ for the primitives and the board's own hardware.**
- **JS drivers** are the easy on-ramp for the community and the natural fit for BLE parsers. Their API follows ECMA-419 names (`device.io.I2C`, `sample()`, `configure()`, `close()`), so Moddable documentation and examples carry over.

### 4.2 The driver interface

Manifest **[I]**:

```json
{"id": "dev.espos.drv.sht4x", "type": "driver", "runtime": "wasm", "entry": "sht4x.wasm",
 "version": "1.0.0", "code": 10000, "api": {"min": 1}, "perms": ["hw:i2c"],
 "match": [{"bus": "i2c", "addr": ["0x44", "0x45", "0x46"], "probe": true}],
 "provides": [{"ch": "temperature", "unit": "C"}, {"ch": "humidity", "unit": "%RH"}],
 "power": {"typ_ma": 0.4, "peak_ma": 1}, "rate": {"min_ms": 1000, "default_ms": 60000}}
```

**WASM imports (host → driver), [I]:**
- `bus_write(h, ptr, len)`, `bus_read(h, ptr, len)`, `bus_write_read(h, wptr, wlen, rptr, rlen)`
- `gpio_get/set(h)`, `delay_us(n)`, `now_ms()`, `log(ptr, len)`
- `emit(ch_index, value_milli, quality)`

`h` is a handle to the device the OS bound to the driver; the driver never sees a raw bus.

**WASM exports (driver → host):** `probe() -> i32`, `init() -> i32`, `sample() -> i32`, `configure(ptr, len) -> i32`, `close()`.

A Sensirion port only has to implement `sensirion_i2c_hal_*` on top of these imports. JS drivers export an ECMA-419-shaped class instead:

```js
export default class SHT4x {
  constructor(opt) { this.io = new device.io.I2C(opt.sensor); }   // opt.sensor = bound device
  static probe(io) { /* read serial number, check CRC */ }
  sample() { /* measure, return {temperature, humidity} */ }
  close() { this.io.close(); }
}
```

### 4.3 Channels and the app API [I]

**Fixed units, mapped one-to-one to Home Assistant device classes (and to Matter where a cluster exists):**
- **Sensors:** temperature °C; humidity %RH; pressure hPa; co2 ppm; voc_index; pm1/pm25/pm10 µg/m³; light lx; motion, contact, presence, leak (bool); power W; energy kWh; voltage V; current A; battery %; button (event: press, double, long).
- **Actuators:** switch (on/off), light (on/off, 0-100 %, colour), valve and fan later.

**App API:**
- `sensors.find(ch)` → `{id, name, room, unit}`
- `sensors.on(id, fn, {every})` delivers samples `{value, unit, ts, quality}`
- `sensors.history(id, {since, step})` returns logged data
- `devices.find("switch")[i].set(true)` controls an actuator, with the per-device prompt the first time

Widgets and faces bind channels declaratively, with no code (`"bind": "sensor:temperature@living"`).

### 4.4 First drivers to ship [I]

| Bus | Driver | Form | Why |
|---|---|---|---|
| BLE | **BTHome v2** receiver (many channels) | JS | Open format, HA-compatible, no pairing; covers DIY and reflashed Xiaomi sensors |
| BLE | Generic **ESS 0x181A** (GATT) | JS | Standard service |
| I2C/Qwiic | **SHT4x** (temperature, humidity) | WASM (Sensirion C) | Common and cheap; has a serial number for probing |
| I2C/Qwiic | **BME280** (temperature, humidity, pressure) | WASM (Bosch C) | Very common; has a chip-id register |
| I2C/Qwiic | **SCD4x** (true CO2) | WASM (Sensirion C) | The flagship "desk air quality" use |
| I2C/Qwiic | Light sensor (BH1750 or VEML7700) | JS | Tiny driver; serves as the JS example |
| GPIO | Button, relay, PIR motion (generic, added by hand) | C++ primitive + JS config | Basic automations |
| 1-Wire | DS18B20 | WASM on the `onewire_bus` primitive | Waterproof probes, ROM ids |
| On-board | Battery level, codec microphone level | C++ | Already in the firmware |
| UART (later) | PM2.5 (PMS-series), NDIR CO2 | WASM/JS | Air quality, added by hand |
| USB (last) | CDC-ACM serial bridge, HID buttons/keypads | C++ class drivers + JS mapping | After the USB-mode work |

### 4.5 Simulator and QEMU: fake devices with scripted values [I]

- **A fake bus layer, not electrical emulation.** In the SDL simulator and in our QEMU board variant (next to `lcd_qemu.cpp`), the bus primitives are replaced by `bus_fake`. It serves:
  - I2C transactions from **register-map models** (a small JSON or JS model per chip: registers, the ID value, how a measurement command fills result registers from the current scripted value);
  - BLE advertisements from BTHome or ESS encoders;
  - 1-Wire ROM ids;
  - GPIO levels.

  Drivers run unchanged against it, so their `probe()` and `match` logic is tested too.
- **`sim/devices.json`** lists devices: bus, address or id, model, and a value script. A script is a constant, sine (period, min, max), random walk, step schedule ("22 °C until 08:00, then 26"), or CSV replay. A CSV exported from a real device's history can be replayed in the simulator.
- **Time control.** Sim time can run faster (e.g. 60×), so history, aggregates and rules can be tested in seconds.
- **Dev console and CLI:**
  - `dev sensor add sht4x@0x44`, `dev sensor set living.temp 31.5`, `dev sensor unplug ...`, `dev ble adv bthome temp=21.3 hum=40`
  - `espos sim --devices devices.json`
  - in CI: run a scenario headless and assert notifications and actuator states
- **Driver authoring.** `espos driver test` runs a driver against its chip model and recorded I2C traces.

### 4.6 Memory and flash cost [I unless linked]

- **The runtime is already paid for.** WAMR is ≈59 KB (fast interpreter) or ≈29 KB (AOT runtime) of code on Cortex-M4F ([WAMR](https://github.com/bytecodealliance/wasm-micro-runtime)). Xtensa size is still to be measured, and it ships anyway for WASM apps. QuickJS is already in the firmware.
- **New firmware code:**
  - The bus primitives layer with binding and access checks.
  - The `onewire_bus` component.
  - The sensor service: sampler, history, rules, Devices page and MQTT client. Estimate 60-120 KB of flash in total, to be measured.
  - A BLE stack for scanning, if it is not linked already. This is the largest unknown, so measure it before committing.
- **Flash budget.** The firmware slot is at 3.68 of 3.75 MB, so this work depends on moving built-in apps into packages or enlarging the OTA slots first. Drivers live in the package area, not the firmware slot.
- **RAM:**
  - Each WASM driver: linear memory truncated to its data plus a small stack, plus a WAMR module instance. Target ≤16 KB per small driver.
  - JS drivers share one QuickJS runtime, one context per driver; target ≤256 KB PSRAM for all of them.
  - History: one channel at 1 sample/min for 24 h ≈ 1,440 × 8 B ≈ 11 KB of PSRAM. Ten channels ≈ 110 KB.
  - The framework's share of internal SRAM should stay at a few tens of KB. BLE scanning and WiFi share the radio and internal RAM, so measure their coexistence with TLS (only one TLS session fits today) before scanning continuously; scan in windows otherwise.

### 4.7 Where it fits the owner's priority

**[I]** The order is Java games → OS foundation + SDK → sensors.
1. **During the foundation work** (manifest v2, API level 1, JS + WASM runtimes, signing), reserve these names: the `driver` type, `sensor:`, `actuator:` and `hw:*` permissions, the channel vocabulary, and the WASM driver imports. Nothing ships yet.
2. **Sensors v1:** the bus primitives, the Devices page, sampling and history, BTHome/ESS over BLE, SHT4x/BME280/SCD4x on Qwiic, and fake devices in the simulator.
3. **Sensors v2:** rules, GPIO actuators with consent, the MQTT/HA bridge, 1-Wire, and community JS drivers through review.
4. **Sensors v3:** UART devices, USB host, a display-less "sensor node" build profile, and a look at a Matter bridge.

## 5. Risks [I]

| Risk | Mitigation |
|---|---|
| Shared I2C bus with touch/codec | Bind-only addressing, stuck-bus detection and recovery (clock pulses), a separate Qwiic port on the next board |
| I2C probe side effects and address clashes | Probe only claimed addresses, chip-ID checks, ask the user once |
| BLE + WiFi + TLS RAM | Measure; passive scan windows; BLE optional per board profile |
| Actuators switching real loads | Per-device consent, fail-safe states, rule rate limits, mains warning |
| WASM toolchain friction for hobbyists | JS drivers as the easy path; templates and `espos driver test` |
| USB host takes the console/flash port | USB-mode setting, BOOT recovery, ship last |
| Flash slot full | Packages-first migration and repartitioning before the sensor service |
