#include "Pkgs.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <mbedtls/sha256.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "inkdesk/Decl.h"
#include "inkdesk/InkDeskStore.h"
#include "network/HttpDownloader.h"

namespace inkdesk::pkgs {
namespace {

constexpr uint32_t MAX_PKG = 512 * 1024;  // bytes of files in one package (profiles.md §3)
constexpr uint32_t MAX_INKP = MAX_PKG + 64 * 1024;  // the .inkp file: files + header, manifest, table, padding
constexpr uint32_t MAX_MANIFEST = 8 * 1024;  // tools/inkp.py refuses bigger manifests too
constexpr size_t MAX_LISTED = 32;  // packages listed (UI, sync report)
constexpr uint32_t MAX_FILES = 64;
constexpr size_t MANIFEST_CAP = 16 * 1024;  // installed manifest.json (with the file list)
constexpr size_t COPY_BUF = 1024;
// What this device has, in the canonical names of profiles.md §1.1.
constexpr const char* CAPS[] = {"screen.eink", "input.buttons", "store.sd", "net.wifi", "net.ble", "power.battery"};

// The last install or removal of this boot, for the sync report.
struct Job {
  std::string id, state, err;
  int pct = 0;
} gJob;

void heapLog(const char* what, const std::string& id) {
  LOG_INF("PKG", "%s %s: heap %u free, %u largest block", what, id.c_str(), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

// Same rule as the server's pkgID: a-z or 0-9 first, then a-z 0-9 . _ -, at most 64.
bool idOk(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  for (size_t i = 0; i < s.size(); i++) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '.' || c == '_' || c == '-')))) {
      return false;
    }
  }
  return true;
}

bool nameOk(const char* n) {
  return *n && *n != '/' && !strstr(n, "..") && !strchr(n, '\\') && strcmp(n, "manifest.json") != 0;
}

uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

uint32_t u32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

int rank(const Info& i) { return (i.card ? 10 : 0) + (i.type == "face" ? 0 : i.type == "widget" ? 1 : 2); }

bool readInfo(const std::string& id, Info& out) {
  std::string raw;
  if (!Storage.readFileToString("PKG", dirOf(id) + "/manifest.json", MANIFEST_CAP, raw)) return false;
  JsonDocument filter;
  for (const char* k : {"id", "name", "version", "code", "type", "refresh", "entry", "screens", "perms", "src"}) {
    filter[k] = true;
  }
  filter["files"][0]["s"] = true;
  JsonDocument m;
  if (deserializeJson(m, raw, DeserializationOption::Filter(filter))) return false;
  out = Info{};
  out.id = m["id"] | "";
  if (out.id != id) return false;
  out.name = m["name"] | id.c_str();
  out.version = m["version"] | "";
  out.code = m["code"] | 0u;
  out.type = m["type"] | "";
  out.refresh = m["refresh"] | "data";
  const char* entryKey = out.type == "widget" ? "widget" : out.type == "theme" ? "theme" : "face";
  out.entry = m["entry"][entryKey] | (std::string(entryKey) + ".json").c_str();
  for (JsonVariantConst s : m["screens"].as<JsonArrayConst>()) out.screens.push_back(s | "");
  for (JsonVariantConst p : m["perms"].as<JsonArrayConst>()) out.perms.push_back(p | "");
  out.card = strcmp(m["src"] | "", "card") == 0;
  uint32_t bytes = 0;
  for (JsonObjectConst f : m["files"].as<JsonArrayConst>()) bytes += f["s"] | 0u;
  out.kb = (bytes + 1023) / 1024;
  return true;
}

// Package folders on the card, every one (list() caps what it shows). A ".old-<id>" left by a power cut
// between an update's two renames goes back to "<id>" when "<id>" is missing; otherwise it is stale and goes.
std::vector<std::string> folderNames() {
  std::vector<std::string> ids, olds;
  if (HalFile dir = Storage.open(DIR)) {
    char name[80];
    for (HalFile e = dir.openNextFile(); e; e = dir.openNextFile()) {
      const size_t len = e.getName(name, sizeof(name));
      const bool isDir = e.isDirectory();
      e.close();
      if (!isDir || len == 0 || len >= sizeof(name)) continue;
      if (!strncmp(name, ".old-", 5)) olds.emplace_back(name);
      else if (name[0] != '.') ids.emplace_back(name);
    }
    dir.close();
  }
  for (const auto& old : olds) {
    const std::string id = old.substr(5), from = std::string(DIR) + "/" + old;
    if (!idOk(id)) continue;
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
      Storage.removeDir(from.c_str());
    } else if (Storage.rename(from.c_str(), dirOf(id).c_str())) {
      LOG_INF("PKG", "Recovered %s from an update cut short", id.c_str());
      ids.push_back(id);
    }
  }
  return ids;
}

