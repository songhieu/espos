// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
// The registry of installed packages (docs/ecosystem.md §3-4): the internal app area (index in NVS), small
// JavaScript apps on LittleFS, and the memory card. Also: adopting the PawBoop packs already on the board,
// converting the JavaScript apps of firmware 0.2.x, File reading, and starting a package's app.
#include <lvgl.h>
#include <unistd.h>

#include <atomic>
#include <set>

#include "desk.h"
#include "i18n.h"
#include "internal.h"
#include "js/jsapp.h"
#include "os.h"
#include "store.h"
#include "splash.h"
#include "trace.h"

#ifndef NO_PET
#include "apps.h"
#include "pet/formats.h"
#include "pet/pet.h"
#endif

#if __has_include("../run/run.h")
#include "../run/run.h"
#define HAVE_RUN 1
#else
namespace run {
void open(const char* id, const char* arg);
}
#endif

#if __has_include("../j2me/app_j2me.h")
#include "../j2me/app_j2me.h"
#define HAVE_J2ME 1
#endif

#if __has_include("../tank/tank.h")
#include "../tank/tank.h"
#define HAVE_TANK 1  // Hồ cá cảnh (src/tank, docs/tank.md)
#endif

#ifdef SIM
#include <string>
extern std::string simDir;
#endif

// Until the run game's engine is in the firmware: a package that needs it says so.
namespace run {
__attribute__((weak)) void open(const char*, const char*) { os::toast(N_("Needs a software update")); }
}  // namespace run

