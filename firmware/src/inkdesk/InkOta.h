#pragma once

#include <cstddef>
#include <functional>
#include <string>

// Firmware updates over WiFi from the InkDesk server (docs/inkdesk-api.md, "Firmware").
// The sync stores the server's offer in INKDESK.fwOffer; install() streams that image into
// the spare OTA slot. The bootloader keeps a new image on probation: confirmRunning() must
// run once it works, or the next restart goes back to the previous firmware.
namespace inkdesk::ota {

// "inkdesk-1.2.3" or "1.2.3" -> 10203; 0 when unreadable.
int versionCode(const std::string& version);
// "inkdesk-0.2.0" -> "0.2.0".
std::string shortVersion(const std::string& version);
// The last sync offered a firmware newer than the one running.
bool offerAvailable();

enum class Result { Ok, NoOffer, Network, WrongImage, Hash, Flash };
// Downloads the offer into the spare slot, checks its SHA-256 and selects it for the next
// boot. On Ok the caller restarts. Failures are queued for the next sync's report.
Result install(const std::function<void(size_t done, size_t total)>& progress);

// After INKDESK is loaded at boot: reports how the last install went.
void checkAfterBoot();
// The running firmware works: keep it. Cheap after the first call.
void confirmRunning();

}  // namespace inkdesk::ota
