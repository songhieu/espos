// Host stand-ins for the hardware and network modules, so the UI runs on a computer.
// Data: sim/data/settings.json (device settings), sim/data/dash.json (dashboard), sim/data/net/*
// (answers to desk::fetch by path), sim/data/photos/*.jpg (full-screen pictures).
#include <ArduinoJson.h>
#include <lvgl.h>
#include <sys/time.h>

#include <cstdarg>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "appset.h"
#include "preset.h"
#include "desk.h"
#include "hal/board.h"
#include "jpeg.h"
#include "lcd.h"
#include "net.h"
#include "ota.h"
#include "photo.h"
#include "power.h"
#include "remote.h"
#include "sound.h"
#include "touch.h"
#include "mic.h"
#include <atomic>
#include <chrono>
#include <thread>
#include "trace.h"
#include "WiFi.h"
#include <curl/curl.h>
#include <set>

SerialClass Serial;
EspClass ESP;
WiFiClass WiFi;

extern uint32_t simNow;
uint32_t millis() { return simNow; }

// SIM_CLOCK="2026-11-18 06:29:50" (or the script's "clock" command): the wall clock starts there and moves with
// the simulated time (alarms, the dog assistant's greetings by the hour). Unset: the computer's clock.
long long simClockBase = -1;  // epoch seconds at simNow 0 (-1: the real clock)
extern "C" time_t time(time_t* out) {
  static bool init = false;
  if (!init) {
    init = true;
    if (const char* e = getenv("SIM_CLOCK")) {
      struct tm t = {};
      if (sscanf(e, "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) >= 5) {
        t.tm_year -= 1900, t.tm_mon -= 1, t.tm_isdst = -1;
        simClockBase = (long long)mktime(&t) - simNow / 1000;
      }
    }
  }
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const time_t v = simClockBase >= 0 ? (time_t)(simClockBase + simNow / 1000) : tv.tv_sec;
  if (out) *out = v;
  return v;
}
uint32_t micros() { return simNow * 1000; }
void delay(uint32_t) {}
long random(long max) { return rand() % max; }
long random(long a, long b) { return a + rand() % (b - a); }

// SIM_DATA=dir runs the simulator on another copy of sim/data (one per tester).
std::string simDir = getenv("SIM_DATA") ? getenv("SIM_DATA") : "sim/data";

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return "";
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// ---------------------------------------------------------------- memory
// The live simulator at the board's speed (sim/main.cpp --live, sim/live.cpp). Off: the script simulator as before.
bool simBoard = false;
// heap_caps: every block counted by kind. With simBoard, the free sizes are the board's: PSRAM_KB is what the
// board has for heap_caps once the OS runs (calibrated on the board: 1642 KB free when the run game starts from
// the memory card, 2026-11-16), internal RAM what the board had free at Home. Past it, a warning (the block is
// still given: the numbers are a model, not the board's allocator).
size_t simPsramKB = 0, simInternalKB = 0;
namespace {
// Made on first use, never destroyed: other files' static constructors allocate before this file's run.
std::mutex& heapLock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::unordered_map<void*, std::pair<size_t, bool>>& blockMap() {  // size, PSRAM
  static auto* b = new std::unordered_map<void*, std::pair<size_t, bool>>;
  return *b;
}
#define heapMtx heapLock()
#define blocks blockMap()
size_t usedPs = 0, usedIn = 0, peakPs = 0;
bool psWarned = false;
void* counted(void* p, size_t n, uint32_t caps) {
  if (!p) return p;
  const bool ps = caps & MALLOC_CAP_SPIRAM;
  std::lock_guard<std::mutex> g(heapMtx);
  blocks[p] = {n, ps};
  (ps ? usedPs : usedIn) += n;
  peakPs = std::max(peakPs, usedPs);
  if (simBoard && ps && simPsramKB && usedPs > simPsramKB * 1024 && !psWarned) {
    psWarned = true;
    fprintf(stderr, "[sim] PSRAM: %zu KB in use, more than the board has (%zu KB): this would fail on the board\n",
            usedPs / 1024, simPsramKB);
  }
  return p;
}
void uncount(void* p) {
  std::lock_guard<std::mutex> g(heapMtx);
  auto it = blocks.find(p);
  if (it == blocks.end()) return;
  (it->second.second ? usedPs : usedIn) -= it->second.first;
  blocks.erase(it);
}
}  // namespace
void* heap_caps_malloc(size_t n, uint32_t caps) { return counted(malloc(n), n, caps); }
void* heap_caps_calloc(size_t n, size_t s, uint32_t caps) { return counted(calloc(n, s), n * s, caps); }
void* heap_caps_realloc(void* p, size_t n, uint32_t caps) {
  bool ps = caps & MALLOC_CAP_SPIRAM;
  if (p) {
    std::lock_guard<std::mutex> g(heapMtx);
    auto it = blocks.find(p);
    if (it != blocks.end()) ps = it->second.second;
  }
  uncount(p);
  void* q = realloc(p, n);
  return counted(q, n, ps ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
}
void heap_caps_free(void* p) {
  if (!p) return;
  uncount(p);
  free(p);
}
size_t heap_caps_get_free_size(uint32_t caps) {
  if (!simBoard) return 180 * 1024;
  std::lock_guard<std::mutex> g(heapMtx);
  const size_t cap = (caps & MALLOC_CAP_SPIRAM ? simPsramKB : simInternalKB) * 1024,
               used = caps & MALLOC_CAP_SPIRAM ? usedPs : usedIn;
  return cap > used ? cap - used : 0;
}
size_t heap_caps_get_largest_free_block(uint32_t caps) {
  // SIM_PSRAM_BLOCK_KB: a fragmented PSRAM (wake.cpp sizes a recording to it); off the board model: room enough
  if (const char* e = getenv("SIM_PSRAM_BLOCK_KB"); e && (caps & MALLOC_CAP_SPIRAM)) return (size_t)atoi(e) * 1024;
  if (!simBoard) return caps & MALLOC_CAP_SPIRAM ? 4 * 1024 * 1024 : 100 * 1024;
  return heap_caps_get_free_size(caps) * 9 / 10;
}
void simHeap(size_t& ps, size_t& in, size_t& peak) {
  std::lock_guard<std::mutex> g(heapMtx);
  ps = usedPs, in = usedIn, peak = peakPs;
}

// ---------------------------------------------------------------- trace
namespace trace {
bool quiet = false;
void begin() {}
void add(const char* fmt, ...) {
  if (quiet) return;
  static std::mutex m;  // the live simulator's decoder thread traces too
  char line[1024];
  va_list a;
  va_start(a, fmt);
  vsnprintf(line, sizeof line, fmt, a);
  va_end(a);
  std::lock_guard<std::mutex> g(m);
  fprintf(stderr, "[%6u] %s\n", simNow, line);
}
void dump() {}
void setEcho(bool) {}
}  // namespace trace

// ---------------------------------------------------------------- desk
namespace desk {
// The dog assistant's turn: on a thread like the board's network worker (src/pet/talk.cpp runs wake/tools/simnet.py
// against the live API when SIM_DEVICE_TOKEN is set: wake/tools/simpair.py links the simulator); without it, no network.
bool rawSend(RawFn fn, RawDone done, void* ctx) {
  if (!getenv("SIM_DEVICE_TOKEN") && !getenv("SIM_FAKE_ASSIST")) return false;
  std::thread([=] {
    fn(ctx, nullptr, "api.espos.dev", "");
    done(ctx);
  }).detach();
  return true;
}
namespace {
JsonDocument dashDoc;
uint32_t dashVer = 1, settingsVer = 1, changeCount = 0;
std::map<String, String> vals;
struct Pending {
  String url;
  bool bytes;
  FetchCb cb;
  void* ctx;
  uint32_t at;
};
std::vector<Pending> pending;
}  // namespace

void begin() {
  JsonDocument s;
  if (!deserializeJson(s, readFile(simDir + "/settings.json")))
    for (JsonPairConst kv : s.as<JsonObjectConst>()) vals[kv.key().c_str()] = kv.value().as<const char*>();
  deserializeJson(dashDoc, readFile(simDir + "/dash.json"));
}
void simSync();
extern bool simSyncAsked;
extern uint32_t simSyncAt;
void poll() {
  static const uint32_t syncMs = getenv("SIM_SYNC_MS") ? atoi(getenv("SIM_SYNC_MS")) : 5000;
  if (getenv("SIM_SYNC_URL") && (simSyncAsked || simNow - simSyncAt >= syncMs)) simSync();
  // Answers arrive 300 ms after the request, like a quick network.
  for (size_t i = 0; i < pending.size(); i++) {
    if (simNow - pending[i].at < 300) continue;
    Pending p = pending[i];
    pending.erase(pending.begin() + i);
    String path = p.url;
    if (path.startsWith("https://")) path = path.substring(path.indexOf('/', 8));
    const int q = path.indexOf('?');
    String file = path;
    file.replace("/", "_");
    file.replace("?", "_");
    file.replace("=", "_");
    std::string body = readFile(simDir + "/net/" + file.c_str());
    if (body.empty() && q >= 0) {
      String f2 = path.substring(0, q);
      f2.replace("/", "_");
      body = readFile(simDir + "/net/" + f2.c_str());
      if (!body.empty()) file = f2;
    }
    const std::string st = readFile(simDir + "/net/" + file.c_str() + ".status");  // e.g. "402": a refusal, its body as the answer
    const int status = st.empty() ? 200 : atoi(st.c_str());
    if (body.empty()) {
      trace::add("sim fetch %s -> 404 (no sim/data/net/%s)", p.url.c_str(), file.c_str());
      if (p.cb) p.cb(p.ctx, 404, nullptr, nullptr, 0);
      return;
    }
    if (p.bytes) {
      if (p.cb) p.cb(p.ctx, status, nullptr, (const uint8_t*)body.data(), body.size());
    } else {
      JsonDocument d;
      deserializeJson(d, body);
      if (p.cb) p.cb(p.ctx, status, &d, nullptr, 0);
    }
    return;
  }
}
Link link() { return Link::Linked; }
String deviceName() { return "EspOS S3"; }
String user() { return "ban@example.com"; }
void startPairing() {}
void stopPairing() {}
String pairCode() { return "482913"; }
String pairUrl() { return "https://espos.dev/app#/pair?code=482913"; }
const char* pairStatus() { return "Chờ xác nhận trên web"; }
void unlink() {}
// SIM_SYNC_URL=http://127.0.0.1:PORT/v1/device/sync: a sync against a local mock of device_sync (tools/mock_sync.py,
// docs/app-platform-plan.md lane 1e): the changed settings and appset's secret report go up, the answer's settings
// (not changed here meanwhile) and secrets come down, as desk.cpp does. Every SIM_SYNC_MS (5000) of sim time, and at
// once after syncNow().
std::set<String> simDirty;
bool simSyncAsked = false;
uint32_t simSyncAt = 0;
size_t curlText(char* p, size_t n, size_t k, void* out) { return ((std::string*)out)->append(p, n * k), n * k; }
void simSync() {
  const char* url = getenv("SIM_SYNC_URL");
  if (!url) return;
  simSyncAsked = false, simSyncAt = simNow;
  JsonDocument d;
  JsonArray s = d["settings"].to<JsonArray>();
  const std::set<String> sent = simDirty;
  for (const String& k : sent) {
    JsonObject o = s.add<JsonObject>();
    o["k"] = k, o["v"] = vals[k], o["age_s"] = 0;
  }
  appset::syncBody(d.as<JsonObject>());
  preset::report(d.as<JsonObject>());
  std::string body, answer;
  serializeJson(d, body);
  CURL* c = curl_easy_init();
  curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/json");
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
  curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlText);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &answer);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
  const CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(h);
  curl_easy_cleanup(c);
  JsonDocument a;
  if (rc != CURLE_OK || status != 200 || deserializeJson(a, answer)) {
    trace::add("sim sync: failed (curl %d, HTTP %ld)", (int)rc, status);
    return;
  }
  for (const String& k : sent) simDirty.erase(k);
  bool any = false;
  for (JsonPairConst kv : a["settings"].as<JsonObjectConst>()) {
    const String k = kv.key().c_str(), v = kv.value() | "";
    if (simDirty.count(k) || vals[k] == v) continue;
    vals[k] = v, any = true;
    trace::add("sim sync: setting %s = %s (from the server)", k.c_str(), v.c_str());
  }
  if (any) settingsVer++;
  if (a["preset"].is<JsonObjectConst>()) preset::fromSync(a["preset"]);  // as desk.cpp applySync
  appset::fromSync(a.as<JsonObjectConst>());
  trace::add("sim sync: ok, %u settings up, %u down", (unsigned)sent.size(), (unsigned)a["settings"].size());
}
void syncNow() { simSyncAsked = true; }
bool syncing() { return false; }
int64_t lastSyncMs() { return getenv("SIM_NO_SYNC") ? 0 : nowMs() - 6 * 60000; }  // SIM_NO_SYNC: none yet since power-on
const char* lastError() { return ""; }
uint32_t changes() { return changeCount; }
JsonObjectConst dash() { return dashDoc.as<JsonObjectConst>(); }
uint32_t dashVersion() { return dashVer; }
void simDash(const std::string& path) {  // the script's "dash": a new dashboard, as after a sync
  deserializeJson(dashDoc, readFile(path.empty() ? simDir + "/dash.json" : path));
  dashVer++;
}
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
  settingsVer++;
  simDirty.insert(key);
  // Private calendar links are secrets: never in a log, even the simulator's.
  if (!strcmp(key, "dash.ics_url")) return trace::add("setting %s = (%u chars, hidden)", key, (unsigned)value.length());
  trace::add("setting %s = %s", key, value.length() > 120 ? (value.substring(0, 120) + "...").c_str() : value.c_str());
}
void touchSetting(const char* key) {
  if (vals.count(key)) simDirty.insert(key);
}

