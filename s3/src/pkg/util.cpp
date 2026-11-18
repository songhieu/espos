// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
// Files, hashes and the NVS index of the package system.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>

#include "../i18n.h"
#include "internal.h"
#include "trace.h"

#ifdef SIM
#include <string>
extern std::string simDir;
#else
#include <LittleFS.h>

#include "mbedtls/sha256.h"
#include "nvs.h"
#endif

namespace pkg {
namespace in {

// ---------------------------------------------------------------- folders
#ifdef SIM
String lfsRoot() { return String(simDir.c_str()) + "/fs/pkgs"; }
String sdRoot() { return String(simDir.c_str()) + "/sd/espos/pkgs"; }
String sdCacheFile() { return String(simDir.c_str()) + "/fs/pkgs-sd.json"; }
#else
String lfsRoot() { return "/littlefs/pkgs"; }
String sdRoot() { return "/sdcard/espos/pkgs"; }
String sdCacheFile() { return "/littlefs/pkgs-sd.json"; }
#endif

bool isDir(const String& p) {
  struct stat st;
  return !stat(p.c_str(), &st) && S_ISDIR(st.st_mode);
}

bool exists(const String& p) {
  struct stat st;
  return !stat(p.c_str(), &st);
}

bool mkdirs(const String& path) {
  if (isDir(path)) return true;
  for (int i = 1; i <= (int)path.length(); i++) {
    if (i < (int)path.length() && path[i] != '/') continue;
    const String part = path.substring(0, i);
    if (!isDir(part) && mkdir(part.c_str(), 0775) && !isDir(part)) return false;
  }
  return isDir(path);
}

std::vector<String> listDir(const String& p) {
  std::vector<String> out;
  DIR* d = opendir(p.c_str());
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    out.push_back(e->d_name);
  }
  closedir(d);
  return out;
}

bool rmTree(const String& path) {
  if (!exists(path)) return true;
  if (!isDir(path)) return !unlink(path.c_str());
  closeHandles(path + "/");
  // manifest.json first: a folder cut half way is not taken for a package.
  unlink((path + "/manifest.json").c_str());
  bool ok = true;
  for (const String& n : listDir(path)) ok &= rmTree(path + "/" + n);
  return !rmdir(path.c_str()) && ok;
}

uint64_t treeBytes(const String& p) {
  struct stat st;
  if (stat(p.c_str(), &st)) return 0;
  if (!S_ISDIR(st.st_mode)) return st.st_size;
  uint64_t n = 0;
  for (const String& c : listDir(p)) n += treeBytes(p + "/" + c);
  return n;
}

bool writeText(const String& path, const String& text) {
  const int slash = path.lastIndexOf('/');
  if (slash > 0) mkdirs(path.substring(0, slash));
  const String tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(text.c_str(), 1, text.length(), f) == text.length();
  if (fclose(f) || !ok) return unlink(tmp.c_str()), false;
  unlink(path.c_str());
  return !rename(tmp.c_str(), path.c_str());
}

String readText(const String& path, size_t max) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return "";
  String s;
  char b[512];
  size_t n;
  while (s.length() < max && (n = fread(b, 1, sizeof b, f)) > 0) s.concat(b, n);
  fclose(f);
  return s;
}

bool lfsInfo(uint64_t& total, uint64_t& freeB) {
#ifdef SIM
  total = 0x132000;
  const uint64_t used = treeBytes(String(simDir.c_str()) + "/fs") + 300 * 1024;  // + settings, caches
  freeB = used < total ? total - used : 0;
  return true;
#else
  total = LittleFS.totalBytes();
  const uint64_t used = LittleFS.usedBytes();
  freeB = used < total ? total - used : 0;
  return total > 0;
#endif
}

String sizeStr(uint64_t b) {
  char s[24];
  if (b < 1024) return String((unsigned)b) + " B";
  if (b < 1024 * 1024) return String((unsigned)((b + 1023) / 1024)) + " KB";
  const double mb = b / 1048576.0;
  if (mb < 1024) {
    if (mb < 100) snprintf(s, sizeof s, "%.1f MB", mb);
    else snprintf(s, sizeof s, "%.0f MB", mb);
  } else {
    snprintf(s, sizeof s, "%.1f GB", mb / 1024);
  }
  String r = s;
  r.replace(".0 ", " ");
  r.replace(".", i18n::decSep());  // the language's decimal mark ("7,5 MB" in Vietnamese)
  return r;
}

