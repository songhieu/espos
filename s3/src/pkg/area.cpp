// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
// The internal app area: the flash range of pb_dog + pb_ui + pb_voice (or one partition named "apps"),
// memory-mapped for reading. Files are contiguous 4 KB extents; the index of what lies where is in NVS.
// Simulator: the file $SIM_DATA/apparea.bin, mapped the same way.
#include <algorithm>

#include "internal.h"
#include "trace.h"

#ifdef SIM
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
extern std::string simDir;
#else
#include "esp_flash.h"
#include "esp_memory_utils.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#endif

namespace pkg {
namespace in {
namespace area {
namespace {

uint32_t size_ = 0;
const uint8_t* base_ = nullptr;
std::vector<Ext> reserved, zombies, dirty;

#ifdef SIM
int fd = -1;
constexpr uint32_t SIM_SIZE = 7528448;

bool rawErase(uint32_t off, uint32_t len) {
  static std::vector<uint8_t> ff(SECTOR, 0xff);
  for (uint32_t k = 0; k < len; k += SECTOR)
    if (pwrite(fd, ff.data(), min(SECTOR, len - k), off + k) < 0) return false;
  return true;
}
bool rawWrite(uint32_t off, const void* p, uint32_t len) {
  // Like NOR flash: bits only go from 1 to 0.
  const uint8_t* s = (const uint8_t*)p;
  uint8_t b[1024];
  for (uint32_t k = 0; k < len; k += sizeof b) {
    const uint32_t n = min<uint32_t>(sizeof b, len - k);
    for (uint32_t i = 0; i < n; i++) b[i] = base_[off + k + i] & s[k + i];
    if (pwrite(fd, b, n, off + k) != (ssize_t)n) return false;
  }
  return true;
}

// A new simulator data folder starts like the board after the USB switch: the PawBoop packs at their
// partitions' places (unless SIM_NO_PACKS is set or $SIM_DATA/pawboop is missing).
void makeArea(const std::string& path) {
  int f = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (f < 0) return;
  std::vector<uint8_t> ff(SIM_SIZE, 0xff);
  if (!getenv("SIM_NO_PACKS")) {
    struct P {
      const char* name;
      uint32_t at;
    };
    const P packs[] = {{"biscuit_M.pbs", 0}, {"pawboop_ui.pbu", 0x51A000}, {"biscuit_voice_12k.pba", 0x6FD000}};
    for (const P& p : packs) {
      FILE* in = fopen((simDir + "/pawboop/" + p.name).c_str(), "rb");
      if (!in) continue;
      fread(ff.data() + p.at, 1, SIM_SIZE - p.at, in);
      fclose(in);
    }
  }
  if (::write(f, ff.data(), ff.size()) != (ssize_t)ff.size()) trace::add("pkg: could not write %s", path.c_str());
  ::close(f);
  trace::add("pkg: new simulator app area %s", path.c_str());
}
#else
uint32_t start_ = 0;
spi_flash_mmap_handle_t mapH;

bool rawErase(uint32_t off, uint32_t len) { return esp_flash_erase_region(nullptr, start_ + off, len) == ESP_OK; }
bool rawWrite(uint32_t off, const void* p, uint32_t len) {
  esp_err_t e;
  if (esp_ptr_internal(p)) {
    if ((e = esp_flash_write(nullptr, p, start_ + off, len)) == ESP_OK) return true;
    trace::add("pkg: flash write %u B at 0x%x failed: %s", (unsigned)len, (unsigned)(start_ + off), esp_err_to_name(e));
    return false;
  }
  uint8_t* b = ioBuf();
  if (!b) {
    trace::add("pkg: no %u B of internal RAM for a flash write", (unsigned)IOBUF);
    return false;
  }
  for (uint32_t k = 0; k < len; k += IOBUF) {
    const uint32_t n = min<uint32_t>(IOBUF, len - k);
    memcpy(b, (const uint8_t*)p + k, n);
    if ((e = esp_flash_write(nullptr, b, start_ + off + k, n)) != ESP_OK) {
      trace::add("pkg: flash write %u B at 0x%x failed: %s", (unsigned)n, (unsigned)(start_ + off + k), esp_err_to_name(e));
      return false;
    }
  }
  return true;
}
#endif

void addExt(std::vector<Ext>& v, uint32_t off, uint32_t len) {
  if (len) v.push_back({off, roundUp(len)});
}

// Finishes a slide that a power cut interrupted (see compact).
void recover() {
  String j;
  if (!idx::getRaw("jr", j)) return;
  JsonDocument d;
  String blob;
  Rec r;
  if (deserializeJson(d, j) == DeserializationError::Ok && idx::getRaw(idx::keyOf(d["id"] | "").c_str(), blob) &&
      recFromJson(blob, r)) {
    const uint32_t from = d["from"] | 0u, to = d["to"] | 0u, n = d["n"] | 0u;
    uint32_t done = d["done"] | 0u;
    for (FileRec& f : r.files)
      if (f.name == (d["f"] | "") && f.addr == from) {
        trace::add("pkg: finishing an interrupted move of %s/%s (%u of %u sectors)", r.p.id.c_str(), f.name.c_str(),
                   (unsigned)done, (unsigned)n);
        uint8_t* b = ioBuf();
        for (; b && done < n; done++) {
          memcpy(b, base_ + from + done * SECTOR, SECTOR);
          rawErase(to + done * SECTOR, SECTOR);
          rawWrite(to + done * SECTOR, b, SECTOR);
        }
        f.addr = to;
        idx::put(r.p.id, recJson(r));
      }
  }
  idx::delRaw("jr");
}

void eraseTombstones() {
  String t;
  if (!idx::getRaw("tomb", t)) return;
  JsonDocument d;
  deserializeJson(d, t);
  std::vector<Ext> live;
  for (const String& j : idx::all()) {
    Rec r;
    if (recFromJson(j, r))
      for (const FileRec& f : r.files) addExt(live, f.addr, f.size);
  }
  for (JsonVariantConst v : d.as<JsonArrayConst>()) {
    const uint32_t off = v | 0xffffffffu;
    if (off >= size_ || off % SECTOR) continue;
    bool inUse = false;
    for (const Ext& e : live) inUse |= off >= e.off && off < e.off + e.len;
    if (!inUse) rawErase(off, SECTOR);
  }
  idx::delRaw("tomb");
}

bool movable(const Ext& e, Rec*& owner, FileRec*& file) {
  for (const Ext& z : zombies)
    if (z.off == e.off) return false;
  for (const Ext& z : reserved)
    if (z.off == e.off) return false;
  for (Rec& r : recs)
    if (r.area)
      for (FileRec& f : r.files)
        if (f.size && f.addr == e.off) {
          owner = &r, file = &f;
          return !pinned(r.p.id);
        }
  return false;
}

}  // namespace

bool begin() {
  if (base_) return true;
#ifdef SIM
  const std::string path = simDir + "/apparea.bin";
  struct stat st;
  if (stat(path.c_str(), &st)) makeArea(path);
  fd = ::open(path.c_str(), O_RDWR);
  if (fd < 0 || fstat(fd, &st) || st.st_size < (off_t)SIM_SIZE) {
    trace::add("pkg: no app area (%s)", path.c_str());
    return false;
  }
  size_ = SIM_SIZE;
  void* m = mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd, 0);
  if (m == MAP_FAILED) return false;
  base_ = (const uint8_t*)m;
#else
  const esp_partition_t* a = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "apps");
  if (a) {
    start_ = a->address, size_ = a->size;
  } else {
    const esp_partition_t* d = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "pb_dog");
    const esp_partition_t* u = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "pb_ui");
    const esp_partition_t* v = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "pb_voice");
    if (!d || !u || !v || d->address + d->size != u->address || u->address + u->size != v->address) {
      trace::add("pkg: no app area in this partition table");
      return false;
    }
    start_ = d->address, size_ = v->address + v->size - d->address;
  }
  const void* ptr = nullptr;
  if (spi_flash_mmap(start_, size_, SPI_FLASH_MMAP_DATA, &ptr, &mapH) != ESP_OK) {
    trace::add("pkg: mmap of the app area failed");
    return false;
  }
  base_ = (const uint8_t*)ptr;
