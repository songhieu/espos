#pragma once
// Product presets on the device (docs/app-slots.md §17): the starting point of one product, written at the factory
// (tools/make_bundle.py --preset) as /sys/preset.json in LittleFS, or on the unit's memory card as /espos/preset.json
// (copied in at the first boot). The file is the compiled form (`espos preset compile --board`, schema "device" of
// sdk/schemas/preset-v1.schema.json): the packages, the settings (slots, saver pages, orientation, a fixed locale
// already turned into keys), the first-boot questions left to ask and the setup steps to skip.
//
// Applying is a three-way merge per key (preset_merge.h): a key changes only where the device still holds the
// preset's last value, or never had one; packages are added (wanted in s3.pkgs, installed from the store when the
// device is online), never removed. The device keeps what it applied in /sys/preset-state.json ({"id","v","base",
// "pkgs"}). A newer version comes from Kho máy over the sync ("preset" in the answer) or over USB (PRESET <json>).
// A unit without a preset behaves as before. Entitlements are the server's (granted at the claim), never here.
// UI loop only.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace preset {

void begin();           // after desk::begin, before Home is built: /sys/preset.json applied if new
void tick();            // UI loop: without one, the card's /espos/preset.json once the card has been looked at
void afterProvision();  // desk::provision (Kho máy's USB line): the preset's values again over the server's defaults
bool apply(const String& json, const char* from, String& why);  // a preset file (compiled form) from anywhere
void fromSync(JsonObjectConst p);  // the sync answer's "preset": a newer version for this unit's batch
void report(JsonObject body);      // the sync body's "preset": {"id", "v"} (nothing without a preset)

const String& id();  // "" without a preset
int version();
bool skips(const char* step);  // the first-boot setup leaves this step out ("dog", "name", "call", "city", "pair", "tips")
bool asks(const char* what);   // "lang", "timezone", "city", "orientation": not fixed by the preset (true without one)

// USB and the simulator: PRESET (the state as ESPOS:PRESET {json}), PRESET {json} (apply it), PRESET FORGET (QA: the
// state goes, the file stays: the next start applies it as new).
bool console(const char* cmd);

}  // namespace preset
