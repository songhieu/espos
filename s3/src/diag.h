#pragma once
// Small health numbers (heap, frame rate, slow loops, reset reason) sent with each sync, so a
// board updated over WiFi can be checked without a cable.
#include <ArduinoJson.h>

namespace diag {
void set(const char* key, int32_t v);
void inc(const char* key);
void maxOf(const char* key, int32_t v);
void fill(JsonObject o);
}  // namespace diag
