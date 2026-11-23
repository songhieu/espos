#pragma GCC optimize("Os")  // cold: a first boot, a sync now and then
// Product presets on the device (preset.h, docs/app-slots.md §17).
#include "preset.h"

#include <unistd.h>

#include <vector>

#include "board.h"
#include "desk.h"
#include "pkg/internal.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "preset_merge.h"
#include "trace.h"

#ifdef SIM
#include <string>
extern std::string simDir;
#endif

namespace preset {

namespace {

constexpr const char* FILE_PATH = "/sys/preset.json";         // the preset (LittleFS)
constexpr const char* STATE_PATH = "/sys/preset-state.json";  // what was applied (LittleFS)
constexpr const char* CARD_PATH = "/espos/preset.json";       // a factory card's copy

String lfs(const char* p) {
#ifdef SIM
  return String(simDir.c_str()) + "/fs" + p;
#else
  return String("/littlefs") + p;
#endif
}
String card(const char* p) {
#ifdef SIM
  return String(simDir.c_str()) + "/sd" + p;
#else
  return String("/sdcard") + p;
#endif
}

struct State {
  String id;
  int v = 0;
  Values base;                    // key -> the value the preset set last
  std::vector<std::string> pkgs;  // every package a preset asked for
  std::vector<std::string> skip;  // the setup steps it leaves out
} st;
bool loaded = false, cardLooked = false;

void load() {
  if (loaded) return;
  loaded = true;
  JsonDocument d;
  if (deserializeJson(d, pkg::in::readText(lfs(STATE_PATH)))) return;
  st.id = d["id"] | "";
  st.v = d["v"] | 0;
  for (JsonPairConst kv : d["base"].as<JsonObjectConst>()) st.base[kv.key().c_str()] = kv.value() | "";
  for (JsonVariantConst x : d["pkgs"].as<JsonArrayConst>()) st.pkgs.push_back(x | "");
  for (JsonVariantConst x : d["skip"].as<JsonArrayConst>()) st.skip.push_back(x | "");
}

void save() {
  JsonDocument d;
  d["id"] = st.id;
  d["v"] = st.v;
  JsonObject b = d["base"].to<JsonObject>();
  for (const auto& kv : st.base) b[kv.first] = kv.second;
  JsonArray p = d["pkgs"].to<JsonArray>();
  for (const std::string& s : st.pkgs) p.add(s);
  JsonArray k = d["skip"].to<JsonArray>();
  for (const std::string& s : st.skip) k.add(s);
  String out;
  serializeJson(d, out);
  if (!pkg::in::writeText(lfs(STATE_PATH), out)) trace::add("preset: cannot write %s", STATE_PATH);
}

bool idOk(const String& s) {
  if (s.length() < 2 || s.length() > 41) return false;
  for (unsigned i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i && c == '-'))) return false;
  }
  return true;
}

// The packages new to the preset that are not here: wanted in s3.pkgs, as the web asks for an install (the store's
// reconcile installs them when the catalog can be had: on the card when one is in, else on the device).
void want(const std::vector<std::string>& ids, JsonArrayConst pkgs) {
  JsonDocument d;
  if (deserializeJson(d, desk::setting("s3.pkgs", "[]")) || !d.is<JsonArray>()) d.to<JsonArray>();
  JsonArray a = d.as<JsonArray>();
  const bool sd = pkg::volume(pkg::SD).present;
  for (const std::string& id : ids) {
    bool listed = false;
    for (JsonObjectConst e : a) listed |= id == (e["id"] | "");
    if (listed) continue;
    JsonObject e = a.add<JsonObject>();
    e["id"] = id;
    for (JsonObjectConst p : pkgs)
      if (id == (p["id"] | "") && p["parts"].is<JsonArrayConst>()) e["parts"] = p["parts"];
    e["vol"] = sd ? "sd" : "flash";
  }
  String out;
  serializeJson(d, out);
  desk::setSetting("s3.pkgs", out);
  pkg::store::afterSync();  // the store looks at the list again now
}

}  // namespace

