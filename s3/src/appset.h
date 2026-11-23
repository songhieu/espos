#pragma once
// A package's settings on the device (docs/manifest-driven.md §1, app-platform-plan.md lane 1e): the manifest's
// `settings` list drawn as a form from the OS's own rows (Settings › Apps › the app › Settings), the values kept as
// the device settings "app.<id>.<key>" (desk.h: synced both ways, newest wins, like the s3.* keys), checked by the same
// rules as the SDK and the server (appset_rules.h), and the secret fields kept by the OS (secret.h).
//
// A value is text: a bool "1"/"0", a number "2.5", a list or a city JSON. "" is "not set": the default applies.
// Values of other apps are never readable by an app; JS gets its own through settings.get (src/js/jsset.cpp).
// UI loop only.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "appset_rules.h"

namespace appset {

bool hasForm(const char* id);  // its manifest has settings fields (not only sections)

// The value of `key`: the stored one when it passes the field's rules, else the default ("" when none).
String get(const char* id, const char* key);
// Stores a value typed on the device after the checks (installed, a key of its schema, the rules, 8 KB per package);
// false with a translated reason in `why`. It goes up with the next sync.
bool set(const char* id, const char* key, const String& raw, String& why);
// settings.get(key) for JS as JSON text: a string, number, bool, list, city object; {"set": bool} for a secret; null.
String jsValue(const char* id, const char* key);
// The keys of its schema (no sections), for the JS watcher.
std::vector<String> keys(const char* id);
// Its fields (no sections) as the language shows them: the LAN page (lane 1f, lanform.h) builds its form from them.
struct Field {
  String key, type, label, where;  // where: "device" / "server" for a secret, else ""
};
std::vector<Field> fields(const char* id);
// A secret field with where "device": typed on the device (or by the LAN form, lane 1f); "" clears.
bool setSecret(const char* id, const char* key, const String& value, String& why);
// The package was uninstalled: its app.<id>.* values (not sent up: the server drops them itself) and secrets go.
void forget(const char* id);

// Screens: Settings › Apps, one app's page, its form.
void openApps();
void openApp(const char* id);
void openForm(const char* id);

// The sync (desk.cpp): the body's report of the secrets kept and the acks of delivered ones; the answer's delivered
// secrets ("secrets"), and the re-pointing check after the answer's settings were applied.
void syncBody(JsonObject body);
void fromSync(JsonObjectConst answer);

}  // namespace appset
