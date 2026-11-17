#include "InkDeskSync.h"

#include <ArduinoJson.h>
#include <HalGPIO.h>
#include <LibraryBuilder.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <TrustedTime.h>
#include <sys/time.h>

#include <cstdlib>
#include <cstring>

#include "InkDeskSettings.h"
#include "WifiCredentialStore.h"
#include "inkdesk/FontSync.h"
#include "inkdesk/InkOta.h"
#include "inkdesk/ReadingStats.h"
#include "inkdesk/Wallpapers.h"
#include "inkdesk/Widgets.h"
#include "network/HttpDownloader.h"
#include "InkDeskStore.h"
#include "RecentBooksStore.h"
#include "util/BookProgress.h"

namespace inkdesk::sync {
namespace {

constexpr size_t SYNC_RESPONSE_CAP = 48 * 1024;
constexpr size_t DASH_RESPONSE_CAP = 8 * 1024;
constexpr const char* LEGACY_TASKS_FILE = "/.crosspoint/inkdesk_tasks.json";

std::string bookPath(const std::string& file) { return std::string(BOOKS_DIR) + "/" + file; }

bool writeRawFile(const char* path, const std::string& content) {
  HalFile file;
  if (!Storage.openFileForWrite("INKD", path, file)) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size()) == content.size();
  file.close();
  return ok;
}

bool fileNameTaken(const std::string& file, const int32_t id) {
  for (const auto& b : INKDESK.books) {
    if (b.id != id && b.file == file) return true;
  }
  return false;
}


}  // namespace

void adoptServerClock(const int64_t nowMs) {
  if (nowMs <= 0) return;
  InkDeskStore::markClockValid();
  const int64_t local = InkDeskStore::nowMs();
  const int64_t drift = local > nowMs ? local - nowMs : nowMs - local;
  // The RTC drifts in deep sleep, so a few seconds off is corrected. Smaller gaps are left alone:
  // the stamp is already old by the response's travel time, which a correction would copy in.
  if (local != 0 && drift < 3000) return;
  struct timeval tv;
  tv.tv_sec = static_cast<time_t>(nowMs / 1000);
  tv.tv_usec = static_cast<suseconds_t>((nowMs % 1000) * 1000);
  settimeofday(&tv, nullptr);
  trustedtime::note();
  LOG_INF("INKD", "Clock set from server (drift %lld ms)", static_cast<long long>(drift));
}

std::string sanitizeFileName(const std::string& name, const int32_t id) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
        c == '|') {
      out.push_back('_');
    } else {
      out.push_back(c);
    }
  }
  while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  if (out.size() > 120) {
    // Keep the extension when shortening (cut on a UTF-8 boundary).
    const size_t dot = out.find_last_of('.');
    const std::string ext = (dot != std::string::npos && out.size() - dot <= 6) ? out.substr(dot) : "";
    size_t cut = 120 - ext.size();
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
    out = out.substr(0, cut) + ext;
  }
  if (out.empty()) out = "book-" + std::to_string(id) + ".epub";
  return out;
}