bool hexEq(const uint8_t* digest, const char* hex) {
  static const char* kHex = "0123456789abcdef";
  char got[65];
  for (int i = 0; i < 32; i++) {
    got[i * 2] = kHex[digest[i] >> 4];
    got[i * 2 + 1] = kHex[digest[i] & 15];
  }
  got[64] = '\0';
  return strcasecmp(got, hex) == 0;
}

// Checks an .inkp file and writes its files into DIR/<id>; the old version stays until the new one is
// complete. `sha`: the catalog's SHA-256 (null for a file copied onto the card).
bool unpack(const std::string& path, const std::string& wantId, const char* sha, const uint32_t size, const bool card,
            std::string& why) {
  HalFile f;
  if (!Storage.openFileForRead("PKG", path, f)) return why = "open", false;
  const uint32_t total = static_cast<uint32_t>(f.fileSize());
  if (size > 0 && total != size) return why = "size", false;
  if (total > MAX_INKP) return why = "too big", false;
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[COPY_BUF]);
  if (!buf) return why = "memory", false;

  if (sha && *sha) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    for (uint32_t done = 0; done < total;) {
      const int n = f.read(buf.get(), std::min<uint32_t>(COPY_BUF, total - done));
      if (n <= 0) break;
      mbedtls_sha256_update(&ctx, buf.get(), n);
      done += n;
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    if (!hexEq(digest, sha)) return why = "checksum", false;
    f.seek(0);
  }

  uint8_t head[16];
  if (f.read(head, 16) != 16 || memcmp(head, "INKP", 4) != 0 || head[4] != 1 || head[5] != 0) {
    return why = "not inkp", false;
  }
  const uint32_t mlen = u32(head + 8), nf = u32(head + 12);
  if (mlen == 0 || mlen > MAX_MANIFEST || nf > MAX_FILES) return why = "header", false;
  JsonDocument md;
  {
    std::string m(mlen, '\0');
    if (f.read(&m[0], mlen) != static_cast<int>(mlen) || deserializeJson(md, m)) return why = "manifest", false;
  }
  const std::string id = md["id"] | "";
  if (!idOk(id) || (!wantId.empty() && id != wantId)) return why = "id", false;
  if (strcmp(md["part"] | "core", "core") != 0) return why = "part", false;
  if (strcmp(md["runtime"] | "", "decl") != 0 || (md["api"]["min"] | 1) > decl::LEVEL) return why = "needs update", false;
  const std::string type = md["type"] | "";
  if (type != "face" && type != "widget" && type != "theme") return why = "type", false;
  for (JsonVariantConst r : md["requires"].as<JsonArrayConst>()) {
    const char* need = r | "";
    bool have = false;
    for (const char* c : CAPS) have = have || strcmp(c, need) == 0;
    if (!have) return why = std::string("requires ") + need, false;
  }

  // At most MAX_LISTED packages: a new one past that is refused (updates of installed ones go on).
  if (!Storage.exists(dirOf(id).c_str()) && folderNames().size() >= MAX_LISTED) return why = "full: 32 packages", false;
  const std::string tmp = std::string(DIR) + "/.new-" + id;
  if (Storage.exists(tmp.c_str())) Storage.removeDir(tmp.c_str());
  if (!Storage.mkdir(tmp.c_str())) return why = "mkdir", false;
  const auto failed = [&](const char* w) {
    why = w;
    Storage.removeDir(tmp.c_str());
    return false;
  };

  const uint32_t tableAt = 16 + ((mlen + 15) & ~15u);
  const uint32_t dataAt = tableAt + 64 * nf;
  uint32_t bytes = 0;
  JsonArray files = md["files"].to<JsonArray>();
  for (uint32_t i = 0; i < nf; i++) {
    uint8_t ent[64];
    if (!f.seek(tableAt + 64 * i) || f.read(ent, 64) != 64) return failed("table");
    char name[53];
    memcpy(name, ent, 52);
    name[52] = '\0';
    const uint32_t off = u32(ent + 52), len = u32(ent + 56), crc = u32(ent + 60);
    bytes += len;
    if (!nameOk(name) || off % 16 || off < dataAt || off + len > total || bytes > MAX_PKG) return failed("file table");
    const std::string out = tmp + "/" + name;
    if (const char* slash = strrchr(name, '/')) {
      Storage.mkdir((tmp + "/" + std::string(name, slash - name)).c_str());
    }
    HalFile o;
    if (!f.seek(off) || !Storage.openFileForWrite("PKG", out, o)) return failed("write");
    uint32_t c = 0;
    for (uint32_t done = 0; done < len;) {
      const int n = f.read(buf.get(), std::min<uint32_t>(COPY_BUF, len - done));
      if (n <= 0 || o.write(buf.get(), n) != static_cast<size_t>(n)) {
        o.close();  // before its folder goes
        return failed("write");
      }
      c = crc32(c, buf.get(), n);
      done += n;
    }
    o.close();
    if (c != crc) return failed("crc");
    JsonObject e = files.add<JsonObject>();
    e["n"] = name;
    e["s"] = len;
    e["c"] = crc;
  }
  f.close();
  md.remove("part");
  md["parts"].to<JsonArray>().add("core");
  if (card) md["src"] = "card";
  {
    std::string man;
    serializeJson(md, man);
    HalFile o;
    if (!Storage.openFileForWrite("PKG", tmp + "/manifest.json", o)) return failed("write");
    const bool ok = o.write(reinterpret_cast<const uint8_t*>(man.data()), man.size()) == man.size();
    o.close();
    if (!ok) return failed("write");
  }
  // The working copy stays until the new one is in place: old -> .old-<id>, new -> <id>, then drop .old.
  const std::string dest = dirOf(id);
  const std::string old = std::string(DIR) + "/.old-" + id;
  if (Storage.exists(old.c_str())) Storage.removeDir(old.c_str());
  const bool hadOld = Storage.exists(dest.c_str());
  if (hadOld && !Storage.rename(dest.c_str(), old.c_str())) return failed("rename");
  if (!Storage.rename(tmp.c_str(), dest.c_str())) {
    if (hadOld) Storage.rename(old.c_str(), dest.c_str());
    return failed("rename");
  }
  if (hadOld) Storage.removeDir(old.c_str());
  LOG_INF("PKG", "Installed %s %s (%u files, %u bytes)", id.c_str(), md["version"] | "", static_cast<unsigned>(nf),
          static_cast<unsigned>(bytes));
  return true;
}

}  // namespace

