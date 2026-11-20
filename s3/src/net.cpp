#pragma GCC optimize("Os")  // cold code: built small (0.5.7 flash diet; the app slot is full)
#include "net.h"

#include <Preferences.h>
#include <WiFi.h>

#include <algorithm>

#include "i18n.h"
#include "ota.h"
#include "pkg/store.h"
#include "power.h"
#include "trace.h"

#ifdef ESPOS_QEMU
// QEMU has no radio: its OpenCores Ethernet (lib/qemu_eth; QEMU -nic user,model=open_eth: NAT, DHCP, DNS) stands in
// for WiFi. The rest of the firmware sees "connected" as with WiFi, and the same TLS, sync and store code runs on it.
#include "esp_event.h"
#include "esp_netif.h"
#include "qemu_eth.h"
#endif

namespace net {
namespace {

constexpr int MAX_SAVED = 8;
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;

struct Saved {
  String ssid, pass;
};
std::vector<Saved> saved;  // most recently used first
std::vector<Ap> found;
uint32_t foundAt = 0;

State st = State::Off;
String target, targetPass, lastError;
bool userConnect = false;   // started from the screen: report errors, save on success
uint32_t connectStart = 0, changeCount = 0;
int attempt = 0;  // a refused join is tried again before giving up (auto-join: 3 times, a moment apart)
uint32_t retryAt = 0;  // when that next try starts (0: none waiting)

bool scanBusy = false, scanWanted = false;
enum class Auto { Waiting, Scanning, Trying } autoPhase = Auto::Waiting;
bool quickDone = false;  // the last-used network was tried straight away, without a scan
constexpr uint32_t FIRST_RETRY_MS = 5000;  // then doubling up to 5 minutes
uint32_t nextAuto = 0, backoff = FIRST_RETRY_MS;
std::vector<String> triedThisRound;  // saved networks that failed since the last auto scan

int refusal = 0;  // reason code of the last refusal, for the trace

// Set by the WiFi event task, read in poll().
volatile bool evGotIp = false, evDisc = false;
volatile int evReason = 0;

#ifdef ESPOS_QEMU
constexpr const char* QEMU_NET = "Wired network (QEMU)";  // its "SSID": a name, not translated
esp_netif_t* ethIf = nullptr;

void startEthernet() {
  Network.begin();  // esp_netif and the default event loop (WiFi does this on the board)
  eth_mac_config_t mc = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t pc = ETH_PHY_DEFAULT_CONFIG();
  pc.autonego_timeout_ms = 100;
  esp_eth_mac_t* mac = esp_eth_mac_new_openeth(&mc);
  esp_eth_phy_t* phy = esp_eth_phy_new_dp83848(&pc);
  esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth = nullptr;
  if (!mac || !phy || esp_eth_driver_install(&cfg, &eth) != ESP_OK) {
    trace::add("net: QEMU Ethernet missing (run QEMU with -nic user,model=open_eth)");
    return;
  }
  esp_netif_config_t nc = ESP_NETIF_DEFAULT_ETH();
  ethIf = esp_netif_new(&nc);
  esp_netif_attach(ethIf, esp_eth_new_netif_glue(eth));
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, [](void*, esp_event_base_t, int32_t, void*) { evGotIp = true; },
                             nullptr);
  esp_eth_start(eth);
  target = QEMU_NET;
  st = State::Connecting;
  connectStart = millis();
  trace::add("net: QEMU Ethernet started");
}

String ethIp() {
  esp_netif_ip_info_t i;
  if (!ethIf || esp_netif_get_ip_info(ethIf, &i) != ESP_OK) return String();
  char b[16];
  snprintf(b, sizeof b, IPSTR, IP2STR(&i.ip));
  return b;
}
#endif

void changed() { changeCount++; }

void load() {
  Preferences p;
  p.begin("wifi", true);
  const int n = p.getInt("n", 0);
  saved.clear();
  for (int i = 0; i < n && i < MAX_SAVED; i++) {
    Saved s{p.getString(("s" + String(i)).c_str(), ""), p.getString(("p" + String(i)).c_str(), "")};
    if (s.ssid.length()) saved.push_back(s);
  }
  p.end();
}

void store() {
  Preferences p;
  p.begin("wifi", false);
  p.clear();
  p.putInt("n", saved.size());
  for (size_t i = 0; i < saved.size(); i++) {
    p.putString(("s" + String(i)).c_str(), saved[i].ssid);
    p.putString(("p" + String(i)).c_str(), saved[i].pass);
  }
  p.end();
}

int savedIndex(const String& ssid) {
  for (size_t i = 0; i < saved.size(); i++)
    if (saved[i].ssid == ssid) return i;
  return -1;
}

void remember(const String& ssid, const String& pass) {
  const int i = savedIndex(ssid);
  if (i >= 0) saved.erase(saved.begin() + i);
  saved.insert(saved.begin(), Saved{ssid, pass});
  if (saved.size() > MAX_SAVED) saved.resize(MAX_SAVED);
  store();
}

void onEvent(arduino_event_id_t id, arduino_event_info_t info) {
  if (id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    evGotIp = true;
  } else if (id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // Leaving on our own (ours, or the Arduino core's one automatic retry) says nothing about
    // why the network refused us, and would hide the real reason that came just before.
    const int r = info.wifi_sta_disconnected.reason;
    if (r == WIFI_REASON_ASSOC_LEAVE || r == WIFI_REASON_STA_LEAVING) return;
    evReason = r;
    evDisc = true;
  }
}

const char* reasonText(int reason, int rssi) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_EXPIRE:
      return rssi < -78 ? T_("Couldn't join: wrong password or the signal is too weak.")
                        : T_("Wrong password, please type it again.");
    case WIFI_REASON_NO_AP_FOUND:
      return T_("This network is not around here.");
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
      return T_("This network uses a kind of security the device does not support yet.");
    default:
      return nullptr;
  }
}

