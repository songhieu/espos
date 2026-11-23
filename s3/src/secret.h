#pragma once
// Package secrets kept on the device (manifest-driven.md §1.4, app-platform-plan.md lane 1e): the values of a manifest's
// `secret` settings fields with `where: "device"` (typed on the device), and `where: "server"` ones the server delivered
// because they have `send_to`. In NVS (namespace "sec"), never synced up, never shown in full.
//
// OS only. A JavaScript app never reads a value: it sees {set: true|false} (appset.h); the OS adds the value to the
// app's requests whose URL its `send_to` names (src/js/jsnet.cpp, lane 1f). Removed when the package is uninstalled.
// Limits, said plainly: the S3 has no flash encryption, so whoever holds the board can read its flash.
#include <Arduino.h>

#include <vector>

namespace secret {

// value "" clears. `tag`: what the value is bound to (appset.cpp: the url its send_to expanded to when it was typed);
// false: bad arguments (app/key with spaces, value over 2000 bytes) or NVS refused.
bool set(const char* app, const char* key, const String& value, const char* tag = "");
bool get(const char* app, const char* key, String& out);  // OS only (net); never to JS, never traced
bool has(const char* app, const char* key);
String hint(const char* app, const char* key);  // the last 4 characters, "" when unset
String tag(const char* app, const char* key);   // as given to set()
void clear(const char* app, const char* key);
void clearApp(const char* app);  // the package was uninstalled
struct Entry {
  String app, key;
};
std::vector<Entry> list();  // every secret kept (no values): the sync's report, the re-pointing check
uint32_t version();          // bumps on every change

}  // namespace secret
