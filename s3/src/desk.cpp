#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "desk.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <sys/time.h>

#include "esp_mac.h"
#include "esp_system.h"

#include <deque>
#include <mutex>
#include <map>
#include <vector>

#include "board.h"
#include "diag.h"
#include "mbedtls/platform.h"
#include "mbedtls/sha256.h"
#include "net.h"
#include "os.h"
#include "photo.h"
#include "pkg/store.h"
#include "power.h"
#include "remote.h"
#include "trace.h"

#include "i18n.h"
#include "led.h"
#include "lic.h"
#include "lock.h"
#include "claim.h"

// The IDF's built-in root certificates (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE): the server's
// certificate is checked, never skipped.
extern const uint8_t x509_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace desk {
namespace {

// JSON documents live in PSRAM: a dashboard with its layouts is tens of KB.
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} psram;

constexpr const char* DASH_FILE = "/dash.json";
constexpr const char* SETTINGS_FILE = "/settings.json";
#ifdef ESPOS_QEMU
constexpr const char* DEVICE_NAME = "EspOS S3 (QEMU)";  // the emulator, told apart from the board on the web
#else
constexpr const char* DEVICE_NAME = "EspOS S3";
#endif

// ---------------------------------------------------------------- state (UI loop only)
Link state = Link::None;
String api = ESPOS_API, token, devName, devUser;
int devId = 0;
uint32_t changeCount = 0;

JsonDocument dashDoc(&psram);
uint32_t dashVer = 0;

// The dashboard's content without the server's clock (now_ms, next_s change at every sync): a sync bringing the same
// weather, events and prices neither rewrites dash.json nor rebuilds Home (0.5.7: both happened at every sync).
struct SigWriter {
  uint32_t h = 2166136261u;
  size_t write(uint8_t c) {
    h = (h ^ c) * 16777619u;
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; i++) write(b[i]);
    return n;
  }
};
uint32_t dashSig(JsonObjectConst d) {
  SigWriter w;
  for (JsonPairConst kv : d) {
    if (!strcmp(kv.key().c_str(), "now_ms") || !strcmp(kv.key().c_str(), "next_s")) continue;
    w.write((const uint8_t*)kv.key().c_str(), strlen(kv.key().c_str()));
    serializeJson(kv.value(), w);
  }
  return w.h;
}
uint32_t dashSigNow = 0;

std::map<String, String> vals;           // merged settings
std::map<String, uint32_t> dirty;        // changed on the device: key -> millis() of the change
int64_t settingsRev = 0, savedRev = -1;  // savedRev: the rev in settings.json
uint32_t settingsVer = 0;

bool inFlight = false;
uint32_t nextSync = 0, failures = 0, syncStartedAt = 0;
int64_t lastSync = 0;
String lastErr;

struct Ack {
  int id;
  bool ok;
  String why;
};
std::vector<Ack> wifiAcks, acksSent;

struct OtaReport {
  String version, why;
  bool ok;
  int64_t atMs;
};
std::vector<OtaReport> otaReports;
size_t otaSent = 0;
FwOffer offer;

// Photo widget pictures still to download (after each sync).
std::vector<photo::File> photoQueue;
photo::File photoNow;

void planPhotos() {
  const auto want = photo::wanted();
  photo::prune(want);
  photoQueue.clear();
  for (const auto& f : want)
    if (!photo::have(f)) photoQueue.push_back(f);
  if (photoQueue.size()) trace::add("photos to fetch: %u", (unsigned)photoQueue.size());
}

// App fetches.
struct FetchReq {
  String url;  // API path or full URL
  bool bytes;
  bool urgent;
  FetchCb cb;
  void* ctx;
  int hops;
};
std::deque<FetchReq> fetchQueue;
FetchReq fetchNow;
bool fetchActive = false;

// Pairing.
String pendingToken, code, pairState;
int codeTries = 0;
uint32_t codeExpires = 0, nextPoll = 0;

// Ping (GET /v1/device/ping, 2026-11-17): while linked and online, ask every minute whether the web changed something
// since the last sync (its settings_rev, an update asked for or a new release), and sync at once when it did, so a
// change saved on the web shows here within about a minute instead of at the next sync (dash.interval_min). It rides
// the kept connection (KEEP_IDLE_MS > PING_MS keeps it open); it never opens a fresh one without internal RAM to
// spare, nor while a screen that asked for the RAM (releaseConnection: a game, a download) is still open.
constexpr uint32_t PING_MS = 45000;  // 0.5.7: 45 s (was 60), so the kept connection never idles past KEEP_IDLE_MS
constexpr size_t PING_MIN_INTERNAL = 40 * 1024;  // to open a fresh connection for a ping
constexpr uint32_t PING_FW_GAP_MS = 120000;      // "fw" starts at most one sync in this long
uint32_t nextPing = 0, fwActedAt = 0;
int64_t pingRevActed = 0;  // the rev a ping started a sync for: a server whose rev never matches cannot loop
volatile bool pingHold = false;