void dropSettings(const char* prefix) {
  const size_t n = strlen(prefix);
  bool any = false;
  for (auto it = vals.begin(); it != vals.end();)
    if (n && !strncmp(it->first.c_str(), prefix, n)) it = vals.erase(it), any = true;
    else ++it;
  if (any) settingsVer++, trace::add("settings %s* dropped", prefix);
}
uint32_t settingsVersion() { return settingsVer; }
#if ESPOS_ONBOARD
bool hadSettings() { return !getenv("SIM_FRESH"); }  // SIM_FRESH=1: a device that never had settings (the first boot)
#endif
bool provision(const char*, String& err) { return err = "sim", false; }
String hwid(String* mac4) {  // a fixed board in the simulator: "espos-hw:7cdfa1b2c3d4"
  if (mac4) *mac4 = "c3d4";
  return "5a8e0c3b1f2d4e6a7b9c0d1e2f3a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c";
}
String tokenSha8() { return "1a2b3c4d"; }
static FwOffer offer = [] {  // SIM_FW_USB=espos-s3-0.6.0: a newer version for table v2 only (docs/partitions.md)
  FwOffer o;
  if (getenv("SIM_FW_USB")) o.usb = getenv("SIM_FW_USB");
  return o;
}();
const FwOffer& fwOffer() { return offer; }
void reportOta(const String&, bool, const String&) {}
int64_t nowMs() {
  time(nullptr);  // SIM_CLOCK: the simulated wall clock (time() above)
  if (::simClockBase >= 0) return (::simClockBase + simNow / 1000) * 1000 + simNow % 1000;
  timeval tv;
  gettimeofday(&tv, nullptr);
  return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
void fetch(const String& pathOrUrl, bool bytes, FetchCb cb, void* ctx, bool urgent) {
  if (urgent) pending.insert(pending.begin(), {pathOrUrl, bytes, cb, ctx, simNow});
  else pending.push_back({pathOrUrl, bytes, cb, ctx, simNow});
}
void cancelFetches(void* ctx) {
  for (auto& p : pending)
    if (p.ctx == ctx) p.cb = nullptr;
}
}  // namespace desk