#endif
  recover();
  eraseTombstones();
  ioRelease();
  trace::add("pkg: app area %u KB mapped", (unsigned)(size_ / 1024));
  return true;
}

bool ready() { return base_ != nullptr; }
uint32_t size() { return size_; }
const uint8_t* base() { return base_; }

bool erase(uint32_t off, uint32_t len) {
  if (!base_ || off % SECTOR || len % SECTOR || off + len > size_ || off + len < off) return false;
  return rawErase(off, len);
}

bool write(uint32_t off, const void* p, uint32_t len) {
  if (!base_ || off + len > size_ || off + len < off) return false;
  return rawWrite(off, p, len);
}

std::vector<Ext> used() {
  std::vector<Ext> v;
  for (const Rec& r : recs)
    if (r.area)
      for (const FileRec& f : r.files) addExt(v, f.addr, f.size);
  v.insert(v.end(), reserved.begin(), reserved.end());
  v.insert(v.end(), zombies.begin(), zombies.end());
  std::sort(v.begin(), v.end(), [](const Ext& a, const Ext& b) { return a.off < b.off; });
  return v;
}

bool alloc(uint32_t len, uint32_t& off) {
  const uint32_t need = roundUp(len);
  if (!need) return off = 0, true;
  uint32_t at = 0;
  for (const Ext& e : used()) {
    if (e.off >= at + need) break;
    at = max(at, e.off + e.len);
  }
  if (at + need > size_) return false;
  reserved.push_back({at, need});
  off = at;
  return true;
}