enum class Job { Sync, PairStart, WhoAmI, PhotoWhere, PhotoGet, Fetch, Remote, Ping, Raw };
Job inFlightJob = Job::Sync;
// Remote control (remoteSend): the one request waiting, and whether the last one sent was a remote one (then the
// others go first).
String remoteBody;
RemoteCb remoteCb = nullptr, remoteCbNow = nullptr;
bool remoteWaiting = false, lastRemote = false;
// The assistant's raw request (rawSend): the one waiting, and the one running.
RawFn rawFn = nullptr;
RawDone rawDone = nullptr, rawDoneNow = nullptr;
void *rawCtx = nullptr, *rawCtxNow = nullptr;
struct Request {
  Job job;
  String path, body, token;
  bool post;
  String url;  // a full URL (storage) instead of api + path
  bool bytes = false;  // keep the raw body
  RawFn raw = nullptr;
  void* rawCtx = nullptr;
};
struct Response {
  Job job;
  int status;  // HTTP status, or < 0 for a network error
  JsonDocument* doc;
  String location;           // a redirect's target (PhotoWhere)
  uint8_t* data = nullptr;   // a downloaded file in PSRAM (PhotoGet)
  size_t len = 0;
};
QueueHandle_t jobs, results;

// One TLS connection is kept open between requests (the API and its storage are one host):
// a new handshake costs the board ~3 s, so a list of 10 thumbnails took 30 s without it.
NetworkClientSecure* keepTls = nullptr;
HTTPClient* keepHttp = nullptr;
uint32_t keepUsed = 0;
// The server keeps an idle connection open for minutes (measured from a computer on 2026-11-16: still the same
// socket after 100 s idle; 2026-11-19: Cloudflare closes it after 400 s idle). Dropping ours after 15 s cost a 3-5 s
// handshake on almost every request. On the board (0.5.6) kept connections still died sooner on the way (a router or
// the carrier forgetting an idle connection): 50 s, with the ping every 45 s keeping the one in use warm, and a
// request that fails at once on it is sent again on a fresh one (http()).
constexpr uint32_t KEEP_IDLE_MS = 50000;
constexpr size_t KEEP_MIN_INTERNAL = 24 * 1024;
volatile bool dropKeep = false, keepOpen = false;

void dropKept() {
  if (keepTls) keepTls->stop();
  keepOpen = false;
}

void makeKept() {
  if (keepTls) return;
  keepTls = new NetworkClientSecure();
  keepTls->setCACertBundle(x509_bundle_start, x509_bundle_end - x509_bundle_start);
  keepTls->setHandshakeTimeout(20);
  keepHttp = new HTTPClient();
  keepHttp->setConnectTimeout(10000);
  keepHttp->setTimeout(20000);
  keepHttp->setReuse(true);
  // Redirects are followed by hand: the device token must never go to the storage host.
  keepHttp->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
}

// rawSend's request: the kept connection handed to the assistant (HTTPClient reuses it afterwards, as it is).
int raw(const Request& r) {
  makeKept();
  if (millis() - keepUsed > KEEP_IDLE_MS) dropKept();
  String host = api.substring(api.indexOf("//") + 2);
  if (host.indexOf('/') > 0) host = host.substring(0, host.indexOf('/'));
  bool up = keepTls->connected();
  if (!up) up = keepTls->connect(host.c_str(), 443) == 1;
  if (!up) {
    char err[96] = "";
    keepTls->lastError(err, sizeof err);
    trace::add("raw: no connection (%s)", err);
  }
  const bool keep = r.raw(r.rawCtx, up ? keepTls : nullptr, host.c_str(), r.token.c_str());
  keepUsed = millis();
  if (keep && up && keepTls->connected()) {
    keepOpen = true;
  } else {
    dropKept();
  }
  return up ? 200 : -1;
}

void hwHeaders(HTTPClient& h);
int http(const Request& r, String& out, Response* res) {
  const String url = r.url.length() ? r.url : api + r.path;
  for (int attempt = 0; attempt < 2; attempt++) {
    makeKept();
    // Idle a long while: the server has probably dropped it, start afresh (a dropped one is retried below anyway).
    if (millis() - keepUsed > KEEP_IDLE_MS) dropKept();
    HTTPClient& h = *keepHttp;
    const char* keep[] = {"Location"};
    if (!h.begin(*keepTls, url)) return -1;
    h.collectHeaders(keep, 1);
    if (!r.url.length()) h.addHeader("Content-Type", "application/json");
    if (r.token.length()) h.addHeader("Authorization", "Device " + r.token);
    if (!r.url.length()) hwHeaders(h);  // the trial's hardware id, to our API only (never to storage links)
    const uint32_t sent = millis();
    const int code = r.post ? h.POST(r.body) : h.GET();
    keepUsed = millis();
    keepOpen = code > 0;
    // A kept connection the server had closed fails at once (-2/-3 in under ~2 s, 0.5.6 on the board: "Couldn't
    // reach the server"): once more on a fresh one. A slow failure (a timeout) is resent only when it is a GET, never
    // a POST that may have reached the server (0.5.7).
    if (code < 0 && attempt == 0 && (millis() - sent < 3000 || !r.post)) {
      h.end();
      dropKept();
      trace::add("http %d after %lu ms, retrying on a fresh connection", code, millis() - sent);
      continue;
    }
    if (code == 301 || code == 302 || code == 307) {
      res->location = h.header("Location");
      h.getString();  // drain the body so the connection can serve the next request
    } else if (code == 200 && (r.job == Job::PhotoGet || r.bytes)) {
      const int len = h.getSize();
      if (len > 0 && len <= 600000 && (res->data = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM))) {
        NetworkClient* st = h.getStreamPtr();
        size_t got = 0;
        const uint32_t t0 = millis();
        while (got < (size_t)len && millis() - t0 < 30000 && (h.connected() || st->available())) {
          const int n = st->readBytes(res->data + got, len - got);
          if (n > 0) got += n;
          else delay(5);
        }
        res->len = got;
        if (got < (size_t)len) dropKept();  // a cut body leaves the stream unusable
      } else {
        dropKept();  // unknown or huge length: the body is not read, the stream is spent
      }
    } else if (code > 0) {
      out = h.getString();
    } else {
      char err[96] = "";
      keepTls->lastError(err, sizeof err);
      trace::add("http error %d (%s) tls: %s, largest block %u KB", code, h.errorToString(code).c_str(), err,
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
      dropKept();
    }
    h.end();
    return code;
  }
  return -1;
}

