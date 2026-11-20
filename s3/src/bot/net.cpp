#include "net.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <sys/time.h>

#include <algorithm>
#include <vector>

#include "portal.h"
#include "store.h"
#include "../trace.h"

namespace bot {
namespace net {
namespace {

enum class St { Idle, Trying, Up, Portal };
St st = St::Idle;
size_t tryIdx = 0;
uint32_t tryAt = 0, downAt = 0, idleAt = 0, portalAt = 0, upAt = 0;
bool ntpStarted = false, portalOn = false;
DNSServer* dns = nullptr;
WebServer* web = nullptr;
std::vector<std::string> networks;  // the last scan
uint32_t scanAt = 0;
constexpr uint32_t TRY_MS = 15000;

// The setup page's join: the page answers at once ("connecting", it refreshes itself), the join starts a moment
// later (so that answer reaches the phone before the radio follows the home network's channel), and its result is
// on the page and the OLED. The network is saved only once it worked; the setup WiFi stays until the phone has
// seen the result (or 3 minutes).
portal::Join join = portal::Join::None;
portal::Why why = portal::WHY_NONE;
std::string joinSsid, joinPass;
uint32_t joinStartAt = 0, joinAt = 0, okAt = 0, okSeenAt = 0, failAt = 0, tryStart = 0;
int tries = 0, pwFails = 0, nfFails = 0;
std::vector<std::pair<std::string, int>> channels;  // the last scan: each network's channel
// The saved networks' tries: their name, refusals of the password (a router whose password changed), full rounds
// that failed; when it went offline (0: online).
std::string trySsid, lostSsid, badSsid;
int pwRefusals = 0, failedRoundsN = 0;
uint32_t offlineAt = 1;  // from boot until the first join
volatile int lastReason = 0;
volatile uint32_t lastReasonAt = 0;
constexpr uint32_t JOIN_MS = 30000;

void onDisconnect(arduino_event_id_t, arduino_event_info_t info) {
  lastReason = info.wifi_sta_disconnected.reason;
  lastReasonAt = millis() | 1;
}

void send(const std::string& html) {
  web->sendHeader("Cache-Control", "no-store");
  web->send(200, "text/html; charset=utf-8", html.c_str());
}

void statusPage() {
  if (join == portal::Join::None) return send(portal::page(networks, ""));
  if (join == portal::Join::Ok && !okSeenAt) okSeenAt = millis() | 1;
  send(portal::status(join, joinSsid, why, networks));
}

void save() {
  std::string ssid, pass;
  portal::form(web->arg("s").c_str(), web->arg("t").c_str(), web->arg("p").c_str(), web->arg("l").c_str(), ssid, pass);
  if (ssid.empty()) return send(portal::page(networks, "Chưa chọn WiFi · No WiFi chosen"));
  joinSsid = ssid, joinPass = pass;
  join = portal::Join::Joining, why = portal::WHY_NONE;
  joinStartAt = (millis() + 800) | 1, okSeenAt = 0;
  joinAt = 0;  // nothing of an earlier try counts for this one
  lastReason = 0;
  lastReasonAt = 0;
  statusPage();
}

void redirect() {
  web->sendHeader("Location", "http://192.168.4.1/", true);
  web->send(302, "text/plain", "");
}

void scan() {
  const int n = WiFi.scanComplete();
  if (n >= 0) {
    networks.clear(), channels.clear();
    for (int i = 0; i < n && networks.size() < 20; i++) {  // the strongest first (the scan's order)
      const std::string s = WiFi.SSID(i).c_str();
      if (!s.empty() && std::find(networks.begin(), networks.end(), s) == networks.end())
        networks.push_back(s), channels.push_back({s, (int)WiFi.channel(i)});
    }
    WiFi.scanDelete();
  }
  if (n != WIFI_SCAN_RUNNING && millis() - scanAt > 20000) scanAt = millis(), WiFi.scanNetworks(true);
}

void openPortal() {
  if (portalOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName().c_str(), store::apPass().c_str());  // WPA2: the password is on the OLED (text and QR)
  delay(100);
  dns = new DNSServer();
  dns->start(53, "*", WiFi.softAPIP());
  web = new WebServer(80);
  web->on("/", HTTP_GET, statusPage);
  web->on("/status", HTTP_GET, statusPage);
  web->on("/save", HTTP_POST, save);
  for (const char* u : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt",
                        "/library/test/success.html", "/success.txt"})
    web->on(u, HTTP_GET, redirect);
  web->onNotFound(redirect);
  web->begin();
  portalOn = true;
  portalAt = millis();
  scanAt = 0;
  join = portal::Join::None;
  trace::add("portal: %s on", apName().c_str());
}

void closePortal() {
  if (!portalOn) return;
  web->stop();
  dns->stop();
  delete web;
  delete dns;
  web = nullptr, dns = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalOn = false;
  trace::add("portal: off");
}

void tryNext() {
  const auto nets = store::wifi();
  if (nets.empty()) return;
  if (tryIdx >= nets.size()) tryIdx = 0;
  lastReason = 0;
  lastReasonAt = 0;
  trySsid = nets[tryIdx].ssid;
  int ch = 0;  // its channel from the last scan (0: any)
  for (const auto& c : channels)
    if (c.first == trySsid) ch = c.second;
  WiFi.begin(nets[tryIdx].ssid.c_str(), nets[tryIdx].pass.c_str(), ch);
  trace::add("wifi: trying \"%s\" (channel %d)", trySsid.c_str(), ch);
  tryAt = millis() | 1;
  st = St::Trying;
}

// Is a saved network in the last scan? (With the setup WiFi up, the saved ones are tried only when one is there.)
bool savedVisible() {
  for (const auto& w : store::wifi())
    for (const auto& c : channels)
      if (c.first == w.ssid) return true;
  return false;
}

}  // namespace

std::string mac12() {
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char s[13];
  snprintf(s, sizeof s, "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  return s;
}

std::string apName() {
  std::string m = mac12();
  for (char& c : m) c = (char)toupper((unsigned char)c);
  return "EspOS-" + m.substr(8);
}

// The setup page's join, step by step (from loop()).
void startTry(uint32_t now) {
  lastReason = 0;
  lastReasonAt = 0;
  WiFi.disconnect(false);
  // On the network's own channel (from the last scan): the setup WiFi moves there first, so the radio does not hop
  // between two channels during the handshake (on a test board, 2026-11-20, the first join on another channel
  // timed out in the 4-way handshake, reason 15, with the right password).
  int ch = 0;
  for (const auto& c : channels)
    if (c.first == joinSsid) ch = c.second;
  if (ch > 0 && ch != WiFi.channel()) {
    WiFi.softAP(apName().c_str(), store::apPass().c_str(), ch);
    delay(150);
  }
  WiFi.begin(joinSsid.c_str(), joinPass.c_str(), ch);
  tryStart = now | 1;
  trace::add("portal: joining \"%s\" (try %d, channel %d)", joinSsid.c_str(), tries, ch);
}

void joinStep() {
  const uint32_t now = millis();
  if (join != portal::Join::Joining) return;
  if (joinStartAt) {
    if ((int32_t)(now - joinStartAt) < 0) return;  // the page's answer goes first
    joinStartAt = 0;
    joinAt = now | 1;
    tries = 1, pwFails = nfFails = 0;
    startTry(now);
    return;
  }
  if (!joinAt) return;
  if (WiFi.status() == WL_CONNECTED) {
    join = portal::Join::Ok;
    okAt = now;
    store::addWifi(joinSsid, joinPass);  // saved now that it works
    trace::add("portal: \"%s\" joined in %lu ms (try %d)", joinSsid.c_str(), (unsigned long)(now - joinAt), tries);
    return;
  }
  // A try ended (a disconnect after it started): counted by why. The IDF's reasons: 2 auth expired, 14 MIC failure,
  // 15 4-way handshake timeout, 202 auth failed, 204 handshake timeout = the password, or a refusal some routers give
  // the first join after a reboot (the S3 on the owner's router: 202 first), so twice before saying so; 201,
  // 210-212 = no such network.
  const int r = lastReason;
  if (r == 8 || r == 36) lastReason = 0;  // our own leave (8) and the Arduino core's own retry (36): not a try's end
  else if (r && lastReasonAt && (int32_t)(lastReasonAt - tryStart) > 0) {
    lastReason = 0;
    if (r == 2 || r == 14 || r == 15 || r == 202 || r == 204) pwFails++;
    else if (r == 201 || (r >= 210 && r <= 212)) nfFails++;
    trace::add("portal: try %d ended, reason %d", tries, r);
    if (pwFails < 2 && nfFails < 2 && tries < 4 && now - joinAt < JOIN_MS) {
      tries++;
      startTry(now);
      return;
    }
  }
  if (pwFails >= 2 || nfFails >= 2 || tries >= 4 || now - joinAt > JOIN_MS) {
    why = pwFails >= 2 ? portal::WHY_PASSWORD : nfFails >= 2 ? portal::WHY_NOT_FOUND : portal::WHY_OTHER;
    join = portal::Join::Failed;
    failAt = now;
    WiFi.disconnect(false);  // the setup WiFi stays
    trace::add("portal: \"%s\" failed (tries %d, password-like %d, not found %d, last reason %d)", joinSsid.c_str(),
               tries, pwFails, nfFails, r);
  }
}

void begin() {
  WiFi.onEvent(onDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(("espos-bot-" + mac12().substr(8)).c_str());
  WiFi.setAutoReconnect(true);
  if (store::wifi().empty()) openPortal();
  else tryNext();
}

void loop() {
  if (portalOn) {
    dns->processNextRequest();
    web->handleClient();
    if (join != portal::Join::Joining) scan();
    joinStep();
  }
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && st != St::Up) {
    st = St::Up;
    upAt = millis();
    offlineAt = 0;
    pwRefusals = failedRoundsN = 0;
    badSsid.clear();
    const std::string s = WiFi.SSID().c_str();
    lostSsid = s;
    trace::add("wifi: up on \"%s\", %s", s.c_str(), WiFi.localIP().toString().c_str());
    // the one that worked goes first (its password as saved)
    for (const auto& w : store::wifi())
      if (w.ssid == s) store::addWifi(w.ssid, w.pass);
    if (!ntpStarted) configTime(0, 0, "pool.ntp.org", "time.google.com"), ntpStarted = true;
  }
  if (!up && st == St::Up) {
    st = St::Idle;
    downAt = idleAt = millis();
    offlineAt = millis() | 1;
    trace::add("wifi: down");
  }
  // the setup WiFi closes once the phone has seen "connected" (+15 s), or 3 minutes after; a join of a saved network
  // (not the page's) closes it 20 s after
  if (portalOn && st == St::Up) {
    const uint32_t now = millis();
    const bool pageJoin = join == portal::Join::Ok;
    if (pageJoin ? (okSeenAt && now - okSeenAt > 15000) || now - okAt > 180000 : now - upAt > 20000) closePortal();
  }
  if (portalOn && join == portal::Join::Joining) return;  // the page's join runs alone
  if (st == St::Trying) {
    // a try that ended with a refusal of the password, three times over (some routers refuse the first join after
    // a reboot): the router's password changed. Reasons 8 and 36 are our own leave and the core's retry.
    const int r = lastReason;
    const bool ended = r && r != 8 && r != 36 && lastReasonAt && (int32_t)(lastReasonAt - tryAt) > 0;
    if (ended && (r == 2 || r == 14 || r == 15 || r == 202 || r == 204) && ++pwRefusals >= 3 && badSsid != trySsid) {
      badSsid = trySsid;
      trace::add("wifi: \"%s\" refuses the password (reason %d)", trySsid.c_str(), r);
    }
    if (ended || millis() - tryAt > TRY_MS) {
      lastReason = 0;
      if (++tryIdx >= store::wifi().size()) {  // every saved network failed: a pause, then round again
        tryIdx = 0;
        failedRoundsN++;
        st = St::Idle;
        idleAt = millis();
      } else {
        tryNext();
      }
    }
  }
  if (st == St::Portal) return;  // radioOff()
  // Offline: the saved networks again every 5 s; with the setup WiFi up, every 30 s, only when one is in the scan
  // and no phone is on the setup WiFi (a join moves the radio's channel and would drop the phone).
  if (st == St::Idle && !store::wifi().empty()) {
    const bool apBusy = portalOn && (WiFi.softAPgetStationNum() > 0 || !savedVisible());
    if (!apBusy && millis() - idleAt > (portalOn ? 30000u : 5000u)) tryNext();
  }
}

bool online() { return WiFi.status() == WL_CONNECTED; }
bool portal() { return portalOn; }
std::string ssid() { return online() ? std::string(WiFi.SSID().c_str()) : std::string(); }
void startPortal() { openPortal(); }
uint32_t offlineMs() {  // (offlineAt is |1: it can be a millisecond ahead)
  return offlineAt && (int32_t)(millis() - offlineAt) > 0 ? millis() - offlineAt : 0;
}
std::string lostName() {
  if (!lostSsid.empty()) return lostSsid;
  const auto w = store::wifi();
  return w.empty() ? std::string() : w.front().ssid;
}
std::string badPassword() { return badSsid; }
int failedRounds() { return failedRoundsN; }
bool simulate(const char*) { return false; }  // the simulator's network tests (net_sim.cpp)
portal::Join joinState() { return join; }
portal::Why joinWhy() { return why; }
std::string joinName() { return joinSsid; }
uint32_t joinFailedAgo() { return join == portal::Join::Failed ? millis() - failAt : UINT32_MAX; }
void reconnect() {
  tryIdx = 0;
  if (!online()) tryNext();
}

void radioOff() {
  closePortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  st = St::Portal;  // nothing tried again until a restart
}

bool timeValid() { return time(nullptr) > 1700000000; }
int64_t nowMs() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec > 1700000000 ? (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 : 0;
}
void adoptClock(int64_t ms) {
  if (ms < 1700000000000LL) return;
  const int64_t have = nowMs();
  if (have && llabs(have - ms) < 2000) return;  // NTP is better than a server's answer time
  timeval tv = {(time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000)};
  settimeofday(&tv, nullptr);
}

}  // namespace net
}  // namespace bot
