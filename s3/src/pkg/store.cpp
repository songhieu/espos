// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
// The store's catalog, the installer jobs (install, update, remove, move), the sync of setting s3.pkgs and the
// sync report (docs/ecosystem.md §5-6).
#include "store.h"

#include <algorithm>
#include <deque>
#include <map>

#include "board.h"
#include "desk.h"
#include "espk.h"
#include "i18n.h"
#include "internal.h"
#include "os.h"
#include "sound.h"
#include "trace.h"

#ifdef SIM
#include <cstdlib>
#include <string>
#include <thread>
extern std::string simDir;
#endif

namespace pkg {
namespace store {

using namespace in;

bool installAs(const Entry& e, const std::vector<String>& parts, Vol v, String& why, bool user);

namespace {

const char* CATALOG = ESPOS_API "/storage/bb/149/apps-s3/catalog.json";

int fwCode() {
#ifdef SIM
  if (const char* c = getenv("SIM_FW_CODE")) return atoi(c);  // the simulator as another firmware
#endif
  const char* p = strrchr(FW_VERSION, '-');
  p = p ? p + 1 : FW_VERSION;
  int a = 0, b = 0, c = 0;
  sscanf(p, "%d.%d.%d", &a, &b, &c);
  return a * 10000 + b * 100 + c;
}

// At most n characters (UTF-8 code points), never cut inside one.
String clip(const String& s, unsigned n) {
  unsigned chars = 0, i = 0;
  for (; i < s.length(); i++)
    if (((uint8_t)s[i] & 0xC0) != 0x80 && ++chars > n) break;
  return i < s.length() ? s.substring(0, i) : s;
}

uint32_t hexColor(const char* s, uint32_t def) {
  if (!s || s[0] != '#' || strlen(s) != 7) return def;
  return strtoul(s + 1, nullptr, 16);
}

// ---------------------------------------------------------------- catalog
std::vector<Entry> cat;
bool catLoaded = false, catFailed = false, catLoading = false;
uint32_t catVer = 1, catAt = 0;

// The user's own packages (Java games uploaded on the web): catalog-shaped entries the sync answer carries in
// "user_pkgs", with signed links (web/backend device_sync). Not listed in the store; installed through s3.pkgs.
std::vector<Entry> userCat;

Entry parseEntry(JsonObjectConst a) {
    Entry e;
    e.id = a["id"] | "";
    e.name = a["name"] | e.id.c_str();
    e.version = a["version"] | "";
    e.code = a["code"] | 0u;
    e.kind = a["kind"] | "js";
    e.engine = a["engine"] | (e.kind == "js" ? "js" : "");
    e.minFw = a["min_fw"] | 0u;
    e.category = a["category"] | "tool";
    e.author = a["author"] | "";
    e.desc = a["desc"] | "";
    JsonObjectConst tr = a["i18n"][i18n::lang()];  // its name and description in the language shown
    if (tr["name"].is<const char*>()) e.name = tr["name"].as<const char*>();
    if (tr["desc"].is<const char*>()) e.desc = tr["desc"].as<const char*>();
    e.glyph = a["icon"]["glyph"] | "grid";
    e.bg1 = hexColor(a["icon"]["bg"][0] | "", 0x64748b);
    e.bg2 = hexColor(a["icon"]["bg"][1] | "", e.bg1);
    e.iconUrl = a["icon_url"] | "";
    e.addonOf = a["addon_of"] | "";
    e.type = a["type"] | "";
    e.set = a["set"] | "";
    e.needs = a["needs"] | "";
    for (JsonVariantConst p : a["perms"].as<JsonArrayConst>()) e.perms.push_back(p | "");
    for (JsonObjectConst p : a["parts"].as<JsonArrayConst>()) {
      Part pt;
      pt.id = p["id"] | "";
      pt.group = p["group"] | "";
      pt.label = p["label"] | pt.id.c_str();
      if (p["labels"][i18n::lang()].is<const char*>()) pt.label = p["labels"][i18n::lang()].as<const char*>();  // shown
      pt.url = p["url"] | "";
      pt.sha256 = p["sha256"] | "";
      pt.size = p["size"] | 0u;
      pt.req = p["req"] | !pt.group.length();  // no group: required (publish_pkg.py)
      pt.def = p["default"] | false;
      pt.board = p["board"] | "";
      if (pt.id.length() && pt.url.length()) e.parts.push_back(pt);
    }
    return e;
}

void catalogCb(void*, int status, JsonDocument* doc, const uint8_t*, size_t) {
  catLoading = false;
  catLoaded = true;
  catAt = millis();
  catVer++;
  if (status != 200 || !doc) {
    catFailed = true;
    return;
  }
  catFailed = false;
  cat.clear();
  for (JsonObjectConst a : (*doc)["packages"].as<JsonArrayConst>()) {
    Entry e = parseEntry(a);
    if (e.id.length() && e.parts.size()) cat.push_back(e);
  }
  trace::add("store: catalog has %u packages", (unsigned)cat.size());
}

// ---------------------------------------------------------------- jobs
struct Job {
  enum Kind : uint8_t { INSTALL, REMOVE, MOVE } kind = INSTALL;
  String id, name;
  std::vector<Part> parts;
  Vol vol = FLASH;
  bool js = false, user = true;
  String mainOf, mainName;  // an add-on: its main package must be installed by the time it runs
};
struct Finished {
  Job job;
  bool ok;
  String err;
};

std::mutex qmu;  // queue, cur, st, finished
std::deque<Job> queue;
Job cur;
bool running = false;
Status st;
std::vector<Finished> finished;
uint32_t jobVer = 1;
std::map<String, uint8_t> syncFails;  // installs asked by the web that failed this boot

void setState(State s, uint64_t done = 0, uint64_t total = 0) {
  std::lock_guard<std::mutex> l(qmu);
  st.state = s;
  st.done = done, st.total = total;
  st.pct = total ? (int)(done * 100 / total) : 0;
  st.seq++;
}

void progress(uint64_t done, uint64_t total) {
  std::lock_guard<std::mutex> l(qmu);
  const int pct = total ? (int)(done * 100 / total) : 0;
  st.done = done, st.total = total;
  if (pct != st.pct) st.pct = pct, st.seq++;
}

void compactProgress(uint32_t d, uint32_t t) { progress(d, t); }

// Frees what an old copy of a package holds, once the new one is committed (caller holds mu).
void dropCopy(const Rec& old) {
  if (old.area) {
    const bool inUse = pinned(old.p.id);
    for (const FileRec& f : old.files) {
      if (!f.size) continue;
      if (inUse) {
        area::zombie(f.addr, f.size);  // still read by its engine until the restart
        area::tombstone(f.addr);
      } else {
        area::erase(f.addr, SECTOR);  // its header gone: never taken for a package again
        area::freed(f.addr, f.size);  // the rest is erased while the device is idle
      }
    }
  } else if (old.dir.length()) {
    closeHandles(old.dir + "/");
    rmTree(old.dir);
  }
}

void replaceRec(const Rec& nr) {
  for (Rec& r : recs)
    if (r.p.id == nr.p.id) {
      r = nr;
      return;
    }
  recs.push_back(nr);
}

void eraseRec(const String& id) {
  for (auto it = recs.begin(); it != recs.end(); ++it)
    if (it->p.id == id) {
      recs.erase(it);
      return;
    }
}

String manifestText(const Package& p, const std::vector<FileRec>& files, const String& coreManifest) {
  JsonDocument d;
  if (coreManifest.length()) deserializeJson(d, coreManifest);
  if (!d.is<JsonObject>()) d.to<JsonObject>();
  JsonObject m = d.as<JsonObject>();
  if (!coreManifest.length()) toManifest(p, m);
  m.remove("part");
  JsonArray parts = m["parts"].to<JsonArray>();
  for (const String& s : p.parts) parts.add(s);
  JsonArray fa = m["files"].to<JsonArray>();
  for (const FileRec& f : files) {
    JsonObject o = fa.add<JsonObject>();
    o["n"] = f.name;
    o["s"] = f.size;
    o["c"] = f.crc;
  }
  String out;
  serializeJson(d, out);
  return out;
}

// ---- install: the .espk files' bytes go straight to their place.
struct Writer : EspkReader::Sink {
  const Job& job;
  bool toDir;
  String tmp;
  std::vector<FileRec> files;
  std::vector<int> map;  // this part's table -> files
  String core;           // the core part's manifest
  String partId;
  FILE* fp = nullptr;
  int fpFile = -1;
  uint32_t erasedTo = 0;
  uint32_t eraseMs = 0, writeMs = 0, checkMs = 0, blankKB = 0;  // where the time goes (trace)

