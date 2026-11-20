// sscanf through newlib's integer-only scanner (0.5.7: 8.7 KB of the app slot). Nothing in the firmware reads a
// floating-point number with sscanf (only %d %u %lu %c %s: os, desk, data, pkg, store, remote, devtools, pet/game,
// wake), and newlib's tzset links that scanner anyway (siscanf). A %f here would read nothing: use strtof for that.
#ifndef SIM
#include <stdarg.h>
#include <stdio.h>

int sscanf(const char* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = vsiscanf(s, fmt, ap);
  va_end(ap);
  return n;
}
#endif