void unreserve(uint32_t off) {
  for (auto it = reserved.begin(); it != reserved.end(); ++it)
    if (it->off == off) {
      reserved.erase(it);
      return;
    }
}
void unreserveAll() { reserved.clear(); }

void zombie(uint32_t off, uint32_t len) { addExt(zombies, off, len); }

uint32_t freeBytes() {
  uint32_t u = 0;
  for (const Ext& e : used()) u += e.len;
  return u < size_ ? size_ - u : 0;
}

uint32_t largestGap() {
  uint32_t at = 0, best = 0;
  for (const Ext& e : used()) {
    if (e.off > at) best = max(best, e.off - at);
    at = max(at, e.off + e.len);
  }
  return max(best, size_ > at ? size_ - at : 0);
}

uint32_t zombieBytes() {
  uint32_t n = 0;
  for (const Ext& z : zombies) n += z.len;
  return n;
}

bool blank(uint32_t off, uint32_t len) {
  if (!base_ || off + len > size_ || (off & 3) || (len & 3)) return false;
  const uint32_t* w = (const uint32_t*)(base_ + off);
  for (uint32_t i = 0; i < len / 4; i++)
    if (w[i] != 0xffffffffu) return false;
  return true;
}

void freed(uint32_t off, uint32_t len) {
  if (len) dirty.push_back({off & ~(SECTOR - 1), roundUp(len + (off & (SECTOR - 1)))});
}

bool eraseIdle() {
  std::lock_guard<std::recursive_mutex> l(mu);
  for (int checks = 0; !dirty.empty() && checks < 8; checks++) {
    Ext& d = dirty.front();
    if (!d.len) {
      dirty.erase(dirty.begin());
      continue;
    }
    // One step: up to the next 64 KB boundary (a whole block erases much faster than its 16 sectors).
    const uint32_t end = min(d.off + d.len, (d.off & ~0xFFFFu) + 0x10000u);
    const uint32_t off = d.off, len = end - off;
    d.off = end, d.len -= len;
    bool inUse = false;  // installed again, reserved by an install, or a removed app still read until restart
    for (const Ext& u : used()) inUse |= off < u.off + u.len && u.off < off + len;
    if (inUse || blank(off, len)) continue;
    rawErase(off, len);
    return true;
  }
  return !dirty.empty();
}

