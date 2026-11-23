#pragma GCC optimize("Os")  // cold code: built small (the app slot is nearly full)
// fs and crypto for JavaScript apps. See jsfs.h.
#include "jsfs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "pkg/internal.h"
#include "pkg/pkg.h"
#include "trace.h"

#ifdef SIM
#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>
extern std::string simDir;
#else
#include "esp_random.h"
#include "mbedtls/sha256.h"
#endif

namespace jsfs {
namespace {

constexpr uint32_t CACHE_FLASH_MAX = 1024 * 1024;  // the cache on LittleFS when no card is in
constexpr size_t MAX_FILE = 512 * 1024;

struct App {
  String id;
  JSContext* ctx = nullptr;
  uint32_t dataQuota = 64 * 1024, cacheQuota = 0;
  int64_t dataUsed = -1, cacheUsed = -1;  // -1: not measured yet
} app;

bool sd() { return pkg::in::card::present(); }

String root(bool cache, const String& id) {
#ifdef SIM
  const String base = String(simDir.c_str());
  return (cache && sd() ? base + "/sd/espos/appdata/" : base + "/fs/appdata/") + id + (cache ? "/cache" : "/data");
#else
  return String(cache && sd() ? "/sdcard/espos/appdata/" : "/littlefs/appdata/") + id + (cache ? "/cache" : "/data");
#endif
}

uint32_t cacheCap() {
  const uint32_t q = app.cacheQuota;
  return sd() ? q : std::min(q, CACHE_FLASH_MAX);
}

// "data/q/a.json" -> (false, "q/a.json"); "cache/p1.jpg" -> (true, "p1.jpg"); a bare name is data/.
bool split(const String& name, bool& cache, String& rel) {
  if (!name.length() || name.length() > 80) return false;
  cache = name.startsWith("cache/");
  rel = cache ? name.substring(6) : name.startsWith("data/") ? name.substring(5) : name;
  int parts = 0;
  for (int a = 0; a <= (int)rel.length();) {
    int e = rel.indexOf('/', a);
    if (e < 0) e = rel.length();
    const String p = rel.substring(a, e);
    if (!p.length() || p == "." || p == ".." || p.endsWith(".tmp")) return false;
    for (unsigned i = 0; i < p.length(); i++)
      if (!isalnum((uint8_t)p[i]) && p[i] != '.' && p[i] != '_' && p[i] != '-') return false;
    parts++;
    a = e + 1;
  }
  return parts >= 1 && parts <= 2;
}

int64_t& used(bool cache) {
  int64_t& u = cache ? app.cacheUsed : app.dataUsed;
  if (u < 0) u = (int64_t)pkg::in::treeBytes(root(cache, app.id));
  return u;
}

int64_t sizeOf(const String& p) {
  struct stat st;
  return stat(p.c_str(), &st) ? -1 : (int64_t)st.st_size;
}

// The cache over its cap: the oldest files go first (by modification time, then by name).
void evict(uint32_t need) {
  const uint32_t cap = cacheCap();
  if (used(true) + need <= cap) return;
  struct F {
    String p;
    time_t t;
    int64_t s;
  };
  std::vector<F> fs;
  const String r = root(true, app.id);
  std::vector<String> dirs = {r};
  for (const String& n : pkg::in::listDir(r))
    if (pkg::in::isDir(r + "/" + n)) dirs.push_back(r + "/" + n);
  for (const String& d : dirs)
    for (const String& n : pkg::in::listDir(d)) {
      struct stat st;
      const String p = d + "/" + n;
      if (!stat(p.c_str(), &st) && S_ISREG(st.st_mode)) fs.push_back({p, st.st_mtime, (int64_t)st.st_size});
    }
  std::sort(fs.begin(), fs.end(), [](const F& a, const F& b) { return a.t != b.t ? a.t < b.t : a.p < b.p; });
  int n = 0;
  for (const F& f : fs) {
    if (used(true) + need <= cap) break;
    if (!unlink(f.p.c_str())) used(true) -= f.s, n++;
  }
  if (n) trace::add("app %s: cache full, %d old files removed", app.id.c_str(), n);
}

bool setWhy(String* why, const char* text) {
  if (why) *why = text;
  return false;
}

bool writeFile(const String& name, const uint8_t* data, size_t len, String* why) {
  bool cache;
  String rel;
  if (!app.ctx || !split(name, cache, rel)) return setWhy(why, "bad file name");
  if (len > MAX_FILE) return setWhy(why, "files are at most 512 KB");
  const String p = root(cache, app.id) + "/" + rel;
  const int64_t old = std::max<int64_t>(0, sizeOf(p));
  if (cache) {
    if (len > cacheCap()) return setWhy(why, "over the cache quota (cache:<KB>)");
    evict(len > old ? len - old : 0);
  } else if (used(false) - old + (int64_t)len > app.dataQuota) {
    return setWhy(why, "over the storage quota (storage:<KB>)");
  }
  pkg::in::mkdirs(p.substring(0, p.lastIndexOf('/')));
  const String tmp = p + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return setWhy(why, "can't write");
  const bool ok = fwrite(data, 1, len, f) == len;
  if (fclose(f) || !ok) return unlink(tmp.c_str()), setWhy(why, "can't write (full?)");
  // LittleFS replaces the old file in one step; FAT (the card) needs it gone first.
  if (rename(tmp.c_str(), p.c_str()) && (unlink(p.c_str()), rename(tmp.c_str(), p.c_str())))
    return setWhy(why, "can't write");
  used(cache) += (int64_t)len - old;
  return true;
}

String argStr(JSContext* ctx, int argc, JSValueConst* argv, int i) {
  if (i >= argc || JS_IsUndefined(argv[i])) return "";
  size_t n = 0;
  const char* s = JS_ToCStringLen(ctx, &n, argv[i]);
  String o;
  if (s) o.concat(s, n);
  JS_FreeCString(ctx, s);
  return o;
}

JSValue nRead(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  bool cache;
  String rel;
  if (!split(argStr(ctx, argc, argv, 0), cache, rel)) return JS_ThrowRangeError(ctx, "fs: bad file name");
  String p = root(cache, app.id) + "/" + rel;
  if (sizeOf(p) < 0) p += ".tmp";  // a power cut between the two steps of a write: the new text is there
  const int64_t n = sizeOf(p);
  if (n < 0) return JS_UNDEFINED;
  if (n > (int64_t)MAX_FILE) return JS_ThrowRangeError(ctx, "fs: file over 512 KB");
  char* b = (char*)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
  FILE* f = b ? fopen(p.c_str(), "rb") : nullptr;
  const size_t got = f ? fread(b, 1, n, f) : 0;
  if (f) fclose(f);
  JSValue v = f && got == (size_t)n ? JS_NewStringLen(ctx, b, n) : JS_UNDEFINED;
  heap_caps_free(b);
  return v;
}

JSValue nWrite(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const String name = argStr(ctx, argc, argv, 0), text = argStr(ctx, argc, argv, 1);
  String why;
  if (!writeFile(name, (const uint8_t*)text.c_str(), text.length(), &why))
    return JS_ThrowRangeError(ctx, "fs.write %s: %s", name.c_str(), why.c_str());
  return JS_TRUE;
}

JSValue nRemove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  bool cache;
  String rel;
  if (!split(argStr(ctx, argc, argv, 0), cache, rel)) return JS_ThrowRangeError(ctx, "fs: bad file name");
  const String p = root(cache, app.id) + "/" + rel;
  const int64_t n = sizeOf(p);
  unlink((p + ".tmp").c_str());
  if (n < 0 || unlink(p.c_str())) return JS_FALSE;
  used(cache) -= n;
  return JS_TRUE;
}

// fs.list("data/q") -> [{name: "data/q/a.json", size}], one folder (not deeper).
JSValue nList(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  String dir = argStr(ctx, argc, argv, 0);
  while (dir.endsWith("/")) dir.remove(dir.length() - 1);
  bool cache = dir.startsWith("cache");
  String rel = cache ? dir.substring(5) : dir.startsWith("data") ? dir.substring(4) : "/" + dir;
  if (rel.startsWith("/")) rel = rel.substring(1);
  String check;
  bool c2;
  if (rel.length() && (!split((cache ? "cache/" : "data/") + rel, c2, check) || rel.indexOf('/') >= 0))
    return JS_ThrowRangeError(ctx, "fs.list: bad folder");
  const String base = root(cache, app.id) + (rel.length() ? "/" + rel : "");
  const String prefix = String(cache ? "cache/" : "data/") + (rel.length() ? rel + "/" : "");
  JSValue arr = JS_NewArray(ctx);
  uint32_t i = 0;
  for (const String& n : pkg::in::listDir(base)) {
    if (n.endsWith(".tmp")) continue;
    struct stat st;
    if (stat((base + "/" + n).c_str(), &st)) continue;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, (prefix + n + (S_ISDIR(st.st_mode) ? "/" : "")).c_str()));
    JS_SetPropertyStr(ctx, o, "size", JS_NewInt64(ctx, S_ISDIR(st.st_mode) ? 0 : (int64_t)st.st_size));
    JS_SetPropertyUint32(ctx, arr, i++, o);
  }
  return arr;
}

