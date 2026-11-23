#pragma GCC optimize("Os")  // cold code: built small
// The device's secret store (secret.h). NVS keys are at most 15 characters, so a value lives under "v" + 8 hex digits
// of a hash of "<app> <key>", and the index "i" lists "<app> <key> <tag>" a line, which says what is kept without
// reading any value.
#include "secret.h"

#include <Preferences.h>

#include "trace.h"

namespace secret {
namespace {

Preferences nv;
bool opened = false;
uint32_t ver = 1;

Preferences& store() {
  if (!opened) opened = nv.begin("sec", false);
  return nv;
}

// The index, read once and kept in RAM: has() runs for every row of a form and every settings.get of a secret.
String idxRam;
bool idxRead = false;
const String& index() {
  if (!idxRead) idxRam = store().isKey("i") ? store().getString("i", "") : String(), idxRead = true;
  return idxRam;
}
void saveIndex(const String& idx) {
  idxRam = idx;
  store().putString("i", idx);
}

bool okName(const char* s) {
  if (!s || !*s || strlen(s) > 80) return false;
  for (; *s; s++)
    if (*s <= ' ' || *s == 0x7f) return false;
  return true;
}

String slot(const char* app, const char* key) {
  uint32_t h = 2166136261u;
  for (const char* p = app; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  h = (h ^ ' ') * 16777619u;
  for (const char* p = key; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  char b[12];
  snprintf(b, sizeof b, "v%08lx", (unsigned long)h);
  return b;
}

// The index line of (app, key): its start in `idx`, -1 when none.
int find(const String& idx, const char* app, const char* key, int* end = nullptr) {
  const String head = String(app) + " " + key + " ";
  for (int at = 0; at < (int)idx.length();) {
    int nl = idx.indexOf('\n', at);
    if (nl < 0) nl = idx.length();
    if (idx.startsWith(head, at)) {
      if (end) *end = nl;
      return at;
    }
    at = nl + 1;
  }
  return -1;
}

// Takes (app, key)'s line out of the index (with its value too when `value`).
void drop(String& idx, const char* app, const char* key, bool value) {
  int end = 0;
  const int at = find(idx, app, key, &end);
  if (at < 0) return;
  idx.remove(at, (end < (int)idx.length() ? end + 1 : end) - at);
  if (value) store().remove(slot(app, key).c_str());
}

}  // namespace

bool set(const char* app, const char* key, const String& value, const char* tag) {
  if (!okName(app) || !okName(key) || value.length() > 2000) return false;
  String idx = index();
  if (!value.length()) {
    clear(app, key);
    return true;
  }
  const String k = slot(app, key);
  // Another (app, key) on the same slot (a hash collision, ~1 in 4 billion): refused rather than overwritten.
  for (const Entry& e : list())
    if ((e.app != app || e.key != key) && slot(e.app.c_str(), e.key.c_str()) == k) return false;
  if (!store().putString(k.c_str(), value)) return false;
  drop(idx, app, key, false);
  String t = tag ? tag : "";
  t.replace(" ", "");
  t.replace("\n", "");
  idx += String(app) + " " + key + " " + t + "\n";
  saveIndex(idx);
  ver++;
  trace::add("secret: %s %s set (%u chars)", app, key, (unsigned)value.length());
  return true;
}

bool get(const char* app, const char* key, String& out) {
  out = "";
  if (!okName(app) || !okName(key) || find(index(), app, key) < 0) return false;
  out = store().getString(slot(app, key).c_str(), "");
  return out.length() > 0;
}

bool has(const char* app, const char* key) {
  return okName(app) && okName(key) && find(index(), app, key) >= 0;
}

String hint(const char* app, const char* key) {
  String v;
  if (!get(app, key, v)) return "";
  int n = 0, at = v.length();
  while (at > 0 && n < 4)
    if (((uint8_t)v[--at] & 0xc0) != 0x80) n++;
  return v.substring(at);
}

String tag(const char* app, const char* key) {
  const String& idx = index();
  int end = 0;
  const int at = okName(app) && okName(key) ? find(idx, app, key, &end) : -1;
  if (at < 0) return "";
  return idx.substring(at + strlen(app) + strlen(key) + 2, end);
}

void clear(const char* app, const char* key) {
  if (!okName(app) || !okName(key)) return;
  String idx = index();
  if (find(idx, app, key) < 0) return;
  drop(idx, app, key, true);
  saveIndex(idx);
  ver++;
  trace::add("secret: %s %s cleared", app, key);
}

void clearApp(const char* app) {
  for (const Entry& e : list())
    if (e.app == app) clear(e.app.c_str(), e.key.c_str());
}

std::vector<Entry> list() {
  std::vector<Entry> out;
  const String& idx = index();
  for (int at = 0; at < (int)idx.length();) {
    int nl = idx.indexOf('\n', at);
    if (nl < 0) nl = idx.length();
    const int s1 = idx.indexOf(' ', at), s2 = s1 < 0 ? -1 : idx.indexOf(' ', s1 + 1);
    if (s1 > at && s2 > s1 && s2 < nl) out.push_back({idx.substring(at, s1), idx.substring(s1 + 1, s2)});
    at = nl + 1;
  }
  return out;
}

uint32_t version() { return ver; }

}  // namespace secret