void worker(void*) {
  for (;;) {
    Request* r;
    if (xQueueReceive(jobs, &r, pdMS_TO_TICKS(1000)) != pdTRUE) {
      // Between requests: close the kept connection when asked (a package download), when idle for long, or
      // when internal RAM runs low (it holds ~10 KB of it; with that gone the SD card and TLS failed, 2026-11-17).
      const bool lowRam = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < KEEP_MIN_INTERNAL;
      if (keepOpen && (dropKeep || lowRam || millis() - keepUsed > KEEP_IDLE_MS)) {
        dropKept();
        trace::add("http: kept connection closed (%s)", dropKeep ? "asked" : lowRam ? "low internal RAM" : "idle");
      }
      dropKeep = false;
      continue;
    }
    String out;
    const uint32_t t0 = millis();
    Response* res = new Response{r->job, 0, new JsonDocument(&psram)};
    res->status = r->job == Job::Raw ? raw(*r) : http(*r, out, res);
    if (out.length() && deserializeJson(*res->doc, out) != DeserializationError::Ok) res->doc->clear();
    // A good ping every minute would push everything else out of the trace: only the ones that failed are kept.
    if (!(r->job == Job::Ping && res->status == 200))
      trace::add("http %s %s -> %d, %u bytes, %lu ms, heap %u KB", r->post ? "POST" : "GET",
                 r->url.length() ? "(storage)" : r->path.c_str(), res->status, (unsigned)(res->len ? res->len : out.length()),
                 millis() - t0, ESP.getFreeHeap() / 1024);
    delete r;
    xQueueSend(results, &res, portMAX_DELAY);
  }
}

void submit(Job job, const String& path, const String& body, const String& tok, bool post, const String& url = "",
            bool bytes = false) {
  Request* r = new Request{job, path, body, tok, post, url, bytes};
  inFlight = true;
  inFlightJob = job;
  xQueueSend(jobs, &r, portMAX_DELAY);
}

void startFetch() {
  fetchNow = fetchQueue.front();
  fetchQueue.pop_front();
  fetchActive = true;
  const bool full = fetchNow.url.startsWith("https://");
  submit(Job::Fetch, full ? "" : fetchNow.url, "", full ? "" : token, false, full ? fetchNow.url : "", fetchNow.bytes);
}

// ---------------------------------------------------------------- storage
void saveLink() {
  Preferences p;
  p.begin("link", false);
  p.putString("api", api);
  p.putString("token", token);
  p.putInt("id", devId);
  p.putString("name", devName);
  p.putString("user", devUser);
  p.end();
}

void loadLink() {
  Preferences p;
  p.begin("link", true);
  api = p.getString("api", ESPOS_API);
  token = p.getString("token", "");
  devId = p.getInt("id", 0);
  devName = p.getString("name", "");
  devUser = p.getString("user", "");
  p.end();
  state = token.length() == 64 ? Link::Linked : Link::None;
}

// settings.json and dash.json are written by a small task of their own (0.5.7): a LittleFS write took 110-320 ms of
// the UI loop, and up to 1.8 s when LittleFS tidied its records (QEMU at board speed), at every change made on the
// device. The UI loop makes the text (its own snapshot); the task writes the newest text of each file, then ends.
// A restart waits for it (esp_register_shutdown_handler). Its stack is internal RAM: flash writes turn the cache off.
std::mutex fsMu;
String fsBody[2];           // settings, dashboard: the newest text not written yet
bool fsWant[2] = {false, false};
volatile bool fsBusy = false;
const char* const FS_PATH[2] = {SETTINGS_FILE, DASH_FILE};

void writeNow(int k, const String& body) {
  File f = LittleFS.open(FS_PATH[k], "w");
  if (f) f.write((const uint8_t*)body.c_str(), body.length()), f.close();
}