// fs.free() -> bytes left in data/; fs.free("cache") -> in cache/ (its cap, less what it holds).
JSValue nFree(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const bool cache = argStr(ctx, argc, argv, 0).startsWith("cache");
  const int64_t cap = cache ? cacheCap() : app.dataQuota;
  return JS_NewInt64(ctx, std::max<int64_t>(0, cap - used(cache)));
}

String hex(const uint8_t* b, size_t n) {
  static const char* H = "0123456789abcdef";
  String s;
  for (size_t i = 0; i < n; i++) s += H[b[i] >> 4], s += H[b[i] & 15];
  return s;
}

JSValue nSha256(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const String s = argStr(ctx, argc, argv, 0);
  uint8_t d[32];
#ifdef SIM
  CC_SHA256(s.c_str(), (CC_LONG)s.length(), d);
#else
  mbedtls_sha256((const uint8_t*)s.c_str(), s.length(), d, 0);
#endif
  return JS_NewString(ctx, hex(d, 32).c_str());
}

JSValue nRandom(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  int32_t n = 16;
  if (argc) JS_ToInt32(ctx, &n, argv[0]);
  if (n < 1 || n > 64) return JS_ThrowRangeError(ctx, "crypto.random(n): 1 to 64 bytes");
  uint8_t b[64];
#ifdef SIM
  arc4random_buf(b, n);
#else
  esp_fill_random(b, n);
#endif
  return JS_NewString(ctx, hex(b, n).c_str());
}

