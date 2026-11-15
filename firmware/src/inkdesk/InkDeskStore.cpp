#include <esp_attr.h>
#include "InkDeskStore.h"

#include <HalStorage.h>

#include <esp_attr.h>
#include <esp_system.h>

#include <sys/time.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {
// Earliest plausible wall clock (2025-01-01): below this the system clock was
// never set since a power-off and must not be used for timestamps.
constexpr int64_t MIN_PLAUSIBLE_EPOCH = 1735689600;
constexpr size_t MAX_BOOKS = 200;
}  // namespace

void InkDeskStore::toJson(JsonDocument& doc) const {
  doc["api"] = api;
  doc["token"] = token;
  doc["device_id"] = deviceId;
  doc["name"] = deviceName;
  doc["user"] = user;
  doc["settings_rev"] = settingsRev;
  doc["last_sync_ms"] = lastSyncMs;
  doc["tz_min"] = tzMin;
  doc["dash_fetched_ms"] = dashFetchedMs;
  doc["dash_offline"] = dashOffline;

  JsonObject s = doc["settings"].to<JsonObject>();
  for (const auto& kv : settings) s[kv.key] = kv.value;
  JsonArray dirty = doc["dirty"].to<JsonArray>();
  for (const auto& k : dirtyKeys) dirty.add(k);
  JsonObject synced = doc["synced_mapped"].to<JsonObject>();
  for (const auto& kv : syncedMapped) synced[kv.key] = kv.value;
  JsonArray bookArr = doc["books"].to<JsonArray>();
  for (const auto& b : books) {
    JsonObject o = bookArr.add<JsonObject>();
    o["id"] = b.id;
    o["file"] = b.file;
  }
  JsonArray wr = doc["wifi_results"].to<JsonArray>();
  for (const auto& r : wifiResults) {
    JsonObject o = wr.add<JsonObject>();
    o["ssid"] = r.ssid;
    o["ok"] = r.ok;
    o["code"] = r.code;
    o["at_ms"] = r.atMs;
  }
  JsonArray wa = doc["wifi_acks"].to<JsonArray>();
  for (const auto& a : wifiAcks) {
    JsonObject o = wa.add<JsonObject>();
    o["id"] = a.id;
    o["ok"] = a.ok;
    o["why"] = a.why;
  }
  doc["wall_id"] = wallId;
  doc["wall_changed_ms"] = wallChangedMs;
  if (!fwOffer.version.empty()) {
    JsonObject o = doc["fw_offer"].to<JsonObject>();
    o["version"] = fwOffer.version;
    o["url"] = fwOffer.url;
    o["sha256"] = fwOffer.sha256;
    o["notes"] = fwOffer.notes;
    o["date"] = fwOffer.date;
    o["size"] = fwOffer.size;
    o["install"] = fwOffer.install;
    o["attempts"] = fwOffer.attempts;
  }
  doc["ota_target"] = otaTarget;
  JsonArray rep = doc["ota_reports"].to<JsonArray>();
  for (const auto& r : otaReports) {
    JsonObject o = rep.add<JsonObject>();
    o["version"] = r.version;
    o["ok"] = r.ok;
    o["why"] = r.why;
    o["at_ms"] = r.atMs;
  }
  doc["just_updated"] = justUpdated;
}