void fsWriter(void*) {
  for (;;) {
    String body;
    int k = -1;
    {
      std::lock_guard<std::mutex> l(fsMu);
      for (int i = 0; i < 2 && k < 0; i++)
        if (fsWant[i]) k = i, fsWant[i] = false, body = std::move(fsBody[i]), fsBody[i] = String();
      if (k < 0) {
        fsBusy = false;
        break;
      }
    }
    const uint32_t t0 = millis();
    writeNow(k, body);
    trace::add("saved %s (%u B) in %lu ms, stack %u B never used", FS_PATH[k], (unsigned)body.length(), millis() - t0,
               (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  }
  vTaskDelete(nullptr);
}

void fsWaitAll() {  // a restart: the last change in first (at most 3 s)
  for (int i = 0; i < 300 && fsBusy; i++) vTaskDelay(pdMS_TO_TICKS(10));
}

void writeLater(int k, String&& body) {
  static bool hooked = false;
  if (!hooked) hooked = true, esp_register_shutdown_handler(fsWaitAll);
  std::lock_guard<std::mutex> l(fsMu);
  fsBody[k] = std::move(body);
  fsWant[k] = true;
  if (fsBusy) return;  // the running task takes it next
  fsBusy = true;
  if (xTaskCreatePinnedToCore(fsWriter, "fsw", 5120, nullptr, 1, nullptr, 0) != pdPASS) {
    fsBusy = fsWant[k] = false;
    writeNow(k, fsBody[k]);  // no room for the task: as before, on this loop
    fsBody[k] = String();
  }
}

void saveSettings() {
  JsonDocument d(&psram);
  d["rev"] = settingsRev;
  JsonObject v = d["v"].to<JsonObject>();
  for (auto& kv : vals) v[kv.first] = kv.second;
  JsonArray dk = d["d"].to<JsonArray>();
  for (auto& kv : dirty) dk.add(kv.first);
  String body;
  serializeJson(d, body);
  writeLater(0, std::move(body));
  savedRev = settingsRev;
}

void loadSettings() {
  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) return;
  JsonDocument d(&psram);
  if (deserializeJson(d, f) == DeserializationError::Ok) {
    settingsRev = savedRev = d["rev"] | (int64_t)0;
    for (JsonPairConst kv : d["v"].as<JsonObjectConst>()) vals[kv.key().c_str()] = kv.value().as<const char*>();
    for (JsonVariantConst k : d["d"].as<JsonArrayConst>()) dirty[k.as<const char*>()] = 0;
  }
  f.close();
}

void saveDash() {
  String body;
  serializeJson(dashDoc, body);
  writeLater(1, std::move(body));
}

void loadDash() {
  File f = LittleFS.open(DASH_FILE, "r");
  if (!f) return;
  if (deserializeJson(dashDoc, f) != DeserializationError::Ok) dashDoc.clear();
  f.close();
  dashSigNow = dashDoc.isNull() ? 0 : dashSig(dashDoc.as<JsonObjectConst>());
  dashVer++;
}

// ---------------------------------------------------------------- helpers
String hex(const uint8_t* b, size_t n) {
  static const char* H = "0123456789abcdef";
  String s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) s += H[b[i] >> 4], s += H[b[i] & 15];
  return s;
}

String sha256hex(const String& s) {
  uint8_t out[32];
  mbedtls_sha256((const uint8_t*)s.c_str(), s.length(), out, 0);
  return hex(out, 32);
}

// The hardware for the trial (one trial per board, admin-users' contract 2026-11-19): X-Espos-Hwid = SHA-256 of
// "espos-hw:" + the base MAC as 12 lowercase hex, X-Espos-Mac4 = its last 4 hex digits. The MAC itself is never sent.
void hwHeaders(HTTPClient& h) {
  String m4;
  h.addHeader("X-Espos-Hwid", hwid(&m4));
  h.addHeader("X-Espos-Mac4", m4);
}

int versionCode(const char* v) {
  const char* p = strrchr(v, '-');
  p = p ? p + 1 : v;
  int a = 0, b = 0, c = 0;
  sscanf(p, "%d.%d.%d", &a, &b, &c);
  return a * 10000 + b * 100 + c;
}

void setTz(int tzMin) {
  // POSIX TZ: the sign is inverted (UTC+7 is "<+07>-7").
  char tz[32];
  const int a = abs(tzMin);
  snprintf(tz, sizeof tz, "<%c%02d%02d>%c%d:%02d", tzMin >= 0 ? '+' : '-', a / 60, a % 60, tzMin >= 0 ? '-' : '+', a / 60,
           a % 60);
  setenv("TZ", tz, 1);
  tzset();
}

void adoptClock(int64_t serverMs) {
  if (serverMs <= 0) return;
  const int64_t mine = nowMs();
  if (mine && llabs(mine - serverMs) < 3000) return;
  timeval tv = {(time_t)(serverMs / 1000), (suseconds_t)((serverMs % 1000) * 1000)};
  settimeofday(&tv, nullptr);
  trace::add("clock set from server (was %lld ms off)", mine ? (long long)(serverMs - mine) : 0LL);
}

void changed() { changeCount++; }

void becomeLinked(const String& tok, JsonObjectConst device) {
  token = tok;
  devId = device["id"] | devId;
  devName = device["name"] | devName.c_str();
  devUser = device["user"] | devUser.c_str();
  state = Link::Linked;
  pendingToken = code = "";
  saveLink();
  nextSync = 0;
  trace::add("linked as device %d", devId);
  changed();
}

// ---------------------------------------------------------------- pairing
String candidate;  // the code offered to the server; shown once it is accepted

void requestCode() {
  char c[8];
  snprintf(c, sizeof c, "%06lu", (unsigned long)(esp_random() % 1000000));
  candidate = c;
  code = "";
  pairState = N_("Getting a code");
  JsonDocument d;
  d["code"] = candidate;
  d["token_hash"] = sha256hex(pendingToken);
  d["name"] = DEVICE_NAME;
  d["kind"] = "s3";
  String body;
  serializeJson(d, body);
  submit(Job::PairStart, "/v1/pair/start", body, "", true);
  changed();
}