void addFn(JSContext* ctx, JSValue g, const char* name, JSCFunction* f, int n) {
  JS_SetPropertyStr(ctx, g, name, JS_NewCFunction(ctx, f, name, n));
}

}  // namespace

void install(JSContext* ctx, const pkg::Package& p) {
  app = App();
  app.id = p.id;
  app.ctx = ctx;
  for (const String& s : p.perms) {
    if (s.startsWith("storage:")) app.dataQuota = std::min<uint32_t>(s.substring(8).toInt(), 4096) * 1024;
    if (s.startsWith("cache:")) app.cacheQuota = std::min<uint32_t>(s.substring(6).toInt(), 65536) * 1024;
  }
  JSValue g = JS_GetGlobalObject(ctx);
  addFn(ctx, g, "__fsRead", nRead, 1);
  addFn(ctx, g, "__fsWrite", nWrite, 2);
  addFn(ctx, g, "__fsRemove", nRemove, 1);
  addFn(ctx, g, "__fsList", nList, 1);
  addFn(ctx, g, "__fsFree", nFree, 1);
  addFn(ctx, g, "__sha256", nSha256, 1);
  addFn(ctx, g, "__random", nRandom, 1);
  JS_FreeValue(ctx, g);
}

void stop() { app = App(); }

bool writeBytes(const char* name, const uint8_t* data, size_t len) { return writeFile(name, data, len, nullptr); }

String path(const char* name) {
  bool cache;
  String rel;
  if (!app.ctx || !name || !split(name, cache, rel)) return "";
  return root(cache, app.id) + "/" + rel;
}

void wipe(const char* id) {
  if (!id || !*id || strchr(id, '/')) return;
  const String a = root(false, id), b = root(true, id);
  pkg::in::rmTree(a.substring(0, a.lastIndexOf('/')));
  pkg::in::rmTree(b.substring(0, b.lastIndexOf('/')));
#ifdef SIM
  pkg::in::rmTree(String(simDir.c_str()) + "/fs/appdata/" + id);
#else
  pkg::in::rmTree(String("/littlefs/appdata/") + id);
#endif
}

}  // namespace jsfs
