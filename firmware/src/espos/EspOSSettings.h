#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "EspOSStore.h"

// Two-way settings between EspOS and the device.
//
// A few contract keys map onto CrossPointSettings fields (language, sleep
// timeout, reader font size, line spacing). The rest (dash.*, tasks.cols) live
// only in EspOSStore::settings. Device-side changes are found by diffing the
// mapped fields against the snapshot taken when the server set was last
// applied (syncedMapped), plus EspOSStore::dirtyKeys for the EspOS-only keys.
namespace espos::settings {

// Applies the server's merged set: stores it, pushes mapped keys into
// SETTINGS (saving when anything changed), and refreshes the snapshot.
// Returns how many mapped device settings changed.
// `webKeys`: settings last changed on the web; on a device that has never synced a key, those
// follow the web and the others keep the device's value.
int applyServerSet(std::vector<KeyValue>&& merged, const std::vector<std::string>& webKeys = {});

// Appends device-side changes as {"k","v","age_s"} objects.
void collectDeviceChanges(JsonArray out);

// Number of device-side changes not yet sent (mapped diffs + dirty keys).
int pendingChangeCount();

// Current device value of a mapped key, or "" when the key is not mapped.
std::string mappedValue(const char* key);

// Settings travel as strings; tolerate numbers/booleans from the server.
std::string valueToString(JsonVariantConst value);

// Reads a {"key": value} object into key/value pairs.
std::vector<KeyValue> readObject(JsonObjectConst object);

}  // namespace espos::settings