// ---------------------------------------------------------------- sync
String buildSyncBody() {
  JsonDocument d(&psram);
  d["fw"] = FW_VERSION;
  d["board"] = board::name();
  d["fw_code"] = versionCode(FW_VERSION);
  d["battery"] = power::percent();
  if (power::mv() > 0) d["battery_mv"] = power::mv();
  d["usb"] = power::usb();
  d["settings_rev"] = settingsRev;
  {
    JsonObject dg = d["diag"].to<JsonObject>();
    diag::fill(dg);
    dg["board"] = board::name();  // the web shows which screen this is (board.h: found at boot)
    dg["ping"] = PING_MS / 1000;  // how often this firmware asks (45 s): the web says a change arrives within that
    // Remote control (remote.h): where the web EspOS opens the device's page, and its PIN (only while it is on).
    if (remote::enabled()) dg["rc_on"] = 1;
    const String rc = remote::url();
    if (rc.length()) dg["rc_url"] = rc, dg["rc_pin"] = remote::pin();
  }
  JsonArray s = d["settings"].to<JsonArray>();
  const uint32_t now = millis();
  for (auto& kv : dirty) {
    JsonObject o = s.add<JsonObject>();
    o["k"] = kv.first;
    o["v"] = vals[kv.first];
    o["age_s"] = kv.second ? (now - kv.second) / 1000 : 0;
  }
  JsonObject wifi = d["wifi"].to<JsonObject>();
  JsonArray saved = wifi["saved"].to<JsonArray>();
  for (const String& ssid : net::savedNames()) {
    JsonObject o = saved.add<JsonObject>();
    o["ssid"] = ssid;
    const bool up = net::state() == net::State::Connected && net::ssid() == ssid;
    if (up) o["ok"] = true, o["code"] = 3;
  }
  JsonArray acks = wifi["acks"].to<JsonArray>();
  acksSent = wifiAcks;
  for (const Ack& a : acksSent) {
    JsonObject o = acks.add<JsonObject>();
    o["id"] = a.id;
    o["ok"] = a.ok;
    if (a.why.length()) o["why"] = a.why;
  }
  // Packages: volumes, what is installed, the running job (docs/ecosystem.md §6).
  pkg::store::report(d["pkgs"].to<JsonObject>());
  otaSent = otaReports.size();
  if (otaSent) {
    JsonArray reps = d["ota"].to<JsonArray>();
    for (const OtaReport& r : otaReports) {
      JsonObject o = reps.add<JsonObject>();
      o["version"] = r.version;
      o["ok"] = r.ok;
      if (r.why.length()) o["why"] = r.why;
      const int64_t n = nowMs();
      if (r.atMs && n > r.atMs) o["age_s"] = (int32_t)((n - r.atMs) / 1000);
    }
  }
  String body;
  serializeJson(d, body);
  return body;
}

void applySettings(JsonObjectConst server, uint32_t requestAt) {
  bool any = false, settled = false;
  for (JsonPairConst kv : server) {
    const String k = kv.key().c_str();
    auto dk = dirty.find(k);
    if (dk != dirty.end() && dk->second > requestAt) continue;  // changed here while the sync ran
    const String v = kv.value().as<const char*>() ? kv.value().as<const char*>() : "";
    if (vals[k] != v) vals[k] = v, any = true;
  }
  for (auto it = dirty.begin(); it != dirty.end();)
    if (it->second <= requestAt) it = dirty.erase(it), settled = true;
    else ++it;
  if (any || settled || settingsRev != savedRev) saveSettings();  // a LittleFS write on the UI loop: only when something changed (0.5.7)
  if (any) settingsVer++;
}

void applySync(JsonDocument& doc) {
  adoptClock(doc["now_ms"] | (int64_t)0);
  if (doc["user_pkgs"].is<JsonArrayConst>()) pkg::store::setUserPkgs(doc["user_pkgs"].as<JsonArrayConst>());
  if (!doc["tz_min"].isNull()) setTz(doc["tz_min"].as<int>());
  JsonObjectConst device = doc["device"];
  if (!device.isNull()) {
    devId = device["id"] | devId;
    devName = device["name"] | devName.c_str();
    devUser = device["user"] | devUser.c_str();
    saveLink();
  }
  settingsRev = doc["settings_rev"] | settingsRev;
  if (!doc["settings"].isNull()) applySettings(doc["settings"], syncStartedAt);
  lic::fromSync(doc["lic"]);  // the account's licence (lic.h): none = no licensing, as before
  if (!doc["dashboard"].isNull()) {
    const uint32_t sig = dashSig(doc["dashboard"].as<JsonObjectConst>());
    if (sig != dashSigNow || dashDoc.isNull()) {
      dashDoc.set(doc["dashboard"]);
      saveDash();
      dashSigNow = sig;
      dashVer++;
    }
  }
  // WiFi changes made on the web; acknowledged on the next sync.
  wifiAcks.erase(wifiAcks.begin(), wifiAcks.begin() + min(acksSent.size(), wifiAcks.size()));
  acksSent.clear();
  for (JsonObjectConst op : doc["wifi_ops"].as<JsonArrayConst>()) {
    const int id = op["id"] | 0;
    const char* ssid = op["ssid"] | "";
    if (id <= 0 || !*ssid) continue;
    if (!strcmp(op["op"] | "", "remove")) net::forget(ssid);
    else net::addSaved(ssid, op["pass"] | "");
    wifiAcks.push_back({id, true, ""});
  }
  otaReports.erase(otaReports.begin(), otaReports.begin() + min(otaSent, otaReports.size()));
  otaSent = 0;
  JsonObjectConst fw = doc["fw_update"];
  const char* v = fw["version"] | "";
  if (*v && versionCode(v) > versionCode(FW_VERSION)) {  // one S3 image for both boards (board.h)
    offer.version = v;
    offer.url = fw["url"] | "";
    offer.sha256 = fw["sha256"] | "";
    offer.notes = fw["notes"] | "";
    offer.date = fw["date"] | "";
    offer.size = fw["size"] | 0u;
    offer.install = fw["install"] | false;
  } else {
    offer = FwOffer();
  }
  lastSync = doc["now_ms"] | nowMs();
}

