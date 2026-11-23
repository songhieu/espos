#pragma once
// Files and crypto for JavaScript apps (app-slots.md §15.4, API level 2): fs.read / write / remove / list / free on
// the app's own folders, crypto.sha256 / crypto.random. Two folders:
// - `data/`: kept; on the internal LittleFS; quota from the manifest's `storage:<KB>` (64 KB when not given).
// - `cache/`: the OS may delete old files; on the memory card when one is in, else LittleFS (at most 1 MB); quota from
//   `cache:<KB>` (none without it). net.request's `save` writes here, ui.image reads from here.
// Names: "data/<path>" or "cache/<path>" (a bare name is data/), parts of a-z A-Z 0-9 . _ - (no ".."), one sub-folder
// at most, 80 characters. Writes are atomic (a temporary file, then a rename). Everything goes with the package.
#include <Arduino.h>

extern "C" {
#include "quickjs.h"
}

namespace pkg {
struct Package;
}

namespace jsfs {
void install(JSContext* ctx, const pkg::Package& p);  // the fs / crypto natives and the app's quotas
void stop();
// For the OS's own pieces (jsnet's save, ui.image): the open app's file. false: bad name, no room, I/O error.
bool writeBytes(const char* name, const uint8_t* data, size_t len);
String path(const char* name);  // its real path ("" for a bad name or when no app is open)
void wipe(const char* id);      // the package was uninstalled (pkg store)
}  // namespace jsfs
