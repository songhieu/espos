#include "InkNet.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

#include "WifiCredentialStore.h"
#include "inkdesk/InkDeskStore.h"
#include <esp_attr.h>
#include <esp_system.h>

namespace inkdesk::net {

namespace {
// One automatic sync per wake. RTC_NOINIT survives the silent restart that follows a sync,
// so Home does not sync again after it (a loop when the clock or the server stays bad).
// Cleared before deep sleep, and on any boot that is not a software restart.
RTC_NOINIT_ATTR uint32_t s_autoSyncMagic;
constexpr uint32_t AUTO_SYNC_MAGIC = 0xA5C0FFEE;

void checkOnBoot() {
  static bool checked = false;
  if (checked) return;
  checked = true;
  if (esp_reset_reason() != ESP_RST_SW) s_autoSyncMagic = 0;
}
}  // namespace

bool autoSyncTried() {
  checkOnBoot();
  return s_autoSyncMagic == AUTO_SYNC_MAGIC;
}
void markAutoSyncTried() {
  checkOnBoot();
  s_autoSyncMagic = AUTO_SYNC_MAGIC;
}
void resetAutoSyncForSleep() { s_autoSyncMagic = 0; }

namespace {
// Active scan of every channel: about 13 x 120 ms. Joining with the channel and access point
// found here skips the scan WiFi.begin() would make anyway.
constexpr uint32_t SCAN_MS_PER_CHANNEL = 120;
// Saved networks the scan did not see (a hidden network, or an iPhone hotspot that only shows
// while its settings page is open) are still tried, but fewer when the scan worked.
constexpr size_t MAX_UNSEEN_AFTER_SCAN = 2;
constexpr size_t MAX_TRIES_WITHOUT_SCAN = 4;

struct Attempt {
  WifiCredential cred;
  int rssi = -127;
  int32_t channel = 0;  // 0: not seen, let WiFi.begin() search
  uint8_t bssid[6] = {};
};
}  // namespace

bool joinSaved(const uint32_t perNetworkMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WIFI_STORE.loadFromFile();
  const size_t count = WIFI_STORE.getCredentialCount();
  if (count == 0) return false;
  WiFi.mode(WIFI_STA);

  // Like a phone: the saved networks in range, strongest first.
  std::vector<Attempt> seen;
  seen.reserve(count);
  const unsigned long scanStart = millis();
  const int found = WiFi.scanNetworks(false, false, false, SCAN_MS_PER_CHANNEL);
  for (int i = 0; i < found; i++) {
    const String ssid = WiFi.SSID(i);
    const int rssi = WiFi.RSSI(i);
    const auto cred = WIFI_STORE.findCredential(ssid.c_str());
    if (!cred) continue;
    auto it = std::find_if(seen.begin(), seen.end(), [&](const Attempt& a) { return a.cred.ssid == cred->ssid; });
    if (it == seen.end()) {
      seen.push_back({*cred});
      it = seen.end() - 1;
    } else if (rssi <= it->rssi) {
      continue;  // a weaker access point of the same network
    }
    it->rssi = rssi;
    it->channel = WiFi.channel(i);
    if (const uint8_t* b = WiFi.BSSID(i)) memcpy(it->bssid, b, sizeof(it->bssid));
  }
  WiFi.scanDelete();
  std::sort(seen.begin(), seen.end(), [](const Attempt& a, const Attempt& b) { return a.rssi > b.rssi; });
  LOG_INF("INKD", "WiFi scan: %d networks, %u saved in range (%lu ms)", found, static_cast<unsigned>(seen.size()),
          millis() - scanStart);

  // Then the saved networks it did not see: the last one joined first, then as saved.
  std::vector<Attempt> plan = seen;
  const size_t maxUnseen = found >= 0 ? MAX_UNSEEN_AFTER_SCAN : MAX_TRIES_WITHOUT_SCAN;
  size_t unseen = 0;
  const auto addUnseen = [&](const std::optional<WifiCredential>& c) {
    if (!c || unseen >= maxUnseen) return;
    for (const auto& a : plan) {
      if (a.cred.ssid == c->ssid) return;
    }
    plan.push_back({*c});
    unseen++;
  };
  addUnseen(WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid()));
  for (size_t i = 0; i < count; i++) addUnseen(WIFI_STORE.getCredentialAt(i));

  for (const auto& a : plan) {
    if (a.channel > 0) {
      WiFi.begin(a.cred.ssid.c_str(), a.cred.password.c_str(), a.channel, a.bssid);
    } else {
      WiFi.begin(a.cred.ssid.c_str(), a.cred.password.c_str());
    }
    const unsigned long deadline = millis() + perNetworkMs;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(100);
    const int status = static_cast<int>(WiFi.status());
    if (status == WL_CONNECTED) {
      LOG_INF("INKD", "Joined saved WiFi %s (%d dBm)", a.cred.ssid.c_str(), a.rssi);
      WIFI_STORE.setLastConnectedSsid(a.cred.ssid);
      INKDESK.noteWifiResult(a.cred.ssid, true, status);
      return true;
    }
    LOG_DBG("INKD", "Saved WiFi %s did not answer (status %d)", a.cred.ssid.c_str(), status);
    INKDESK.noteWifiResult(a.cred.ssid, false, status);
    WiFi.disconnect();
  }
  INKDESK.saveToFile();
  return false;
}

void off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace inkdesk::net