  Writer(const Job& j, bool dir, const String& t) : job(j), toDir(dir), tmp(t) {}

  bool layout(const String& manifest, const std::vector<EspkFile>& tab, String& why) override {
    JsonDocument d;
    if (deserializeJson(d, manifest) != DeserializationError::Ok) return why = T_("the package is broken (manifest)"), false;
    if (job.id != (d["id"] | "")) return why = T_("the package is for another app"), false;
    if (partId == "core" || !core.length()) {
      if (!d["kind"].isNull()) core = manifest;
      const String engine = d["engine"] | (!strcmp(d["kind"] | "js", "js") ? "js" : "");
      if (!d["kind"].isNull() && !engineKnown(engine)) return why = T_("Needs a software update"), false;
      if ((d["min_fw"] | 0) > fwCode()) return why = T_("Needs a software update"), false;
    }
    map.clear();
    std::lock_guard<std::recursive_mutex> l(mu);
    for (const EspkFile& f : tab) {
      for (const FileRec& o : files)
        if (o.name == f.name) return why = T_("two parts have files with the same name"), false;
      FileRec r;
      r.name = f.name, r.size = f.size, r.crc = f.crc;
      if (!toDir && !area::alloc(f.size, r.addr)) return why = T_("Not enough room on the device"), false;
      map.push_back(files.size());
      files.push_back(r);
    }
    return true;
  }

  bool data(int i, uint32_t at, const uint8_t* p, uint32_t n, String& why) override {
    FileRec& f = files[map[i]];
    if (toDir) {
      if (fpFile != map[i]) {
        if (fp) fclose(fp);
        const String path = tmp + "/" + f.name;
        const int slash = path.lastIndexOf('/');
        mkdirs(path.substring(0, slash));
        fp = fopen(path.c_str(), "wb");
        fpFile = map[i];
        if (!fp) return why = T_("cannot write a file"), false;
      }
      if (fwrite(p, 1, n, fp) != n) return why = job.vol == SD ? T_("cannot write to the memory card") : T_("the storage is full"), false;
      return true;
    }
    if (at == 0) erasedTo = f.addr;
    const uint32_t end = f.addr + at + n, extEnd = f.addr + roundUp(f.size);
    while (erasedTo < end) {
      // Up to the next 64 KB boundary: whole blocks erase much faster than sectors. Space erased while the device
      // was idle (or never used) is not erased again: reading 64 KB costs ~2 ms, erasing it ~150-400 ms.
      const uint32_t to = min(extEnd, (erasedTo & ~0xFFFFu) + 0x10000u);
      const uint32_t t0 = millis();
      if (area::blank(erasedTo, to - erasedTo)) {
        blankKB += (to - erasedTo) / 1024;
      } else if (!area::erase(erasedTo, to - erasedTo)) {
        return why = T_("cannot erase the flash"), false;
      }
      eraseMs += millis() - t0;
      erasedTo = to;
    }
    const uint32_t t1 = millis();
    if (!area::write(f.addr + at, p, n)) return why = T_("cannot write the flash"), false;
    writeMs += millis() - t1;
    return true;
  }

  bool done(int i, String& why) override {
    FileRec& f = files[map[i]];
    if (toDir) {
      if (fpFile == map[i] && fp) {
        const bool ok = !fclose(fp);
        fp = nullptr, fpFile = -1;
        if (!ok) return why = T_("cannot write a file"), false;
      } else if (!f.size) {
        const String path = tmp + "/" + f.name;
        mkdirs(path.substring(0, path.lastIndexOf('/')));
        FILE* e = fopen(path.c_str(), "wb");
        if (!e) return why = T_("cannot write a file"), false;
        fclose(e);
      }
      return true;
    }
    // Read back what the flash holds now.
    const uint32_t t0 = millis();
    const bool good = !f.size || crc32(0, area::base() + f.addr, f.size) == f.crc;
    checkMs += millis() - t0;
    if (!good) return why = T_("writing to the flash failed"), false;
    return true;
  }

