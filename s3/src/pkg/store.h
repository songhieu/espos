#pragma once
// The store's catalog and the installer (docs/ecosystem.md §2, §5, §6). One job at a time, in a background
// task: download each part over HTTPS (several byte ranges at once), check the part's SHA-256 against the
// catalog and each file's CRC32, write the files to the chosen volume, and only then commit (index in NVS for
// the internal area, a folder rename on LittleFS or the card). A failure frees everything it took.
// UI loop only, except status() which any task may read.
#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include "pkg.h"

namespace pkg {
namespace store {

// ---------------------------------------------------------------- catalog ("packages" in apps-s3/catalog.json)
struct Part {
  String id, group, label, url, sha256;
  String board;  // made for that board only ("es3c35p", "es3c28p"; the catalog's "board"): "" = any (2026-11-19)
  uint32_t size = 0;
  bool req = false, def = false;
  bool opt = false;  // downloaded with the core when a memory card is in and there is room (PawBoop's game parts)
};
struct Entry {
  String id, name, version, kind, engine, category, author, desc, glyph, iconUrl;
  String addonOf;  // shown inside that package's page ("Phần thêm"), not listed on its own
  String type;     // "pack": data for an engine (e.g. the Java runtime), installed with what needs it, never listed
  String set;      // an add-on's shelf on its app's page: "dog", "map" (PawBoop's dogs and run maps), "" = Add-ons
  String needs;    // another package it is useless without (a run map needs the run game): installed first
  bool assistant = false;  // an assistant app (manifest "provides": ["assistant"], or the dog's engine): the Store's Assistants
  bool saver = false;      // replaces the screen saver ("provides": ["saver"], or the aquarium's engine): its Screen savers
  uint32_t code = 0, minFw = 0, bg1 = 0x64748b, bg2 = 0x334155;
  std::vector<String> perms;
  std::vector<Part> parts;
  bool supported() const;  // the firmware has its engine and is new enough
};
void refresh(bool force = false);  // fetch the catalog (at most every few minutes unless forced)
bool loaded();
bool failed();
const std::vector<Entry>& catalog();
const Entry* entry(const char* id);  // the store's catalog, then the user's own packages
// The user's own packages from the sync answer ("user_pkgs"); a missing key leaves them as they were.
void setUserPkgs(JsonArrayConst list);
constexpr const char* J2ME_RUNTIME = "dev.espos.j2me";  // the Java games' class library (classlib.jar)
uint32_t catalogVersion();
std::vector<String> defaultParts(const Entry& e);
uint64_t partsBytes(const Entry& e, const std::vector<String>& parts);
bool hasUpdate(const char* id);  // the catalog has a newer code than the installed one
String addonOf(const char* id);  // the main package of an add-on (catalog or installed), "" when it is not one
std::vector<String> addons(const char* mainId);  // its add-ons: in the catalog, then installed ones it lacks

// ---------------------------------------------------------------- room
struct Room {
  bool ok = true;
  uint64_t need = 0, free = 0, missing = 0;
  uint64_t capacity = 0;  // the most that place can ever hold for this kind of package
  bool restart = false;  // it would fit after a restart (space of apps removed while in use)
};
// On Máy (FLASH) a package's files go to LittleFS when it is small and file-based: JavaScript apps, languages
// (engine "lang") and small fonts (engine "font", <= 384 KB); everything else to the memory-mapped app area.
bool onLfs(const String& kind, const String& engine, uint64_t bytes);
// Where a package of `bytes` goes: `js` (onLfs) on FLASH -> LittleFS; others on FLASH -> the app area; SD -> card.
Room room(uint64_t bytes, Vol v, bool js, const char* replacing = nullptr);
std::vector<const Package*> biggest(Vol v, int n);  // installed, by size
// Installed packages whose removal frees room for a package going to (v, js), biggest first: native packages
// for the internal app area, JavaScript ones for LittleFS, any for the card.
std::vector<const Package*> helpers(Vol v, bool js, int n);
bool inUse(const char* id);  // read this boot from the internal flash: its room frees after a restart

// ---------------------------------------------------------------- jobs
bool install(const Entry& e, const std::vector<String>& parts, Vol v, String& why);
bool update(const char* id, String& why);  // same parts and place, the catalog's newer version
bool remove(const char* id, String& why);
bool move(const char* id, Vol to, String& why);
// Stops an install that waits or is downloading (0.5.7): nothing of it stays, the old version (an update) neither
// changes. false once it is writing its list (too late to stop safely) or for any other job.
bool cancel(const char* id);
bool cancellable(const char* id);

enum State : uint8_t { IDLE, QUEUED, DOWNLOAD, WRITE, MOVE, REMOVE, COMPACT, DONE, ERROR, CHECK };  // CHECK: 0.5.7
struct Status {
  String id, name;
  State state = IDLE;
  uint8_t kind = 0;  // 0 install or update, 1 remove, 2 move
  int pct = 0;
  uint64_t done = 0, total = 0;
  String err;
  uint32_t seq = 0;  // bumps on every change
  bool cancelled = false;  // ERROR because the user stopped it
};
Status status();                 // the running job, or the last one that ended
State stateOf(const char* id);   // QUEUED, the running job's state, or IDLE
bool busy();                     // a job runs or waits
uint32_t version();              // bumps on job changes (screens refresh)
const char* stateText(State s);  // "Downloading", "Writing"... (translated)

// ---------------------------------------------------------------- the package system and sync (pkg.cpp, desk.cpp)
void begin();
void tick();                 // UI loop
void afterSync();            // a sync brought settings: reconcile s3.pkgs
void report(JsonObject o);   // the sync body's "pkgs"

}  // namespace store
}  // namespace pkg
