#pragma once
// Inside the package system (src/pkg): the registry of installed packages, the internal app area, the memory
// card, the index in NVS, file helpers and hashes. Not for other modules: they use pkg.h and store.h.
#include <Arduino.h>
#include <ArduinoJson.h>

#include <mutex>
#include <vector>

#include "pkg.h"

namespace pkg {
namespace in {

constexpr uint32_t SECTOR = 4096;
inline uint32_t roundUp(uint32_t n) { return (n + SECTOR - 1) & ~(SECTOR - 1); }

struct FileRec {
  String name;
  uint32_t addr = 0;  // area: offset in the app area
  uint32_t size = 0, crc = 0;
};

// One installed package. Files are either extents of the app area (`area`, index in NVS) or files in a folder
// (`dir`: LittleFS for small JavaScript apps on "Máy", the card for "Thẻ nhớ"; self-describing manifest.json).
struct Rec {
  Package p;
  bool area = false;
  String dir;
  std::vector<FileRec> files;
  const FileRec* file(const char* name) const {
    for (const FileRec& f : files)
      if (f.name == name) return &f;
    return nullptr;
  }
};

// The registry. `mu` guards recs and the area's allocation state (any task: installer, engines, UI loop).
extern std::recursive_mutex mu;
extern std::vector<Rec> recs;
Rec* rec(const String& id);   // caller holds mu
void changed();               // rebuild the public list on the UI loop (any task)
bool pinned(const String& id);
void setBusy(const String& id, bool busy);  // being moved: open() refuses it

// Folders (VFS paths on the board, host paths in the simulator).
String lfsRoot();  // small apps on "Máy": /littlefs/pkgs
String sdRoot();   // the card: /sdcard/espos/pkgs
String sdCacheFile();  // what the card holds, to keep its apps on Home while it is out

// Manifest <-> record.
void fromManifest(JsonObjectConst m, Package& p);
void toManifest(const Package& p, JsonObject m);   // the fields a manifest carries
bool scanDir(const String& dir, Vol vol, Rec& r);  // reads dir/manifest.json
std::vector<Rec> scanRoot(const String& root, Vol vol);

// ---------------------------------------------------------------- the app area (area.cpp)
namespace area {
bool begin();           // maps it; finishes a move cut by a power loss; erases tombstones
bool ready();
uint32_t size();
const uint8_t* base();  // memory-mapped, read only
bool erase(uint32_t off, uint32_t len);               // 4 KB aligned
bool write(uint32_t off, const void* p, uint32_t len);  // any source buffer
struct Ext {
  uint32_t off, len;
};
// Allocation (callers hold mu). In use: indexed files, reservations of a running install, and the extents of
// packages removed while still mapped this boot ("zombies", free after a restart).
std::vector<Ext> used();
bool alloc(uint32_t len, uint32_t& off);  // first fit (4 KB granularity); reserves it
void unreserve(uint32_t off);
void unreserveAll();
void zombie(uint32_t off, uint32_t len);
uint32_t freeBytes();
uint32_t largestGap();
uint32_t zombieBytes();
// Slides movable files down until a gap of `need` bytes exists (power-safe, journaled). false: cannot.
bool compact(uint32_t need, void (*progress)(uint32_t done, uint32_t total));
void tombstone(uint32_t off);  // erase this sector at the next boot (a removed package's header)
// Space with old bytes in it (a removed package, a failed install, a slide's old place): erased in the
// background while nobody uses the device (eraseIdle), so the next install mostly skips erasing.
void freed(uint32_t off, uint32_t len);
bool eraseIdle();                          // one 64 KB step (UI loop, internal stack); false: nothing left
bool blank(uint32_t off, uint32_t len);    // all 0xFF already (no erase needed)
}  // namespace area

// ---------------------------------------------------------------- the index of area packages (NVS "pkgs")
namespace idx {
std::vector<String> all();  // every package's JSON
bool put(const String& id, const String& json);
bool del(const String& id);
bool getRaw(const char* key, String& out);
bool putRaw(const char* key, const String& v);
bool delRaw(const char* key);
String keyOf(const String& id);
}  // namespace idx
String recJson(const Rec& r);   // area record -> index JSON
bool recFromJson(const String& json, Rec& r);

// ---------------------------------------------------------------- the memory card (card.cpp)
namespace card {
void begin();
void poll();         // UI loop: applies a scan done by the probe
bool present();      // mounted and readable
bool bad();          // a card is in but cannot be read (not FAT)
uint32_t version();  // bumps when it comes or goes
bool info(uint64_t& total, uint64_t& freeB);
void setBusy(bool busy);  // a job writes to the card: do not unmount under it
}  // namespace card

// ---------------------------------------------------------------- files (util.cpp)
bool mkdirs(const String& path);
bool rmTree(const String& path);
bool isDir(const String& p);
bool exists(const String& p);
std::vector<String> listDir(const String& p);
uint64_t treeBytes(const String& p);
bool writeText(const String& path, const String& text);
String readText(const String& path, size_t max = 65536);
void closeHandles(const String& prefix);  // the open-file cache of File::read
bool readAt(const String& path, uint32_t off, void* dst, uint32_t len);  // through that cache, any task
bool lfsInfo(uint64_t& total, uint64_t& freeB);

// ---------------------------------------------------------------- hashes (util.cpp)
uint32_t crc32(uint32_t crc, const void* p, size_t n);  // zlib's: crc32(0, ...) then chained
class Sha256 {
 public:
  Sha256();
  ~Sha256();
  void update(const void* p, size_t n);
  String hex();  // once
 private:
  void* ctx;
};

// Internal RAM buffer for flash writes (esp_flash writes PSRAM data 32 bytes at a time). One user at a time
// (the installer task, or boot); released when done.
uint8_t* ioBuf();
void ioRelease();
constexpr uint32_t IOBUF = 4096;

// A readable size: "7,5 MB", "16 KB", "14 GB".
String sizeStr(uint64_t bytes);

// ---------------------------------------------------------------- downloads (source.cpp)
// One part file, delivered in order. Board: HTTPS, several byte ranges at once into PSRAM slots.
// Simulator: the file the fake network has for that URL ($SIM_DATA/net), at a set speed.
class Source {
 public:
  virtual ~Source() {}
  // The next piece in order: n == 0 at the end. false: failed (err says why).
  virtual bool next(const uint8_t*& p, size_t& n) = 0;
  virtual void release() = 0;  // done with the piece next() gave
  virtual uint64_t got() = 0;  // bytes downloaded so far
  String err;
};
Source* openSource(const String& url, uint32_t size);
extern volatile bool abortAll;  // the simulator is quitting
// Simulator: a job's bytes follow simulator time (SIM_PKG_KBPS), and the UI loop waits while the job works, so
// progress shows the same in every run. No-ops on the board.
void paceStart();
void pace(uint64_t done);
void paceIdle(bool idle);
void paceWait();  // UI loop: until the job waits for time again

}  // namespace in
}  // namespace pkg
