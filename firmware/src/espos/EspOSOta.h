#pragma once

#include <cstddef>
#include <functional>
#include <string>

// Firmware updates over WiFi from the EspOS server (docs/espos-api.md, "Firmware").
// The sync stores the server's offer in ESPOS.fwOffer; install() streams that image into
// the spare OTA slot. The bootloader keeps a new image on probation: confirmRunning() must
// run once it works, or the next restart goes back to the previous firmware.
namespace espos::ota {

// "espos-x4-1.2.3" or "1.2.3" -> 10203; 0 when unreadable.
int versionCode(const std::string& version);
// "espos-x4-0.2.0" -> "0.2.0".
std::string shortVersion(const std::string& version);
// The last sync offered a firmware newer than the one running.
bool offerAvailable();

enum class Result { Ok, NoOffer, Network, WrongImage, Hash, Flash };
// Downloads the offer into the spare slot, checks its SHA-256 and selects it for the next
// boot. On Ok the caller restarts. Failures are queued for the next sync's report.
// `connect` joins WiFi: called after the slot is erased (the erase holds the flash for
// ~18 s, so WiFi stays off meanwhile) and again whenever the connection was lost.
Result install(const std::function<void(size_t done, size_t total)>& progress, const std::function<bool()>& connect);

// After ESPOS is loaded at boot: reports how the last install went.
void checkAfterBoot();
// The running firmware works: keep it. Cheap after the first call.
void confirmRunning();

}  // namespace espos::ota