Outcome run() {
  Outcome outcome;
  std::string body;
  std::vector<std::string> readingDays;  // reading totals sent, marked once the server has them
  const size_t otaReportsSent = INKDESK.otaReports.size();
  {
    JsonDocument req;
    req["fw"] = FW_VERSION;
    req["fw_code"] = ota::versionCode(FW_VERSION);
    stats::collect(req["reading"].to<JsonArray>(), readingDays);
    if (otaReportsSent > 0) {
      JsonArray reps = req["ota"].to<JsonArray>();
      const int64_t nowMs = InkDeskStore::nowMs();
      for (const auto& r : INKDESK.otaReports) {
        JsonObject o = reps.add<JsonObject>();
        o["version"] = r.version;
        o["ok"] = r.ok;
        if (!r.why.empty()) o["why"] = r.why;
        if (r.atMs > 0 && nowMs > r.atMs) o["age_s"] = static_cast<int32_t>((nowMs - r.atMs) / 1000);
      }
    }
    req["battery"] = powerManager.getBatteryPercentage();
    // Voltage and charger state feed the server's battery history (real drain over a day).
    if (const uint16_t mv = powerManager.getBatteryMillivolts()) req["battery_mv"] = mv;
    req["usb"] = gpio.isUsbConnected();
    req["settings_rev"] = INKDESK.settingsRev;
    JsonArray changes = req["settings"].to<JsonArray>();
    settings::collectDeviceChanges(changes);
    outcome.settingsSent = static_cast<int>(changes.size());
    const int64_t now = InkDeskStore::nowMs();
    JsonArray progress = req["progress"].to<JsonArray>();
    for (const auto& recent : RECENT_BOOKS.getBooks()) {
      for (const auto& b : INKDESK.books) {
        if (bookPath(b.file) != recent.path) continue;
        const int percent = loadBookProgress(recent.path);
        if (percent >= 0) {
          JsonObject o = progress.add<JsonObject>();
          o["book_id"] = b.id;
          o["percent"] = percent;
        }
        break;
      }
    }
    // Each book file's size too: the server deletes its stored original only when the size
    // proves the card holds a full copy.
    JsonArray have = req["have_books"].to<JsonArray>();
    JsonArray haveSizes = req["have_sizes"].to<JsonArray>();
    for (const auto& b : INKDESK.books) {
      const std::string path = bookPath(b.file);
      HalFile f;
      if (!Storage.exists(path.c_str()) || !Storage.openFileForRead("INKD", path, f)) continue;
      have.add(b.id);
      JsonObject s = haveSizes.add<JsonObject>();
      s["id"] = b.id;
      s["size"] = static_cast<uint32_t>(f.fileSize());
      f.close();
    }
    JsonArray fontList = req["fonts"].to<JsonArray>();
    for (const auto& f : fonts::installed()) fontList.add(f);
    JsonArray haveWalls = req["have_walls"].to<JsonArray>();
    for (const int32_t id : wallpapers::ids()) haveWalls.add(id);
    // Saved WiFi networks (names only) with the last join result, and web changes applied.
    JsonObject wifi = req["wifi"].to<JsonObject>();
    JsonArray saved = wifi["saved"].to<JsonArray>();
    WIFI_STORE.loadFromFile();
    for (const auto& c : WIFI_STORE.getCredentialSummaries()) {
      JsonObject o = saved.add<JsonObject>();
      o["ssid"] = c.ssid;
      for (const auto& r : INKDESK.wifiResults) {
        if (r.ssid != c.ssid) continue;
        o["ok"] = r.ok;
        o["code"] = r.code;
        if (r.atMs > 0 && now > r.atMs) o["age_s"] = static_cast<int32_t>((now - r.atMs) / 1000);
      }
    }
    JsonArray acks = wifi["acks"].to<JsonArray>();
    for (const auto& a : INKDESK.wifiAcks) {
      JsonObject o = acks.add<JsonObject>();
      o["id"] = a.id;
      o["ok"] = a.ok;
      if (!a.why.empty()) o["why"] = a.why;
    }
    // What this reader can run and the packages on its card (s3desk/docs/profiles.md §9.3).
    pkgs::addToSyncBody(req.as<JsonObject>());
    serializeJson(req, body);
  }

  std::string response;
  outcome.result = client::postJson("/v1/device/sync", body, response, true, SYNC_RESPONSE_CAP);
  body.clear();
  body.shrink_to_fit();
  if (outcome.result == client::Result::Unauthorized) {
    LOG_ERR("INKD", "Sync refused: token no longer valid, unlinking");
    INKDESK.clearLink();
    INKDESK.saveToFile();
    return outcome;
  }
  if (outcome.result != client::Result::Ok) return outcome;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, response);
  response.clear();
  response.shrink_to_fit();
  if (err) {
    LOG_ERR("INKD", "Sync response parse failed: %s", err.c_str());
    outcome.result = client::Result::Server;
    return outcome;
  }

  const int64_t serverNow = doc["now_ms"] | static_cast<int64_t>(0);
  adoptServerClock(serverNow);
  // The server has these now.
  stats::markSent(readingDays);
  if (otaReportsSent > 0 && INKDESK.otaReports.size() >= otaReportsSent) {
    INKDESK.otaReports.erase(INKDESK.otaReports.begin(), INKDESK.otaReports.begin() + otaReportsSent);
  }
  // Sessions read before the clock was set land on today now that it is.
  stats::compact();

  // Firmware: the newest version when it is newer than this one (else none).
  {
    JsonObjectConst fw = doc["fw_update"];
    const std::string version = fw["version"] | "";
    if (version.empty() || ota::versionCode(version) <= ota::versionCode(FW_VERSION)) {
      INKDESK.fwOffer = {};
    } else {
      if (version != INKDESK.fwOffer.version) INKDESK.fwOffer = {};  // a new version: fresh tries
      INKDESK.fwOffer.version = version;
      INKDESK.fwOffer.url = fw["url"] | "";
      INKDESK.fwOffer.sha256 = fw["sha256"] | "";
      INKDESK.fwOffer.notes = fw["notes"] | "";
      INKDESK.fwOffer.date = fw["date"] | "";
      INKDESK.fwOffer.size = fw["size"] | 0u;
      INKDESK.fwOffer.install = fw["install"] | false;
    }
  }
  INKDESK.lastSyncMs = serverNow > 0 ? serverNow : InkDeskStore::nowMs();
  INKDESK.tzMin = doc["tz_min"] | INKDESK.tzMin;

  JsonObjectConst device = doc["device"];
  if (!device.isNull()) {
    INKDESK.deviceId = device["id"] | INKDESK.deviceId;
    INKDESK.deviceName = device["name"] | INKDESK.deviceName.c_str();
    INKDESK.user = device["user"] | INKDESK.user.c_str();
  }

  // Settings: full merged set.
  INKDESK.settingsRev = doc["settings_rev"] | INKDESK.settingsRev;
  {
    std::vector<std::string> webKeys;
    for (JsonVariantConst k : doc["web_keys"].as<JsonArrayConst>()) webKeys.emplace_back(k.as<const char*>() ? k.as<const char*>() : "");
    outcome.settingsApplied = settings::applyServerSet(settings::readObject(doc["settings"]), webKeys);
  }

  outcome.fontWanted = fonts::missing();
  // Packages: x4.pkgs (just merged) says what is wanted, the answer's "pkgs" where to get it.
  pkgs::applyRemovals();
  outcome.pkgs = pkgs::plan(doc["pkgs"].as<JsonArrayConst>(), !doc["pkgs"].isNull());

  // Left over from the removed task board.
  if (Storage.exists(LEGACY_TASKS_FILE)) Storage.remove(LEGACY_TASKS_FILE);

  // Dashboard cache.
  if (!doc["dashboard"].isNull()) {
    JsonDocument dashDoc;
    dashDoc.set(doc["dashboard"]);
    if (PersistableStoreBase::writeDocToFile(DASH_FILE, dashDoc)) {
      INKDESK.dashFetchedMs = INKDESK.lastSyncMs;
      INKDESK.dashOffline = false;
    }
  }

  // Books: queue missing ones, drop the ones deleted on the web.
  JsonArrayConst books = doc["books"];
  std::vector<int32_t> listed;
  listed.reserve(books.size());
  outcome.downloads.reserve(books.size());
  for (JsonObjectConst b : books) {
    const int32_t id = b["id"] | 0;
    if (id <= 0) continue;
    listed.push_back(id);
    const char* url = b["url"];
    if (!url || !*url) continue;
    BookDownload d;
    d.id = id;
    d.title = b["title"] | "";
    d.file = sanitizeFileName(b["file"] | "", id);
    if (fileNameTaken(d.file, id)) d.file = std::to_string(id) + "-" + d.file;
    d.url = url;
    d.size = b["size"] | 0u;
    outcome.downloads.push_back(std::move(d));
  }
  for (auto it = INKDESK.books.begin(); it != INKDESK.books.end();) {
    bool stillListed = false;
    for (const int32_t id : listed) {
      if (id == it->id) {
        stillListed = true;
        break;
      }
    }
    if (stillListed) {
      ++it;
      continue;
    }
    outcome.deletions.push_back(bookPath(it->file));
    it = INKDESK.books.erase(it);
  }

  LOG_INF("INKD", "Sync: %d books listed, %d to fetch, %d to delete, %d pictures to fetch",
          static_cast<int>(books.size()), static_cast<int>(outcome.downloads.size()),
          static_cast<int>(outcome.deletions.size()), static_cast<int>(doc["wallpapers"].size()));

  // WiFi: the acks just sent are done; apply the web's new changes and ack them next time.
  INKDESK.wifiAcks.clear();
  {
    bool changed = false;
    for (JsonObjectConst op : doc["wifi_ops"].as<JsonArrayConst>()) {
      inkdesk::WifiAck ack;
      ack.id = op["id"] | 0;
      const char* ssid = op["ssid"] | "";
      if (ack.id <= 0 || !*ssid) continue;
      if (strcmp(op["op"] | "", "remove") == 0) {
        WIFI_STORE.removeCredential(ssid);
        ack.ok = true;
      } else {
        ack.ok = WIFI_STORE.addCredential(ssid, op["pass"] | "");
        if (!ack.ok) ack.why = "full";
      }
      changed = changed || ack.ok;
      INKDESK.wifiAcks.push_back(std::move(ack));
      outcome.wifiApplied++;
    }
    if (changed) WIFI_STORE.saveToFile();
  }

  // Sleep-screen pictures: drop the ones deleted on the web, queue the new ones.
  if (!doc["wallpapers"].isNull()) {
    std::vector<int32_t> keep;
    for (JsonObjectConst w : doc["wallpapers"].as<JsonArrayConst>()) {
      const int32_t id = w["id"] | 0;
      if (id <= 0) continue;
      keep.push_back(id);
      const char* url = w["url"];
      if (url && *url) outcome.walls.push_back({id, url});
    }
    wallpapers::removeExcept(keep);
  }

  INKDESK.saveToFile();
  return outcome;
}


