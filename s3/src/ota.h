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
}  // namespace ota
