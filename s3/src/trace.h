#pragma once
// Always-on event history in PSRAM (touch reports, scrolls, saver, screens, slow frames), so a
// problem the user hits by hand can be read back afterwards with the TRACE console command.
namespace trace {
void begin();
void add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void dump();          // print the whole history, oldest first
void setEcho(bool);   // also print each entry live
}  // namespace trace