// ---------------------------------------------------------------- the open-file cache of File::read
// Plain file descriptors, not stdio: a FILE's 128-byte buffer made a 14 KB read from the card take 8.2 ms (one
// file-system call per 128 bytes); read() fetches it whole in 2.3 ms (CARDBENCH, 2026-11-17). An unbuffered FILE is
// far worse (380 ms: one byte a call).
namespace {
struct Handle {
  String path;
  int fd = -1;
  uint32_t used = 0;
};
std::mutex hmu;
Handle handles[4];
uint32_t useClock = 0;
}  // namespace

void closeHandles(const String& prefix) {
  std::lock_guard<std::mutex> l(hmu);
  for (Handle& h : handles)
    if (h.fd >= 0 && h.path.startsWith(prefix)) ::close(h.fd), h.fd = -1, h.path = "";
}

bool readAt(const String& path, uint32_t off, void* dst, uint32_t len) {
  std::lock_guard<std::mutex> l(hmu);
  Handle* h = nullptr;
  for (Handle& c : handles)
    if (c.fd >= 0 && c.path == path) h = &c;
  if (!h) {
    h = &handles[0];
    for (Handle& c : handles)
      if (c.fd < 0 || c.used < h->used) h = &c;
    if (h->fd >= 0) ::close(h->fd);
    h->fd = ::open(path.c_str(), O_RDONLY);
    h->path = h->fd >= 0 ? path : String();
    if (h->fd < 0) return false;
  }
  h->used = ++useClock;
  bool ok = ::lseek(h->fd, off, SEEK_SET) == (off_t)off;
  for (uint32_t done = 0; ok && done < len;) {  // read() may return less than asked
    const ssize_t n = ::read(h->fd, (uint8_t*)dst + done, len - done);
    ok = n > 0;
    if (ok) done += n;
  }
  if (!ok) {
    ::close(h->fd);  // a pulled card: start afresh next time
    h->fd = -1, h->path = "";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- CRC32 (zlib) and SHA-256
namespace {
uint32_t crcTable[256];
bool crcReady = false;
}  // namespace

uint32_t crc32(uint32_t crc, const void* p, size_t n) {
  if (!crcReady) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      crcTable[i] = c;
    }
    crcReady = true;
  }
  const uint8_t* b = (const uint8_t*)p;
  crc = ~crc;
  while (n--) crc = crcTable[(crc ^ *b++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

#ifdef SIM
namespace {
// Plain SHA-256 for the simulator (the board uses mbedTLS, with the hardware).
struct ShaCtx {
  uint32_t h[8], len[2];
  uint8_t buf[64];
  size_t n;
};
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
void block(ShaCtx* c, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
  for (int i = 0; i < 64; i++) {
    const uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
    const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
    h = g, g = f, f = e, e = d + t1, d = cc, cc = b, b = a, a = t1 + t2;
  }
  c->h[0] += a, c->h[1] += b, c->h[2] += cc, c->h[3] += d, c->h[4] += e, c->h[5] += f, c->h[6] += g, c->h[7] += h;
}
}  // namespace

Sha256::Sha256() {
  ShaCtx* c = new ShaCtx();
  const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(c->h, H0, sizeof H0);
  c->len[0] = c->len[1] = 0;
  c->n = 0;
  ctx = c;
}
Sha256::~Sha256() { delete (ShaCtx*)ctx; }
void Sha256::update(const void* p, size_t n) {
  ShaCtx* c = (ShaCtx*)ctx;
  const uint8_t* b = (const uint8_t*)p;
  const uint64_t bits = ((uint64_t)c->len[1] << 32 | c->len[0]) + (uint64_t)n * 8;
  c->len[0] = (uint32_t)bits, c->len[1] = (uint32_t)(bits >> 32);
  while (n) {
    const size_t k = min(n, 64 - c->n);
    memcpy(c->buf + c->n, b, k);
    c->n += k, b += k, n -= k;
    if (c->n == 64) block(c, c->buf), c->n = 0;
  }
}
String Sha256::hex() {
  ShaCtx* c = (ShaCtx*)ctx;
  const uint64_t bits = (uint64_t)c->len[1] << 32 | c->len[0];
  uint8_t pad[72] = {0x80};
  const size_t padLen = (c->n < 56 ? 56 : 120) - c->n;
  uint8_t lenb[8];
  for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
  update(pad, padLen);
  update(lenb, 8);
  static const char* H = "0123456789abcdef";
  String s;
  for (int i = 0; i < 8; i++)
    for (int k = 24; k >= 0; k -= 8) {
      const uint8_t v = (uint8_t)(c->h[i] >> k);
      s += H[v >> 4], s += H[v & 15];
    }
  return s;
}
#else
Sha256::Sha256() {
  mbedtls_sha256_context* c = (mbedtls_sha256_context*)heap_caps_malloc(sizeof(mbedtls_sha256_context), MALLOC_CAP_8BIT);
  mbedtls_sha256_init(c);
  mbedtls_sha256_starts(c, 0);
  ctx = c;
}
Sha256::~Sha256() {
  mbedtls_sha256_free((mbedtls_sha256_context*)ctx);
  heap_caps_free(ctx);
}
void Sha256::update(const void* p, size_t n) { mbedtls_sha256_update((mbedtls_sha256_context*)ctx, (const uint8_t*)p, n); }
String Sha256::hex() {
  uint8_t out[32];
  mbedtls_sha256_finish((mbedtls_sha256_context*)ctx, out);
  static const char* H = "0123456789abcdef";
  String s;
  for (uint8_t b : out) s += H[b >> 4], s += H[b & 15];
  return s;
}
#endif

// ---------------------------------------------------------------- the I/O buffer
namespace {
uint8_t* io = nullptr;
}
uint8_t* ioBuf() {
  if (!io) io = (uint8_t*)heap_caps_malloc(IOBUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return io;
}
void ioRelease() {
  heap_caps_free(io);
  io = nullptr;
}

// ---------------------------------------------------------------- the index (NVS namespace "pkgs")
namespace idx {

String keyOf(const String& id) {
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < id.length(); i++) h = (h ^ (uint8_t)id[i]) * 16777619u;
  char b[12];
  snprintf(b, sizeof b, "p%08lx", (unsigned long)h);
  return b;
}

#ifdef SIM
namespace {
String dirOf() { return String(simDir.c_str()) + "/nvs/pkgs"; }
}  // namespace
bool getRaw(const char* key, String& out) {
  if (!exists(dirOf() + "/" + key)) return false;
  out = readText(dirOf() + "/" + key);
  return true;
}
bool putRaw(const char* key, const String& v) {
  mkdirs(dirOf());
  return writeText(dirOf() + "/" + key, v);
}
bool delRaw(const char* key) { return !unlink((dirOf() + "/" + key).c_str()); }
std::vector<String> all() {
  std::vector<String> out;
  for (const String& k : listDir(dirOf()))
    if (k[0] == 'p' && !k.endsWith(".tmp")) out.push_back(readText(dirOf() + "/" + k));
  return out;
}
#else
namespace {
nvs_handle_t open(bool rw) {
  nvs_handle_t h = 0;
  if (nvs_open("pkgs", rw ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK) return 0;
  return h;
}
}  // namespace
bool getRaw(const char* key, String& out) {
  nvs_handle_t h = open(false);
  if (!h) return false;
  size_t n = 0;
  bool ok = nvs_get_blob(h, key, nullptr, &n) == ESP_OK;
  if (ok) {
    char* b = (char*)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
    ok = b && nvs_get_blob(h, key, b, &n) == ESP_OK;
    if (ok) b[n] = 0, out = b;
    heap_caps_free(b);
  }
  nvs_close(h);
  return ok;
}
bool putRaw(const char* key, const String& v) {
  nvs_handle_t h = open(true);
  if (!h) return false;
  const bool ok = nvs_set_blob(h, key, v.c_str(), v.length()) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  if (!ok) trace::add("pkg: NVS write %s failed", key);
  return ok;
}
bool delRaw(const char* key) {
  nvs_handle_t h = open(true);
  if (!h) return false;
  const bool ok = nvs_erase_key(h, key) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
std::vector<String> all() {
  std::vector<String> keys, out;
  nvs_iterator_t it = nullptr;
  esp_err_t r = nvs_entry_find(NVS_DEFAULT_PART_NAME, "pkgs", NVS_TYPE_BLOB, &it);
  while (r == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (info.key[0] == 'p') keys.push_back(info.key);
    r = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  for (const String& k : keys) {
    String v;
    if (getRaw(k.c_str(), v)) out.push_back(v);
  }
  return out;
}
#endif

bool put(const String& id, const String& json) { return putRaw(keyOf(id).c_str(), json); }
bool del(const String& id) { return delRaw(keyOf(id).c_str()); }

}  // namespace idx

}  // namespace in
}  // namespace pkg