int rssiOf(const String& ssid) {
  for (const Ap& a : found)
    if (a.ssid == ssid) return a.rssi;
  return -100;
}

void startConnect(const String& ssid, const String& pass, bool user) {
  if (scanBusy) {  // a scan running on the radio delays the join; drop it
    WiFi.scanDelete();
    scanBusy = false;
  }
  target = ssid;
  targetPass = pass;
  userConnect = user;
  if (user) {
    autoPhase = Auto::Waiting;
    nextAuto = millis() + 60000;  // let the user retype a password before auto-join steps in
  }
  lastError = "";
  refusal = 0;
  attempt = 0;
  retryAt = 0;
  evGotIp = evDisc = false;
  connectStart = millis();
  st = State::Connecting;
  trace::add("wifi connect \"%s\" (%s)", ssid.c_str(), user ? "user" : "auto");
  if (WiFi.isConnected()) WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  changed();
}

void fail(const char* why) {
  retryAt = 0;
  trace::add("wifi fail \"%s\" (reason %d): %s", target.c_str(), refusal, why);
  WiFi.disconnect(false, false);
  if (userConnect) lastError = why;
  else triedThisRound.push_back(target);  // the auto-join moves on to the next saved network
  st = State::Idle;
  changed();
}

// Strongest saved network in the last scan that has not failed this round.
int pickSaved() {
  int best = -1;
  for (size_t i = 0; i < found.size(); i++) {
    if (!found[i].saved) continue;
    bool tried = false;
    for (const String& t : triedThisRound) tried |= t == found[i].ssid;
    if (!tried && (best < 0 || found[i].rssi > found[best].rssi)) best = i;
  }
  return best;
}

void finishScan(int n) {
  found.clear();
  for (int i = 0; i < n; i++) {
    const String s = WiFi.SSID(i);
    if (!s.length()) continue;  // hidden network
    bool merged = false;
    for (Ap& a : found)
      if (a.ssid == s) {
        a.rssi = max(a.rssi, (int)WiFi.RSSI(i));
        merged = true;
      }
    if (!merged) found.push_back(Ap{s, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN, savedIndex(s) >= 0});
  }
  WiFi.scanDelete();
  std::sort(found.begin(), found.end(), [](const Ap& a, const Ap& b) { return a.rssi > b.rssi; });
  foundAt = millis();
  scanBusy = false;
  trace::add("wifi scan: %d networks", (int)found.size());
  changed();
}

}  // namespace