void tombstone(uint32_t off) {
  String t;
  JsonDocument d;
  if (idx::getRaw("tomb", t)) deserializeJson(d, t);
  if (!d.is<JsonArray>()) d.to<JsonArray>();
  d.as<JsonArray>().add(off);
  String out;
  serializeJson(d, out);
  idx::putRaw("tomb", out);
}

// Moves files down, one at a time, until a gap of `need` exists. A slide copies sector by sector from low to
// high (the destination is below the source, so a sector is only overwritten after it was copied); the journal
// in NVS records the progress at least every gap-size sectors, so after a power cut the copy resumes from a point
// whose source sectors are all still intact. The index moves to the new place only when the copy is complete.
bool compact(uint32_t need, void (*progress)(uint32_t, uint32_t)) {
  std::lock_guard<std::recursive_mutex> l(mu);
  need = roundUp(need);
  uint32_t total = 0;
  for (const Ext& e : used()) total += e.len;
  uint32_t moved = 0;
  for (int guard = 0; guard < 256; guard++) {
    if (largestGap() >= need) return true;
    uint32_t at = 0;
    bool slid = false;
    for (const Ext& e : used()) {
      Rec* r = nullptr;
      FileRec* f = nullptr;
      if (e.off > at && movable(e, r, f)) {
        const uint32_t from = e.off, to = at, n = e.len / SECTOR, step = max<uint32_t>(1, min<uint32_t>((from - to) / SECTOR, 64));
        trace::add("pkg: moving %s/%s down %u KB", r->p.id.c_str(), f->name.c_str(), (unsigned)((from - to) / 1024));
        setBusy(r->p.id, true);
        JsonDocument j;
        j["id"] = r->p.id, j["f"] = f->name, j["from"] = from, j["to"] = to, j["n"] = n, j["done"] = 0;
        String js;
        serializeJson(j, js);
        idx::putRaw("jr", js);
        uint8_t* b = ioBuf();
        bool ok = b != nullptr;
        for (uint32_t k = 0; ok && k < n; k++) {
          memcpy(b, base_ + from + k * SECTOR, SECTOR);
          ok = rawErase(to + k * SECTOR, SECTOR) && rawWrite(to + k * SECTOR, b, SECTOR);
          if (ok && (k + 1) % step == 0 && k + 1 < n) {
            j["done"] = k + 1;
            js = "";
            serializeJson(j, js);
            idx::putRaw("jr", js);
          }
          if (progress && k % 16 == 0) progress(moved + k * SECTOR, total);
#ifdef SIM
          // SIM_CUT_AT=n: the power goes off after n copied sectors (tests the journal).
          static int cut = getenv("SIM_CUT_AT") ? atoi(getenv("SIM_CUT_AT")) : -1;
          if (cut >= 0 && cut-- == 0) {
            trace::add("pkg: simulated power cut during a move");
            _exit(3);
          }
#endif
        }
        if (!ok) {
          // The journal stays: the next boot finishes this slide.
          setBusy(r->p.id, false);
          trace::add("pkg: move failed");
          return false;
        }
        j["done"] = n;
        js = "";
        serializeJson(j, js);
        idx::putRaw("jr", js);
        f->addr = to;
        idx::put(r->p.id, recJson(*r));
        idx::delRaw("jr");
        if (from + e.len > to + e.len) freed(max(from, to + e.len), from + e.len - max(from, to + e.len));
        setBusy(r->p.id, false);
        moved += e.len;
        slid = true;
        break;
      }
      at = max(at, e.off + e.len);
    }
    if (!slid) return largestGap() >= need;
  }
  return largestGap() >= need;
}

}  // namespace area
}  // namespace in
}  // namespace pkg