bool InkDeskStore::fromJson(JsonVariantConst doc) {
  api = doc["api"] | "";
  token = doc["token"] | "";
  deviceId = doc["device_id"] | 0;
  deviceName = doc["name"] | "";
  user = doc["user"] | "";
  settingsRev = doc["settings_rev"] | static_cast<int64_t>(0);
  lastSyncMs = doc["last_sync_ms"] | static_cast<int64_t>(0);
  tzMin = doc["tz_min"] | 420;
  dashFetchedMs = doc["dash_fetched_ms"] | static_cast<int64_t>(0);
  dashOffline = doc["dash_offline"] | false;

  settings.clear();
  JsonObjectConst s = doc["settings"];
  settings.reserve(s.size());
  for (JsonPairConst kv : s) settings.push_back({kv.key().c_str(), kv.value().as<const char*>() ? kv.value().as<const char*>() : ""});

  dirtyKeys.clear();
  JsonArrayConst dirty = doc["dirty"];
  dirtyKeys.reserve(dirty.size());
  for (JsonVariantConst k : dirty) dirtyKeys.emplace_back(k.as<const char*>() ? k.as<const char*>() : "");

  syncedMapped.clear();
  JsonObjectConst synced = doc["synced_mapped"];
  syncedMapped.reserve(synced.size());
  for (JsonPairConst kv : synced) {
    syncedMapped.push_back({kv.key().c_str(), kv.value().as<const char*>() ? kv.value().as<const char*>() : ""});
  }

  books.clear();
  JsonArrayConst bookArr = doc["books"];
  books.reserve(bookArr.size());
  for (JsonObjectConst o : bookArr) {
    inkdesk::LocalBook b;
    b.id = o["id"] | 0;
    b.file = o["file"] | "";
    if (b.id > 0 && !b.file.empty()) books.push_back(std::move(b));
  }

  wifiResults.clear();
  for (JsonObjectConst o : doc["wifi_results"].as<JsonArrayConst>()) {
    inkdesk::WifiResult r;
    r.ssid = o["ssid"] | "";
    r.ok = o["ok"] | false;
    r.code = o["code"] | 0;
    r.atMs = o["at_ms"] | static_cast<int64_t>(0);
    if (!r.ssid.empty()) wifiResults.push_back(std::move(r));
  }
  wifiAcks.clear();
  for (JsonObjectConst o : doc["wifi_acks"].as<JsonArrayConst>()) {
    inkdesk::WifiAck a;
    a.id = o["id"] | 0;
    a.ok = o["ok"] | false;
    a.why = o["why"] | "";
    if (a.id > 0) wifiAcks.push_back(std::move(a));
  }
  wallId = doc["wall_id"] | 0;
  wallChangedMs = doc["wall_changed_ms"] | static_cast<int64_t>(0);
  fwOffer = {};
  JsonObjectConst fo = doc["fw_offer"];
  if (!fo.isNull()) {
    fwOffer.version = fo["version"] | "";
    fwOffer.url = fo["url"] | "";
    fwOffer.sha256 = fo["sha256"] | "";
    fwOffer.notes = fo["notes"] | "";
    fwOffer.date = fo["date"] | "";
    fwOffer.size = fo["size"] | 0u;
    fwOffer.install = fo["install"] | false;
    fwOffer.attempts = fo["attempts"] | 0;
  }
  otaTarget = doc["ota_target"] | "";
  otaReports.clear();
  for (JsonObjectConst o : doc["ota_reports"].as<JsonArrayConst>()) {
    inkdesk::OtaReport r;
    r.version = o["version"] | "";
    r.ok = o["ok"] | false;
    r.why = o["why"] | "";
    r.atMs = o["at_ms"] | static_cast<int64_t>(0);
    if (otaReports.size() < 4) otaReports.push_back(std::move(r));
  }
  justUpdated = doc["just_updated"] | "";
  return true;
}

void InkDeskStore::noteWifiResult(const std::string& ssid, const bool ok, const int code) {
  for (auto& r : wifiResults) {
    if (r.ssid == ssid) {
      r.ok = ok;
      r.code = code;
      r.atMs = nowMs();
      return;
    }
  }
  if (wifiResults.size() >= 10) wifiResults.erase(wifiResults.begin());
  wifiResults.push_back({ssid, ok, code, nowMs()});
}

const std::string& InkDeskStore::apiBase() const {
  static const std::string fallback = inkdesk::DEFAULT_API;
  return api.empty() ? fallback : api;
}

// A self-hosted server serves its web app at its own address; only the hosted service keeps
// the API and the web app apart.
std::string InkDeskStore::webUrl() const {
  if (apiBase() == inkdesk::DEFAULT_API) return inkdesk::DEFAULT_WEB;
  return apiBase();
}

std::string InkDeskStore::webHost() const {
  std::string u = webUrl();
  const size_t scheme = u.find("://");
  if (scheme != std::string::npos) u.erase(0, scheme + 3);
  const size_t path = u.find('/');
  if (path != std::string::npos) u.erase(path);
  return u;
}

std::string InkDeskStore::pairUrl(const char* code) const {
  return webUrl() + "#/pair?code=" + code;
}

std::string InkDeskStore::get(const char* key, const char* fallback) const {
  for (const auto& kv : settings) {
    if (kv.key == key) return kv.value;
  }
  return fallback;
}

