#pragma GCC optimize("Os")  // not speed-critical: built small (the firmware must fit 3.75 MB)
// Languages (i18n.h, docs/i18n.md). A language pack's strings.bin, little endian:
//   "ESL1", u32 n, u32 poolOff, u32 0, then n x {u32 hash, u32 off} sorted by hash (FNV-1a 32 of the English),
//   then the texts (UTF-8, NUL-terminated; a list's items are NUL-separated and end with an extra NUL).
// It is read in place when it lies in the memory-mapped flash, otherwise copied into PSRAM once. A pack replaced
// or left while running stays where it is until the next restart (labels may still point into it).
#include "i18n.h"

#include <lvgl.h>

#include <cstring>
#include <vector>

#include "desk.h"
#include "fonts.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "trace.h"

#ifdef SIM
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#else
extern "C" {
extern const char _rodata_start[], _rodata_end[];
}
#endif

namespace i18n {

// The same list: tools/i18n.py LANGS and i18n/languages.json.
const Lang LANGS[] = {
    {"en", "English", N_("English"), nullptr},
    {"vi", "Tiếng Việt", N_("Vietnamese"), "dev.espos.font.vi"},
    {"zh-Hans", "简体中文", N_("Chinese (Simplified)"), "dev.espos.font.sc"},
    {"es", "Español", N_("Spanish"), nullptr},
    {"fr", "Français", N_("French"), nullptr},
    {"de", "Deutsch", N_("German"), nullptr},
    {"ja", "日本語", N_("Japanese"), "dev.espos.font.jp"},
    {"ko", "한국어", N_("Korean"), "dev.espos.font.kr"},
    {"pt-BR", "Português (Brasil)", N_("Portuguese (Brazil)"), nullptr},
    {"ru", "Русский", N_("Russian"), "dev.espos.font.cyr"},
    {"id", "Bahasa Indonesia", N_("Indonesian"), nullptr},
};
const int LANG_COUNT = sizeof LANGS / sizeof LANGS[0];

namespace {

struct Pack {
  String tag;
  const uint8_t* base = nullptr;
  uint32_t n = 0;
  const uint32_t* tab = nullptr;  // n x {hash, off}
  const char* pool = nullptr;
  char plural = 'e';              // 'e' one|other (n == 1), 'f' (n <= 1), 'r' Russian, '1' one form
  char grp[4] = ",", dec[4] = ".";
};
Pack* cur = nullptr;  // nullptr: English
String shownTag = "en";
bool readyFlag = false;
String dlTag;              // downloading for this language
uint32_t dlTries = 0, dlFailAt = 0;
String autoTried;          // sys.lang value already fetched once this boot (retried after 30 min)

uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

const char* lookup(const Pack* p, const char* key) {
  if (!p || !key) return nullptr;
  const uint32_t h = fnv(key);
  uint32_t lo = 0, hi = p->n;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2, mh = p->tab[mid * 2];
    if (mh == h) return p->pool + p->tab[mid * 2 + 1];
    if (mh < h) lo = mid + 1;
    else hi = mid;
  }
  return nullptr;
}

// The English of a key with a context ("ctx\x04text").
const char* plain(const char* s) {
  const char* c = strchr(s, 4);
  return c ? c + 1 : s;
}

void copyChar(char* dst, const char* src) {  // one UTF-8 letter (at most 3 bytes)
  int n = 1;
  const uint8_t c = *src;
  if (c >= 0xE0) n = 3;
  else if (c >= 0xC0) n = 2;
  if (!c) n = 0;
  memcpy(dst, src, n);
  dst[n] = 0;
}

Pack* load(const char* tag) {
  pkg::File f;
  if (!pkg::open(packId(tag).c_str(), "strings.bin", f) || f.size() < 16) return nullptr;
  const uint8_t* b = f.data();
  if (!b || ((uintptr_t)b & 3)) b = pkg::loadAll(f);  // on the card or LittleFS: a copy in PSRAM
  if (!b) return nullptr;
  uint32_t hd[4];
  memcpy(hd, b, 16);
  if (memcmp(b, "ESL1", 4) || hd[2] > f.size() || 16 + hd[1] * 8 > hd[2]) {
    trace::add("i18n: %s: bad strings.bin", tag);
    return nullptr;
  }
  Pack* p = new Pack;
  p->tag = tag;
  p->base = b;
  p->n = hd[1];
  p->tab = (const uint32_t*)(b + 16);
  p->pool = (const char*)b + hd[2];
  if (const char* r = lookup(p, "@plural")) p->plural = *r;
  if (const char* s = lookup(p, "@num")) {  // group separator then decimal separator, e.g. ".," or " ,"
    copyChar(p->grp, s);
    copyChar(p->dec, s + strlen(p->grp));
  }
  return p;
}

// Font package of a language: files r<px>.bin / b<px>.bin (tools/i18n.py font).
bool fontReady(const Lang* l) {
  if (!l || !l->font) return true;
  const pkg::Package* f = pkg::find(l->font);
  return f && f->available;  // on a card: once it is in
}

std::vector<std::pair<String, Pack*>> loaded;  // kept until the next restart

Pack* packFor(const char* tag) {
  for (auto& x : loaded)
    if (x.first == tag) return x.second;
  Pack* p = load(tag);
  if (p) loaded.push_back({tag, p});
  return p;
}

}  // namespace

