#pragma once
#include <Arduino.h>
// Firmware updates over WiFi from the release the server offers (desk::fwOffer()).
// start() saves the offer and restarts; the next boot is install mode (only the install screen, so the
// download has the internal RAM it needs) and then boots the new version (see ota.cpp).
namespace ota {
void begin();       // after boot: confirm a fresh image, report a rollback, or enter install mode
bool installing();  // this boot is install mode (setup() then builds only the install screen)
void beginInstallScreen();
void poll();        // UI loop: confirms the image after its first sync, runs install mode, starts asked installs
void start();       // install the offered version now (restarts into install mode)
bool running();
const char* status();  // progress or the last error, Vietnamese; "" when idle

enum class Stage : uint8_t { Idle, Download, Verify, Write, Restart, Failed };
struct Progress {
  Stage stage;
  uint32_t done, total, bps;
  int streams;
};
Progress progress();
const String& lastError();         // why the last install failed ("" when none), kept across restarts
const String& lastErrorVersion();  // the version it tried
void clearError();
// The partition table (docs/partitions.md): the app slot's size (the largest image an OTA can take) and the
// table's version (1: pb_* packs, 3.75 MiB apps; 2: one "apps" area, 4.75 MiB apps). Sent in every sync (diag).
uint32_t slotBytes();
int table();
#if ESPOS_QA
bool installFile(const char* path);  // QA: an image file (e.g. on the card) written as an OTA would, then restart
#endif
}  // namespace ota