// ---------------------------------------------------------------- net
namespace net {
static std::vector<Ap> aps = {{"Home", -48, true, true}, {"Nha Ben Canh", -71, true, false}, {"Cafe 5G", -80, false, false}};
void begin() {}
void poll() {}
void scan() {}
bool scanning() { return false; }
const std::vector<Ap>& results() { return aps; }
uint32_t resultsAt() { return 1; }
bool simWifi = !getenv("SIM_WIFI_OFF");  // SIM_WIFI_OFF=1: no WiFi until the WiFi page connects (claim.h's first boot)
void connect(const String&, const String&) { simWifi = true; }
void connectSaved(const String&) { simWifi = true; }
void disconnect() {}
void resumeAuto() {}
void forget(const String&) {}
bool isSaved(const String& s) { return s == "Home"; }
std::vector<String> savedNames() { return {"Home"}; }
void addSaved(const String&, const String&) {}
int savedCount() { return 1; }
State state() { return simWifi ? State::Connected : State::Idle; }
String ssid() { return "Home"; }
String ip() { return "192.168.1.23"; }
int rssi() { return -48; }
const char* error() { return ""; }
int lastReason() { return 0; }
uint32_t changes() { return 1; }
int bars(int r) { return r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : 1; }
}  // namespace net

// ---------------------------------------------------------------- power, sound, ota
namespace power {
void poll() {}
int mv() { return 4020; }
int percent() { return 95; }
bool usb() { return false; }
bool full() { return false; }
}  // namespace power
// Weak: the live simulator's speaker (sim/live.cpp) takes over.
namespace sound {
__attribute__((weak)) bool begin() { return true; }
__attribute__((weak)) void setVolume(int) {}
__attribute__((weak)) void click() {}
__attribute__((weak)) void chime() {}
__attribute__((weak)) void blip() {}
__attribute__((weak)) void blipEnd() {}
__attribute__((weak)) void test() {}
__attribute__((weak)) void alarmBeep() { trace::add("sim: beep"); }
// Streams are drained at once; with SIM_WAV=path every streamed sample is appended to that raw file
// (16 kHz mono s16le), to listen to what the speaker would play.
__attribute__((weak)) void stream(Pull pull, void* ctx) {
  if (!pull) return;
  static FILE* wav = getenv("SIM_WAV") ? fopen(getenv("SIM_WAV"), "wb") : nullptr;
  short buf[320];
  for (int n; (n = pull(buf, 320, ctx)) > 0;)
    if (wav) fwrite(buf, 2, n, wav), fflush(wav);
}
__attribute__((weak)) void stopStream() {}
__attribute__((weak)) bool streaming() { return false; }
// The microphone: only with SIM_MIC=file (16 kHz mono s16le raw, or a WAV) played into it in real time, then silence.
__attribute__((weak)) void* micChannel() { return getenv("SIM_MIC") ? (void*)1 : nullptr; }
__attribute__((weak)) bool outputSince(uint32_t) { return false; }
}  // namespace sound