int intervalMs() {
  const int m = constrain(settingInt("dash.interval_min", 15), 5, 120);
  return m * 60000;
}

void handle(Response* r) {
  inFlight = false;
  JsonDocument& doc = *r->doc;
  const char* err = doc["error"] | "";
  // A blocked account (2026-11-19): every route answers 403 account_disabled. The token stays, lock.h shows a calm
  // screen over Home, syncing keeps its pace; the first 200 of a sync or a ping ends it.
  if (r->status == 403 && !strcmp(err, "account_disabled")) lock::account(doc["state"] | "blocked");
  else if (r->status == 200 && (r->job == Job::Sync || r->job == Job::Ping)) lock::account(nullptr);
  switch (r->job) {
    case Job::Raw:
      if (rawDoneNow) rawDoneNow(rawCtxNow);
      rawDoneNow = nullptr;
      break;
    case Job::Remote:
      if (remoteCbNow) remoteCbNow(r->status, doc);
      remoteCbNow = nullptr;
      break;
    case Job::PairStart:
      if (state != Link::Pairing) break;
      if (r->status == 200) {
        code = candidate;
        codeExpires = millis() + (doc["expires_s"] | 600) * 1000UL;
        nextPoll = millis() + 5000;
        pairState = N_("Waiting for you to confirm on the web");
      } else if (r->status == 409 && ++codeTries < 4) {
        requestCode();
      } else {
        pairState = N_("Couldn't get a code, trying again in a few seconds");
        codeExpires = millis() + 10000;  // retried by poll()
      }
      changed();
      break;
    case Job::WhoAmI:
      if (state != Link::Pairing) break;
      if (r->status == 200) becomeLinked(pendingToken, doc["device"]);
      else if (r->status == 401 && strcmp(err, "not_linked")) code = "", codeExpires = 0;  // the code died
      nextPoll = millis() + 5000;
      break;
    case Job::PhotoWhere:
      if (r->location.length()) submit(Job::PhotoGet, "", "", "", false, r->location);
      else photoQueue.erase(photoQueue.begin());  // not rendered yet: the web renders on save
      break;
    case Job::PhotoGet:
      if (r->data && r->len) photo::save(photoNow, r->data, r->len);
      heap_caps_free(r->data);
      if (photoQueue.size()) photoQueue.erase(photoQueue.begin());
      break;
    case Job::Fetch:
      fetchActive = false;
      if (r->location.length() && fetchNow.hops < 3) {
        // Storage redirect: follow without the token, ahead of the queue.
        FetchReq next = fetchNow;
        next.url = r->location;
        next.hops++;
        fetchQueue.push_front(next);
      } else if (fetchNow.cb) {
        fetchNow.cb(fetchNow.ctx, r->status, r->doc, r->data, r->len);
      }
      heap_caps_free(r->data);
      break;
    case Job::Ping:
      if (r->status == 200) {
        const int64_t rev = doc["rev"] | (int64_t)0;
        const bool revNew = rev > settingsRev && rev != pingRevActed;
        const bool fwNew = (doc["fw"] | false) && (!fwActedAt || millis() - fwActedAt >= PING_FW_GAP_MS);
        if (revNew || fwNew) {
          trace::add("ping: %s, syncing now", revNew ? "settings changed on the web" : "firmware news");
          if (revNew) led::receive();  // the LED's soft green: something came from the web
          if (revNew) pingRevActed = rev;
          if (fwNew) fwActedAt = millis() | 1;
          syncNow();
        }
      } else if (r->status == 401 && !strcmp(err, "unauthorized")) {
        syncNow();  // the token was refused: the sync hears it too and unlinks
      } else if (r->status == 404 || r->status == 429 || r->status >= 500) {
        // No such route (a server from before the ping), too many calls, a server fault: ask again much later.
        nextPing = millis() + (r->status == 404 ? 15 * 60000UL : 5 * 60000UL);
      }
      break;
    case Job::Sync:
      nextPing = millis() + PING_MS;  // the sync brought everything: the next ping PING_MS from now
      if (r->status == 200) {
        const uint32_t dashVerBefore = dashVer;
        applySync(doc);
        if (claim::pending()) claim::claimed();  // someone entered the code: this unit is theirs
        planPhotos();
        pkg::store::afterSync();  // reconcile s3.pkgs (install, move, remove what the web asked)
        failures = 0;
        lastErr = "";
        nextSync = millis() + intervalMs();
        trace::add("sync ok: dashboard %s, settings rev %lld",
                   doc["dashboard"].isNull() ? "none" : dashVer != dashVerBefore ? "new" : "same", (long long)settingsRev);
      } else if (r->status == 401 && !strcmp(err, "unauthorized")) {
        trace::add("sync: token refused, unlinking");
        unlink();
      } else if (r->status == 401 && !strcmp(err, "not_linked") && claim::pending()) {
        nextSync = millis() + 5000;  // waiting for its owner to enter the code (claim.h): asked every 5 s
      } else if (r->status == 403 && !strcmp(err, "account_disabled")) {
        lastErr = T_("Your account is locked");
        nextSync = millis() + intervalMs();  // the normal pace: it may open again any time
      } else {
        failures++;
        lastErr = r->status < 0 ? String(T_("Couldn't reach the server")) : i18n::fmt(N_("Server error {0}"), String(r->status));
        nextSync = millis() + min<uint32_t>(failures * 60000UL, 600000UL);
      }
      changed();
      break;
  }
  delete r->doc;
  delete r;
}