std::string dirOf(const std::string& id) { return std::string(DIR) + "/" + id; }

std::vector<Info> list() {
  std::vector<Info> out;
  for (const auto& id : folderNames()) {
    Info i;
    if (readInfo(id, i)) out.push_back(std::move(i));
  }
  std::stable_sort(out.begin(), out.end(), [](const Info& a, const Info& b) {
    if (rank(a) != rank(b)) return rank(a) < rank(b);
    return a.name < b.name;
  });
  if (out.size() > MAX_LISTED) out.resize(MAX_LISTED);
  return out;
}

bool get(const std::string& id, Info& out) { return idOk(id) && readInfo(id, out); }

bool remove(const std::string& id, bool* wasFace) {
  if (wasFace) {
    FaceRef ref;
    *wasFace = FaceRef::parse(INKDESK.get("dash.layout", "desk"), ref) && ref.id == id;
  }
  if (!idOk(id)) return false;
  const std::string d = dirOf(id);
  const bool ok = !Storage.exists(d.c_str()) || Storage.removeDir(d.c_str());
  gJob = {id, ok ? "done" : "error", ok ? "" : "remove", 100};
  LOG_INF("PKG", "Removed %s (%s)", id.c_str(), ok ? "ok" : "failed");
  return ok;
}

void forget(const std::string& id) {
  JsonDocument wish;
  if (deserializeJson(wish, INKDESK.get("x4.pkgs", "")) || !wish.is<JsonArray>()) return;
  JsonArray a = wish.as<JsonArray>();
  bool found = false;
  for (size_t i = 0; i < a.size();) {
    if (id == (a[i]["id"] | "")) {
      a.remove(i);
      found = true;
    } else {
      i++;
    }
  }
  if (!found) return;
  std::string s;
  serializeJson(a, s);
  INKDESK.setLocal("x4.pkgs", s);
}