// The RGB LED (src/led.cpp): drawn by the live build's window (sim/live.cpp), nothing headless.
__attribute__((weak)) void simLed(uint8_t, uint8_t, uint8_t) {}

// SIM_MIC=live: the computer's microphone (sim/live.cpp, the live build only).
__attribute__((weak)) bool simLiveMic(mic::Sink, bool) { return false; }
// SIM_ECHO=dB (the live build): what the speaker played comes back into the mic file's blocks, as on the board where
// the speaker sits next to the mic (sim/live.cpp simEcho); the script build has no sound.
__attribute__((weak)) void simEcho(int16_t*, int) {}
// The live simulator plays sound in real time (PawBoop's loops and music only there: sfx.cpp).
__attribute__((weak)) bool simLiveSound() { return false; }
__attribute__((weak)) void simWakeEvent(const char*, float) {}

namespace mic {
namespace {
std::atomic<Sink> simSink{nullptr};
std::atomic<bool> simOn{false};
std::atomic<int> simNext{0};  // opens so far: each one plays the next file of SIM_MIC (a:b:c), then silence
std::thread* simThread = nullptr;
}  // namespace
bool available() { return getenv("SIM_MIC") != nullptr; }
bool open(Sink s) {
  if (!available()) return false;
  if (!strcmp(getenv("SIM_MIC"), "live")) {
    simOn = simLiveMic(s, true);
    return simOn;
  }
  simSink = s;
  if (!simOn) simNext++;
  simOn = true;
  if (!simThread) simThread = new std::thread([] {
      std::vector<std::string> files;
      std::stringstream ss(getenv("SIM_MIC"));
      for (std::string f; std::getline(ss, f, ':');) files.push_back(f);
      FILE* f = nullptr;
      int playing = 0;
      int16_t b[160];
      auto t = std::chrono::steady_clock::now();  // real time, like the codec (the sim's millis() is its own)
      for (;;) {
        if (simNext != playing) {  // the mic was opened again: the next file from its start
          playing = simNext;
          if (f) fclose(f);
          f = playing - 1 < (int)files.size() ? fopen(files[playing - 1].c_str(), "rb") : nullptr;
          char hdr[4] = {};
          if (f && fread(hdr, 1, 4, f) == 4 && !memcmp(hdr, "RIFF", 4)) fseek(f, 44, SEEK_SET);
          else if (f) fseek(f, 0, SEEK_SET);
          trace::add("sim mic: %s", f ? files[playing - 1].c_str() : "silence");
        }
        const int n = f && simOn ? (int)fread(b, 2, 160, f) : 0;
        for (int i = n; i < 160; i++) b[i] = 0;
        simEcho(b, 160);
        const Sink k = simSink;
        if (simOn && k) k(b, 160);
        t += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(t);
      }
    });
  return true;
}
void close() {
  if (getenv("SIM_MIC") && !strcmp(getenv("SIM_MIC"), "live")) simLiveMic(nullptr, false);
  simOn = false;
}
bool isOpen() { return simOn; }
Stats stats() { return {0, -96, -96, 0, 0}; }
}  // namespace mic
namespace ota {
void begin() {}
void poll() {}
void start() {}
bool running() { return false; }
const char* status() { return ""; }
bool installing() { return false; }
void beginInstallScreen() {}
Progress progress() { return Progress{Stage::Idle, 0, 0, 0, 0}; }
static const String noErr;
const String& lastError() { return noErr; }
const String& lastErrorVersion() { return noErr; }
void clearError() {}
}  // namespace ota

