#include "EspOSSerial.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "EspOSSettings.h"
#include "EspOSStore.h"
#include "EspOSSync.h"
#include "WifiCredentialStore.h"

namespace espos::serial {
namespace {

ProvisionSummary g_summary;

// One write per reply so a log line from another task cannot split it.
void reply(const char* line) {
  std::string out = PREFIX;
  out += line;
  out += "\n";
  logSerial.write(reinterpret_cast<const uint8_t*>(out.data()), out.size());
  logSerial.flush();
}

bool isHexToken(const char* token) {
  if (!token || strlen(token) != 64) return false;
  for (const char* p = token; *p; ++p) {
    const char c = *p;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool provision(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    reply("ERR bad_json");
    return false;
  }
  const char* token = doc["token"];
  if (!isHexToken(token)) {
    reply("ERR bad_token");
    return false;
  }

  const char* api = doc["api"] | DEFAULT_API;
  if (strncmp(api, "https://", 8) != 0) {
    reply("ERR bad_api");
    return false;
  }

  // WiFi first: the password stays on the device (WifiCredentialStore).
  g_summary = {};
  WIFI_STORE.loadFromFile();
  for (JsonObjectConst w : doc["wifi"].as<JsonArrayConst>()) {
    const char* ssid = w["ssid"] | "";
    const char* pass = w["pass"] | "";
    if (!*ssid) continue;
    if (WIFI_STORE.addCredential(ssid, pass)) {
      if (g_summary.wifiSsid.empty()) {
        g_summary.wifiSsid = ssid;
        WIFI_STORE.setLastConnectedSsid(ssid);
      }
    }
  }
  if (!g_summary.wifiSsid.empty()) WIFI_STORE.saveToFile();

  ESPOS.api = api;
  ESPOS.token = token;
  JsonObjectConst device = doc["device"];
  ESPOS.deviceId = device["id"] | 0;
  ESPOS.deviceName = device["name"] | "";
  ESPOS.user = device["user"] | "";
  ESPOS.settingsRev = doc["settings_rev"] | static_cast<int64_t>(0);
  ESPOS.syncedMapped.clear();

  g_summary.settingsApplied = settings::applyServerSet(settings::readObject(doc["settings"]));
  // The installer's clock: the X4 has no RTC, so this gives it the time before its first sync.
  sync::adoptServerClock(doc["now_ms"] | static_cast<int64_t>(0));

  if (!ESPOS.saveToFile()) {
    reply("ERR storage");
    return false;
  }
  LOG_INF("ESPOS", "Provisioned device %d", static_cast<int>(ESPOS.deviceId));
  reply("OK");
  return true;
}

}  // namespace

bool handleLine(const char* payload) {
  while (*payload == ' ') payload++;
  if (strncmp(payload, "HELLO", 5) == 0) {
    // "HELLO <epoch ms>": the host's clock rides along (the X4 has no RTC).
    if (payload[5] == ' ') sync::adoptServerClock(strtoll(payload + 6, nullptr, 10));
    char line[96];
    snprintf(line, sizeof(line), "READY {\"fw\":\"%s\",\"linked\":%s}", FW_VERSION,
             ESPOS.isLinked() ? "true" : "false");
    reply(line);
    return false;
  }
  if (strncmp(payload, "PROVISION ", 10) == 0) return provision(payload + 10);
  reply("ERR unknown_command");
  return false;
}

const ProvisionSummary& lastProvision() { return g_summary; }

}  // namespace espos::serial
