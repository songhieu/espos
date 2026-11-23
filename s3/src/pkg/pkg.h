#pragma once
// Installed packages: apps and games downloaded from the store, kept in the internal flash or on the memory
// card (docs/ecosystem.md). A package is one .espk file (tools/espk.py): a manifest and its files. Big
// packages come in parts (each part its own .espk), e.g. the run game's "core" + "roads10".
//
// Readers (engines) only need open() + File: the same code runs from the flash or the card.
// list(), find(), volume(), run() are for the UI loop; open() and File::read() work from any task.
// Installing, removing, moving: store.h. Developer guide: docs/packages.md.
#include <Arduino.h>

#include <vector>

namespace pkg {

enum Vol : uint8_t { FLASH = 0, SD = 1 };

struct VolInfo {
  bool present = false;  // FLASH: always; SD: a card is in and mounted
  uint32_t totalKB = 0, freeKB = 0, pkgsKB = 0;
};
VolInfo volume(Vol v);

struct Package {
  String id, name, version;
  String kind;    // "js" (QuickJS app) or "native" (an engine built into the firmware)
  String engine;  // "js", "pet", "run"
  String category, desc, author, glyph;
  String addonOf;  // an add-on of that package (e.g. the run "Chạy lấy xu" of vn.pawboop.pet): no icon of its own
  String type;     // manifest "type" (docs/platform.md): "pack" = data for an engine (no icon, never opened)
  String set;      // an add-on's shelf on its app's store page: "dog", "map" (PawBoop), "" = Add-ons
  String names;    // the manifest's "i18n" names, {"vi": "Tập trung", ...}: list() shows the one of the language shown
  uint32_t code = 0, bg1 = 0x64748b, bg2 = 0x334155;
  Vol vol = FLASH;
  std::vector<String> parts;  // installed parts, e.g. {"core", "roads10"}
  std::vector<String> perms;  // "sound", "notify", "net", "bg"
  uint32_t bytes = 0;
  bool available = true;  // false: it is on a card that is not in
  bool icon = false;      // has icon.jpg
};
const std::vector<Package>& list();
const Package* find(const char* id);
uint32_t version();  // changes on every install, removal, move, card in/out
// The memory card has been looked for since power-on (mounted or not there). Until then the apps it held last time
// are drawn as usual rather than greyed: the card nearly always answers a moment later (0.5.7: Home was built twice).
bool cardChecked();
// At boot, before Home is built: waits up to `ms` for the first look for a card and takes in what it found.
void awaitCard(uint32_t ms);
void relabel();      // the language changed: names again (i18n.cpp)

// One file of an installed package (any of its parts), e.g. open("vn.pawboop.run", "core/run_dog.pbs", f).
class File {
 public:
  bool ok() const { return len_ || map_; }
  uint32_t size() const { return len_; }
  // Any task, thread safe. false on an I/O error (card pulled out) or a range past the end.
  bool read(uint32_t off, void* dst, uint32_t len) const;
  // The whole file memory-mapped when it lies in the internal flash (zero copy, readable from any task, may be at
  // any alignment: copy before 16/32-bit loads); nullptr when it is on the card: use read().
  const uint8_t* data() const { return map_; }

 private:
  friend bool open(const char*, const char*, File&);
  const uint8_t* map_ = nullptr;
  uint32_t len_ = 0;
  uint32_t addr_ = 0;  // FLASH: offset in the app area
  String path_;        // SD (and the simulator): file path
};
bool open(const char* id, const char* name, File& f);
// The whole file in PSRAM (heap_caps_malloc SPIRAM; free with heap_caps_free). nullptr if no memory / I/O error.
uint8_t* loadAll(const File& f);
// The manifest.json kept with a package that lives in a folder (JavaScript apps on the internal LittleFS or the card),
// with every field the store sent (settings, perms...); "" for others. UI loop. (lane 1f: jsnet reads `settings`.)
String manifestJson(const char* id);

// Starts the app of an installed package (engine dispatch). arg as in apps::open.
void run(const char* id, const char* arg);

// ---- added by the package system (2026-11-16)
void begin();                   // once, on the UI loop (apps::all() calls it)
bool engineKnown(const String& engine);
uint32_t restartKB();           // removed while in use: free after the next restart
String sizeText(uint64_t bytes);  // "7,5 MB", "16 KB" (Vietnamese decimal comma)
// The Dung lượng (storage) page: a bar per volume, the apps by size, remove / move (storage_ui.cpp).
void openStorage();

}  // namespace pkg
