// EspOS Bot simulator: src/bot/net.h without a radio. The computer's network is the bot's WiFi: a saved network (the
// setup page, ESPOS:PROVISION's wifi, CMD:WIFIADD) counts as joined 1.5 s later. With none saved, the setup page is
// served at http://localhost:8099 (the board's own WiFi "EspOS-XXXX" + captive page), the same page (portal.cpp).
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <Arduino.h>
#include <WiFi.h>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "../../src/bot/net.h"
#include "../../src/bot/portal.h"
#include "../../src/bot/store.h"
#include "../../src/trace.h"

SimWiFi WiFi;

namespace bot {
int64_t simClockMs();  // sim.cpp: the computer's clock, or the headless script's

namespace net {
namespace {

bool portalOn = false, up = false, radio = true;
uint32_t upJoinAt = 0, upAt = 0;
std::string joined;
// The router, played (console NETSIM down / up / newpass): off, or with a new password the saved one lacks.
bool routerOn = true;
std::string routerPass;  // "" = it takes any password (as before NETSIM newpass)
uint32_t offlineAt = 1, retryAt = 0;
int pwRefusals = 0, failedRoundsN = 0;
std::string lostSsid, badSsid;
int portalPort = 8099;

// The page's "Connect" is done on the main loop (the store is not thread-safe): the server thread waits for it.
// The join is played: the password "wrong" fails as a wrong password, the network "Nope" is not found, anything
// else joins 1.5 s later.
std::mutex mu;
std::condition_variable cv;
bool formWaiting = false, formDone = false;
std::string fS, fT, fP, fL;
portal::Join join = portal::Join::None;
portal::Why why = portal::WHY_NONE;
std::string joinSsid, joinPass;
uint32_t joinAt = 0, failAt = 0;
const std::vector<std::string> NETS = {"Home WiFi", "Office 5G", "Nope"};

std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16), i += 2;
    else o += s[i];
  }
  return o;
}

std::string field(const std::string& body, const char* k) {
  const std::string key = std::string(k) + "=";
  size_t p = 0;
  while ((p = body.find(key, p)) != std::string::npos) {
    if (p == 0 || body[p - 1] == '&') {
      const size_t e = body.find('&', p);
      return urlDecode(body.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size()));
    }
    p += key.size();
  }
  return "";
}

void serve() {
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET, a.sin_port = htons(portalPort), a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, (sockaddr*)&a, sizeof a) || listen(s, 4)) {
    fprintf(stderr, "[bot-sim] the setup page could not use port %d\n", portalPort);
    return;
  }
  for (;;) {
    const int c = accept(s, nullptr, nullptr);
    if (c < 0) continue;
    std::string req;
    char buf[4096];
    for (;;) {  // the headers, then the body by its length
      const ssize_t n = recv(c, buf, sizeof buf, 0);
      if (n <= 0) break;
      req.append(buf, n);
      const size_t he = req.find("\r\n\r\n");
      if (he == std::string::npos) continue;
      size_t cl = 0;
      const size_t p = req.find("Content-Length:");
      if (p != std::string::npos && p < he) cl = strtoul(req.c_str() + p + 15, nullptr, 10);
      if (req.size() >= he + 4 + cl) break;
    }
    std::string html;
    if (!portalOn) {
      html = "<!doctype html><meta charset=utf-8><p>The bot's setup WiFi is off (hold BOOT 5 s).</p>";
    } else if (req.rfind("POST /save", 0) == 0) {
      const std::string body = req.substr(req.find("\r\n\r\n") + 4);
      std::unique_lock<std::mutex> l(mu);
      fS = field(body, "s"), fT = field(body, "t"), fP = field(body, "p"), fL = field(body, "l");
      formWaiting = true, formDone = false;
      cv.wait_for(l, std::chrono::seconds(5), [] { return formDone; });
      html = join == portal::Join::None ? portal::page(NETS, "Chưa chọn WiFi · No WiFi chosen")
                                        : portal::status(join, joinSsid, why, NETS);
    } else {
      std::lock_guard<std::mutex> l(mu);
      html = join == portal::Join::None ? portal::page(NETS, "") : portal::status(join, joinSsid, why, NETS);
    }
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                             std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n";
    send(c, head.data(), head.size(), 0);
    send(c, html.data(), html.size(), 0);
    close(c);
  }
}

}  // namespace

bool simulate(const char* what);

std::string mac12() { return "de00ad5e7e38"; }  // made up (DE:AD)
std::string apName() { return "EspOS-7E38"; }