namespace pkg {

namespace in {

std::recursive_mutex mu;
std::vector<Rec> recs;

namespace {
std::atomic<bool> dirty{false};
std::set<String> pinnedIds, busyIds;
}  // namespace

Rec* rec(const String& id) {
  for (Rec& r : recs)
    if (r.p.id == id) return &r;
  return nullptr;
}

void changed() { dirty = true; }

bool pinned(const String& id) {
  std::lock_guard<std::recursive_mutex> l(mu);
  return pinnedIds.count(id) > 0;
}

void setBusy(const String& id, bool b) {
  std::lock_guard<std::recursive_mutex> l(mu);
  if (b) busyIds.insert(id);
  else busyIds.erase(id);
}

namespace {
uint32_t hexColor(const char* s, uint32_t def) {
  if (!s || s[0] != '#' || strlen(s) != 7) return def;
  return strtoul(s + 1, nullptr, 16);
}
String hexOf(uint32_t c) {
  char b[8];
  snprintf(b, sizeof b, "#%06lx", (unsigned long)(c & 0xffffff));
  return b;
}
}  // namespace

void fromManifest(JsonObjectConst m, Package& p) {
  p.id = m["id"] | "";
  p.name = m["name"] | p.id.c_str();
  p.version = m["version"] | "";
  p.code = m["code"] | 0u;
  p.kind = m["kind"] | "js";
  p.engine = m["engine"] | (p.kind == "js" ? "js" : "");
  p.category = m["category"] | "tool";
  p.desc = m["desc"] | "";
  p.author = m["author"] | "";
  p.addonOf = m["addon_of"] | "";
  p.type = m["type"] | "";
  p.set = m["set"] | "";
  // Names in other languages: "i18n": {"vi": {"name": ..., "desc": ...}} in a package's manifest, kept as "names".
  p.names = "";
  if (m["i18n"].is<JsonObjectConst>()) {
    JsonDocument n;
    for (JsonPairConst kv : m["i18n"].as<JsonObjectConst>())
      if (kv.value()["name"].is<const char*>()) n[kv.key()] = kv.value()["name"];
    if (n.size()) serializeJson(n, p.names);
  } else if (m["names"].is<JsonObjectConst>()) {
    serializeJson(m["names"], p.names);
  }
  p.glyph = m["icon"]["glyph"] | (m["glyph"] | "grid");
  JsonVariantConst bg = m["icon"]["bg"].isNull() ? m["bg"] : m["icon"]["bg"];
  p.bg1 = hexColor(bg[0] | "", 0x64748b);
  p.bg2 = hexColor(bg[1] | "", p.bg1);
  p.perms.clear();
  for (JsonVariantConst v : m["perms"].as<JsonArrayConst>()) p.perms.push_back(v | "");
  for (JsonVariantConst v : m["permissions"].as<JsonArrayConst>()) p.perms.push_back(v | "");
  p.parts.clear();
  for (JsonVariantConst v : m["parts"].as<JsonArrayConst>())
    if (v.is<const char*>()) p.parts.push_back(v.as<const char*>());
  if (p.parts.empty()) p.parts.push_back("core");
}

void toManifest(const Package& p, JsonObject m) {
  m["id"] = p.id;
  m["name"] = p.name;
  m["version"] = p.version;
  m["code"] = p.code;
  m["kind"] = p.kind;
  m["engine"] = p.engine;
  m["category"] = p.category;
  if (p.author.length()) m["author"] = p.author;
  if (p.addonOf.length()) m["addon_of"] = p.addonOf;
  if (p.type.length()) m["type"] = p.type;
  if (p.set.length()) m["set"] = p.set;
  if (p.desc.length()) m["desc"] = p.desc;
  if (p.names.length()) {
    JsonDocument n;
    if (deserializeJson(n, p.names) == DeserializationError::Ok) m["names"] = n;
  }
  JsonObject ic = m["icon"].to<JsonObject>();
  ic["glyph"] = p.glyph;
  JsonArray bg = ic["bg"].to<JsonArray>();
  bg.add(hexOf(p.bg1));
  bg.add(hexOf(p.bg2));
  JsonArray pe = m["perms"].to<JsonArray>();
  for (const String& s : p.perms) pe.add(s);
}

bool scanDir(const String& dir, Vol vol, Rec& r) {
  const String js = readText(dir + "/manifest.json");
  JsonDocument d;
  if (!js.length() || deserializeJson(d, js) != DeserializationError::Ok) return false;
  r = Rec();
  fromManifest(d.as<JsonObjectConst>(), r.p);
  if (!r.p.id.length()) return false;
  r.p.vol = vol;
  r.dir = dir;
  for (JsonObjectConst f : d["files"].as<JsonArrayConst>()) {
    FileRec fr;
    fr.name = f["n"] | "";
    fr.size = f["s"] | 0u;
    fr.crc = f["c"] | 0u;
    if (!fr.name.length()) continue;
    r.files.push_back(fr);
    r.p.bytes += fr.size;
    if (fr.name == "icon.jpg") r.p.icon = true;
  }
  return true;
}

std::vector<Rec> scanRoot(const String& root, Vol vol) {
  std::vector<Rec> out;
  for (const String& n : listDir(root)) {
    if (n[0] == '.' || !isDir(root + "/" + n)) continue;
    Rec r;
    if (scanDir(root + "/" + n, vol, r) && r.p.id == n) out.push_back(r);
    else if (!exists(root + "/" + n + "/manifest.json")) rmTree(root + "/" + n);  // a removal cut half way
  }
  return out;
}

String recJson(const Rec& r) {
  JsonDocument d;
  JsonObject m = d.to<JsonObject>();
  toManifest(r.p, m);
  m.remove("desc");  // NVS is small; the store shows the catalog's text
  JsonArray parts = m["parts"].to<JsonArray>();
  for (const String& s : r.p.parts) parts.add(s);
  JsonArray f = m["f"].to<JsonArray>();
  for (const FileRec& fr : r.files) {
    JsonArray e = f.add<JsonArray>();
    e.add(fr.name);
    e.add(fr.addr);
    e.add(fr.size);
    e.add(fr.crc);
  }
  String out;
  serializeJson(d, out);
  return out;
}

bool recFromJson(const String& json, Rec& r) {
  JsonDocument d;
  if (deserializeJson(d, json) != DeserializationError::Ok) return false;
  r = Rec();
  fromManifest(d.as<JsonObjectConst>(), r.p);
  r.area = true;
  r.p.vol = FLASH;
  for (JsonArrayConst e : d["f"].as<JsonArrayConst>()) {
    FileRec fr;
    fr.name = e[0] | "";
    fr.addr = e[1] | 0u;
    fr.size = e[2] | 0u;
    fr.crc = e[3] | 0u;
    r.files.push_back(fr);
    r.p.bytes += fr.size;
    if (fr.name == "icon.jpg") r.p.icon = true;
  }
  return r.p.id.length() > 0;
}

}  // namespace in

using namespace in;

namespace {

std::vector<Package> pub;
std::atomic<uint32_t> ver{1};
bool started = false;

void rebuild() {
  std::lock_guard<std::recursive_mutex> l(mu);
  pub.clear();
  for (const Rec& r : recs) {
    pub.push_back(r.p);
    if (r.p.names.length()) {  // the name in the language shown
      JsonDocument n;
      if (deserializeJson(n, r.p.names) == DeserializationError::Ok && n[i18n::lang()].is<const char*>())
        pub.back().name = n[i18n::lang()].as<const char*>();
    }
  }
}

// ---------------------------------------------------------------- the PawBoop packs already on the board
#ifndef NO_PET
// The places and limits of the packs since the USB switch (partitions_pawboop.csv), from the area's start.
struct Pack {
  const char* name;
  uint32_t at, max;
};
const Pack PACKS[] = {{"biscuit_M.pbs", 0, 0x51A000}, {"pawboop_ui.pbu", 0x51A000, 0x1E3000},
                      {"biscuit_voice_12k.pba", 0x6FD000, 0x31000}};

uint32_t rd32(const uint8_t* b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

// The file's length from its own header (0 when it is not that pack).
uint32_t packSize(int k, const uint8_t* b, uint32_t lim) {
  if (k == 0) {
    pbs2_head_t h;
    memcpy(&h, b, sizeof h);
    if (memcmp(h.magic, "PBS2", 4) || h.w != 320 || h.h != 304) return 0;
    const uint32_t tab = sizeof h + h.hdr_len + h.nclips * sizeof(pbs2_clip_t) + 4 * h.nframes;
    if (tab + 4 > lim) return 0;
    const uint32_t end = rd32(b + tab);
    return end <= lim && end > tab ? end : 0;
  }
  if (k == 1) {
    pbu_head_t h;
    memcpy(&h, b, sizeof h);
    if (memcmp(h.magic, "PBU1", 4) || 8 + h.count * sizeof(pbu_piece_t) > lim) return 0;
    uint32_t end = 8 + h.count * sizeof(pbu_piece_t);
    for (int i = 0; i < h.count; i++) {
      pbu_piece_t p;
      memcpy(&p, b + 8 + i * sizeof p, sizeof p);
      end = max(end, p.off + (uint32_t)p.w * p.h * (p.fmt ? 3 : 2));
    }
    return end <= lim ? end : 0;
  }
  pba_head_t h;
  memcpy(&h, b, sizeof h);
  if (memcmp(h.magic, "PBA1", 4) || h.channels != 1 || h.data_off > lim) return 0;
  uint32_t end = h.data_off;
  for (int i = 0; i < h.nclips; i++) {
    pba_clip_t c;
    if (sizeof h + (i + 1) * sizeof c > lim) return 0;
    memcpy(&c, b + sizeof h + i * sizeof c, sizeof c);
    end = max(end, h.data_off + c.off + c.bytes);
  }
  return end <= lim ? end : 0;
}

void adoptPet() {
  if (!area::ready() || area::size() < 0x72E000 || rec("vn.pawboop.pet")) return;
  Rec r;
  for (int k = 0; k < 3; k++) {
    const uint32_t n = packSize(k, area::base() + PACKS[k].at, PACKS[k].max);
    if (!n) return;
    FileRec f;
    f.name = PACKS[k].name;
    f.addr = PACKS[k].at;
    f.size = n;
    r.files.push_back(f);
  }
  for (const area::Ext& e : area::used())
    if (e.off < 0x72E000) return;  // something else lives there now
  const uint32_t t0 = millis();
  for (FileRec& f : r.files) f.crc = crc32(0, area::base() + f.addr, f.size), r.p.bytes += f.size;
  r.area = true;
  r.p.id = "vn.pawboop.pet";
  r.p.name = "PawBoop";
  r.p.version = "1.0.0";
  r.p.code = 1;
  r.p.kind = "native";
  r.p.engine = "pet";
  r.p.category = "game";
  r.p.author = "PawBoop";
  r.p.glyph = "play";
  r.p.bg1 = 0xffcf7a, r.p.bg2 = 0xe8892c;
  r.p.perms = {"sound", "notify"};
  r.p.parts = {"core"};
  if (!idx::put(r.p.id, recJson(r))) return;
  recs.push_back(r);
  trace::add("pkg: adopted the PawBoop packs as vn.pawboop.pet (%u KB, crc in %lu ms)", (unsigned)(r.p.bytes / 1024),
             (unsigned long)(millis() - t0));
}
#endif

// ---------------------------------------------------------------- JavaScript apps of firmware 0.2.x
const char* categoryOf(const String& id) {
  if (id == "dev.espos.dice") return "game";
  if (id == "dev.espos.vocab") return "study";
  if (id == "dev.espos.water") return "health";
  return "tool";
}

void convertOldApps() {
#ifdef SIM
  const String old = String(simDir.c_str()) + "/fs/apps";
#else
  const String old = "/littlefs/apps";
#endif
  int n = 0;
  for (const String& f : listDir(old)) {
    if (!f.endsWith(".json")) continue;
    const String path = old + "/" + f;
    JsonDocument a;
    const String js = readText(path, 200000);
    if (deserializeJson(a, js) != DeserializationError::Ok) continue;
    const String id = a["id"] | "";
    const String main = a["main"] | "";
    if (!id.length() || id.indexOf('/') >= 0 || id[0] == '.' || !main.length()) continue;
    if (!rec(id)) {
      const String tmp = lfsRoot() + "/.new-" + id, dir = lfsRoot() + "/" + id;
      rmTree(tmp);
      mkdirs(tmp);
      Package p;
      fromManifest(a.as<JsonObjectConst>(), p);
      p.kind = p.engine = "js";
      p.category = categoryOf(id);
      if (!p.author.length()) p.author = "EspOS";
      if (!p.code) {  // as apps/pack.py numbers them
        int x = 0, y = 0, z = 0;
        sscanf(p.version.c_str(), "%d.%d.%d", &x, &y, &z);
        p.code = x * 10000 + y * 100 + z;
      }
      JsonDocument m;
      JsonObject mo = m.to<JsonObject>();
      toManifest(p, mo);
      mo["parts"].to<JsonArray>().add("core");
      JsonObject fe = mo["files"].to<JsonArray>().add<JsonObject>();
      fe["n"] = "main.js";
      fe["s"] = main.length();
      fe["c"] = crc32(0, main.c_str(), main.length());
      String ms;
      serializeJson(m, ms);
      if (!writeText(tmp + "/main.js", main) || !writeText(tmp + "/manifest.json", ms) || rename(tmp.c_str(), dir.c_str())) {
        rmTree(tmp);
        trace::add("pkg: could not convert app %s", id.c_str());
        continue;
      }
      Rec r;
      if (scanDir(dir, FLASH, r)) recs.push_back(r);
    }
    unlink(path.c_str());
    n++;
  }
  if (n) {
    trace::add("pkg: %d JavaScript apps of 0.2.x are packages now", n);
    if (desk::setting("s3.apps", "[]") != "[]") desk::setSetting("s3.apps", "[]");
  }
}

void refresh() {
  if (dirty.exchange(false)) {
    rebuild();
    ver++;
  }
}

void tickCb(lv_timer_t*) {
  card::poll();
  refresh();      // before the store looks at finished jobs: s3.pkgs is written from the fresh list
  store::tick();
  refresh();
}

}  // namespace

void begin() {
  if (started) return;
  started = true;
  const uint32_t t0 = millis();
  {
    std::lock_guard<std::recursive_mutex> l(mu);
    area::begin();
    for (const String& j : idx::all()) {
      Rec r;
      if (recFromJson(j, r) && !rec(r.p.id)) recs.push_back(r);
    }
#ifndef NO_PET
    adoptPet();
#endif
    // Small apps on LittleFS; leftovers of an install or a move cut short.
    mkdirs(lfsRoot());
    for (const String& n : listDir(lfsRoot())) {
      if (n.startsWith(".old-") && !exists(lfsRoot() + "/" + n.substring(5)))
        rename((lfsRoot() + "/" + n).c_str(), (lfsRoot() + "/" + n.substring(5)).c_str());
      else if (n.startsWith(".")) rmTree(lfsRoot() + "/" + n);
    }
    for (Rec& r : scanRoot(lfsRoot(), FLASH))
      if (!rec(r.p.id)) recs.push_back(r);
    convertOldApps();
    // Apps on the card, as last seen (greyed until the card answers).
    JsonDocument d;
    if (deserializeJson(d, readText(sdCacheFile())) == DeserializationError::Ok)
      for (JsonObjectConst m : d.as<JsonArrayConst>()) {
        Rec r;
        fromManifest(m, r.p);
        r.p.vol = SD;
        r.p.available = false;
        r.p.bytes = (m["kb"] | 0u) * 1024;
        r.dir = sdRoot() + "/" + r.p.id;
        if (r.p.id.length() && !rec(r.p.id)) recs.push_back(r);
      }
    ioRelease();
  }
  card::begin();
  rebuild();
  lv_timer_create(tickCb, 100, nullptr);
  store::begin();
  trace::add("pkg: %u packages, area %u KB free, started in %lu ms", (unsigned)pub.size(),
             (unsigned)(area::freeBytes() / 1024), (unsigned long)(millis() - t0));
}

const std::vector<Package>& list() {
  begin();
  return pub;
}

const Package* find(const char* id) {
  begin();
  if (!id) return nullptr;
  for (const Package& p : pub)
    if (p.id == id) return &p;
  return nullptr;
}

uint32_t version() { return ver; }
bool cardChecked() { return in::card::probed(); }

void awaitCard(uint32_t ms) {
  begin();
  const uint32_t t0 = millis();
  while (!in::card::probed() && millis() - t0 < ms) delay(5);
  in::card::poll();  // what the probe found, taken in now (setup: no LVGL timers yet)
  refresh();
  trace::add("pkg: card %s after %lu ms", in::card::present() ? "read" : in::card::probed() ? "not in" : "still looking",
             (unsigned long)(millis() - t0));
}

void relabel() {
  rebuild();
  ver++;
}

VolInfo volume(Vol v) {
  begin();
  VolInfo i;
  uint64_t pk = 0;
  for (const Package& p : pub)
    if (p.vol == v) pk += p.bytes;
  i.pkgsKB = (pk + 1023) / 1024;
  if (v == FLASH) {
    uint64_t t = 0, f = 0;
    lfsInfo(t, f);
    std::lock_guard<std::recursive_mutex> l(mu);
    i.present = true;
    i.totalKB = (area::size() + t) / 1024;
    i.freeKB = (area::freeBytes() + f) / 1024;
  } else {
    uint64_t t = 0, f = 0;
    i.present = card::info(t, f);
    i.totalKB = t / 1024;
    i.freeKB = f / 1024;
  }
  return i;
}

uint32_t restartKB() {
  std::lock_guard<std::recursive_mutex> l(mu);
  return area::zombieBytes() / 1024;
}

String sizeText(uint64_t b) { return in::sizeStr(b); }

bool engineKnown(const String& e) {
  if (e == "js" || e == "lang" || e == "font") return true;  // lang, font: language packs (i18n.cpp, fonts.cpp)
  if (e == "wake") return true;  // a wake word's model (src/wake/wake.cpp)
#ifndef NO_PET
  if (e == "pet") return true;
#endif
#ifdef HAVE_RUN
  if (e == "run") return true;
#endif
#ifdef HAVE_J2ME
  if (e == "j2me") return true;
#endif
#ifdef HAVE_TANK
  if (e == "tank") return true;
#endif
  return false;
}

// ---------------------------------------------------------------- files
bool File::read(uint32_t off, void* dst, uint32_t len) const {
  if (off + len > len_ || off + len < off) return false;
  if (map_) {
    memcpy(dst, map_ + off, len);
    return true;
  }
  return path_.length() && readAt(path_, off, dst, len);
}

bool open(const char* id, const char* name, File& f) {
  f = File();
  if (!id || !name) return false;
  {
    std::lock_guard<std::recursive_mutex> l(mu);
    Rec* r = rec(id);
    if (r) {
      const FileRec* fr = r->file(name);
      if (!r->p.available || busyIds.count(id) || !fr) return false;
      f.len_ = fr->size;
      if (r->area) {
        f.addr_ = fr->addr;
        f.map_ = area::base() + fr->addr;
        pinnedIds.insert(id);  // its bytes may be in use until the next restart
      } else {
        f.path_ = r->dir + "/" + name;
      }
      return true;
    }
  }
  return false;
}

String manifestJson(const char* id) {
  String dir;
  {
    std::lock_guard<std::recursive_mutex> l(mu);
    Rec* r = id ? rec(id) : nullptr;
    if (!r || r->area || !r->p.available) return "";
    dir = r->dir;
  }
  return dir.length() ? readText(dir + "/manifest.json") : "";
}

uint8_t* loadAll(const File& f) {
  if (!f.ok()) return nullptr;
  uint8_t* b = (uint8_t*)heap_caps_malloc(f.size() ? f.size() : 1, MALLOC_CAP_SPIRAM);
  if (b && !f.read(0, b, f.size())) {
    heap_caps_free(b);
    b = nullptr;
  }
  return b;
}

// ---------------------------------------------------------------- starting an app
void run(const char* id, const char* arg) {
  const Package* p = find(id);
  if (!p) {
    os::toast(N_("This app is not installed. Get it from the Store."));
    return;
  }
  if (!p->available) {
    os::toast(N_("Needs the memory card"));
    return;
  }
  if (store::stateOf(id) != store::IDLE) {
    os::toast(N_("This app is being installed or moved"));
    return;
  }
  trace::add("pkg: run %s (%s) %s", id, p->engine.c_str(), arg ? arg : "");
  // Engines that read their data before their first picture (the aquarium's water, a Java game's classes, a
  // JavaScript app's code) open under a cover drawn at once: its icon, its name, a bar; Home no longer stands still
  // under the finger meanwhile (0.5.7). PawBoop and the run game show their own splash and spinner.
  const bool covered = p->engine == "js" || p->engine == "j2me" || p->engine == "tank";
  const uint32_t t0 = millis();
  if (covered) splash::cover(id, p->name.c_str());
  if (p->engine == "js") jsapp::open(id, arg);
#ifndef NO_PET
  else if (p->engine == "pet" && p->vol == SD && !pet::ready()) {
    // The card did not give the packs (a read error): its page offers to move it back to Máy.
    os::toast(N_("Cannot read PawBoop on the memory card"));
    apps::open("store", id);
  } else if (p->engine == "pet") {
    app_pet::open(arg);
  }
#endif
  else if (p->engine == "run") run::open(id, arg);
#ifdef HAVE_J2ME
  else if (p->engine == "j2me") j2me_app::open(id, arg);
#endif
#ifdef HAVE_TANK
  else if (p->engine == "tank") tank::open(id, arg);
#endif
  else os::toast(N_("Needs a software update"));
  if (covered) {
    splash::coverDone();
    trace::add("pkg: %s opened in %lu ms", id, (unsigned long)(millis() - t0));
  }
}

}  // namespace pkg