// ---------------------------------------------------------------- photo
namespace photo {
namespace {
std::vector<std::string> files() {
  std::vector<std::string> out;
  for (int i = 1; i <= 12; i++) {
    const std::string p = simDir + "/photos/" + std::to_string(i) + ".jpg";
    if (!readFile(p).empty()) out.push_back(p);
  }
  return out;
}
std::map<std::string, lv_image_dsc_t*> decoded;
}  // namespace
String File::path() const { return ""; }
// Picture sets (the saver): sim/data/photos/<id % count>.jpg, as the board's copies, nothing kept here.
uint8_t* readCopy(int id, int, int, size_t& len) {
  const auto fs = files();
  len = 0;
  if (fs.empty() || id <= 0) return nullptr;
  const std::string jpg = readFile(fs[(id - 1) % fs.size()]);  // id N: photos/N.jpg (as the editor's tiles)
  uint8_t* out = (uint8_t*)heap_caps_malloc(jpg.size(), MALLOC_CAP_SPIRAM);
  if (out) memcpy(out, jpg.data(), len = jpg.size());
  return out;
}
uint16_t* decodeJpg(const uint8_t* jpg, size_t len, int w, int h) { return jpeg::cover(jpg, len, w, h); }
std::vector<File> wanted() { return {}; }
void prune(const std::vector<File>&) {}
bool have(const File&) { return false; }
bool save(const File&, const uint8_t*, size_t) { return false; }
uint32_t version() { return 0; }
bool show(lv_obj_t*, const String&, int, int) { return false; }
void unpinAll() {}
int screenCount(int, int) { return files().size(); }
const lv_image_dsc_t* screen(uint32_t n, int w, int h, bool) {
  const auto fs = files();
  if (fs.empty()) return nullptr;
  const std::string key = fs[n % fs.size()] + "#" + std::to_string(w) + "x" + std::to_string(h);
  auto it = decoded.find(key);
  if (it != decoded.end()) return it->second;
  const std::string jpg = readFile(fs[n % fs.size()]);
  uint16_t* px = jpeg::cover((const uint8_t*)jpg.data(), jpg.size(), w, h);
  if (!px) return nullptr;
  lv_image_dsc_t* d = new lv_image_dsc_t();
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_RGB565;
  d->header.w = w;
  d->header.h = h;
  d->header.stride = w * 2;
  d->data_size = w * h * 2;
  d->data = (const uint8_t*)px;
  decoded[key] = d;
  return d;
}
}  // namespace photo

