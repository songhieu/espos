#pragma GCC optimize("Os")
#include "dogs.h"

#include "desk.h"
#include "lock.h"
#include "packs.h"
#include "pkg/pkg.h"

namespace pet {
namespace dogs {

const Info ALL[COUNT] = {{"bao", "Bao"},     {"momo", "Momo"},   {"boop", "Boop"},     {"leaf", "Leaf"},
                         {"volt", "Volt"},   {"byte", "Byte"},   {"shiba", "Rocket"}, {"corgi", "Biscuit"},
                         {"husky", "Frost"}, {"golden", "Sunny"}};

namespace {
uint32_t seenPkg = 0, seenSet = 0, seenLock = 0, ver = 1;
const Info* cur = nullptr;

// Installed and not locked (lock.h: a paid dog after the trial stays installed; s3.pet_dog keeps naming it, so it
// is the dog again, as it was, once it is bought).
bool usable(const char* key) { return installed(key) && lock::may(pkgId(key).c_str()); }

void refresh() {
  if (seenPkg == pkg::version() && seenSet == desk::settingsVersion() && seenLock == lock::version() && seenPkg) return;
  seenPkg = pkg::version(), seenSet = desk::settingsVersion(), seenLock = lock::version();
  const Info* want = byKey(desk::setting("s3.pet_dog", "").c_str());
  const Info* pick = want && usable(want->key) ? want : nullptr;
  for (int i = 0; i < COUNT && !pick; i++)
    if (usable(ALL[i].key)) pick = &ALL[i];
  if (pick != cur) cur = pick, ver++;
}
}  // namespace

const Info* byKey(const char* key) {
  for (const Info& d : ALL)
    if (key && !strcmp(d.key, key)) return &d;
  return nullptr;
}

String pkgId(const char* key) { return String("vn.pawboop.dog.") + key; }

bool installed(const char* key) {
  const pkg::Package* p = pkg::find(pkgId(key).c_str());
  return p && p->available;
}

int installedCount() {
  int n = 0;
  for (const Info& d : ALL) n += installed(d.key);
  return n;
}

const Info* chosen() {
  refresh();
  return cur;
}

const char* name() {
  const Info* d = chosen();
  if (!d) d = byKey(pk::dogKey());  // PawBoop 1.0's own Biscuit
  return d ? d->name : "PawBoop";
}

void choose(const char* key) {
  if (!byKey(key)) return;
  desk::setSetting("s3.pet_dog", key);
  refresh();
}

uint32_t version() {
  refresh();
  return ver;
}

}  // namespace dogs
}  // namespace pet
