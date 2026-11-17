#pragma once
// Host stub: what FontDecompressor.cpp uses from Arduino.
#include <chrono>
#include <cstdint>

inline uint32_t millis() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline uint32_t micros() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}
