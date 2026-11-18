#include "trace.h"

#include <Arduino.h>

#include <cstdarg>

namespace trace {
namespace {
constexpr size_t SIZE = 64 * 1024;  // about a minute of continuous touching
char* ring = nullptr;
size_t head = 0;      // next write position
bool wrapped = false, echo = false;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

void begin() { ring = (char*)ps_malloc(SIZE); }

void add(const char* fmt, ...) {
  char line[128];
  int n = snprintf(line, sizeof line, "%lu ", millis());
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
  va_end(ap);
  n = min(n, (int)sizeof line - 2);
  line[n++] = '\n';
  line[n] = 0;
  if (echo) Serial.print(line);
  if (!ring) return;
  portENTER_CRITICAL(&lock);
  for (int i = 0; i < n; i++) {
    ring[head++] = line[i];
    if (head == SIZE) head = 0, wrapped = true;
  }
  portEXIT_CRITICAL(&lock);
}

void dump() {
  if (!ring) return;
  Serial.println("TRACE begin");
  size_t i = wrapped ? head : 0, left = wrapped ? SIZE : head;
  if (wrapped) {  // skip the partial line the ring cut into
    while (left && ring[i] != '\n') i = (i + 1) % SIZE, left--;
    if (left) i = (i + 1) % SIZE, left--;
  }
  while (left) {  // at most two contiguous pieces
    const size_t n = min(left, SIZE - i);
    Serial.write((const uint8_t*)ring + i, n);
    i = (i + n) % SIZE, left -= n;
  }
  Serial.println("TRACE end");
}

void setEcho(bool on) { echo = on; }

}  // namespace trace