  void cleanup() {
    if (fp) fclose(fp), fp = nullptr;
    std::lock_guard<std::recursive_mutex> l(mu);
    if (!toDir)
      for (const FileRec& f : files) area::unreserve(f.addr), area::freed(f.addr, f.size);
    if (toDir) rmTree(tmp);
  }
};

bool lookup(const String& id, Package& out);

bool doInstall(const Job& j, String& err) {
  if (j.mainOf.length()) {
    Package m;
    if (!lookup(j.mainOf, m)) return err = i18n::fmt(N_("Install {0} first"), j.mainName), false;
  }
  const bool toDir = j.vol == SD || j.js;
  const String root = j.vol == SD ? sdRoot() : lfsRoot();
  uint64_t need = 0;
  for (const Part& p : j.parts) need += p.size;
  if (j.vol == SD && !card::present()) return err = T_("Needs the memory card"), false;
  if (!toDir) {
    std::lock_guard<std::recursive_mutex> l(mu);
    const uint32_t want = min<uint64_t>(roundUp(need) + SECTOR * 4 * j.parts.size(), area::freeBytes());
    if (area::freeBytes() < need)
      return err = area::freeBytes() + area::zombieBytes() >= need ? T_("Restart the device to use the room just freed") : T_("Not enough room on the device"),
             false;
    if (area::largestGap() < want) {
      setState(COMPACT);
      area::compact(want, compactProgress);
    }
  }
  const String tmp = root + "/.new-" + j.id;
  if (toDir) {
    rmTree(tmp);
    if (!mkdirs(tmp)) return err = j.vol == SD ? T_("cannot write to the memory card") : T_("cannot write the flash"), false;
  }
  Writer w(j, toDir, tmp);
  uint64_t base = 0;
  bool ok = true;
  for (const Part& part : j.parts) {
    setState(DOWNLOAD, base, need);
    w.partId = part.id;
    Source* src = openSource(part.url, part.size);
    if (!src) {
      err = T_("cannot download");
      ok = false;
      break;
    }
    EspkReader rd(&w);
    Sha256 sha;
    const uint8_t* p;
    size_t n;
    uint64_t got = 0;
    uint32_t waitMs = 0, feedMs = 0, partT0 = millis();
    while (ok) {
      uint32_t t = millis();
      const bool more = src->next(p, n);
      waitMs += millis() - t;
      if (!more) {
        err = src->err.length() ? src->err : String(T_("the network dropped midway"));
        ok = false;
        break;
      }
      if (!n) break;
      sha.update(p, n);
      got += n;
      t = millis();
      if (!rd.feed(p, n)) err = rd.err(), ok = false;
      feedMs += millis() - t;
      src->release();
      progress(base + max<uint64_t>(got, src->got()), need);
    }
    delete src;
    trace::add("store: part %s %u KB in %lu ms (waiting for the network %u ms, writing %u ms: erase %u ms, %u KB "
               "already blank, program %u ms, read-back %u ms)",
               part.id.c_str(), (unsigned)(got / 1024), (unsigned long)(millis() - partT0), (unsigned)waitMs,
               (unsigned)feedMs, (unsigned)w.eraseMs, (unsigned)w.blankKB, (unsigned)w.writeMs, (unsigned)w.checkMs);
    w.eraseMs = w.writeMs = w.checkMs = w.blankKB = 0;
    if (ok && !rd.finish()) err = rd.err(), ok = false;
    if (ok && part.size && got != part.size) err = T_("a downloaded file has the wrong size"), ok = false;
    if (ok && part.sha256.length() && !sha.hex().equalsIgnoreCase(part.sha256)) err = T_("a downloaded file is wrong (SHA-256)"), ok = false;
    if (!ok) {
      trace::add("store: %s part %s failed: %s", j.id.c_str(), part.id.c_str(), err.c_str());
      break;
    }
    base += part.size;
  }
  if (!ok) {
    w.cleanup();
    return false;
  }
  // Commit: the index (area) or the folder rename (LittleFS, card). Before this nothing of the old copy changed.
  setState(WRITE, need, need);
  Rec nr;
  {
    JsonDocument d;
    deserializeJson(d, w.core);
    fromManifest(d.as<JsonObjectConst>(), nr.p);
  }
  nr.p.id = j.id;
  if (!nr.p.name.length() || nr.p.name == nr.p.id) nr.p.name = j.name;
  nr.p.parts.clear();
  for (const Part& p : j.parts) nr.p.parts.push_back(p.id);
  nr.p.vol = j.vol;
  nr.p.bytes = 0;
  nr.files = w.files;
  for (const FileRec& f : nr.files) {
    nr.p.bytes += f.size;
    if (f.name == "icon.jpg") nr.p.icon = true;
  }
  std::lock_guard<std::recursive_mutex> l(mu);
  Rec* old = rec(j.id);
  Rec oldCopy;
  const bool hadOld = old != nullptr;
  if (old) oldCopy = *old;
  if (!toDir) {
    nr.area = true;
    if (!idx::put(j.id, recJson(nr))) {
      w.cleanup();
      return err = T_("cannot write the app list"), false;
    }
    replaceRec(nr);
    for (const FileRec& f : nr.files) area::unreserve(f.addr);
  } else {
    if (!writeText(tmp + "/manifest.json", manifestText(nr.p, nr.files, w.core))) {
      w.cleanup();
      return err = T_("cannot write a file"), false;
    }
    const String fin = root + "/" + j.id, prev = root + "/.old-" + j.id;
    closeHandles(fin + "/");
    if (exists(fin)) {
      rmTree(prev);
      if (rename(fin.c_str(), prev.c_str())) {
        w.cleanup();
        return err = T_("cannot replace the old version"), false;
      }
    }
    if (rename(tmp.c_str(), fin.c_str())) {
      rename(prev.c_str(), fin.c_str());
      w.cleanup();
      return err = T_("cannot write a file"), false;
    }
    rmTree(prev);
    Rec r;
    if (!scanDir(fin, j.vol, r)) r = nr, r.dir = fin;
    nr = r;
    replaceRec(nr);
    if (hadOld && !oldCopy.area && oldCopy.dir == fin) oldCopy.dir = "";  // already replaced in place
  }
  if (hadOld && oldCopy.area) {
    if (!nr.area) idx::del(j.id);
    dropCopy(oldCopy);
  } else if (hadOld) {
    dropCopy(oldCopy);
  }
  changed();
  trace::add("store: installed %s %s on %s (%u KB)", j.id.c_str(), nr.p.version.c_str(), j.vol == SD ? "card" : "flash",
             (unsigned)(nr.p.bytes / 1024));
  return true;
}

bool doRemove(const Job& j, String& err) {
  setState(REMOVE);
  std::lock_guard<std::recursive_mutex> l(mu);
  Rec* r = rec(j.id);
  if (!r) return true;
  const Rec copy = *r;
  if (copy.p.vol == SD && !card::present()) return err = T_("Insert the memory card with this app to remove it"), false;
  if (copy.area && !idx::del(j.id)) return err = T_("cannot write the app list"), false;
  eraseRec(j.id);
  dropCopy(copy);
  changed();
  trace::add("store: removed %s", j.id.c_str());
  return true;
}

// Copies every file of `r` to the folder `tmp` (from the area or another folder), checking CRCs.
bool copyToDir(const Rec& r, const String& tmp, uint64_t total, String& err) {
  uint8_t* buf = (uint8_t*)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
  if (!buf) return err = T_("not enough memory"), false;
  uint64_t done = 0;
  bool ok = true;
  for (const FileRec& f : r.files) {
    const String path = tmp + "/" + f.name;
    mkdirs(path.substring(0, path.lastIndexOf('/')));
    FILE* out = fopen(path.c_str(), "wb");
    if (!out) {
      err = T_("cannot write a file"), ok = false;
      break;
    }
    uint32_t crc = 0;
    for (uint32_t at = 0; ok && at < f.size; at += 32768) {
      const uint32_t n = min<uint32_t>(32768, f.size - at);
      if (r.area) memcpy(buf, area::base() + f.addr + at, n);
      else if (!readAt(r.dir + "/" + f.name, at, buf, n)) err = T_("cannot read a file"), ok = false;
      if (ok && fwrite(buf, 1, n, out) != n) err = T_("cannot write a file (full?)"), ok = false;
      crc = crc32(crc, buf, n);
      done += n;
      pace(done);
      progress(done, total);
    }
    if (fclose(out)) err = T_("cannot write a file"), ok = false;
    if (ok && f.crc && crc != f.crc) err = T_("the original file is damaged"), ok = false;
    if (!ok) break;
  }
  heap_caps_free(buf);
  return ok;
}

bool doMove(const Job& j, String& err) {
  Rec r;
  {
    std::lock_guard<std::recursive_mutex> l(mu);
    Rec* p = rec(j.id);
    if (!p) return err = T_("the app is gone"), false;
    r = *p;
  }
  if (r.p.vol == j.vol) return true;
  if ((j.vol == SD || r.p.vol == SD) && !card::present()) return err = T_("Needs the memory card"), false;
  uint64_t total = 0;
  for (const FileRec& f : r.files) total += f.size;
  const bool js = onLfs(r.p.kind, r.p.engine, r.p.bytes);
  const bool toDir = j.vol == SD || js;
  setState(MOVE, 0, total);
  setBusy(j.id, true);
  closeHandles(r.dir + "/");
  bool ok = true;
  Rec nr = r;
  nr.p.vol = j.vol;
  if (toDir) {
    const String root = j.vol == SD ? sdRoot() : lfsRoot(), tmp = root + "/.new-" + j.id, fin = root + "/" + j.id;
    rmTree(tmp);
    ok = mkdirs(tmp) && copyToDir(r, tmp, total, err);
    if (ok) {
      std::vector<FileRec> files = r.files;
      for (FileRec& f : files) f.addr = 0;
      ok = writeText(tmp + "/manifest.json", manifestText(r.p, files, readText(r.dir + "/manifest.json")));
      if (!ok) err = T_("cannot write a file");
    }
    if (ok) {
      rmTree(fin);
      ok = !rename(tmp.c_str(), fin.c_str());
      if (!ok) err = T_("cannot write a file");
    }
    if (!ok) rmTree(tmp);
    else {
      std::lock_guard<std::recursive_mutex> l(mu);
      if (!scanDir(fin, j.vol, nr)) nr.dir = fin, nr.area = false;
      if (r.area) idx::del(j.id);
      replaceRec(nr);
      dropCopy(r);
    }
  } else {
    // Card -> the internal area.
    {
      std::lock_guard<std::recursive_mutex> l(mu);
      uint64_t need = 0;
      for (const FileRec& f : r.files) need += roundUp(f.size);
      if (area::freeBytes() < need) ok = false, err = T_("Not enough room on the device");
      else if (area::largestGap() < need) {
        setState(COMPACT);
        area::compact(need, compactProgress);
        setState(MOVE, 0, total);
      }
      for (FileRec& f : nr.files)
        if (ok && !area::alloc(f.size, f.addr)) ok = false, err = T_("Not enough room on the device");
    }
    uint8_t* buf = (uint8_t*)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    if (!buf) ok = false, err = T_("not enough memory");
    uint64_t done = 0;
    for (const FileRec& f : nr.files) {
      if (!ok) break;
      uint32_t erased = f.addr;
      for (uint32_t at = 0; ok && at < f.size; at += 32768) {
        const uint32_t n = min<uint32_t>(32768, f.size - at);
        if (!readAt(r.dir + "/" + f.name, at, buf, n)) {
          err = T_("cannot read the memory card"), ok = false;
          break;
        }
        while (ok && erased < f.addr + at + n) {
          const uint32_t to = min(f.addr + roundUp(f.size), (erased & ~0xFFFFu) + 0x10000u);
          ok = area::erase(erased, to - erased);
          erased = to;
        }
        ok = ok && area::write(f.addr + at, buf, n);
        if (!ok && !err.length()) err = T_("cannot write the flash");
        done += n;
        pace(done);
        progress(done, total);
      }
      if (ok && f.size && crc32(0, area::base() + f.addr, f.size) != f.crc && f.crc) err = T_("writing to the flash failed"), ok = false;
    }
    heap_caps_free(buf);
    std::lock_guard<std::recursive_mutex> l(mu);
    nr.area = true;
    nr.dir = "";
    if (ok && !idx::put(j.id, recJson(nr))) ok = false, err = T_("cannot write the app list");
    for (const FileRec& f : nr.files) area::unreserve(f.addr);
    if (ok) {
      replaceRec(nr);
      dropCopy(r);
    }
  }
  setBusy(j.id, false);
  changed();
  if (ok) trace::add("store: moved %s to %s", j.id.c_str(), j.vol == SD ? "card" : "flash");
  return ok;
}

bool execute(const Job& j, String& err) {
  bool card = j.vol == SD;
  {
    std::lock_guard<std::recursive_mutex> l(mu);
    card |= rec(j.id) && rec(j.id)->p.vol == SD;
  }
  if (card) card::setBusy(true);
  bool ok = false;
  if (j.kind == Job::INSTALL) ok = doInstall(j, err);
  else if (j.kind == Job::REMOVE) ok = doRemove(j, err);
  else ok = doMove(j, err);
  if (card) card::setBusy(false);
  return ok;
}

#ifdef SIM
std::thread worker;
void stopAtExit() {
  abortAll = true;
  if (worker.joinable()) worker.join();
}
#endif

void runner(void*) {
  for (;;) {
    Job j;
    {
      std::lock_guard<std::mutex> l(qmu);
      if (queue.empty() || abortAll) {
        running = false;
        break;
      }
      j = queue.front();
      queue.pop_front();
      cur = j;
      st = Status();
      st.id = j.id, st.name = j.name;
      st.state = j.kind == Job::REMOVE ? REMOVE : j.kind == Job::MOVE ? MOVE : DOWNLOAD;
      st.kind = j.kind == Job::REMOVE ? 1 : j.kind == Job::MOVE ? 2 : 0;
      st.seq = ++jobVer;
    }
    trace::add("store: job %s %s", j.kind == Job::INSTALL ? "install" : j.kind == Job::REMOVE ? "remove" : "move",
               j.id.c_str());
    paceIdle(false);
    paceStart();
    ioBuf();  // its 4 KB of internal RAM before a TLS handshake takes what is left
    String err;
    const bool ok = execute(j, err);
    {
      std::lock_guard<std::mutex> l(qmu);
      st.state = ok ? DONE : ERROR;
      st.err = err;
      if (ok) st.pct = 100;
      st.seq++;
      finished.push_back({j, ok, err});
      cur = Job();
    }
    if (!ok) trace::add("store: %s failed: %s", j.id.c_str(), err.c_str());
  }
  paceIdle(true);
  ioRelease();
#ifndef SIM
  vTaskDelete(nullptr);
#endif
}

void startRunner() {
  {
    std::lock_guard<std::mutex> l(qmu);
    if (running || queue.empty()) return;
    running = true;
  }
#ifdef SIM
  if (worker.joinable()) worker.join();
  static bool hooked = false;
  if (!hooked) hooked = true, atexit(stopAtExit);
  worker = std::thread(runner, nullptr);
#else
  // Flash writes need the task's stack in internal RAM (the cache is off while they run).
  if (xTaskCreatePinnedToCore(runner, "pkgjob", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
    std::lock_guard<std::mutex> l(qmu);
    running = false;
  }
#endif
}

bool enqueue(const Job& j, String& why) {
  std::lock_guard<std::mutex> l(qmu);
  if (cur.id == j.id && running) return why = T_("Something else is being done with this app"), false;
  for (const Job& q : queue)
    if (q.id == j.id) return why = T_("Already in the queue"), false;
  queue.push_back(j);
  jobVer++;
  return true;
}

// ---------------------------------------------------------------- s3.pkgs
uint32_t seenSettings = 0;
String seenPkgs;
uint32_t reconcileAt = 0;
bool started = false;

const char* volKey(Vol v) { return v == SD ? "sd" : "flash"; }

// One s3.pkgs entry follows what is installed; other keys (code, keys the web added) stay.
void fillEntry(JsonObject e, const Package& p) {
  e["id"] = p.id;
  e["vol"] = volKey(p.vol);
  if (p.parts.size() == 1 && p.parts[0] == "core") {
    e.remove("parts");
  } else {
    JsonArray parts = e["parts"].to<JsonArray>();
    for (const String& x : p.parts) parts.add(x);
  }
  e.remove("rm");
}

void savePkgs(JsonDocument& d) {
  String out;
  serializeJson(d, out);
  if (out.length() > 6000) {  // the server's limit: keep the web's list rather than send one it refuses
    trace::add("store: s3.pkgs would be %u bytes, not saved", (unsigned)out.length());
    return;
  }
  seenPkgs = out;
  desk::setSetting("s3.pkgs", out);
}

void loadPkgs(JsonDocument& d) {
  if (deserializeJson(d, desk::setting("s3.pkgs", "[]")) != DeserializationError::Ok || !d.is<JsonArray>())
    d.to<JsonArray>();
}

// The live record (not the UI list, which is rebuilt a tick later): what s3.pkgs and the reconcile look at.
bool lookup(const String& id, Package& out) {
  std::lock_guard<std::recursive_mutex> l(mu);
  const Rec* r = rec(id);
  if (r) out = r->p;
  return r != nullptr;
}

bool isQueued(const String& id) {
  std::lock_guard<std::mutex> l(qmu);
  if (running && cur.id == id) return true;
  for (const Job& q : queue)
    if (q.id == id) return true;
  return false;
}

void reconcile() {
  JsonDocument d;
  loadPkgs(d);
  JsonArray a = d.as<JsonArray>();
  bool dirtyList = false, needCatalog = false;
  for (size_t i = 0; i < a.size(); i++) {
    JsonObject e = a[i];
    const String id = e["id"] | "";
    if (!id.length()) {
      a.remove(i--);
      dirtyList = true;
      continue;
    }
    Package live;
    const Package* p = lookup(id, live) ? &live : nullptr;
    String why;
    if (e["rm"] | false) {
      if (!p) {
        a.remove(i--);
        dirtyList = true;
      } else if (!isQueued(id)) {
        Job j;
        j.kind = Job::REMOVE, j.id = id, j.name = p->name, j.user = false;
        enqueue(j, why);
      }
      continue;
    }
    const bool hasVol = !e["vol"].isNull();
    const Vol want = !strcmp(e["vol"] | "", "sd") ? SD : FLASH;
    if (!p) {
      if (isQueued(id) || syncFails[id] >= 3) continue;
      if (hasVol && want == SD && !card::present()) continue;
      const Entry* c = entry(id.c_str());
      if (!c) {
        needCatalog = true;
        continue;
      }
      // An add-on waits for its app (listed later, or still installing): tried again after each install.
      Package mainPkg;
      if (c->addonOf.length() && !lookup(c->addonOf, mainPkg) && !isQueued(c->addonOf)) continue;
      if (c->needs.length() && !lookup(c->needs, mainPkg) && !isQueued(c->needs)) continue;  // e.g. a map, the run
      std::vector<String> parts;
      for (JsonVariantConst v : e["parts"].as<JsonArrayConst>()) parts.push_back(v | "");
      if (parts.empty()) parts = defaultParts(*c);
      if (!installAs(*c, parts, hasVol ? want : FLASH, why, false)) {
        syncFails[id]++;
        trace::add("store: sync install %s refused: %s", id.c_str(), why.c_str());
      }
    } else if (hasVol && want != p->vol && p->available && (want != SD || card::present()) && !isQueued(id)) {
      if (!move(id.c_str(), want, why)) trace::add("store: sync move %s refused: %s", id.c_str(), why.c_str());
    } else if ((e["code"] | 0u) > p->code && p->available && !isQueued(id) && syncFails[id] < 3) {
      // "Update" on the web: the entry carries the catalog's code.
      const Entry* c = entry(id.c_str());
      if (!c) {
        needCatalog = true;
      } else if (c->code > p->code) {
        std::vector<String> parts;
        for (const String& x : p->parts)
          for (const Part& q : c->parts)
            if (q.id == x) parts.push_back(x);
        for (const Part& q : c->parts)
          if (q.req && std::find(parts.begin(), parts.end(), q.id) == parts.end()) parts.push_back(q.id);
        if (!installAs(*c, parts, p->vol, why, false)) syncFails[id]++;
      }
    }
  }
  // Packs (data for an engine, e.g. the Java runtime) are not listed in the store, so nobody presses "Update":
  // a newer one in the catalog is installed by itself.
  {
    std::vector<String> packs;
    {
      std::lock_guard<std::recursive_mutex> l(mu);
      for (const Rec& r : recs)
        if (r.p.type == "pack" && r.p.available) packs.push_back(r.p.id);
    }
    for (const String& id : packs) {
      const Entry* c = entry(id.c_str());
      Package live;
      if (c && lookup(id, live) && c->code > live.code && c->supported() && !isQueued(id) && syncFails[id] < 3) {
        String why;
        if (!update(id.c_str(), why)) syncFails[id]++, trace::add("store: pack %s update refused: %s", id.c_str(), why.c_str());
        else trace::add("store: pack %s %u -> %u", id.c_str(), (unsigned)live.code, (unsigned)c->code);
      }
    }
  }
  // Installed here but not in the list: added (left alone otherwise).
  std::vector<Package> all;
  {
    std::lock_guard<std::recursive_mutex> l(mu);
    for (const Rec& r : recs) all.push_back(r.p);
  }
  for (const Package& p : all) {
    bool listed = false;
    for (JsonObject e : a) listed |= p.id == (e["id"] | "");
    if (listed || a.size() >= 64) continue;
    fillEntry(a.add<JsonObject>(), p);
    dirtyList = true;
  }
  if (needCatalog && !catLoading && (!catLoaded || millis() - catAt > 60000)) refresh(true);
  if (dirtyList) savePkgs(d);
  startRunner();
}

// A job ended (UI loop): s3.pkgs follows what the device has.
void afterJob(const Finished& f) {
  JsonDocument d;
  loadPkgs(d);
  JsonArray a = d.as<JsonArray>();
  bool dirtyList = false;
  const int k = f.job.kind == Job::REMOVE ? 0 : f.job.kind == Job::MOVE ? 1 : 2;
  if (!f.ok) {
    if (!f.job.user) syncFails[f.job.id]++;
    static const char* const FAIL[] = {N_("Could not remove {0}: {1}"), N_("Could not move {0}: {1}"),
                                       N_("Could not install {0}: {1}")};
    os::toast(i18n::fmt(FAIL[k], f.job.name, f.err).c_str());
    return;
  }
  static const char* const OK[] = {N_("Removed {0}"), N_("Moved {0}"), N_("Installed {0}")};
  os::toast(i18n::fmt(OK[k], f.job.name).c_str());
  if (f.job.kind == Job::INSTALL) reconcileAt = millis() + 300;  // an add-on may have waited for this app
  Package live;
  const Package* p = f.job.kind != Job::REMOVE && lookup(f.job.id, live) ? &live : nullptr;
  bool found = false;
  for (size_t i = 0; i < a.size(); i++) {
    if (f.job.id != (a[i]["id"] | "")) continue;
    if (p && !found) fillEntry(a[i].as<JsonObject>(), *p), found = true;
    else a.remove(i--);
    dirtyList = true;
  }
  if (p && !found && a.size() < 64) {
    fillEntry(a.add<JsonObject>(), *p);
    dirtyList = true;
  }
  if (dirtyList) savePkgs(d);
}

}  // namespace

// ---------------------------------------------------------------- public
bool Entry::supported() const { return engineKnown(engine) && (int)minFw <= fwCode(); }

void refresh(bool force) {
  if (catLoading) return;
  if (!force && catLoaded && !catFailed && millis() - catAt < 5 * 60000) return;
  catLoading = true;
  desk::fetch(CATALOG, false, catalogCb, nullptr, true);
}

bool loaded() { return catLoaded && !catLoading; }
bool failed() { return catFailed; }
const std::vector<Entry>& catalog() { return cat; }
uint32_t catalogVersion() { return catVer; }

const Entry* entry(const char* id) {
  for (const Entry& e : cat)
    if (e.id == id) return &e;
  for (const Entry& e : userCat)
    if (e.id == id) return &e;
  return nullptr;
}

void setUserPkgs(JsonArrayConst list) {
  std::vector<Entry> out;
  for (JsonObjectConst a : list) {
    Entry e = parseEntry(a);
    // only the user's own ids ("u."), so a sync answer can never stand in for a store package
    if (e.id.startsWith("u.") && e.parts.size()) out.push_back(e);
  }
  userCat.swap(out);
  catVer++;
}

// A group's part made for another board, swapped for the group's part made for this one (the aquarium's backgrounds:
// bg35 / bg28). Whoever asked (the web's store picks the catalog's default), a 2.8" board never downloads 3.5" pictures.
void fitBoard(const Entry& e, std::vector<String>& parts) {
  for (String& id : parts)
    for (const Part& p : e.parts) {
      if (p.id != id || !p.group.length() || !p.board.length() || p.board == board::name()) continue;
      for (const Part& q : e.parts)
        if (q.group == p.group && q.board == board::name()) id = q.id;
    }
}

std::vector<String> defaultParts(const Entry& e) {
  std::vector<String> out, groups;
  for (const Part& p : e.parts)
    if (p.req) out.push_back(p.id);
  // One part of each group: the one made for this board, else its default, else its first.
  for (const Part& p : e.parts) {
    if (p.req || !p.group.length() || std::find(groups.begin(), groups.end(), p.group) != groups.end()) continue;
    groups.push_back(p.group);
    const Part* pick = &p;
    for (const Part& q : e.parts)
      if (q.group == p.group && q.def) {
        pick = &q;
        break;
      }
    for (const Part& q : e.parts)
      if (q.group == p.group && q.board.length() && q.board == board::name()) {
        pick = &q;
        break;
      }
    out.push_back(pick->id);
  }
  if (out.empty() && e.parts.size()) out.push_back(e.parts[0].id);
  return out;
}

uint64_t partsBytes(const Entry& e, const std::vector<String>& parts) {
  uint64_t n = 0;
  for (const Part& p : e.parts)
    for (const String& s : parts)
      if (p.id == s) n += p.size;
  return n;
}

String addonOf(const char* id) {
  if (const Entry* e = entry(id))
    if (e->addonOf.length()) return e->addonOf;
  const Package* p = find(id);
  return p ? p->addonOf : String();
}

std::vector<String> addons(const char* mainId) {
  std::vector<String> out;
  for (const Entry& e : cat)
    if (e.addonOf == mainId) out.push_back(e.id);
  for (const Package& p : list())
    if (p.addonOf == mainId && std::find(out.begin(), out.end(), p.id) == out.end()) out.push_back(p.id);
  return out;
}

bool hasUpdate(const char* id) {
  const Package* p = find(id);
  const Entry* e = entry(id);
  return p && e && e->code > p->code && e->supported();
}

bool onLfs(const String& kind, const String& engine, uint64_t bytes) {
  return kind == "js" || engine == "lang" || engine == "wake" || (engine == "font" && bytes <= 384 * 1024);
}

Room room(uint64_t bytes, Vol v, bool js, const char*) {
  Room r;
  r.need = bytes;
  if (v == SD) {
    uint64_t t = 0, f = 0;
    if (card::info(t, f)) r.free = f > (1u << 20) ? f - (1u << 20) : 0;
    r.capacity = t;
  } else if (js) {
    uint64_t t = 0, f = 0;
    lfsInfo(t, f);
    r.free = f > 96 * 1024 ? f - 96 * 1024 : 0;  // settings, photos and caches need room too
    r.capacity = t / 2;
  } else {
    std::lock_guard<std::recursive_mutex> l(mu);
    r.need = roundUp(bytes) + SECTOR * 2;
    r.free = area::freeBytes();
    r.capacity = area::size();
    r.restart = r.free < r.need && r.free + area::zombieBytes() >= r.need;
  }
  r.ok = r.free >= r.need;
  r.missing = r.ok ? 0 : r.need - r.free;
  return r;
}

std::vector<const Package*> biggest(Vol v, int n) {
  std::vector<const Package*> out;
  for (const Package& p : list())
    if (p.vol == v && p.available) out.push_back(&p);
  std::sort(out.begin(), out.end(), [](const Package* a, const Package* b) { return a->bytes > b->bytes; });
  if ((int)out.size() > n) out.resize(n);
  return out;
}

std::vector<const Package*> helpers(Vol v, bool js, int n) {
  std::vector<const Package*> out;
  for (const Package& p : list())
    if (p.vol == v && p.available && (v == SD || onLfs(p.kind, p.engine, p.bytes) == js)) out.push_back(&p);
  std::sort(out.begin(), out.end(), [](const Package* a, const Package* b) { return a->bytes > b->bytes; });
  if ((int)out.size() > n) out.resize(n);
  return out;
}

bool inUse(const char* id) { return pinned(id); }

bool install(const Entry& e, const std::vector<String>& parts, Vol v, String& why) { return installAs(e, parts, v, why, true); }

bool installAs(const Entry& e, const std::vector<String>& asked, Vol v, String& why, bool user) {
  if (!e.supported()) return why = T_("Needs a software update"), false;
  std::vector<String> parts = asked;
  fitBoard(e, parts);
  if (v == SD && !card::present()) return why = T_("Needs the memory card"), false;
  // A Java game needs the Java runtime package (its class library): installed first, on the same volume.
  // The runtime's own entry also says engine "j2me": never for itself (that recursed until the loop task's
  // stack overflowed, on every boot of a board that wanted a Java game without the runtime, 2026-11-17).
  if (e.engine == "j2me" && e.id != J2ME_RUNTIME && e.type != "pack" && !find(J2ME_RUNTIME) &&
      !isQueued(J2ME_RUNTIME)) {
    const Entry* rt = entry(J2ME_RUNTIME);
    if (!rt) {
      if (!catLoading) refresh(true);
      return why = T_("Getting the Java game runtime, try again soon"), false;
    }
    if (!installAs(*rt, defaultParts(*rt), v, why, user)) return false;
  }
  Job j;
  j.kind = Job::INSTALL;
  j.id = e.id, j.name = e.name, j.vol = v, j.js = onLfs(e.kind, e.engine, partsBytes(e, parts)), j.user = user;
  if (e.addonOf.length()) {
    const Entry* m = entry(e.addonOf.c_str());
    const Package* mp = find(e.addonOf.c_str());
    j.mainOf = e.addonOf;
    j.mainName = mp ? mp->name : m ? m->name : e.addonOf;
    if (!mp && !isQueued(e.addonOf)) return why = i18n::fmt(N_("Install {0} first"), j.mainName), false;
  }
  if (e.needs.length() && !find(e.needs.c_str()) && !isQueued(e.needs)) {  // a run map without the run game
    const Entry* n = entry(e.needs.c_str());
    return why = i18n::fmt(N_("Install {0} first"), n ? n->name : e.needs), false;
  }
  // Required parts first (the core carries the manifest), then the chosen ones in the catalog's order.
  for (const Part& p : e.parts)
    for (const String& s : parts)
      if (p.id == s && p.req) j.parts.push_back(p);
  for (const Part& p : e.parts)
    for (const String& s : parts)
      if (p.id == s && !p.req) j.parts.push_back(p);
  if (j.parts.empty()) return why = T_("No part chosen to download"), false;
  // The web may free room with "rm" entries queued just before: its installs are checked when they run.
  const Room r = room(partsBytes(e, parts), v, j.js);
  if (user && !r.ok)
    return why = i18n::fmt(N_("{0} is {1} short"), v == SD ? T_("Memory card") : T_("Device"), sizeText(r.missing)), false;
  if (!enqueue(j, why)) return false;
  startRunner();
  return true;
}

bool update(const char* id, String& why) {
  const Package* p = find(id);
  const Entry* e = entry(id);
  if (!p || !e) return why = T_("No new version"), false;
  std::vector<String> parts;
  for (const String& s : p->parts)
    for (const Part& q : e->parts)
      if (q.id == s) parts.push_back(s);
  for (const Part& q : e->parts)  // a part that became required
    if (q.req) {
      bool have = false;
      for (const String& s : parts) have |= s == q.id;
      if (!have) parts.push_back(q.id);
    }
  return install(*e, parts, p->vol, why);
}

bool remove(const char* id, String& why) {
  const Package* p = find(id);
  if (!p) return why = T_("The app is not installed"), false;
  if (p->vol == SD && !card::present()) return why = T_("Insert the memory card with this app to remove it"), false;
  Job j;
  j.kind = Job::REMOVE, j.id = id, j.name = p->name;
  if (!enqueue(j, why)) return false;
  startRunner();
  return true;
}

bool move(const char* id, Vol to, String& why) {
  const Package* p = find(id);
  if (!p) return why = T_("The app is not installed"), false;
  if (p->vol == to) return why = T_("It is already there"), false;
  if (!card::present()) return why = T_("Needs the memory card"), false;
  const Room r = room(p->bytes, to, onLfs(p->kind, p->engine, p->bytes));
  if (!r.ok) return why = i18n::fmt(N_("{0} is {1} short"), to == SD ? T_("Memory card") : T_("Device"), sizeText(r.missing)), false;
  Job j;
  j.kind = Job::MOVE, j.id = id, j.name = p->name, j.vol = to;
  if (!enqueue(j, why)) return false;
  startRunner();
  return true;
}

Status status() {
  std::lock_guard<std::mutex> l(qmu);
  return st;
}

State stateOf(const char* id) {
  std::lock_guard<std::mutex> l(qmu);
  if (running && cur.id == id) return st.state == DONE || st.state == ERROR ? IDLE : st.state;
  for (const Job& q : queue)
    if (q.id == id) return QUEUED;
  return IDLE;
}

bool busy() {
  std::lock_guard<std::mutex> l(qmu);
  return running || queue.size();
}

uint32_t version() {
  std::lock_guard<std::mutex> l(qmu);
  return jobVer + st.seq + catVer;
}

const char* stateText(State s) {
  switch (s) {
    case QUEUED: return T_("Waiting");
    case DOWNLOAD: return T_("Downloading");
    case WRITE: return T_("Writing");
    case MOVE: return T_("Moving");
    case REMOVE: return T_("Removing");
    case COMPACT: return T_("Tidying the storage");
    case DONE: return T_("Done");
    case ERROR: return T_("Error");
    default: return "";
  }
}

void begin() {
  started = true;
  reconcileAt = millis() + 3000;
}

void tick() {
  if (busy()) paceWait();
  // Freed flash erased while nobody uses the device: one 64 KB block a second (each stops the screen ~0.2 s).
  static uint32_t eraseAt = 0;
  if (!busy() && os::idleMs() > 20000 && !sound::streaming() && millis() - eraseAt > 1000) {
    eraseAt = millis();
    area::eraseIdle();
  }
  std::vector<Finished> done;
  {
    std::lock_guard<std::mutex> l(qmu);
    done.swap(finished);
  }
  for (const Finished& f : done) afterJob(f);
  // s3.pkgs changed (a sync from the web, or the simulator's "set").
  if (seenSettings != desk::settingsVersion()) {
    seenSettings = desk::settingsVersion();
    const String now = desk::setting("s3.pkgs", "");
    if (now != seenPkgs) {
      seenPkgs = now;
      reconcileAt = millis();
    }
  }
  static uint32_t seenCat = 0;
  if (seenCat != catVer) seenCat = catVer, reconcileAt = reconcileAt ? reconcileAt : millis();
  if (reconcileAt && (int32_t)(millis() - reconcileAt) >= 0) {
    reconcileAt = 0;
    reconcile();
  }
  startRunner();
#ifdef SIM
  // What the next sync would report, for tests: $SIM_DATA/pkgs-report.json.
  static uint32_t dumped = 0, dumpAt = 0;
  const uint32_t v = pkg::version() * 131 + version();
  if (v != dumped && millis() - dumpAt > 500) {
    dumped = v, dumpAt = millis();
    JsonDocument d;
    report(d.to<JsonObject>());
    String out;
    serializeJson(d, out);
    writeText(String(simDir.c_str()) + "/pkgs-report.json", out);
  }
#endif
}

void afterSync() { reconcileAt = millis(); }

void report(JsonObject o) {
  JsonObject vols = o["vols"].to<JsonObject>();
  const VolInfo f = volume(FLASH), s = volume(SD);
  JsonObject fl = vols["flash"].to<JsonObject>();
  fl["total"] = f.totalKB;
  fl["free"] = f.freeKB;
  JsonObject sd = vols["sd"].to<JsonObject>();
  sd["present"] = s.present;
  if (s.present) sd["total"] = s.totalKB, sd["free"] = s.freeKB;
  JsonArray inst = o["installed"].to<JsonArray>();
  for (const Package& p : list()) {
    if (inst.size() >= 64) break;
    JsonObject e = inst.add<JsonObject>();
    e["id"] = p.id;
    e["name"] = clip(p.name, 40);
    e["version"] = p.version;
    e["code"] = p.code;
    e["vol"] = volKey(p.vol);
    JsonArray parts = e["parts"].to<JsonArray>();
    for (const String& x : p.parts)
      if (parts.size() < 8) parts.add(x);
    e["kb"] = (p.bytes + 1023) / 1024;
    e["ok"] = p.available;
  }
  const Status j = status();
  if (j.state != IDLE) {
    JsonObject jo = o["job"].to<JsonObject>();
    jo["id"] = j.id;
    static const char* S[] = {"", "download", "download", "write", "move", "remove", "move", "done", "error"};
    jo["state"] = S[j.state];
    jo["pct"] = j.pct;
    if (j.err.length()) jo["err"] = clip(j.err, 80);
  }
}

}  // namespace store

namespace in {
volatile bool abortAll = false;
}

}  // namespace pkg