void begin() {
  load();
#ifdef ESPOS_QEMU
  nextAuto = UINT32_MAX;  // no WiFi to join: QEMU's Ethernet instead
  startEthernet();
  return;
#endif
  WiFi.persistent(false);  // credentials live in our own list, not in the SDK's
  WiFi.setHostname("espos-s3");
  WiFi.onEvent(onEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  st = State::Idle;
  nextAuto = saved.empty() ? UINT32_MAX : millis() + 500;
  trace::add("wifi begin, %d saved, heap %u KB", (int)saved.size(), ESP.getFreeHeap() / 1024);
}

void poll() {
  if (st == State::Off) return;
  const uint32_t now = millis();

  if (evGotIp && st == State::Connecting) {
    evGotIp = false;
    st = State::Connected;
#ifdef ESPOS_QEMU
    configTzTime("ICT-7", "pool.ntp.org", "time.google.com");
    trace::add("net: QEMU Ethernet up %s heap %u KB", ethIp().c_str(), ESP.getFreeHeap() / 1024);
    changed();
    return;
#endif
    remember(target, targetPass);  // move to the front; saves a new network
    backoff = FIRST_RETRY_MS;
    triedThisRound.clear();
    autoPhase = Auto::Waiting;
    configTzTime("ICT-7", "pool.ntp.org", "time.google.com");
    trace::add("wifi up \"%s\" %s rssi %d heap %u KB", target.c_str(), WiFi.localIP().toString().c_str(),
               WiFi.RSSI(), ESP.getFreeHeap() / 1024);
    changed();
  }
  if (evDisc) {
    evDisc = false;
    const int reason = refusal = evReason;
    // Right after a restart the router often still holds our old session and refuses the first
    // join (seen: reason 202 within a second, then the second join works). So an auth-type
    // refusal gets one clean retry before it counts as a wrong password.
    const bool authLike = reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_AUTH_EXPIRE ||
                          reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                          reason == WIFI_REASON_ASSOC_FAIL || reason == WIFI_REASON_CONNECTION_FAIL;
    // A join of our own (not the user's typing) is tried 3 times, 1, 2 and 3 s apart: on 2026-11-18 the board's
    // retry at once was refused again within 0.1 s and WiFi came 12 s later, after two scans (0.5.7).
    if (st == State::Connecting && authLike && attempt < (userConnect ? 1 : 3)) {
      attempt++;
      trace::add("wifi retry \"%s\" after reason %d (%d)", target.c_str(), reason, attempt);
      WiFi.disconnect(false, false);
      retryAt = userConnect ? 0 : (now + 1000u * attempt) | 1;
      if (!retryAt) WiFi.begin(target.c_str(), targetPass.length() ? targetPass.c_str() : nullptr);
      connectStart = millis();
    } else if (st == State::Connecting) {
      const char* why = reasonText(reason, rssiOf(target));
      static char other[96];
      snprintf(other, sizeof other, T_("The network refused the connection (code %d). Check the password."), reason);
      fail(why ? why : other);
    } else if (st == State::Connected) {
      trace::add("wifi lost \"%s\" reason %d", target.c_str(), reason);
      st = State::Idle;
      quickDone = false;
      nextAuto = now + 2000;
      changed();
    }
  }
  if (retryAt && st == State::Connecting && (int32_t)(now - retryAt) >= 0) {
    retryAt = 0;
    WiFi.begin(target.c_str(), targetPass.length() ? targetPass.c_str() : nullptr);
    connectStart = millis();
  }
  if (st == State::Connecting && millis() - connectStart > CONNECT_TIMEOUT_MS) fail(T_("The network took too long to answer."));
  // WiFi power save (the S3's default, modem sleep) holds what the router sends until the next beacon: up to a few
  // tenths of a second on every round trip, which with lwIP's 5.7 KB window kept a download near 20 KB/s and made each
  // TLS handshake slower (0.5.7). Awake on USB power and while something downloads; on the battery it sleeps otherwise.
#ifndef ESPOS_QEMU  // no radio there
  static uint32_t psAt = 0;
  static int psNow = -1;
  if (st != State::Connected) {
    psNow = -1;  // chosen again once joined
  } else if (now - psAt >= 1000) {
    psAt = now;
    const int want = power::usb() || pkg::store::busy() || ota::installing() || ota::running() ? 0 : 1;
    if (want != psNow) {
      psNow = want;
      WiFi.setSleep(want ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
      trace::add("wifi power save %s", want ? "on" : "off");
    }
  }
#endif

  if (scanBusy) {
    const int n = WiFi.scanComplete();
    if (n >= 0) {
      finishScan(n);
    } else if (n == WIFI_SCAN_FAILED) {
      scanBusy = false;
      trace::add("wifi scan failed");
      changed();
    }
  }
  if (scanWanted && !scanBusy && st != State::Connecting) {
    scanWanted = false;
    scanBusy = WiFi.scanNetworks(true, false, false, 150) == WIFI_SCAN_RUNNING;
    changed();
  }

  // Auto-join: scan, then try saved networks strongest first; back off when none works.
  if (st == State::Idle && !saved.empty()) {
    if (autoPhase == Auto::Waiting && now >= nextAuto && !quickDone) {
      quickDone = true;  // most of the time it is the same network as last time: skip the scan
      startConnect(saved[0].ssid, saved[0].pass, false);
    } else if (autoPhase == Auto::Waiting && now >= nextAuto) {
      autoPhase = Auto::Scanning;
      scanWanted = true;
    } else if (autoPhase == Auto::Scanning && !scanBusy && !scanWanted) {
      autoPhase = Auto::Trying;
    } else if (autoPhase == Auto::Trying) {
      const int i = pickSaved();
      if (i >= 0) {
        startConnect(found[i].ssid, saved[savedIndex(found[i].ssid)].pass, false);
      } else {
        autoPhase = Auto::Waiting;
        triedThisRound.clear();
        nextAuto = now + backoff;
        backoff = min<uint32_t>(backoff * 2, 300000);
      }
    }
  }
}

void scan() {
#ifndef ESPOS_QEMU  // no radio to scan with (starting WiFi there would hang)
  scanWanted = true;
#endif
}
bool scanning() { return scanBusy || scanWanted; }
const std::vector<Ap>& results() {
  for (Ap& a : found) a.saved = savedIndex(a.ssid) >= 0;
  return found;
}
uint32_t resultsAt() { return foundAt; }

void connect(const String& ssid, const String& pass) {
#ifdef ESPOS_QEMU
  lastError = T_("QEMU has no WiFi: the device uses QEMU's wired network.");
  changed();
  return;
#endif
  startConnect(ssid, pass, true);
}

void connectSaved(const String& ssid) {
#ifdef ESPOS_QEMU
  return connect(ssid, "");
#endif
  const int i = savedIndex(ssid);
  if (i >= 0) startConnect(ssid, saved[i].pass, true);
}

void disconnect() {
  WiFi.disconnect(false, false);
  st = State::Idle;
  autoPhase = Auto::Waiting;
  nextAuto = UINT32_MAX;  // stay off until the user picks a network or the device restarts
  changed();
}

void resumeAuto() {
  autoPhase = Auto::Waiting;
  quickDone = false;
  nextAuto = millis();
}

void forget(const String& ssid) {
  const int i = savedIndex(ssid);
  if (i < 0) return;
  saved.erase(saved.begin() + i);
  store();
  if (st != State::Idle && target == ssid) {
    WiFi.disconnect(false, false);
    st = State::Idle;
    autoPhase = Auto::Waiting;
    nextAuto = millis() + 1000;  // join another saved network if there is one
  }
  changed();
}

bool isSaved(const String& ssid) { return savedIndex(ssid) >= 0; }

std::vector<String> savedNames() {
  std::vector<String> out;
  for (const Saved& s : saved) out.push_back(s.ssid);
  return out;
}

void addSaved(const String& ssid, const String& pass) {
  const int i = savedIndex(ssid);
  if (i >= 0) {
    saved[i].pass = pass;
    store();
  } else if ((int)saved.size() < MAX_SAVED) {
    // Last in line: the network in use stays the one tried first after a restart.
    saved.push_back(Saved{ssid, pass});
    store();
  }
  trace::add("wifi saved \"%s\" from the web", ssid.c_str());
  if (st == State::Idle) {
    quickDone = false;
    autoPhase = Auto::Waiting;
    nextAuto = millis();
  }
  changed();
}
int savedCount() { return saved.size(); }
State state() { return st; }
String ssid() { return st == State::Idle || st == State::Off ? String() : target; }
#ifdef ESPOS_QEMU
String ip() { return st == State::Connected ? ethIp() : String(); }
int rssi() { return st == State::Connected ? -40 : -100; }  // a cable: full bars
#else
String ip() { return st == State::Connected ? WiFi.localIP().toString() : String(); }
int rssi() { return st == State::Connected ? WiFi.RSSI() : -100; }
#endif
const char* error() { return lastError.c_str(); }
int lastReason() { return refusal; }
uint32_t changes() { return changeCount; }

int bars(int rssi) { return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -77 ? 2 : 1; }

}  // namespace net
