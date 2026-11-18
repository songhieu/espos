#pragma once
// The JavaScript engine (QuickJS-ng) for EspOS apps: one runtime, heap in PSRAM.
#include <Arduino.h>

namespace jsrt {
// Evaluates `code` in a scratch context; the result (or the error) as text. Dev console.
String eval(const char* code);
}  // namespace jsrt