// ---------------------------------------------------------------- touch and display
namespace touch {
lv_indev_t* dev = nullptr;
uint32_t last = 0;
bool begin(lv_display_t*) { return true; }
lv_indev_t* indev() { return dev; }
uint32_t lastTouchMs() { return last; }
void setPrivate(bool) {}
i2c_master_bus_handle_t i2cBus() { return nullptr; }
void inject(int, int, int, int, uint32_t) {}
Watcher watcher = nullptr;
void setWatcher(Watcher w) { watcher = w; }
}  // namespace touch

namespace lcd {
extern lv_display_t* simDisp;
int simBacklight = 70;
lv_display_t* begin() { return simDisp; }
void setRotation(lv_display_rotation_t r) {
  const bool land = r == LV_DISPLAY_ROTATION_90 || r == LV_DISPLAY_ROTATION_270;
  lv_display_set_resolution(simDisp, land ? hal::uiH() : hal::uiW(), land ? hal::uiW() : hal::uiH());
}
void setBacklight(int p) { simBacklight = p; }
bool flipped() { return false; }
void setFlipped(bool) {}
void stats(uint32_t& f, uint32_t& u) { f = u = 0; }
void logInvalid(bool) {}
}  // namespace lcd

// ---------------------------------------------------------------- remote control (the board's: src/remote.cpp)
namespace remote {
namespace {
bool simOn = false;
}
void begin() {}
void poll() {}
bool enabled() { return simOn; }
void setEnabled(bool on) { simOn = on; }
String pin() { return simOn ? "482913" : ""; }
String url() { return simOn ? "http://192.168.1.37" : ""; }
bool watching() { return false; }
}  // namespace remote
