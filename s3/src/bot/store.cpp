#include "store.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_random.h>

#include "config.h"
#include "logic.h"

namespace bot {
namespace store {
namespace {

Preferences nvs;
bool opened = false;
std::string tok, apiUrl, devName, code, url;
int devId = 0;
std::map<std::string, std::string> vals;
std::map<std::string, uint32_t> dirtyAt;  // key -> millis() (0: from before this boot)
int64_t revNow = 0;
uint32_t ver = 1;
bool langOk = false;
std::vector<Wifi> nets;

std::string str(const char* k) { return nvs.isKey(k) ? std::string(nvs.getString(k, "").c_str()) : std::string(); }

void saveSettings() {
  JsonDocument d;
  for (auto& kv : vals) d["v"][kv.first] = kv.second;
  for (auto& kv : dirtyAt) d["d"].add(kv.first);
  std::string s;
  serializeJson(d, s);
  nvs.putBytes("set", s.data(), s.size());
  nvs.putLong64("rev", revNow);
}

void saveWifi() {
  JsonDocument d;
  for (const Wifi& w : nets) {
    JsonObject o = d.add<JsonObject>();
    o["s"] = w.ssid, o["p"] = w.pass;
  }
  std::string s;
  serializeJson(d, s);
  nvs.putBytes("wifi", s.data(), s.size());
}

std::string blob(const char* k) {
  const size_t n = nvs.isKey(k) ? nvs.getBytesLength(k) : 0;
  std::string s(n, '\0');
  if (n) nvs.getBytes(k, &s[0], n);
  return s;
}

}  // namespace

void begin() {
  if (opened) return;
  opened = nvs.begin("bot", false);
  tok = str("token"), apiUrl = str("api"), devName = str("dname"), code = str("claim"), url = str("curl");
  devId = nvs.getInt("dev", 0);
  revNow = nvs.getLong64("rev", 0);
  langOk = nvs.getBool("lang", false);
  JsonDocument d;
  const std::string s = blob("set");
  if (!s.empty() && !deserializeJson(d, s)) {
    for (JsonPairConst kv : d["v"].as<JsonObjectConst>()) vals[kv.key().c_str()] = kv.value().as<const char*>() ?: "";
    for (JsonVariantConst k : d["d"].as<JsonArrayConst>()) dirtyAt[k.as<const char*>() ?: ""] = 0;
  }
  JsonDocument w;
  const std::string ws = blob("wifi");
  if (!ws.empty() && !deserializeJson(w, ws))
    for (JsonObjectConst o : w.as<JsonArrayConst>()) nets.push_back({o["s"] | "", o["p"] | ""});
}

std::string token() { return tok; }
std::string api() { return apiUrl.empty() ? std::string(ESPOS_API) : apiUrl; }
void setLink(const std::string& t, const std::string& a, int id, const std::string& name) {
  tok = t, apiUrl = a, devId = id, devName = name;
  nvs.putString("token", tok.c_str());
  nvs.putString("api", apiUrl.c_str());
  nvs.putInt("dev", devId);
  nvs.putString("dname", devName.c_str());
}
int deviceId() { return devId; }
std::string claimCode() { return code; }
std::string claimUrl() { return url; }
void setClaim(const std::string& c, const std::string& u) {
  std::string k;
  for (char ch : c)
    if (ch != '-' && ch != ' ') k += (char)toupper((unsigned char)ch);
  code = k, url = u;
  if (code.empty()) nvs.remove("claim"), nvs.remove("curl");
  else nvs.putString("claim", code.c_str()), nvs.putString("curl", url.c_str());
}

const std::map<std::string, std::string>& settings() { return vals; }
std::string setting(const std::string& k) { return logic::get(vals, k); }

void set(const std::string& k, const std::string& v, bool dirty) {
  auto it = vals.find(k);
  const bool same = it != vals.end() && it->second == v;
  if (dirty) dirtyAt[k] = millis() | 1;
  else if (dirtyAt.count(k)) return;  // changed here while a sync ran: ours goes up first
  if (same && !dirty) return;
  vals[k] = v;
  ver++;
  if (k == "sys.lang") setLangChosen();
  saveSettings();
}

std::vector<std::pair<std::string, uint32_t>> dirty() {
  std::vector<std::pair<std::string, uint32_t>> out(dirtyAt.begin(), dirtyAt.end());
  return out;
}

void settle(uint32_t before) {
  bool any = false;
  for (auto it = dirtyAt.begin(); it != dirtyAt.end();)
    if ((int32_t)(it->second - before) <= 0) it = dirtyAt.erase(it), any = true;
    else ++it;
  if (any) saveSettings();
}

int64_t rev() { return revNow; }
void setRev(int64_t r) {
  if (r == revNow) return;
  revNow = r;
  nvs.putLong64("rev", revNow);
}
uint32_t version() { return ver; }
bool langChosen() { return langOk; }
void setLangChosen() {
  if (langOk) return;
  langOk = true;
  nvs.putBool("lang", true);
}

std::vector<Wifi> wifi() { return nets; }
void addWifi(const std::string& ssid, const std::string& pass) {
  if (ssid.empty()) return;
  forgetWifi(ssid);
  nets.insert(nets.begin(), {ssid, pass});
  if (nets.size() > 5) nets.resize(5);
  saveWifi();
}
void forgetWifi(const std::string& ssid) {
  for (auto it = nets.begin(); it != nets.end();) it = ssid.empty() || it->ssid == ssid ? nets.erase(it) : std::next(it);
  saveWifi();
}

std::string apPass() {
  std::string p = str("appass");
  if (p.size() == 8) return p;
  char t[9];
  snprintf(t, sizeof t, "%08u", (unsigned)(esp_random() % 100000000u));
  p = t;
  nvs.putString("appass", p.c_str());
  return p;
}

void factoryReset() {
  vals.clear(), dirtyAt.clear(), nets.clear();
  revNow = 0, langOk = false;
  nvs.remove("set"), nvs.remove("wifi"), nvs.remove("rev"), nvs.remove("lang");
  ver++;
}

}  // namespace store
}  // namespace bot
