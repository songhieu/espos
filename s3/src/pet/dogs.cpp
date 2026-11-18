#pragma GCC optimize("Os")
#include "dogs.h"

#include "desk.h"
#include "packs.h"
#include "pkg/pkg.h"

namespace pet {
namespace dogs {

const Info ALL[COUNT] = {{"bao", "Bao"},     {"momo", "Momo"},   {"boop", "Boop"},     {"leaf", "Leaf"},
                         {"volt", "Volt"},   {"byte", "Byte"},   {"shiba", "Rocket"}, {"corgi", "Biscuit"},
                         {"husky", "Frost"}, {"golden", "Sunny"}};

namespace {
uint32_t seenPkg = 0, seenSet = 0, ver = 1;
const Info* cur = nullptr;

void refresh() {
  if (seenPkg == pkg::version() && seenSet == desk::settingsVersion() && seenPkg) return;
  seenPkg = pkg::version(), seenSet = desk::settingsVersion();
  const Info* want = byKey(desk::setting("s3.pet_dog", "").c_str());
  const Info* pick = want && installed(want->key) ? want : nullptr;
  for (int i = 0; i < COUNT && !pick; i++)
    if (installed(ALL[i].key)) pick = &ALL[i];
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