// Photo widgets: the pictures every layout needs at its box sizes; missing ones are downloaded,
// ones no layout uses any more are removed. Returns the number of failed downloads.
int syncPhotos(const bool* cancel) {
  namespace wg = inkdesk::widgets;
  std::vector<wg::PhotoFile> need;
  for (const auto screen : {wg::Screen::Home, wg::Screen::SleepLandscape, wg::Screen::SleepPortrait}) {
    const wg::Layout l = wg::load(screen, true);
    for (auto& f : wg::photoFiles(l, wg::boxesFor(l))) need.push_back(std::move(f));
  }
  // Remove what no layout uses.
  if (HalFile dir = Storage.open(wg::PHOTO_DIR)) {
    std::vector<std::string> stale;
    char name[96];
    for (HalFile e = dir.openNextFile(); e; e = dir.openNextFile()) {
      const size_t len = e.getName(name, sizeof(name));
      const bool isDir = e.isDirectory();
      e.close();
      if (len == 0 || len >= sizeof(name) || isDir) continue;
      const std::string path = std::string(wg::PHOTO_DIR) + "/" + name;
      bool used = false;
      for (const auto& f : need) used = used || f.path == path;
      if (!used) stale.push_back(path);
    }
    dir.close();
    for (const auto& p : stale) Storage.remove(p.c_str());
  }
  if (need.empty()) return 0;
  Storage.mkdir(wg::PHOTO_DIR);
  const std::vector<HttpDownloader::Header> headers = {{"Authorization", INKDESK.authHeader()}};
  int failed = 0, fetched = 0;
  for (const auto& f : need) {
    if (cancel && *cancel) break;
    if (Storage.exists(f.path.c_str())) continue;
    fetched++;
    const std::string url = INKDESK.apiBase() + "/v1/device/photos/" + std::to_string(f.id) + "/" + f.dim + "/" + f.params;
    const auto r = HttpDownloader::downloadToFile(url, f.path, nullptr, cancel, "", "", headers);
    if (r != HttpDownloader::OK) {
      LOG_ERR("INKD", "Photo %ld %s download failed: %d", static_cast<long>(f.id), f.dim.c_str(), static_cast<int>(r));
      Storage.remove(f.path.c_str());
      failed++;
    }
  }
  LOG_INF("INKD", "Photos: %d needed, %d fetched, %d failed", static_cast<int>(need.size()), fetched, failed);
  return failed;
}