const Lang* find(const char* tag) {
  for (const Lang& l : LANGS)
    if (!strcmp(l.tag, tag)) return &l;
  return nullptr;
}

String packId(const char* tag) { return String("dev.espos.lang.") + tag; }

bool inImage(const void* p) {
#ifdef SIM
  static uintptr_t lo = 0, hi = 0;
  if (!hi) {
    // The simulator's own image (found through this function): image 0 is another one when a library is inserted
    // (DYLD_INSERT_LIBRARIES, the widget harness's frozen clock), and then no literal was translated.
    Dl_info di{};
    dladdr((const void*)&inImage, &di);
    unsigned long size = 0;
    const uint8_t* d = getsegmentdata((const struct mach_header_64*)di.dli_fbase, "__TEXT", &size);
    lo = (uintptr_t)d, hi = lo + size;
  }
  return (uintptr_t)p >= lo && (uintptr_t)p < hi;
#else
  return (const char*)p >= _rodata_start && (const char*)p < _rodata_end;
#endif
}

const char* tr(const char* en) {
  if (!en) return en;
  if (cur) {
    if (const char* t = lookup(cur, en)) return t;
  }
  return plain(en);
}

const char* trl(const char* s) { return s && *s && inImage(s) ? tr(s) : s; }

bool installed(const char* tag) {
  const Lang* l = find(tag);
  if (!l) return false;
  if (!strcmp(tag, "en")) return true;
  const pkg::Package* p = pkg::find(packId(tag).c_str());
  return p && p->available && fontReady(l);
}

bool use(const char* tag) {
  const Lang* l = find(tag);
  if (!l) return false;
  Pack* p = nullptr;
  if (strcmp(tag, "en")) {
    if (!installed(tag) || !(p = packFor(tag))) return false;
  }
  if (!fonts::useLang(l->font)) {
    trace::add("i18n: %s: font package %s unreadable", tag, l->font ? l->font : "");
    if (l->font) return false;
  }
  cur = p;
  shownTag = l->tag;
  trace::add("i18n: %s", l->tag);
  pkg::relabel();                                      // apps' own names in this language
  if (pkg::store::loaded()) pkg::store::refresh(true);  // and the store's
  return true;
}

const char* lang() { return shownTag.c_str(); }

const char* wanted() {
  static String w;
  w = desk::setting("sys.lang", "en");
  if (!find(w.c_str())) w = "en";
  return w.c_str();
}

void begin() {
  pkg::begin();
  const String w = wanted();
  if (w != "en" && !use(w.c_str())) trace::add("i18n: %s not ready at boot", w.c_str());
}

bool switchReady() { return readyFlag; }
void switchDone() { readyFlag = false; }

namespace {
// The catalog entries a language needs that are not installed yet (font first).
std::vector<const pkg::store::Entry*> missing(const char* tag) {
  std::vector<const pkg::store::Entry*> out;
  const Lang* l = find(tag);
  if (!l || !strcmp(tag, "en")) return out;
  const String id = packId(tag);
  for (const char* x : {l->font, id.c_str()}) {
    if (!x || pkg::find(x)) continue;
    out.push_back(pkg::store::entry(x));  // nullptr: not in the catalog (yet)
  }
  return out;
}
}  // namespace

bool download(const char* tag, String& why) {
  namespace st = pkg::store;
  if (!find(tag)) return why = T_("Unknown language"), false;
  if (!st::loaded()) {
    st::refresh(true);
    dlTag = tag;
    return true;  // tick() goes on once the catalog is in
  }
  for (const st::Entry* e : missing(tag)) {
    if (!e) return why = T_("This language is not in the store yet"), false;
    if (st::stateOf(e->id.c_str()) != st::IDLE) continue;
    // Small packages stay on the device (LittleFS); a big font goes where it fits.
    pkg::Vol v = pkg::FLASH;
    const uint64_t bytes = st::partsBytes(*e, st::defaultParts(*e));
    if (!st::room(bytes, pkg::FLASH, st::onLfs(e->kind, e->engine, bytes)).ok && pkg::volume(pkg::SD).present)
      v = pkg::SD;
    if (!st::install(*e, st::defaultParts(*e), v, why)) return false;
  }
  dlTag = tag;
  return true;
}