void begin() {
  if (const char* p = getenv("BOT_SIM_PORTAL_PORT")) portalPort = atoi(p);
  if (const char* r = getenv("BOT_SIM_ROUTER")) simulate(r);  // e.g. "down": the bot starts with its router off
  std::thread(serve).detach();
  if (store::wifi().empty()) startPortal();
  else upJoinAt = millis() | 1;
}

void loop() {
  {
    std::lock_guard<std::mutex> l(mu);
    if (formWaiting) {
      std::string ssid, pass;
      portal::form(fS, fT, fP, fL, ssid, pass);
      if (!ssid.empty()) join = portal::Join::Joining, why = portal::WHY_NONE, joinSsid = ssid, joinPass = pass, joinAt = millis() | 1;
      formWaiting = false, formDone = true;
      cv.notify_all();
    }
    if (join == portal::Join::Joining && millis() - joinAt > 1500) {
      const bool wrong = routerPass.empty() ? joinPass == "wrong" : joinPass != routerPass;
      if (wrong || joinSsid == "Nope" || !routerOn) {
        join = portal::Join::Failed, why = wrong && routerOn && joinSsid != "Nope" ? portal::WHY_PASSWORD : portal::WHY_NOT_FOUND;
        failAt = millis();
        trace::add("portal: \"%s\" failed (simulated)", joinSsid.c_str());
      } else {
        join = portal::Join::Ok;
        store::addWifi(joinSsid, joinPass);
        joinAt = 0;
        up = false;
        reconnect();
      }
    }
  }
  if (!radio) return;
  const uint32_t now = millis();
  // offline: the saved networks again every 5 s (30 s with the setup WiFi up), as the board
  if (!up && !upJoinAt && !store::wifi().empty() && join != portal::Join::Joining &&
      (int32_t)(now - retryAt) >= (int32_t)(portalOn ? 30000 : 5000))
    upJoinAt = now | 1;
  if (!up && upJoinAt && now - upJoinAt > 1500 && !store::wifi().empty()) {
    upJoinAt = 0, retryAt = now;
    const auto w = store::wifi().front();
    if (!routerOn) {
      failedRoundsN++;
      trace::add("wifi: \"%s\" not found (simulated)", w.ssid.c_str());
    } else if (!routerPass.empty() && w.pass != routerPass) {
      if (++pwRefusals >= 3 && badSsid != w.ssid) badSsid = w.ssid, trace::add("wifi: \"%s\" refuses the password", w.ssid.c_str());
      failedRoundsN++;
    } else {
      up = true, upAt = now, offlineAt = 0, pwRefusals = failedRoundsN = 0, badSsid.clear();
      joined = lostSsid = w.ssid;
      trace::add("wifi: up on \"%s\" (simulated)", joined.c_str());
    }
  }
  if (up && store::wifi().empty()) up = false, offlineAt = now | 1;  // forgotten (BOOT held 5 s)
  if (portalOn && up && millis() - upAt > 20000) {  // (the board waits for the phone to see "connected")
    portalOn = false;
    trace::add("portal: off");
  }
}

bool online() { return up; }
bool portal() { return portalOn; }
std::string ssid() { return up ? joined : std::string(); }
portal::Join joinState() { return join; }
portal::Why joinWhy() { return why; }
std::string joinName() { return joinSsid; }
uint32_t joinFailedAgo() { return join == portal::Join::Failed ? millis() - failAt : UINT32_MAX; }

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
bool simulate(const char* what) {
  const std::string w = what;
  if (w == "down") {
    routerOn = false;
    if (up) up = false, offlineAt = millis() | 1, trace::add("wifi: down (simulated: the router is off)");
  } else if (w == "up") {
    routerOn = true;
  } else if (w == "newpass") {
    routerPass = "newpass";
    if (up) up = false, offlineAt = millis() | 1, trace::add("wifi: down (simulated: the router's password changed)");
  } else {
    return false;
  }
  return true;
}

void startPortal() {
  join = portal::Join::None;
  if (!portalOn) {
    portalOn = true;
    trace::add("portal: %s on (http://localhost:%d)", apName().c_str(), portalPort);
    fprintf(stderr, "[bot-sim] setup page: http://localhost:%d\n", portalPort);
  }
  if (store::wifi().empty()) up = false;
}
void reconnect() {
  if (!up && !store::wifi().empty()) upJoinAt = millis() | 1;
}
bool timeValid() { return simClockMs() > 1700000000000LL; }
int64_t nowMs() { return simClockMs(); }
void adoptClock(int64_t) {}
void radioOff() { radio = false, up = false, portalOn = false; }

}  // namespace net
}  // namespace bot
