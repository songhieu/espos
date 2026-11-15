#include "InkNet.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

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

bool joinSaved(const uint32_t perNetworkMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WIFI_STORE.loadFromFile();
  std::vector<WifiCredential> creds;
  creds.reserve(4);
  const auto last = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (last) creds.push_back(*last);
  for (size_t i = 0; i < WIFI_STORE.getCredentialCount() && creds.size() < 4; i++) {
    const auto c = WIFI_STORE.getCredentialAt(i);
    if (c && (!last || c->ssid != last->ssid)) creds.push_back(*c);
  }
  if (creds.empty()) return false;
  WiFi.mode(WIFI_STA);
  for (const auto& c : creds) {
    WiFi.begin(c.ssid.c_str(), c.password.c_str());
    const unsigned long deadline = millis() + perNetworkMs;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(100);
    const int status = static_cast<int>(WiFi.status());
    if (status == WL_CONNECTED) {
      LOG_INF("INKD", "Joined saved WiFi %s", c.ssid.c_str());
      WIFI_STORE.setLastConnectedSsid(c.ssid);
      INKDESK.noteWifiResult(c.ssid, true, status);
      return true;
    }
    LOG_DBG("INKD", "Saved WiFi %s did not answer (status %d)", c.ssid.c_str(), status);
    INKDESK.noteWifiResult(c.ssid, false, status);
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
