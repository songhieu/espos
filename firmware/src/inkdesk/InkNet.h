#pragma once

#include <cstdint>

namespace inkdesk::net {
// Joins a saved WiFi network without scanning first, so hidden or idle networks
// (an iPhone hotspot only advertises while its settings page is open) still work.
// Tries the last connected network, then the others (at most 4). True when connected.
bool joinSaved(uint32_t perNetworkMs = 9000);
void off();

// Quick sync when the reader is opened: tried once per wake (RTC memory, cleared before deep sleep).
bool autoSyncTried();
void markAutoSyncTried();
void resetAutoSyncForSleep();
}  // namespace inkdesk::net