void useFace(const FaceRef& ref) {
  INKDESK.setLocal("dash.layout", ref.format());
  // A face shows only on the drawn sleep screen.
  if (INKDESK.get("dash.enabled", "1") == "0") INKDESK.setLocal("dash.enabled", "1");
  if (INKDESK.get("sleep.mode", "dashboard") == "images") INKDESK.setLocal("sleep.mode", "dashboard");
  INKDESK.saveToFile();
}

void leaveFace() { INKDESK.setLocal("dash.layout", "desk"); }

namespace {
// .inkp files copied into DIR, by name.
std::vector<std::string> copiedFiles() {
  std::vector<std::string> files;
  if (HalFile dir = Storage.open(DIR)) {
    char name[96];
    for (HalFile e = dir.openNextFile(); e; e = dir.openNextFile()) {
      const size_t len = e.getName(name, sizeof(name));
      const bool isDir = e.isDirectory();
      e.close();
      if (!isDir && len > 5 && len < sizeof(name) && strcasecmp(name + len - 5, ".inkp") == 0) files.emplace_back(name);
    }
    dir.close();
  }
  return files;
}
}  // namespace

int sideloadPending() { return static_cast<int>(copiedFiles().size()); }

int sideload() {
  int done = 0;
  for (const auto& n : copiedFiles()) {
    const std::string path = std::string(DIR) + "/" + n;
    std::string why;
    heapLog("Sideload", n);
    if (unpack(path, "", nullptr, 0, true, why)) {
      Storage.remove(path.c_str());
      done++;
    } else {
      // Renamed so it is not tried again at every visit (a refused copy of the same name is replaced).
      LOG_ERR("PKG", "Sideload %s refused: %s", n.c_str(), why.c_str());
      const std::string bad = path + ".bad";
      if (Storage.exists(bad.c_str())) Storage.remove(bad.c_str());
      Storage.rename(path.c_str(), bad.c_str());
    }
  }
  return done;
}

bool FaceRef::parse(const std::string& layout, FaceRef& out) {
  if (layout.rfind("pkg:", 0) != 0) return false;
  const size_t at = layout.find('@');
  out = FaceRef{};
  out.id = layout.substr(4, at == std::string::npos ? std::string::npos : at - 4);
  if (at != std::string::npos && sscanf(layout.c_str() + at + 1, "%dx%d", &out.w, &out.h) != 2) out.w = out.h = 0;
  if (out.w <= 0 || out.h <= 0) out.w = out.h = 0;
  return idOk(out.id);
}