int progress(const char* tag) {
  namespace st = pkg::store;
  if (dlTag != tag) return -1;
  if (!st::loaded()) return 0;
  uint64_t total = 0, done = 0;
  const Lang* l = find(tag);
  const String id = packId(tag);
  const st::Status s = st::status();
  for (const char* x : {l ? l->font : nullptr, id.c_str()}) {
    if (!x) continue;
    const st::Entry* e = st::entry(x);
    const uint64_t b = e ? st::partsBytes(*e, st::defaultParts(*e)) : 0;
    total += b;
    if (pkg::find(x)) done += b;
    else if (s.id == x && (s.state == st::DOWNLOAD || s.state == st::WRITE)) done += b * s.pct / 100;
  }
  return total ? (int)(done * 100 / total) : 0;
}

namespace {
// Vietnamese text was drawn without the Vietnamese font package (a dog's line, news, an event, whatever the language):
// fetch the package once a boot, quietly (170 KB, Máy's LittleFS; a "pack": never listed, removable in Storage), then
// redraw. Its letters are read size by size when drawn (fonts.cpp).
void fetchVietnamese() {
  namespace st = pkg::store;
  static bool asked = false, wanted = false, fetched = false;
  static const char* VI_FONT = "dev.espos.font.vi";
  if (fetched && pkg::find(VI_FONT)) {
    fetched = false;
    lv_obj_invalidate(lv_screen_active());  // the letters drawn without marks, again with them
    lv_obj_invalidate(lv_layer_top());
  }
  wanted |= fonts::missedVietnamese();
  if (asked || !wanted || pkg::find(VI_FONT)) return;
  if (!st::loaded()) {
    st::refresh();
    return;
  }
  asked = true;
  const st::Entry* e = st::entry(VI_FONT);
  if (!e || st::stateOf(VI_FONT) != st::IDLE) return;
  const uint64_t bytes = st::partsBytes(*e, st::defaultParts(*e));
  pkg::Vol v = pkg::FLASH;
  if (!st::room(bytes, pkg::FLASH, st::onLfs(e->kind, e->engine, bytes)).ok && pkg::volume(pkg::SD).present) v = pkg::SD;
  String why;
  fetched = st::install(*e, st::defaultParts(*e), v, why);
  trace::add("i18n: Vietnamese letters without their font: %s", fetched ? "fetching it" : why.c_str());
}
}  // namespace

void tick() {
  namespace st = pkg::store;
  static uint32_t last = 0;
  if (millis() - last < 400) return;
  last = millis();
  fetchVietnamese();
  // A download asked for: start the jobs once the catalog is in; stop when they are done or one failed.
  if (dlTag.length()) {
    if (!st::loaded()) {
      if (st::failed()) dlTag = "";
    } else if (installed(dlTag.c_str())) {
      dlTag = "";
    } else {
      bool queued = false;
      for (const st::Entry* e : missing(dlTag.c_str())) queued |= e && st::stateOf(e->id.c_str()) != st::IDLE;
      if (!queued) {
        String why;
        if (dlTries++ > 0 || !download(dlTag.c_str(), why)) dlTag = "", dlFailAt = millis();  // failed: shown by the page
      }
    }
  }
  // sys.lang from the web (or set on this device): show it once its packages are in.
  const String w = wanted();
  if (w == shownTag) return;
  if (installed(w.c_str())) {
    if (use(w.c_str())) readyFlag = true;
    return;
  }
  if (!dlTag.length() && (autoTried != w || millis() - dlFailAt > 30 * 60000)) {
    autoTried = w;
    dlTries = 0;
    String why;
    if (!download(w.c_str(), why)) trace::add("i18n: %s: %s", w.c_str(), why.c_str());
  }
}

// ---------------------------------------------------------------- lists, formats, dates, numbers
const char* List::operator[](int i) const {
  if (const char* p = cur ? lookup(cur, key) : nullptr) {  // a pack's list may be longer or shorter (@quotes)
    for (int k = 0; k < i && *p; k++) p += strlen(p) + 1;
    if (i >= 0 && *p) return p;
  }
  return en[i >= 0 && i < n ? i : 0];
}

