#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

// InkDesk companion-service state, persisted on the SD card.
//
// Contract: docs/device-api.md in the InkDesk repository. The device keeps its
// token, the last merged settings set
// from the server, the device-side changes not yet sent, and which InkDesk
// books it already downloaded. The dashboard cache lives in its own file (only
// the screens that show it read it).
namespace inkdesk {

constexpr const char* FW_VERSION = "inkdesk-0.3.5";
// The server a reader talks to until USB setup gives it one. Empty by default: the web
// installer always sends the server's address. Set them in platformio.local.ini (see the
// firmware README) so a reader can also be linked with a code, without a cable.
// INKDESK_DEFAULT_WEB is only needed when the web app is not served by the API host.
#ifndef INKDESK_DEFAULT_API
#define INKDESK_DEFAULT_API ""
#endif
#ifndef INKDESK_DEFAULT_WEB
#define INKDESK_DEFAULT_WEB INKDESK_DEFAULT_API
#endif
constexpr const char* DEFAULT_API = INKDESK_DEFAULT_API;
constexpr const char* DEFAULT_WEB = INKDESK_DEFAULT_WEB;
constexpr const char* BOOKS_DIR = "/InkDesk";
constexpr const char* DASH_FILE = "/.crosspoint/inkdesk_dash.json";

struct KeyValue {
  std::string key;
  std::string value;
};

struct LocalBook {
  int32_t id = 0;
  std::string file;  // file name inside BOOKS_DIR
};

// Last join attempt for one saved network (reported to the web, names only).
struct WifiResult {
  std::string ssid;
  bool ok = false;
  int code = 0;      // WiFi status after the attempt (3 connected, 1 not found, 4 failed)
  int64_t atMs = 0;  // wall clock, 0 when unknown
};

// A firmware the server offers (newer than the running one).
struct FwOffer {
  std::string version;  // "inkdesk-0.2.0"
  std::string url;
  std::string sha256;   // hex
  std::string notes;    // "What's new", one item per line
  std::string date;     // "16/11"
  uint32_t size = 0;
  bool install = false;  // the web asked for it (button, or auto-update while charging)
  uint8_t attempts = 0;  // installs started for this version (a crash must not loop)
};

// Result of the last install attempt, reported on the next sync.
struct OtaReport {
  std::string version;
  bool ok = false;
  std::string why;
  int64_t atMs = 0;
};

// A web WiFi change applied on the device, acknowledged on the next sync.
struct WifiAck {
  int32_t id = 0;
  bool ok = false;
  std::string why;
};

}  // namespace inkdesk

class InkDeskStore : public PersistableStore<InkDeskStore> {
  InkDeskStore() = default;
  friend class PersistableStore<InkDeskStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/inkdesk.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Link
  std::string api;
  std::string token;
  int32_t deviceId = 0;
  std::string deviceName;
  std::string user;

  // Server merge state
  int64_t settingsRev = 0;
  int64_t lastSyncMs = 0;  // server wall clock of the last successful sync
  int32_t tzMin = 420;
  std::vector<inkdesk::KeyValue> settings;        // full merged set from the server
  std::vector<std::string> dirtyKeys;             // InkDesk-only keys changed on the device
  std::vector<inkdesk::KeyValue> syncedMapped;    // mapped device settings as last applied/sent
  std::vector<inkdesk::LocalBook> books;


  // Dashboard cache bookkeeping
  int64_t dashFetchedMs = 0;  // server wall clock of the cached dashboard
  bool dashOffline = false;   // last refresh attempt failed

  std::vector<inkdesk::WifiResult> wifiResults;
  std::vector<inkdesk::WifiAck> wifiAcks;

  // Sleep-screen picture on show and when it last changed.
  int32_t wallId = 0;
  int64_t wallChangedMs = 0;

  // Firmware updates over WiFi.
  inkdesk::FwOffer fwOffer;          // empty version: up to date (as of the last sync)
  std::string otaTarget;             // version being installed; checked on the next boot
  std::vector<inkdesk::OtaReport> otaReports;  // waiting to be sent
  std::string justUpdated;           // shown once on Home after an update

  void noteWifiResult(const std::string& ssid, bool ok, int code);

  bool isLinked() const { return !token.empty(); }
  const std::string& apiBase() const;
  // The web app for this reader's server: the hosted site, or a self-hosted server itself.
  std::string webUrl() const;
  // webUrl() without its scheme, for printing under a QR code.
  std::string webHost() const;
  // The web page that links a reader showing this pairing code.
  std::string pairUrl(const char* code) const;
  std::string authHeader() const { return "Device " + token; }

  // Settings lookup over the merged set (string values, per the contract).
  std::string get(const char* key, const char* fallback) const;
  int getInt(const char* key, int fallback) const;
  bool isDirty(const char* key) const;
  // A change made on the device: stored now, sent on the next sync.
  void setLocal(const char* key, const std::string& value);
  void replaceSettings(std::vector<inkdesk::KeyValue>&& merged);

  // The newest morning-news issue on the card ("" when none); `day` gets "14/11".
  std::string latestNewsPath(std::string* day = nullptr) const;

  bool hasBook(int32_t id) const;
  void setBook(int32_t id, const std::string& file);

  void clearLink();

  // Estimated wall clock in ms (system clock once it is plausible), else 0.
  static int64_t nowMs();
  // True once the clock was set from the server since power-on (kept across deep sleep).
  static bool clockValid();
  static void markClockValid();
  // "HH:MM" of a server wall-clock ms in the device's time zone ("--:--" for 0).
  void formatClock(int64_t epochMs, char* out, size_t outSize) const;
};

#define INKDESK InkDeskStore::getInstance()
