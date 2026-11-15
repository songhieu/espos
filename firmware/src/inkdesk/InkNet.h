#pragma once

#include <cstdint>

namespace inkdesk::net {
// Joins a saved WiFi network the way a phone does: a quick scan first, then the saved
// networks in range, strongest first. Saved networks the scan missed (hidden ones, or an
// iPhone hotspot that only advertises while its settings page is open) are tried after
// them, the last joined first. True when connected.
bool joinSaved(uint32_t perNetworkMs = 9000);
void off();

// Quick sync when the reader is opened: tried once per wake (RTC memory, cleared before deep sleep).
bool autoSyncTried();
void markAutoSyncTried();
void resetAutoSyncForSleep();
}  // namespace inkdesk::net
