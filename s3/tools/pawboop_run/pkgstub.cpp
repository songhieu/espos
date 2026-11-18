// Simulator only, used by tools/pawboop_run/sim.sh when PKGSTUB=1 (the package system's sources are being
// changed and do not build right now): the first temporary src/pkg/pkg.cpp, a package's files read from
// $SIM_DATA/pkgs/<id>/<name>. Not part of the firmware.
#include <stdio.h>

#include <string>

#include "pkg/pkg.h"

extern std::string simDir;

namespace pkg {

namespace {
std::vector<Package> none;
}

VolInfo volume(Vol v) {
  VolInfo i;
  i.present = v == FLASH;
  return i;
}
const std::vector<Package>& list() { return none; }
const Package* find(const char*) { return nullptr; }
uint32_t version() { return 0; }

bool File::read(uint32_t off, void* dst, uint32_t len) const {
  if (off + len > len_ || off + len < off) return false;
  if (map_) {
    memcpy(dst, map_ + off, len);
    return true;
  }
  FILE* f = fopen(path_.c_str(), "rb");
  if (!f) return false;
  const bool ok = !fseek(f, off, SEEK_SET) && fread(dst, 1, len, f) == len;
  fclose(f);
  return ok;
}

bool open(const char* id, const char* name, File& f) {
  f = File();
  const String p = String(simDir.c_str()) + "/pkgs/" + id + "/" + name;
  FILE* h = fopen(p.c_str(), "rb");
  if (!h) return false;
  fseek(h, 0, SEEK_END);
  f.len_ = ftell(h);
  fclose(h);
  f.path_ = p;
  return true;
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

void run(const char*, const char*) {}
void begin() {}
bool engineKnown(const String& e) { return e == "run" || e == "pet" || e == "js"; }
uint32_t restartKB() { return 0; }
String sizeText(uint64_t b) { return String((unsigned long)(b / 1024)) + " KB"; }
void openStorage() {}

}  // namespace pkg

// The screens of app_more.cpp, left out of the stub build when they need the package system's new parts.
namespace app_store {
void __attribute__((weak)) open(const char*) {}
}
namespace app_photos {
void __attribute__((weak)) open(const char*) {}
}
namespace app_calc {
void __attribute__((weak)) open(const char*) {}
}
namespace app_lunar {
void __attribute__((weak)) open(const char*) {}
}