// Whether to ping now (UI loop). Holds back while a screen that asked for the connection's RAM is open, when the
// periodic sync is due within the minute anyway, during a package download, and when only a fresh connection would do
// and internal RAM is short.
bool pingReady(uint32_t now) {
  if (state != Link::Linked || (int32_t)(now - nextPing) < 0) return false;
  if (pingHold) {
    if (!os::atHome()) return false;
    pingHold = false;  // back at Home: whatever asked for the RAM is closed
  }
  const bool keptOpen = keepOpen;
  if ((int32_t)(nextSync - now) < (int32_t)PING_MS || pkg::store::busy() ||
      (!keptOpen && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < PING_MIN_INTERNAL)) {
    nextPing = now + PING_MS;
    return false;
  }
  return true;
}

}  // namespace

// TLS buffers (16 KB records, certificate chains) go to PSRAM: internal RAM is what WiFi and the
// UI's DMA bands need, and a handshake failed for want of one 16 KB block. Small allocations stay
// internal (fast); either falls back to the other.
// Every TLS allocation goes to PSRAM now, the small ones too: a handshake makes hundreds of them
// (certificates, big numbers), which took 15-25 KB of internal RAM per connection. Measured on the board
// 2026-11-17: a store download right after boot took internal RAM to 0 KB, so downloads broke off and
// the AES engine could not get its own small internal buffers. Internal only when PSRAM has nothing left.
void* tlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
}
void tlsFree(void* p) { heap_caps_free(p); }

// ---------------------------------------------------------------- public
String hwid(String* mac4) {
  static String id, m4;
  if (!id.length()) {
    uint8_t m[6] = {};
    esp_read_mac(m, ESP_MAC_BASE);
    const String mac = hex(m, 6);
    id = sha256hex("espos-hw:" + mac), m4 = mac.substring(8);
  }
  if (mac4) *mac4 = m4;
  return id;
}
String tokenSha8() { return token.length() ? sha256hex(token).substring(0, 8) : String(); }

void releaseConnection() {
  dropKeep = true;
  pingHold = true;  // no ping reopens it while the screen that asked is open (pingReady)
}
bool connectionOpen() { return keepOpen; }

