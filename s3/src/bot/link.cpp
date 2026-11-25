#include "link.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include "../lic.h"
#include "../trace.h"
#include "config.h"
#include "net.h"
#include "spk.h"
#include "store.h"
#include "transport.h"

namespace bot {
namespace link {
namespace {

// ---------------------------------------------------------------- the HTTPS task
enum class Job : uint8_t { Sync, Ping, Ask, Warm, Ota, AskEnd, PairStart, WhoAmI };  // AskEnd: a streamed voice ended
struct Req {
  Job job;
  std::string path, body, query;
  const uint8_t* data = nullptr;  // Ask: the WAV (the caller's)
  uint32_t len = 0;
  bool voice = false;  // Ask: the reply's voice streams back (bot.voice 1)
};
struct Res {
  Job job;
  int status = 0;
  bool voice = false;
  std::string body;
  uint32_t ms = 0;
};
QueueHandle_t jobs = nullptr, results = nullptr;
volatile bool busy = false, askBusy = false, otaBusy = false;
volatile int otaPct = -1;

struct OtaJob {
  std::string version, url, sha;
  uint32_t size = 0;
  bool now = false;  // fw_update.install: the person pressed "Cập nhật" on the web
};
OtaJob ota;

volatile bool streamCancel = false;  // the turn was cancelled: its voice stops streaming

void postHead(const std::string& json) {  // a voice answer's words, at once (the voice still streams)
  Res* h = new Res{Job::Ask, 200, true, json, 0};
  xQueueSend(results, &h, portMAX_DELAY);
}

void task(void*) {
  transport::begin();
  for (;;) {
    Req* q = nullptr;
    if (xQueueReceive(jobs, &q, portMAX_DELAY) != pdTRUE || !q) continue;
    Res* r = new Res{q->job};
    const uint32_t t0 = millis();
    const std::string api = store::api();
    switch (q->job) {
      case Job::Sync:
        r->status = transport::request("POST", api + "/v1/device/sync", (const uint8_t*)q->body.data(), q->body.size(),
                                       "application/json", r->body, 25000, true);
        break;
      case Job::Ping:
        r->status = transport::request("GET", api + "/v1/device/ping", nullptr, 0, nullptr, r->body, 10000, true);
        break;
      case Job::Warm:  // a fresh connection: the server drops idle ones well before the next ping
        r->status = transport::warm(api) ? 200 : -1;
        break;
      case Job::Ask: {
        const std::string url = api + "/v1/device/assist?" + (q->voice ? "stream=1&say_rate=16000" : "voice=0") + q->query;
        transport::dropIfIdle(20000);  // idle 20 s: maybe one the server closed, a fresh one at once
        streamCancel = false;
        bool voice = false;
        r->status = transport::ask(url, q->data, q->len, voice, r->body, postHead, &streamCancel);
        // Failed within 4 s: it never reached the model (a connection the server had closed): once more on a fresh
        // one. A later failure may be a turn the server took: not sent twice.
        if (r->status < 0 && millis() - t0 < 4000 && !streamCancel) {
          trace::add("ask: %d after %lu ms, again", r->status, (unsigned long)(millis() - t0));
          transport::drop();
          r->status = transport::ask(url, q->data, q->len, voice, r->body, postHead, &streamCancel);
        }
        if (voice) r->job = Job::AskEnd;  // the words went first (postHead): this is the end of the voice
        break;
      }
      case Job::Ota:
        r->status = transport::ota(ota.url, ota.size, ota.sha, &otaPct);
        break;
      case Job::PairStart:
        r->status = transport::request("POST", api + "/v1/pair/start", (const uint8_t*)q->body.data(), q->body.size(),
                                       "application/json", r->body, 15000, true, "");
        break;
      case Job::WhoAmI:
        r->status = transport::request("GET", api + "/v1/device/whoami", nullptr, 0, nullptr, r->body, 10000, true,
                                       q->query.c_str());
        break;
      case Job::AskEnd:
        break;
    }
    r->ms = millis() - t0;
    if (q->job != Job::Ping || r->status != 200)
      trace::add("http job %d -> %d, %u bytes, %lu ms, heap %u KB", (int)q->job, r->status, (unsigned)r->body.size(),
                 (unsigned long)r->ms, (unsigned)(ESP.getFreeHeap() / 1024));
    delete q;
    xQueueSend(results, &r, portMAX_DELAY);
  }
}

bool submit(Req* q) {
  if (busy) return delete q, false;
  busy = true;
  if (q->job == Job::Ask) askBusy = true;
  if (q->job == Job::Ota) otaBusy = true;
  xQueueSend(jobs, &q, portMAX_DELAY);
  return true;
}

// ---------------------------------------------------------------- the main loop's side
State st = State::None;
bool isBlocked = false, everSynced = false, markedValid = false, askWanted = false, askDone = false, warmWanted = false;
bool askPending = false, otaWanted = false;
Req* askReq = nullptr;  // the question waiting for the HTTPS task
// self-pairing: a token made here, offered with a 6-digit code (POST /v1/pair/start {code, token_hash}); the web
// confirms the code, then GET /v1/device/whoami with that token answers 200 (until then 401 not_linked)
std::string pendingToken, code, candidate;
uint32_t codeExpires = 0, nextPoll = 0;
int codeTries = 0;
bool paired = false;
uint32_t nextSync = 0, nextPing = 0, failures = 0, syncStartedAt = 0, lastSyncAt = 0;
Answer answer;
ui::Weather wx;
ui::Info info;
uint32_t infoVer = 0;
std::string lunarNow;
std::vector<std::pair<int, bool>> wifiAcks, acksSent;
constexpr uint32_t PING_MS = 45000, SYNC_MS = 15 * 60000;

std::string hexOf(const uint8_t* b, int n) {
  static const char* H = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < n; i++) s += H[b[i] >> 4], s += H[b[i] & 15];
  return s;
}

void requestCode() {
  char c[8];
  snprintf(c, sizeof c, "%06u", (unsigned)(esp_random() % 1000000));
  candidate = c;
  code.clear();
  uint8_t d[32];
  mbedtls_sha256((const uint8_t*)pendingToken.data(), pendingToken.size(), d, 0);
  JsonDocument j;
  j["code"] = candidate;
  j["token_hash"] = hexOf(d, 32);
  j["name"] = "EspOS Bot";
  j["kind"] = "bot";
  std::string body;
  serializeJson(j, body);
  submit(new Req{Job::PairStart, "", body});
}

int versionCode(const char* v) {
  const char* p = strrchr(v, '-');
  p = p ? p + 1 : v;
  int a = 0, b = 0, c = 0;
  sscanf(p, "%d.%d.%d", &a, &b, &c);
  return a * 10000 + b * 100 + c;
}

std::string syncBody() {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["fw_code"] = versionCode(FW_VERSION);
  d["board"] = BOT_BOARD;
  d["kind"] = "bot";
  d["usb"] = true;
  d["settings_rev"] = store::rev();
  JsonObject dg = d["diag"].to<JsonObject>();
  dg["board"] = BOT_BOARD;
  dg["ping"] = PING_MS / 1000;
  dg["uptime_s"] = millis() / 1000;
  dg["heap"] = ESP.getFreeHeap();
  dg["psram"] = ESP.getFreePsram();
  if (net::online()) dg["rssi"] = WiFi.RSSI();
  JsonArray s = d["settings"].to<JsonArray>();
  const uint32_t now = millis();
  for (auto& kv : store::dirty()) {
    JsonObject o = s.add<JsonObject>();
    o["k"] = kv.first;
    o["v"] = store::setting(kv.first);
    o["age_s"] = kv.second ? (now - kv.second) / 1000 : 0;
  }
  JsonObject w = d["wifi"].to<JsonObject>();
  JsonArray saved = w["saved"].to<JsonArray>();
  const std::string on = net::ssid();
  for (const auto& n : store::wifi()) {
    JsonObject o = saved.add<JsonObject>();
    o["ssid"] = n.ssid;
    if (n.ssid == on) o["ok"] = true, o["code"] = 3;
  }
  JsonArray acks = w["acks"].to<JsonArray>();
  acksSent = wifiAcks;
  for (auto& a : acksSent) {
    JsonObject o = acks.add<JsonObject>();
    o["id"] = a.first, o["ok"] = a.second;
  }
  std::string body;
  serializeJson(d, body);
  return body;
}

void applySync(JsonDocument& doc) {
  net::adoptClock(doc["now_ms"] | (int64_t)0);
  JsonObjectConst dev = doc["device"];
  if (!dev.isNull()) store::setLink(store::token(), store::api(), dev["id"] | store::deviceId(), dev["name"] | "");
  store::settle(syncStartedAt);
  for (JsonPairConst kv : doc["settings"].as<JsonObjectConst>())
    store::set(kv.key().c_str(), kv.value().as<const char*>() ?: "", false);
  if (!doc["tz_min"].isNull()) store::set("sys.tz_min", std::to_string(doc["tz_min"].as<int>()), false);
  store::setRev(doc["settings_rev"] | store::rev());
  lic::fromSync(doc["lic"]);
  JsonObjectConst w = doc["weather"];
  if (!w.isNull() && !w["t"].isNull()) {
    wx.valid = true;
    wx.t = w["t"] | 0, wx.hi = w["hi"] | wx.t, wx.lo = w["lo"] | wx.t;
    wx.icon = w["icon"] | "cloud";
    wx.desc = w["desc"] | "";
    wx.city = w["city"] | "";
    // "06:01" -> 361; the server sends them since 2026-11-25 (the night picture at the real sunset)
    auto mins = [](const char* s) { return s && strlen(s) == 5 && s[2] == ':' ? atoi(s) * 60 + atoi(s + 3) : -1; };
    wx.rise = mins(w["rise"] | ""), wx.set = mins(w["set"] | "");
  }
  if (doc["dashboard"]["lunar"].is<const char*>()) lunarNow = doc["dashboard"]["lunar"].as<const char*>();
  // the info screens' data, only when the rotation holds them (admin-users, 2026-11-20): dashboard.gold/fx/news,
  // weather.hum
  JsonObjectConst dash = doc["dashboard"];
  info = ui::Info();
  infoVer++;
  info.hum = w["hum"] | -1;
  for (JsonVariantConst h : dash["news"]["top"].as<JsonArrayConst>())
    if (h.is<const char*>() && info.headlines.size() < 8) info.headlines.push_back(h.as<const char*>());
  for (JsonObjectConst g : dash["gold"]["items"].as<JsonArrayConst>()) {
    if (!info.goldSell.empty()) break;  // the first (SJC)
    char b[16], s[16];
    snprintf(b, sizeof b, "%g", g["b"] | 0.0), snprintf(s, sizeof s, "%g", g["s"] | 0.0);
    info.goldBuy = b, info.goldSell = s;
  }
  for (JsonObjectConst x : dash["fx"]["items"].as<JsonArrayConst>()) {
    if (strcmp(x["c"] | "", "USD")) continue;
    const long v = (long)(x["s"] | 0.0);
    char t[24];
    if (v >= 1000) snprintf(t, sizeof t, "%ld.%03ld", v / 1000, v % 1000);
    else snprintf(t, sizeof t, "%ld", v);
    info.usd = t;
  }
  wifiAcks.erase(wifiAcks.begin(), wifiAcks.begin() + std::min(acksSent.size(), wifiAcks.size()));
  acksSent.clear();
  bool wifiChanged = false;
  for (JsonObjectConst op : doc["wifi_ops"].as<JsonArrayConst>()) {
    const int id = op["id"] | 0;
    const char* ssid = op["ssid"] | "";
    if (id <= 0 || !*ssid) continue;
    if (!strcmp(op["op"] | "", "remove")) store::forgetWifi(ssid);
    else store::addWifi(ssid, op["pass"] | "");
    wifiAcks.push_back({id, true});
    wifiChanged = true;
  }
  if (wifiChanged) net::reconnect();
  JsonObjectConst fw = doc["fw_update"];
  const char* v = fw["version"] | "";
  if (*v && !strncmp(v, "espos-bot-", 10) && versionCode(v) > versionCode(FW_VERSION) && store::setting("bot.ota") != "off") {
    ota.version = v, ota.url = fw["url"] | "", ota.sha = fw["sha256"] | "", ota.size = fw["size"] | 0u;
    ota.now = fw["install"] | false;
  } else {
    ota = OtaJob();
  }
}

void handle(Res* r) {
  busy = false;
  JsonDocument doc;
  if (r->job != Job::Ask && !r->body.empty()) deserializeJson(doc, r->body);
  const char* err = doc["error"] | "";
  if (r->status == 403 && !strcmp(err, "account_disabled")) isBlocked = true;
  else if (r->status == 200 && (r->job == Job::Sync || r->job == Job::Ping)) isBlocked = false;
  switch (r->job) {
    case Job::Sync:
      nextPing = millis() + PING_MS;
      if (r->status == 200) {
        applySync(doc);
        st = State::Linked;
        if (!store::claimCode().empty()) store::setClaim("", "");  // someone entered the code: this unit is theirs
        everSynced = true;
        failures = 0;
        lastSyncAt = millis();
        nextSync = millis() + SYNC_MS;
        if (!markedValid) esp_ota_mark_app_valid_cancel_rollback(), markedValid = true;  // a new image works
        trace::add("sync ok: rev %lld", (long long)store::rev());
      } else if (r->status == 401 && !strcmp(err, "not_linked")) {
        st = State::Claim;  // waiting for its owner to enter the code
        nextSync = millis() + 5000;
      } else if (r->status == 401) {  // the token was refused (the device was removed on the web): pair again
        trace::add("sync: the token was refused, pairing again");
        store::setLink("", store::api(), 0, "");
        st = State::None;
        nextSync = millis();
      } else {
        failures++;
        nextSync = millis() + std::min<uint32_t>(failures * 60000UL, 600000UL);
      }
      break;
    case Job::Ping:
      if (r->status == 200) {
        const int64_t rev = doc["rev"] | (int64_t)0;
        if (rev > store::rev() || (doc["fw"] | false)) syncNow();
      } else if (r->status == 401) {
        syncNow();
      } else if (r->status == 404 || r->status == 429 || r->status >= 500) {
        nextPing = millis() + 5 * 60000UL;
      }
      break;
    case Job::Ask:
      if (r->voice) busy = true;  // the words came first: the voice still streams on the HTTPS task (AskEnd)
      else askBusy = false;
      if (askWanted) {
        answer.status = r->status, answer.body = r->body, answer.ms = r->ms, answer.voice = r->voice;
        askDone = true;
      }
      askWanted = false;
      break;
    case Job::AskEnd:
      askBusy = false;
      break;
    case Job::Warm:
      break;
    case Job::PairStart:
      if (st != State::Pairing) break;
      if (r->status == 200) {
        code = candidate;
        codeExpires = millis() + (doc["expires_s"] | 600) * 1000UL;
        nextPoll = millis() + 5000;
        trace::add("pair: code ..%s", code.substr(3).c_str());
      } else if (r->status == 409 && ++codeTries < 4) {
        codeExpires = millis();  // a new code at once
      } else {
        codeExpires = millis() + 10000;  // asked again in 10 s
      }
      break;
    case Job::WhoAmI:
      if (st != State::Pairing) break;
      if (r->status == 200) {
        store::setLink(pendingToken, store::api(), doc["device"]["id"] | 0, doc["device"]["name"] | "");
        trace::add("pair: confirmed, device %d", store::deviceId());
        st = State::Linked;
        pendingToken.clear(), code.clear();
        paired = true;
        syncNow();
      } else if (r->status == 401 && strcmp(err, "not_linked")) {
        code.clear(), codeExpires = millis();  // the code died: a new one
      }
      nextPoll = millis() + 5000;
      break;
    case Job::Ota:
      otaBusy = false;
      if (r->status == 200) {
        trace::add("ota: %s written, restarting", ota.version.c_str());
        delay(300);
        ESP.restart();
      }
      trace::add("ota: %s failed (%d)", ota.version.c_str(), r->status);
      otaPct = -1;
      ota = OtaJob();
      break;
  }
  delete r;
}

}  // namespace

void begin() {
  store::begin();
  lic::begin();
  st = store::token().size() == 64 ? (store::claimCode().empty() ? State::Linked : State::Claim) : State::None;
  jobs = xQueueCreate(2, sizeof(Req*));
  results = xQueueCreate(2, sizeof(Res*));
  xTaskCreatePinnedToCore(task, "bot-net", 12288, nullptr, 2, nullptr, 1);
}

void loop() {
  Res* r = nullptr;
  while (xQueueReceive(results, &r, 0) == pdTRUE) handle(r);
  if (busy || !net::online()) return;
  const uint32_t now = millis();
  if (store::token().size() != 64) {  // not provisioned: it pairs by itself (a unit for sale waits for its claim)
    if (!store::claimCode().empty()) return;
#ifdef BOT_PAIR_PAUSED
    return;  // a test build for a server without the bot's pairing yet: no call at all
#endif
    if (st != State::Pairing) {
      uint8_t b[32];
      for (int i = 0; i < 32; i += 4) {
        const uint32_t r = esp_random();
        memcpy(b + i, &r, 4);
      }
      pendingToken = hexOf(b, 32);
      code.clear(), codeTries = 0, codeExpires = now;
      st = State::Pairing;
      trace::add("pair: started");
    }
    if (!code.empty() && (int32_t)(now - codeExpires) < 0) {
      if ((int32_t)(now - nextPoll) >= 0) {
        nextPoll = now + 5000;
        Req* q = new Req{Job::WhoAmI};
        q->query = pendingToken;
        submit(q);
      }
    } else if ((int32_t)(now - codeExpires) >= 0) {
      requestCode();
    }
    return;
  }
  if (askPending) {  // the person's question goes before anything else
    askPending = false;
    submit(askReq), askReq = nullptr;
    return;
  }
  if (warmWanted) {
    warmWanted = false;
    submit(new Req{Job::Warm});
    return;
  }
  if (otaWanted) {
    otaWanted = false;
    submit(new Req{Job::Ota});
    return;
  }
  if ((int32_t)(now - nextSync) >= 0) {
    syncStartedAt = now;
    nextSync = now + 60000;  // until the answer sets it
    submit(new Req{Job::Sync, "", syncBody()});
    return;
  }
  if (st == State::Linked && (int32_t)(now - nextPing) >= 0) {
    nextPing = now + PING_MS;
    submit(new Req{Job::Ping});
  }
}

State state() { return st; }
std::string pairCode() { return st == State::Pairing ? code : std::string(); }
std::string pairUrl() { return code.empty() ? std::string() : std::string(ESPOS_WEB) + "/app#/pair?code=" + code; }
bool justPaired() {
  const bool p = paired;
  paired = false;
  return p;
}
bool blocked() { return isBlocked; }
void syncNow() { nextSync = millis(); }
bool synced() { return everSynced; }
uint32_t lastSyncAgoS() { return everSynced ? (millis() - lastSyncAt) / 1000 : UINT32_MAX; }

std::string hwid() {
  const std::string m = "espos-hw:" + net::mac12();
  uint8_t d[32];
  mbedtls_sha256((const uint8_t*)m.data(), m.size(), d, 0);
  return hexOf(d, 32);
}
std::string mac4() { return net::mac12().substr(8); }

bool provision(const char* json, std::string& err) {
  JsonDocument d;
  if (deserializeJson(d, json)) return err = "bad json", false;
  const char* tok = d["token"] | "";
  if (strlen(tok) != 64) return err = "bad token", false;
  net::adoptClock(d["now_ms"] | (int64_t)0);
  for (JsonObjectConst w : d["wifi"].as<JsonArrayConst>()) {
    const char* ssid = w["ssid"] | "";
    if (*ssid) store::addWifi(ssid, w["pass"] | "");
  }
  if (!d["settings"].isNull()) {
    for (JsonPairConst kv : d["settings"].as<JsonObjectConst>())
      store::set(kv.key().c_str(), kv.value().as<const char*>() ?: "", false);
    store::setRev(d["settings_rev"] | (int64_t)0);
  }
  store::setLink(tok, d["api"] | ESPOS_API, d["device"]["id"] | 0, d["device"]["name"] | "");
  if (d["claim"].is<const char*>()) store::setClaim(d["claim"].as<const char*>(), d["claim_url"] | "");
  st = store::claimCode().empty() ? State::Linked : State::Claim;
  pendingToken.clear(), code.clear();
  net::reconnect();
  syncNow();
  return true;
}

bool weather(ui::Weather& w) {
  w = wx;
  return wx.valid;
}
std::string lunar() { return lunarNow; }
const ui::Info& dashInfo() { return info; }
uint32_t dashVersion() { return infoVer; }

bool ask(const uint8_t* wav, uint32_t len, const std::string& query, bool voice) {
  if (askBusy || askPending || store::token().size() != 64) return false;
  delete askReq;
  askReq = new Req{Job::Ask};
  askReq->data = wav, askReq->len = len, askReq->query = query, askReq->voice = voice;
  askWanted = true, askDone = false, askPending = true;
  return true;
}
void cancelAsk() {
  askWanted = false;
  streamCancel = true;
  if (askPending) askPending = false, delete askReq, askReq = nullptr;
}
bool takeAnswer(Answer& a) {
  if (!askDone) return false;
  askDone = false;
  a = answer;
  return true;
}
void warm() {
  if (!busy) submit(new Req{Job::Warm});
  else warmWanted = true;
}

void install() {
  if (!ota.url.empty() && !otaBusy) otaWanted = true;
}
bool updating() { return otaBusy || otaWanted; }
int updatePct() { return otaPct; }
std::string offerVersion() { return ota.version; }
bool offerNow() { return ota.now; }

}  // namespace link
}  // namespace bot
