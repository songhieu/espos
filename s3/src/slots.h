#pragma once
// The default apps (docs/app-slots.md §7; mockup mockups/app-slots/default-apps.html): Settings › Default apps, and the
// screen saver's slot. The assistant's slot is assistant.h.
//
// Setting `slot.saver` (synced with the web): "builtin" (the saver pages, s3.saver_pages: "Clock and photos"), or a
// saver app's package id; unset = builtin. The saver apps this firmware runs: the aquarium (engine "tank"): chosen, it
// draws the whole saver (its tank, the time and the date) in place of the saver pages, which stay as they were for
// when "Clock and photos" is chosen again. Removing the chosen app hands the place back to the built-in one with one
// notification. The saver's timing (after how long, night hours) stays in Settings › Screen saver: the app only draws.
// UI loop only.
#include <Arduino.h>

namespace slots {

constexpr const char* SAVER_KEY = "slot.saver";

String saverId();       // the saver slot's app, or "builtin"
bool saverBuiltin();
String saverName();     // "Clock and photos" (translated) or the app's name
// The pages an app saver draws (JSON as s3.saver_pages), nullptr for the built-in saver (desksaver.cpp).
const char* saverDoc();
bool saverApp(const char* pkgId);  // an installed app the saver slot can take

void openDefaultApps();  // Settings › Default apps
String summary();        // the Settings row's "Aquarium · Bao"

// The store's job (store.cpp afterJob): a package went: if it was the saver, the built-in one is back.
void removed(const char* pkgId, const String& name);

}  // namespace slots