int InkDeskStore::getInt(const char* key, const int fallback) const {
  for (const auto& kv : settings) {
    if (kv.key == key) {
      if (kv.value.empty()) return fallback;
      return atoi(kv.value.c_str());
    }
  }
  return fallback;
}

bool InkDeskStore::isDirty(const char* key) const {
  for (const auto& k : dirtyKeys) {
    if (k == key) return true;
  }
  return false;
}

void InkDeskStore::setLocal(const char* key, const std::string& value) {
  bool found = false;
  for (auto& kv : settings) {
    if (kv.key == key) {
      kv.value = value;
      found = true;
      break;
    }
  }
  if (!found) settings.push_back({key, value});
  if (!isDirty(key)) dirtyKeys.emplace_back(key);
}

void InkDeskStore::replaceSettings(std::vector<inkdesk::KeyValue>&& merged) {
  settings = std::move(merged);
  dirtyKeys.clear();
}

std::string InkDeskStore::latestNewsPath(std::string* day) const {
  // Issues are named "Bao-sang-YYYY-MM-DD.epub" by the server.
  constexpr const char* PREFIX = "Bao-sang-";
  constexpr size_t PREFIX_LEN = 9;
  std::string best;
  std::string bestDate;
  for (const auto& b : books) {
    const size_t at = b.file.find(PREFIX);
    if (at == std::string::npos || b.file.size() < at + PREFIX_LEN + 10) continue;
    const std::string date = b.file.substr(at + PREFIX_LEN, 10);
    if (date <= bestDate) continue;
    const std::string path = std::string(inkdesk::BOOKS_DIR) + "/" + b.file;
    if (!Storage.exists(path.c_str())) continue;
    best = path;
    bestDate = date;
  }
  if (day && !bestDate.empty()) *day = bestDate.substr(8, 2) + "/" + bestDate.substr(5, 2);
  return best;
}

bool InkDeskStore::hasBook(const int32_t id) const {
  for (const auto& b : books) {
    if (b.id == id) return true;
  }
  return false;
}

void InkDeskStore::setBook(const int32_t id, const std::string& file) {
  for (auto& b : books) {
    if (b.id == id) {
      b.file = file;
      return;
    }
  }
  if (books.size() >= MAX_BOOKS) return;
  books.push_back({id, file});
}

void InkDeskStore::clearLink() {
  token.clear();
  deviceId = 0;
  deviceName.clear();
  user.clear();
  settingsRev = 0;
  dirtyKeys.clear();
}

namespace {
// Whether the system clock was set from the server (or the USB host). The system time
// survives deep sleep and software restarts but not a power cut or a chip reset, and on
// those boots it is only restored from the saved floor, which is stale. RTC_NOINIT keeps
// the flag across the silent restart after a sync (RTC_DATA_ATTR would clear it there);
// the reset-reason check drops it on every other kind of boot.
RTC_NOINIT_ATTR uint32_t s_clockValidMagic;
constexpr uint32_t CLOCK_VALID_MAGIC = 0xC10CC0DE;

void checkClockOnBoot() {
  static bool checked = false;
  if (checked) return;
  checked = true;
  const esp_reset_reason_t reason = esp_reset_reason();
  if (reason != ESP_RST_SW && reason != ESP_RST_DEEPSLEEP) s_clockValidMagic = 0;
}
}  // namespace

bool InkDeskStore::clockValid() {
  checkClockOnBoot();
  return s_clockValidMagic == CLOCK_VALID_MAGIC && nowMs() > 0;
}
void InkDeskStore::markClockValid() {
  checkClockOnBoot();
  s_clockValidMagic = CLOCK_VALID_MAGIC;
}

int64_t InkDeskStore::nowMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < MIN_PLAUSIBLE_EPOCH) return 0;
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

void InkDeskStore::formatClock(const int64_t epochMs, char* out, const size_t outSize) const {
  if (epochMs <= 0) {
    snprintf(out, outSize, "--:--");
    return;
  }
  const time_t local = static_cast<time_t>(epochMs / 1000 + static_cast<int64_t>(tzMin) * 60);
  struct tm tmv;
  gmtime_r(&local, &tmv);
  snprintf(out, outSize, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}