bool FaceRef::parseSize(const std::string& size, int& w, int& h) {
  return sscanf(size.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0;
}

std::string FaceRef::format() const {
  std::string s = "pkg:" + id;
  if (w > 0 && h > 0) s += "@" + std::to_string(w) + "x" + std::to_string(h);
  return s;
}

std::string faceRefresh() {
  FaceRef ref;
  Info i;
  if (!FaceRef::parse(INKDESK.get("dash.layout", "desk"), ref) || !get(ref.id, i)) return "";
  return i.refresh;
}

void addToSyncBody(JsonObject req) {
  JsonArray caps = req["caps"].to<JsonArray>();
  for (const char* c : CAPS) caps.add(c);
  req["rt"]["decl"] = decl::LEVEL;
  req["screen"] = "800x480";
  JsonObject p = req["pkgs"].to<JsonObject>();
  p["vols"]["sd"]["present"] = Storage.ready();
  JsonArray inst = p["installed"].to<JsonArray>();
  for (const auto& i : list()) {
    JsonObject o = inst.add<JsonObject>();
    o["id"] = i.id;
    o["version"] = i.version;
    o["code"] = i.code;
    o["vol"] = "sd";
    o["kb"] = i.kb;
    o["ok"] = true;
    o["type"] = i.type;
    o["name"] = i.name;
    if (i.card) o["src"] = "card";
  }
  if (!gJob.id.empty()) {
    JsonObject j = p["job"].to<JsonObject>();
    j["id"] = gJob.id;
    j["state"] = gJob.state;
    j["pct"] = gJob.pct;
    if (!gJob.err.empty()) j["err"] = gJob.err;
  }
}

void applyRemovals() {
  JsonDocument wish;
  if (deserializeJson(wish, INKDESK.get("x4.pkgs", "")) || !wish.is<JsonArray>()) return;
  JsonArray all = wish.as<JsonArray>();
  bool removed = false;
  for (size_t i = 0; i < all.size();) {
    if (!(all[i]["rm"] | false)) {
      i++;
      continue;
    }
    bool wasFace = false;
    if (!remove(all[i]["id"] | "", &wasFace)) {
      i++;  // still on the card: the entry stays and the next sync tries again
      continue;
    }
    if (wasFace) leaveFace();
    all.remove(i);
    removed = true;
  }
  if (!removed) return;
  // The entries leave the list; it goes back to the server on the next sync.
  std::string s;
  serializeJson(all, s);
  INKDESK.setLocal("x4.pkgs", s);
  LOG_INF("PKG", "Removals asked on the web done");
}

std::vector<Download> plan(JsonArrayConst answer, const bool answered) {
  std::vector<Download> out;
  if (!answered) return out;
  JsonDocument wish;
  if (deserializeJson(wish, INKDESK.get("x4.pkgs", "")) || !wish.is<JsonArray>()) return out;
  JsonArrayConst all = wish.as<JsonArrayConst>();
  for (JsonObjectConst a : answer) {
    const std::string id = a["id"] | "";
    const char* url = a["url"] | "";
    if (!idOk(id) || !*url) continue;
    bool wanted = false;
    uint32_t wantCode = 0;
    for (JsonObjectConst w : all) {
      if (id == (w["id"] | "") && !(w["rm"] | false)) {
        wanted = true;
        wantCode = w["code"] | 0u;
      }
    }
    if (!wanted) continue;
    const uint32_t code = a["code"] | 0u;
    Info have;
    if (get(id, have) && !(have.code < wantCode && code > have.code)) continue;
    Download d;
    d.id = id;
    d.name = a["name"] | id.c_str();
    d.version = a["version"] | "";
    d.url = url;
    d.sha256 = a["sha256"] | "";
    d.code = code;
    d.size = a["size"] | 0u;
    out.push_back(std::move(d));
  }
  LOG_INF("PKG", "Sync: %d packages to install", static_cast<int>(out.size()));
  return out;
}

int install(const std::vector<Download>& list, const bool* cancel) {
  int failed = 0;
  if (list.empty()) return 0;
  const std::string dl = std::string(DIR) + "/.dl";
  Storage.mkdir(dl.c_str());
  for (const auto& d : list) {
    if (cancel && *cancel) break;
    gJob = {d.id, "download", "", 0};
    heapLog("Download", d.id);
    if (ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
      gJob.state = "error";
      gJob.err = "memory";
      failed++;
      continue;
    }
    if (d.size > MAX_INKP) {
      gJob.state = "error";
      gJob.err = "too big";
      failed++;
      continue;
    }
    const std::string path = dl + "/" + d.id + ".inkp";
    // Stops at the size the answer gave, or at the largest package when it gave none.
    const uint32_t limit = d.size > 0 ? d.size : MAX_INKP;
    bool stop = false, tooBig = false;
    // Public catalog files: no device token goes with them.
    const auto r = HttpDownloader::downloadToFile(
        d.url, path,
        [&](const size_t done, const size_t total) {
          if (total > 0) gJob.pct = static_cast<int>(static_cast<uint64_t>(done) * 100 / total);
          if (done > limit || total > limit) tooBig = true;
          stop = tooBig || (cancel && *cancel);
        },
        &stop);
    heapLog("Downloaded", d.id);
    if (r != HttpDownloader::OK || tooBig) {
      LOG_ERR("PKG", "%s download failed: %d%s", d.id.c_str(), static_cast<int>(r), tooBig ? " (too big)" : "");
      gJob.state = "error";
      gJob.err = tooBig ? "too big" : "download";
      Storage.remove(path.c_str());
      Storage.remove((path + ".part").c_str());
      failed++;
      continue;
    }
    gJob.state = "write";
    std::string why;
    const bool ok = unpack(path, d.id, d.sha256.c_str(), d.size, false, why);
    Storage.remove(path.c_str());
    heapLog("Unpacked", d.id);
    gJob.state = ok ? "done" : "error";
    gJob.pct = ok ? 100 : gJob.pct;
    gJob.err = ok ? "" : why;
    if (!ok) {
      LOG_ERR("PKG", "%s refused: %s", d.id.c_str(), why.c_str());
      failed++;
    }
  }
  return failed;
}

}  // namespace inkdesk::pkgs