int downloadFiles(const Outcome& outcome, const bool* cancel) {
  // Photo widget pictures first (they follow the layouts the sync just brought).
  int failed = syncPhotos(cancel);
  if (!outcome.walls.empty()) {
    Storage.mkdir(wallpapers::DIR);
    const std::vector<HttpDownloader::Header> headers = {{"Authorization", INKDESK.authHeader()}};
    for (const auto& w : outcome.walls) {
      if (cancel && *cancel) break;
      const auto r = HttpDownloader::downloadToFile(w.url, wallpapers::pathFor(w.id), nullptr, cancel, "", "", headers);
      if (r != HttpDownloader::OK) {
        LOG_ERR("INKD", "Picture %d download failed: %d", static_cast<int>(w.id), static_cast<int>(r));
        failed++;
      }
    }
  }
  if (!(cancel && *cancel)) failed += pkgs::install(outcome.pkgs, cancel);
  return failed;
}

int downloadAll(const Outcome& outcome, const bool* cancel) {
  bool libraryChanged = false;
  for (const auto& path : outcome.deletions) {
    if (Storage.exists(path.c_str())) libraryChanged = Storage.remove(path.c_str()) || libraryChanged;
  }
  int failed = downloadFiles(outcome, cancel);
  if (!outcome.fontWanted.empty() && !fonts::install(outcome.fontWanted, nullptr, cancel)) failed++;
  if (!outcome.downloads.empty()) Storage.mkdir(BOOKS_DIR);
  const std::vector<HttpDownloader::Header> headers = {{"Authorization", INKDESK.authHeader()}};
  for (const auto& d : outcome.downloads) {
    if (cancel && *cancel) break;
    const auto r = HttpDownloader::downloadToFile(d.url, bookPath(d.file), nullptr, cancel, "", "", headers);
    if (r == HttpDownloader::OK) {
      INKDESK.setBook(d.id, d.file);
      INKDESK.saveToFile();
      libraryChanged = true;
    } else {
      LOG_ERR("INKD", "Book %d download failed: %d", static_cast<int>(d.id), static_cast<int>(r));
      failed++;
    }
  }
  // The Library list reads a prebuilt index: rebuild it on the next visit.
  if (libraryChanged) library::markLibraryIndexDirty();
  return failed;
}

client::Result refreshDashboard() {
  std::string response;
  const client::Result result = client::getJson("/v1/device/dashboard", response, true, DASH_RESPONSE_CAP);
  if (result == client::Result::Unauthorized) {
    INKDESK.clearLink();
    INKDESK.saveToFile();
    return result;
  }
  if (result != client::Result::Ok) {
    INKDESK.dashOffline = true;
    INKDESK.saveToFile();
    return result;
  }
  int64_t serverNow = 0;
  {
    JsonDocument doc;
    if (deserializeJson(doc, response)) {
      INKDESK.dashOffline = true;
      INKDESK.saveToFile();
      return client::Result::Server;
    }
    serverNow = doc["now_ms"] | static_cast<int64_t>(0);
    INKDESK.tzMin = doc["tz_min"] | INKDESK.tzMin;
  }
  adoptServerClock(serverNow);
  if (!writeRawFile(DASH_FILE, response)) return client::Result::Server;
  INKDESK.dashFetchedMs = serverNow > 0 ? serverNow : InkDeskStore::nowMs();
  INKDESK.dashOffline = false;
  INKDESK.saveToFile();
  return client::Result::Ok;
}

}  // namespace inkdesk::sync