void begin() {
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  if (!LittleFS.begin(true)) trace::add("LittleFS mount failed");
  loadLink();
  loadSettings();
  // The time zone the device last had (sys.tz_min), so a device outside GMT+7 has its own time from boot.
  if (vals.count("sys.tz_min") && vals["sys.tz_min"].length()) setTz(vals["sys.tz_min"].toInt());
  loadDash();
  jobs = xQueueCreate(4, sizeof(Request*));
  results = xQueueCreate(4, sizeof(Response*));
  // TLS needs a deep stack; it lives in PSRAM (internal RAM is what TLS itself needs, and this
  // task never touches flash). Core 0, next to the WiFi stack, away from the UI.
  xTaskCreatePinnedToCoreWithCaps(worker, "desk", 12288, nullptr, 3, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  trace::add("desk: %s, %u settings, dashboard %s", state == Link::Linked ? "linked" : "not linked",
             (unsigned)vals.size(), dashDoc.isNull() ? "none" : "cached");
}

void poll() {
  Response* r;
  while (xQueueReceive(results, &r, 0) == pdTRUE) handle(r);
  if (inFlight || net::state() != net::State::Connected) {
    if (remoteWaiting && (net::state() != net::State::Connected || state != Link::Linked)) {
      remoteWaiting = false;  // no network: the remote loop hears it at once and tries again later
      JsonDocument none;
      if (remoteCb) remoteCb(-1, none);
    }
    return;
  }
  const uint32_t now = millis();
  if (state == Link::Linked && rawFn) {  // the person is waiting for the dog: first
    Request* q = new Request{Job::Raw, "", "", token, true, ""};
    q->raw = rawFn, q->rawCtx = rawCtx;
    rawDoneNow = rawDone, rawCtxNow = rawCtx;
    rawFn = nullptr;
    inFlight = true;
    inFlightJob = Job::Raw;
    xQueueSend(jobs, &q, portMAX_DELAY);
    return;
  }
  const bool ping = pingReady(now);
  // Remote control first, but after a remote request the others take their turn.
  const bool others =
      fetchQueue.size() || photoQueue.size() || (state == Link::Linked && (int32_t)(now - nextSync) >= 0) || ping;
  if (state == Link::Linked && remoteWaiting && !(lastRemote && others)) {
    remoteWaiting = false;
    lastRemote = true;
    remoteCbNow = remoteCb;
    submit(Job::Remote, "/v1/device/remote", remoteBody, token, true);
    remoteBody = String();
    return;
  }
  lastRemote = false;
  if (state == Link::Pairing) {
    // codeExpires: when the live code dies, or (no code) when to ask for one again.
    if (code.length() && (int32_t)(now - codeExpires) < 0) {
      if ((int32_t)(now - nextPoll) >= 0) submit(Job::WhoAmI, "/v1/device/whoami", "", pendingToken, false);
    } else if ((int32_t)(now - codeExpires) >= 0) {
      codeTries = 0;
      requestCode();
    }
  } else if (state == Link::Linked && fetchQueue.size()) {
    startFetch();
  } else if (state == Link::Linked && photoQueue.size()) {
    photoNow = photoQueue.front();
    submit(Job::PhotoWhere, "/v1/device/photos/" + String(photoNow.id) + "/" + photoNow.dim + "/" + photoNow.p, "", token,
           false);
  } else if (state == Link::Linked && (int32_t)(now - nextSync) >= 0) {
    syncStartedAt = now;
    submit(Job::Sync, "/v1/device/sync", buildSyncBody(), token, true);
    changed();
  } else if (ping) {
    nextPing = now + PING_MS;
    submit(Job::Ping, "/v1/device/ping", "", token, false);
  }
}

Link link() { return state; }
String deviceName() { return devName; }
String user() { return devUser; }

void startPairing() {
  if (state == Link::Linked) return;
  uint8_t b[32];
  esp_fill_random(b, sizeof b);
  pendingToken = hex(b, 32);
  code = "";
  codeTries = 0;
  codeExpires = millis();
  pairState = N_("Getting a code");
  state = Link::Pairing;
  changed();
}

void stopPairing() {
  if (state != Link::Pairing) return;
  state = Link::None;
  pendingToken = code = "";
  changed();
}

String pairCode() { return code; }
String pairUrl() { return String(ESPOS_WEB) + "/app#/pair?code=" + code; }
const char* pairStatus() { return i18n::tr(pairState.c_str()); }  // kept in English (N_), shown translated

void unlink() {
  token = "";
  devId = 0;
  devName = devUser = "";
  state = Link::None;
  saveLink();
  changed();
}

bool rawSend(RawFn fn, RawDone done, void* ctx) {
  if (rawFn || state != Link::Linked || net::state() != net::State::Connected) return false;
  rawFn = fn, rawDone = done, rawCtx = ctx;
  return true;
}

bool remoteSend(const String& body, RemoteCb cb) {
  if (remoteWaiting || state != Link::Linked || net::state() != net::State::Connected) return false;
  remoteBody = body;
  remoteCb = cb;
  remoteWaiting = true;
  return true;
}

void syncNow() {
  nextSync = millis();
  failures = 0;
}
bool syncing() { return inFlight && inFlightJob != Job::Ping && state == Link::Linked; }  // a ping is not a sync
int64_t lastSyncMs() { return lastSync; }
const char* lastError() { return lastErr.c_str(); }
uint32_t changes() { return changeCount; }

JsonObjectConst dash() { return dashDoc.as<JsonObjectConst>(); }
uint32_t dashVersion() { return dashVer; }

String setting(const char* key, const char* def) {
  auto it = vals.find(key);
  return it == vals.end() || !it->second.length() ? String(def) : it->second;
}

int settingInt(const char* key, int def) {
  auto it = vals.find(key);
  return it == vals.end() || !it->second.length() ? def : it->second.toInt();
}

void setSetting(const char* key, const String& value) {
  if (vals[key] == value) return;
  vals[key] = value;
  dirty[key] = millis() | 1;
  saveSettings();
  settingsVer++;
}

uint32_t settingsVersion() { return settingsVer; }

bool provision(const char* json, String& err) {
  JsonDocument d(&psram);
  if (deserializeJson(d, json) != DeserializationError::Ok) return err = "bad json", false;
  const char* tok = d["token"] | "";
  if (strlen(tok) != 64) return err = "bad token", false;
  api = d["api"] | ESPOS_API;
  adoptClock(d["now_ms"] | (int64_t)0);
  for (JsonObjectConst w : d["wifi"].as<JsonArrayConst>()) {
    const char* ssid = w["ssid"] | "";
    if (*ssid) net::addSaved(ssid, w["pass"] | "");
  }
  if (!d["settings"].isNull()) {
    settingsRev = d["settings_rev"] | (int64_t)0;
    dirty.clear();
    applySettings(d["settings"], 0);
    settingsVer++;
  }
  becomeLinked(tok, d["device"]);
  if (d["claim"].is<const char*>()) claim::set(d["claim"], d["claim_url"] | "");  // a unit for sale (claim.h)
  return true;
}

const FwOffer& fwOffer() { return offer; }

void fetch(const String& pathOrUrl, bool bytes, FetchCb cb, void* ctx, bool urgent) {
  // Something the user just asked for goes ahead of pictures still loading.
  if (urgent) fetchQueue.push_front({pathOrUrl, bytes, urgent, cb, ctx, 0});
  else fetchQueue.push_back({pathOrUrl, bytes, urgent, cb, ctx, 0});
}

void cancelFetches(void* ctx) {
  for (auto it = fetchQueue.begin(); it != fetchQueue.end();) it = it->ctx == ctx ? fetchQueue.erase(it) : std::next(it);
  if (fetchActive && fetchNow.ctx == ctx) fetchNow.cb = nullptr;
}

void reportOta(const String& version, bool ok, const String& why) {
  otaReports.push_back({version, why, ok, nowMs()});
}

int64_t nowMs() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec > 1700000000 ? (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 : 0;
}

}  // namespace desk