int List::size() const {
  const char* p = cur ? lookup(cur, key) : nullptr;
  if (!p) return n;
  int k = 0;
  for (; *p; p += strlen(p) + 1) k++;
  return k;
}

namespace {
const char* const WD_EN[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const WD3_EN[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const MON_EN[12] = {"January", "February", "March",     "April",   "May",      "June",
                                 "July",    "August",   "September", "October", "November", "December"};
const char* const MON3_EN[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void replaceAll(String& s, const char* what, const String& with) {
  if (s.indexOf(what) >= 0) s.replace(what, with);
}
}  // namespace

const List WEEKDAY = {"@weekdays", WD_EN, 7};
const List WD_SHORT = {"@weekdays_short", WD3_EN, 7};
const List MONTH = {"@months", MON_EN, 12};
const List MON_SHORT = {"@months_short", MON3_EN, 12};

String fmt(const char* pattern, const String& a, const String& b, const String& c) {
  String s = trl(pattern);
  replaceAll(s, "{0}", a);
  replaceAll(s, "{1}", b);
  replaceAll(s, "{2}", c);
  return s;
}

String plural(int n, const char* forms) {
  const char* f = trl(forms);
  const char rule = cur ? cur->plural : 'e';
  int k = 0;
  if (rule == 'e') k = n == 1 ? 0 : 1;
  else if (rule == 'f') k = n == 0 || n == 1 ? 0 : 1;
  else if (rule == 'r') {
    const int a = abs(n) % 10, b = abs(n) % 100;
    k = a == 1 && b != 11 ? 0 : a >= 2 && a <= 4 && (b < 12 || b > 14) ? 1 : 2;
  }
  for (int i = 0; i < k; i++) {
    const char* bar = strchr(f, '|');
    if (!bar) break;
    f = bar + 1;
  }
  const char* end = strchr(f, '|');
  String s;
  s.concat(f, end ? end - f : strlen(f));
  replaceAll(s, "{n}", String(n));
  return s;
}

String date(const char* pattern, int d, int m, int y, int wd) {
  String s = trl(pattern);
  // Longest names first ({MM} before {M}, {W} before {w}).
  if (m >= 1 && m <= 12) replaceAll(s, "{MM}", MONTH[m - 1]), replaceAll(s, "{M}", MON_SHORT[m - 1]);
  if (wd >= 0) replaceAll(s, "{W}", WEEKDAY[wd % 7]), replaceAll(s, "{w}", WD_SHORT[wd % 7]);
  replaceAll(s, "{d}", String(d));
  replaceAll(s, "{m}", String(m));
  replaceAll(s, "{y}", String(y));
  return s;
}

const char* groupSep() { return cur ? cur->grp : ","; }
const char* decSep() { return cur ? cur->dec : "."; }

String upper(const String& in) {
  String out;
  const char* p = in.c_str();
  while (*p) {
    uint32_t cp;
    const uint8_t c = *p;
    const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    cp = n == 1 ? c : n == 2 ? c & 0x1f : n == 3 ? c & 0x0f : c & 0x07;
    for (int i = 1; i < n && p[i]; i++) cp = (cp << 6) | (p[i] & 0x3f);
    p += n;
    if (cp >= 'a' && cp <= 'z') cp -= 32;
    else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 32;
    else if (((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177)) && (cp & 1))
      cp -= 1;  // Latin Extended-A pairs (ă đ ĩ ũ œ...)
    else if (cp == 0x1A1 || cp == 0x1B0) cp -= 1;                 // ơ ư
    else if (cp >= 0x1EA0 && cp <= 0x1EF9 && (cp & 1)) cp -= 1;   // Vietnamese
    else if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;              // Cyrillic а..я
    else if (cp >= 0x450 && cp <= 0x45F) cp -= 0x50;              // ѐ..џ
    char buf[4];
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) buf[0] = 0xC0 | (cp >> 6), buf[1] = 0x80 | (cp & 0x3f), out.concat(buf, 2);
    else if (cp < 0x10000)
      buf[0] = 0xE0 | (cp >> 12), buf[1] = 0x80 | ((cp >> 6) & 0x3f), buf[2] = 0x80 | (cp & 0x3f), out.concat(buf, 3);
    else
      buf[0] = 0xF0 | (cp >> 18), buf[1] = 0x80 | ((cp >> 12) & 0x3f), buf[2] = 0x80 | ((cp >> 6) & 0x3f),
      buf[3] = 0x80 | (cp & 0x3f), out.concat(buf, 4);
  }
  return out;
}

}  // namespace i18n