bool apply(const String& json, const char* from, String& why) {
  JsonDocument d;
  if (deserializeJson(d, json) || !d.is<JsonObject>()) return why = "not JSON", false;
  if ((d["preset"] | 0) != 1) return why = "not a preset (v1)", false;
  const String pid = d["id"] | "";
  const int v = d["version"] | 0;
  if (!idOk(pid) || v < 1) return why = "no id or version", false;
  const char* b = d["board"] | "";
  if (strcmp(b, board::name())) return why = String("made for ") + b + ", this is " + board::name(), false;
  load();
  if (pid == st.id && v <= st.v) return why = "already applied", true;
  // The factory's first apply (no state yet, the file or the card or USB, never a sync): nobody has chosen anything on
  // this unit, so values the firmware wrote at its first start (s3.rot is stored at every boot) do not count.
  const bool factory = !st.id.length() && st.base.empty() && strcmp(from, "sync");
  Values next, cur, set, after;
  for (JsonPairConst kv : d["settings"].as<JsonObjectConst>()) {
    next[kv.key().c_str()] = kv.value() | "";
    cur[kv.key().c_str()] = factory ? "" : desk::setting(kv.key().c_str()).c_str();
  }
  mergeSettings(st.base, cur, next, set, after);
  for (const auto& kv : set) desk::setSetting(kv.first.c_str(), String(kv.second.c_str()));
  std::vector<std::string> ids, installed, add, pkgsAfter;
  JsonArrayConst pkgs = d["packages"].as<JsonArrayConst>();
  for (JsonObjectConst p : pkgs) {
    const char* pi = p["id"] | "";
    if (!*pi) continue;
    ids.push_back(pi);
    if (pkg::find(pi)) installed.push_back(pi);
  }
  mergePackages(st.pkgs, installed, ids, add, pkgsAfter);
  if (!add.empty()) want(add, pkgs);
  st.id = pid, st.v = v, st.base = after, st.pkgs = pkgsAfter;
  st.skip.clear();
  for (JsonVariantConst x : d["skip"].as<JsonArrayConst>()) st.skip.push_back(x | "");
  save();
  if (strcmp(from, "file")) pkg::in::writeText(lfs(FILE_PATH), json);  // the current preset, for the next start
  trace::add("preset: %s v%d from %s: %u of %u keys written (%u stay the person's), %u packages wanted", pid.c_str(), v,
             from, (unsigned)set.size(), (unsigned)next.size(), (unsigned)(next.size() - after.size()), (unsigned)add.size());
  return true;
}

// Before Home is built (main.cpp): the preset in LittleFS, so the first Home already has its rotation, theme and
// saver. The card's packages count as installed: the card is looked at first (as os::begin does).
void begin() {
  load();
  const String f = pkg::in::readText(lfs(FILE_PATH));
  if (!f.length()) return;  // none: perhaps the card has one (tick)
  cardLooked = true;
  pkg::awaitCard(500);
  String why;
  if (!apply(f, "file", why)) trace::add("preset: %s refused: %s", FILE_PATH, why.c_str());
}

// No preset in LittleFS: the factory card's copy, once the card has been looked at (10 s at most).
void tick() {
  static uint32_t t0 = millis();
  if (cardLooked || (!pkg::cardChecked() && millis() - t0 < 10000)) return;
  cardLooked = true;
  String why;
  if (st.id.length() || !pkg::volume(pkg::SD).present) return;
  const String c = pkg::in::readText(card(CARD_PATH));
  if (c.length() && !apply(c, "card", why)) trace::add("preset: the card's preset refused: %s", why.c_str());
}

void afterProvision() {
  load();
  // Kho máy's PROVISION brought the server's settings for this unit: the preset's values are the factory's, so they go
  // back in and up with the first sync (a unit is provisioned before anyone has chosen anything).
  for (const auto& kv : st.base) {
    const String k = kv.first.c_str(), want = kv.second.c_str();
    if (desk::setting(k.c_str()) != want) desk::setSetting(k.c_str(), want);
    else desk::touchSetting(k.c_str());
  }
  if (st.id.length()) trace::add("preset: %s v%d again after the provisioning (%u keys)", st.id.c_str(), st.v, (unsigned)st.base.size());
}

void fromSync(JsonObjectConst p) {
  String json, why;
  serializeJson(p, json);
  if (!apply(json, "sync", why)) trace::add("preset: the sync's preset refused: %s", why.c_str());
}

void report(JsonObject body) {
  load();
  if (!st.id.length()) return;
  JsonObject p = body["preset"].to<JsonObject>();
  p["id"] = st.id;
  p["v"] = st.v;
}

const String& id() {
  load();
  return st.id;
}
int version() {
  load();
  return st.v;
}

bool skips(const char* step) {
  load();
  for (const std::string& s : st.skip)
    if (s == step) return true;
  return false;
}

bool asks(const char* what) {
  load();
  if (!st.id.length()) return true;
  const char* key = !strcmp(what, "lang") ? "sys.lang" : !strcmp(what, "timezone") ? "sys.tz"
                    : !strcmp(what, "city") ? "dash.city" : !strcmp(what, "orientation") ? "s3.rot" : "";
  return st.base.find(key) == st.base.end();
}

bool console(const char* cmd) {
  if (strncmp(cmd, "PRESET", 6) || (cmd[6] && cmd[6] != ' ')) return false;
  const char* a = cmd[6] ? cmd + 7 : "";
  if (!strcmp(a, "FORGET")) {
    st = State(), loaded = true;
    unlink(lfs(STATE_PATH).c_str());
    Serial.println("ESPOS:OK");
    return true;
  }
  if (*a == '{') {
    String why;
    const bool ok = apply(a, "usb", why);
    Serial.printf(ok ? "ESPOS:OK%s%s\n" : "ESPOS:ERR %s%s\n", ok && why.length() ? " " : "", why.c_str());
    return true;
  }
  load();
  Serial.printf("ESPOS:PRESET {\"id\":\"%s\",\"v\":%d,\"keys\":%u,\"pkgs\":%u,\"skip\":%u}\n", st.id.c_str(), st.v,
                (unsigned)st.base.size(), (unsigned)st.pkgs.size(), (unsigned)st.skip.size());
  return true;
}

}  // namespace preset
