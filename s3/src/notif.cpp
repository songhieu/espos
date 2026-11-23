#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Notifications for every app (notif.h): the history, banners, the Notification Center, scheduled ones,
// icon badges and the settings page. Drawn after mockups/s3-notif.html.
// Built for size: nothing here is hot, and the flash is nearly full.
#ifndef SIM
#pragma GCC optimize("Os")
#endif
#include "notif.h"
#include "led.h"

#include "i18n.h"

#include <ArduinoJson.h>
#include <lvgl.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <memory>
#include <vector>

#include "apps.h"
#include "art.h"
#include "desk.h"
#include "fonts.h"
#include "os.h"
#include "pkg/pkg.h"
#include "popnote.h"
#include "sound.h"
#include "trace.h"
#include "uikit.h"

#ifdef SIM
#include <sys/stat.h>

#include <fstream>
#include <sstream>
#include <string>
extern std::string simDir;
#else
#include <LittleFS.h>
#endif

namespace notif {
namespace {

using namespace kit;

constexpr size_t MAX_ITEMS = 50, MAX_SCHED = 32, MAX_QUEUE = 6, MAX_KNOWN = 40, MAX_DESK = 40;
constexpr uint32_t SHOW_MS = 5000, SHOW_QUEUED_MS = 3000;
constexpr const char* DESK = "desk";  // the pseudo app of the server's notes (dash "notes")

// ---------------------------------------------------------------- texts, kept in PSRAM
// Internal RAM is the scarce one on the board: each notification's strings live in one PSRAM block.
struct PsFree {
  void operator()(char* p) const { heap_caps_free(p); }
};

// At most n bytes, never inside a UTF-8 letter.
String cut(const char* s, size_t n) {
  if (!s) return "";
  const size_t len = strlen(s);
  if (len <= n) return s;
  while (n && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
  String out;
  out.concat(s, n);
  return out;
}

class Texts {
 public:
  enum { APP, TITLE, BODY, ARG, TAG, N };
  Texts() = default;
  Texts(const char* app, const char* title, const char* body, const char* arg, const char* tag) {
    const String s[N] = {cut(app, 60), cut(title, 120), cut(body, 400), cut(arg, 120), cut(tag, 40)};
    size_t total = 0;
    for (const String& x : s) total += x.length() + 1;
    char* p = (char*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!p) return;
    size_t o = 0;
    for (int i = 0; i < N; i++) {
      off_[i] = o;
      memcpy(p + o, s[i].c_str(), s[i].length() + 1);
      o += s[i].length() + 1;
    }
    p_.reset(p);
  }
  const char* operator[](int i) const { return p_ ? p_.get() + off_[i] : ""; }

 private:
  std::unique_ptr<char, PsFree> p_;
  uint16_t off_[N] = {};
};

struct Item {
  uint32_t id = 0;
  Texts t;
  int64_t at = 0;  // ms since 1970 (0: the clock was not known)
  bool read = false, badge = true;
  const char* app() const { return t[Texts::APP]; }
};

struct Pending {
  Texts t;
  time_t at = 0;
  bool sound = true, badge = true;
};

struct Spiram : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} spiram;

std::vector<Item> items;  // newest first
std::vector<Pending> pending;
std::map<String, int> ownBadge;  // setBadge counts
std::vector<String> known;       // apps that posted at least once (the settings page lists them)
std::vector<uint32_t> deskSeen;  // server notes already turned into notifications
uint32_t nextId = 1, ver = 1, dirtyAt = 0, seenDash = 0;
bool loaded = false, dirty = false, firstRun = false;

// ---------------------------------------------------------------- the file
#ifdef SIM
std::string filePath() { return simDir + "/fs/sys/notif.json"; }
bool readDoc(JsonDocument& d) {
  std::ifstream f(filePath(), std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string s = ss.str();
  return deserializeJson(d, s.data(), s.size()) == DeserializationError::Ok;
}
bool writeDoc(const JsonDocument& d) {
  mkdir((simDir + "/fs").c_str(), 0755);
  mkdir((simDir + "/fs/sys").c_str(), 0755);
  std::string s;
  serializeJson(d, s);
  std::ofstream f(filePath(), std::ios::binary);
  f << s;
  return (bool)f;
}
#else
constexpr const char* FILE_PATH = "/sys/notif.json";
bool readDoc(JsonDocument& d) {
  if (!LittleFS.exists(FILE_PATH)) return false;
  fs::File f = LittleFS.open(FILE_PATH, "r");
  if (!f) return false;
  const size_t n = f.size();
  char* buf = (char*)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
  bool ok = false;
  if (buf && f.read((uint8_t*)buf, n) == n) ok = deserializeJson(d, (const char*)buf, n) == DeserializationError::Ok;
  heap_caps_free(buf);
  f.close();
  return ok;
}
bool writeDoc(const JsonDocument& d) {
  const size_t n = measureJson(d);
  char* buf = (char*)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
  if (!buf) return false;
  serializeJson(d, buf, n + 1);
  if (!LittleFS.exists("/sys")) LittleFS.mkdir("/sys");
  fs::File f = LittleFS.open(FILE_PATH, "w");
  const bool ok = f && f.write((const uint8_t*)buf, n) == n;
  if (f) f.close();
  heap_caps_free(buf);
  return ok;
}
#endif

// One copy of the JSON code for both lists (inlined per field it cost kilobytes of flash).
const char* const KEYS[Texts::N] = {"a", "t", "b", "g", "k"};
__attribute__((noinline)) Texts textsOf(JsonObjectConst o) {
  const char* v[Texts::N];
  for (int i = 0; i < Texts::N; i++) v[i] = o[KEYS[i]] | "";
  return Texts(v[0], v[1], v[2], v[3], v[4]);
}
__attribute__((noinline)) void putTexts(JsonObject o, const Texts& t) {
  for (int i = 0; i < Texts::N; i++)
    if (i <= Texts::TITLE || *t[i]) o[KEYS[i]] = t[i];
}
__attribute__((noinline)) uint32_t num(JsonObjectConst o, const char* k) { return o[k] | 0u; }
__attribute__((noinline)) void putNum(JsonObject o, const char* k, uint32_t v) { o[k] = v; }

void load() {
  if (loaded) return;
  loaded = true;
  JsonDocument d(&spiram);
  if (!readDoc(d)) {
    firstRun = true;
    trace::add("notif: no history yet");
    return;
  }
  nextId = d["next"] | 1u;
  for (JsonObjectConst o : d["n"].as<JsonArrayConst>()) {
    if (items.size() >= MAX_ITEMS) break;
    Item it;
    it.id = num(o, "i");
    if (!it.id) it.id = nextId++;
    it.t = textsOf(o);
    it.at = (int64_t)num(o, "at") * 1000;
    it.read = num(o, "r") != 0;
    it.badge = num(o, "nb") == 0;
    nextId = max(nextId, it.id + 1);
    if (*it.app()) items.push_back(std::move(it));
  }
  for (JsonObjectConst o : d["s"].as<JsonArrayConst>()) {
    if (pending.size() >= MAX_SCHED) break;
    Pending p;
    p.t = textsOf(o);
    p.at = (time_t)num(o, "at");
    p.sound = num(o, "q") == 0;
    p.badge = num(o, "nb") == 0;
    if (*p.t[Texts::APP]) pending.push_back(std::move(p));
  }
  for (JsonPairConst kv : d["bd"].as<JsonObjectConst>()) ownBadge[kv.key().c_str()] = kv.value() | 0;
  for (JsonVariantConst v : d["ap"].as<JsonArrayConst>()) known.push_back(v | "");
  for (JsonVariantConst v : d["dn"].as<JsonArrayConst>()) deskSeen.push_back(v | 0u);
  trace::add("notif: %u in the Center, %u scheduled", (unsigned)items.size(), (unsigned)pending.size());
}

void save() {
  dirty = false;
  JsonDocument d(&spiram);
  d["v"] = 1;
  d["next"] = nextId;
  JsonArray n = d["n"].to<JsonArray>();
  for (const Item& it : items) {
    JsonObject o = n.add<JsonObject>();
    putNum(o, "i", it.id);
    putTexts(o, it.t);
    putNum(o, "at", (uint32_t)(it.at / 1000));
    if (it.read) putNum(o, "r", 1);
    if (!it.badge) putNum(o, "nb", 1);
  }
  JsonArray s = d["s"].to<JsonArray>();
  for (const Pending& p : pending) {
    JsonObject o = s.add<JsonObject>();
    putTexts(o, p.t);
    putNum(o, "at", (uint32_t)p.at);
    if (!p.sound) putNum(o, "q", 1);
    if (!p.badge) putNum(o, "nb", 1);
  }
  JsonObject bd = d["bd"].to<JsonObject>();
  for (const auto& kv : ownBadge) bd[kv.first] = kv.second;
  JsonArray ap = d["ap"].to<JsonArray>();
  for (const String& k : known) ap.add(k);
  JsonArray dn = d["dn"].to<JsonArray>();
  for (uint32_t h : deskSeen) dn.add(h);
  if (!writeDoc(d)) trace::add("notif: could not write the history");
}

// Saved a few seconds after the last change (`soon`: scheduled ones, about a second).
void touch(bool soon = false) {
  dirty = true;
  dirtyAt = millis() - (soon ? 3000 : 0);
}

// ---------------------------------------------------------------- switches, quiet hours, names
std::vector<String> offApps;  // from s3.notif
String offRaw;
bool switchesRead = false, preview = true;
uint32_t seenSettings = 0;

void readSwitches() {
  if (switchesRead && seenSettings == desk::settingsVersion()) return;
  seenSettings = desk::settingsVersion();
  preview = desk::settingInt("s3.notif_preview", 1) != 0;
  const String raw = desk::setting("s3.notif", "");
  if (switchesRead && raw == offRaw) return;
  switchesRead = true;
  offRaw = raw;
  offApps.clear();
  JsonDocument d(&spiram);
  if (raw.length() && deserializeJson(d, raw) == DeserializationError::Ok)
    for (JsonPairConst kv : d.as<JsonObjectConst>())
      if ((kv.value().is<bool>() && !kv.value().as<bool>()) || (kv.value().is<int>() && !kv.value().as<int>()))
        offApps.push_back(kv.key().c_str());
  ver++;  // badges of apps turned off disappear
}

// A package needs "notify" in its manifest's perms; built-in apps may always post.
bool permitted(const char* app) {
  const pkg::Package* p = pkg::find(app);
  if (!p) return true;
  for (const String& s : p->perms)
    if (s == "notify") return true;
  return false;
}

int minutesOf(const String& hm) { return hm.substring(0, 2).toInt() * 60 + hm.substring(3, 5).toInt(); }

// Do Not Disturb, or the quiet hours of the dashboard settings (as the screen saver and PawBoop use them).
bool quietNow() {
  if (desk::settingInt("s3.dnd", 0)) return true;
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  tm t;
  localtime_r(&now, &t);
  const int m = t.tm_hour * 60 + t.tm_min;
  const int from = minutesOf(desk::setting("dash.quiet_from", "23:00")), to = minutesOf(desk::setting("dash.quiet_to", "06:00"));
  return from == to ? false : from < to ? (m >= from && m < to) : (m >= from || m < to);
}

String appName(const char* app) {
  if (!strcmp(app, DESK)) return "EspOS";
  if (const apps::App* a = apps::find(app)) return i18n::trl(a->name);  // built-in names are translated
  if (const pkg::Package* p = pkg::find(app)) return p->name;
  return app;
}

lv_obj_t* appIcon(lv_obj_t* parent, const char* app, int size) {
  if (strcmp(app, DESK)) return apps::icon(parent, app, size);
  lv_obj_t* sq = box(parent, 0x5aa9ff, size * 27 / 100);
  grad(sq, 0x5aa9ff, 0x1360d6);
  lv_obj_set_size(sq, size, size);
  lv_obj_t* g = glyph(sq, size >= 40 ? &gl_bell_22 : &gl_bell_16, 0xffffff);
  lv_obj_center(g);
  return sq;
}

// "now", "12 min ago", "09:41", "Yesterday", "Nov 14".
String whenText(int64_t at) {
  const int64_t now = desk::nowMs();
  if (!at || !now) return "";
  const int64_t m = (now - at) / 60000;
  if (m < 1) return T_("now");
  if (m < 60) return i18n::plural((int)m, NP_("{n} min ago|{n} min ago"));
  const time_t as = at / 1000, ns = now / 1000;
  tm a, n;
  localtime_r(&as, &a);
  localtime_r(&ns, &n);
  tm mid = n;
  mid.tm_hour = mid.tm_min = mid.tm_sec = 0;
  const time_t midnight = mktime(&mid);
  if (as >= midnight) return hhmm(a.tm_hour, a.tm_min);
  if (as >= midnight - 86400) return T_("Yesterday");
  return i18n::date(N_("{M} {d}"), a.tm_mday, a.tm_mon + 1);
}

const Item* findItem(uint32_t id) {
  for (const Item& it : items)
    if (it.id == id) return &it;
  return nullptr;
}

void remember(const char* app) {
  for (const String& k : known)
    if (k == app) return;
  known.push_back(app);
  if (known.size() > MAX_KNOWN) known.erase(known.begin());
}

// ---------------------------------------------------------------- banner
lv_obj_t *ban, *banMore;
uint32_t banItem = 0, banAt = 0;
int banTop = 0, banH = 0;
bool banOut = false, banHeld = false, banMoved = false;
lv_point_t banStart;
std::vector<uint32_t> queue;  // item ids waiting for the banner, oldest first

enum class CState { CLOSED, SLIDING, OPEN };
CState cstate = CState::CLOSED;
lv_obj_t* ctr;

void openItem(uint32_t id);
void refillSoon();
void closeCenterNow();

void slideBanner(int to, uint32_t ms, bool leave) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ban);
  lv_anim_set_values(&a, lv_obj_get_y(ban), to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_y((lv_obj_t*)o, v); });
  if (leave)
    lv_anim_set_completed_cb(&a, [](lv_anim_t* a) {
      lv_obj_t* o = (lv_obj_t*)a->var;
      if (o == ban) ban = nullptr;
      banOut = false;
      lv_obj_delete_async(o);
    });
  lv_anim_start(&a);
}

void hideBanner(bool animated) {
  if (!ban || banOut) return;
  banItem = 0;
  banHeld = false;
  if (!animated) {
    lv_obj_t* o = ban;
    ban = nullptr;
    lv_anim_delete(o, nullptr);
    lv_obj_delete_async(o);
    return;
  }
  banOut = true;
  lv_anim_delete(ban, nullptr);
  slideBanner(-banH - 16, 180, true);
}

void onBanner(lv_event_t* e) {
  if (!ban || banOut) return;
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) {
    banHeld = true;
    banMoved = false;
    banStart = p;
    lv_anim_delete(ban, nullptr);
  } else if (code == LV_EVENT_PRESSING) {
    const int dy = p.y - banStart.y, dx = p.x - banStart.x;
    if (abs(dy) > 8 || abs(dx) > 8) banMoved = true;
    if (banMoved) lv_obj_set_y(ban, banTop + (dy < 0 ? dy : dy / 4));
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    banHeld = false;
    const int dy = p.y - banStart.y;
    if (!banMoved) {
      lv_obj_set_y(ban, banTop);
      if (code == LV_EVENT_RELEASED) {
        const uint32_t id = banItem;
        hideBanner(false);
        openItem(id);
      }
    } else if (dy < -20) {
      hideBanner(true);
    } else {
      slideBanner(banTop, 150, false);
      banAt = millis();
    }
  }
}

void showBanner(const Item& it, bool animate) {
  const Pal& p = pal();
  const int w = min(os::W() - 16, 440), pad = 12, cw = w - 2 * pad - 2;
  keep(&ban, box(lv_layer_top(), p.dark ? 0x1f2531 : 0xffffff, 16));
  lv_obj_set_size(ban, w, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(ban, pad, 0);
  lv_obj_set_style_pad_top(ban, 10, 0);
  lv_obj_set_style_pad_bottom(ban, 11, 0);
  lv_obj_set_style_border_width(ban, 1, 0);
  lv_obj_set_style_border_color(ban, C(p.dark ? 0x2a3240 : 0xe4e6ec), 0);
  lv_obj_set_style_shadow_width(ban, 20, 0);
  lv_obj_set_style_shadow_offset_y(ban, 6, 0);
  lv_obj_set_style_shadow_color(ban, C(0x000000), 0);
  lv_obj_set_style_shadow_opa(ban, p.dark ? LV_OPA_60 : LV_OPA_20, 0);
  lv_obj_add_flag(ban, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE));
  const char* app = it.app();
  lv_obj_t* ic = appIcon(ban, app, 28);
  lv_obj_set_pos(ic, 0, 0);
  const String when = whenText(it.at);
  const char* whenS = when.length() ? when.c_str() : T_("now");
  lv_obj_t* tl = label(ban, whenS, 13, false, p.faint);
  const int tw = textW(whenS, 13);
  lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, 0, (28 - lineH(13, false)) / 2);
  lv_obj_t* nm = label(ban, "", 13, false, p.dim);
  fitText(nm, appName(app).c_str(), cw - 36 - tw - 8, 28, 13, 12, false, 0);
  lv_obj_set_pos(nm, 36, (28 - lineH(13, false)) / 2);
  int y = 34;
  const char* title = it.t[Texts::TITLE];
  const char* body = it.t[Texts::BODY];
  if (!*title) title = body, body = "";
  lv_obj_t* t = label(ban, "", 15, true, p.fg);
  const int n = wrapText(t, preview ? title : T_("New notification"), cw, 15, true, 1, 2);
  lv_obj_set_pos(t, 0, y);
  y += n * lv_obj_get_style_text_font(t, 0)->line_height + (n - 1);
  if (preview && *body) {
    lv_obj_t* b = label(ban, "", 14, false, p.fg);
    wrapText(b, body, cw, 14, false, 1, 3);
    lv_obj_set_pos(b, 0, y + 1);
  }
  // A thin edge under the card while more banners wait.
  keep(&banMore, box(ban, p.dark ? 0x3a4250 : 0xc9ccd4, 3));
  lv_obj_set_size(banMore, w - 48, 6);
  lv_obj_add_flag(banMore, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(ban);
  banH = lv_obj_get_height(ban);
  lv_obj_set_height(ban, banH);  // the edge below must not count in its size
  lv_obj_set_pos(banMore, 24 - pad - 1, banH - 11);
  banTop = os::SB_H + 4;
  lv_obj_set_x(ban, (os::W() - w) / 2);
  lv_obj_set_y(ban, animate ? -banH - 16 : banTop);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(ban, onBanner, c, nullptr);
  lv_obj_move_foreground(ban);
  banItem = it.id;
  banAt = millis();
  banOut = banHeld = false;
  if (animate) slideBanner(banTop, 220, false);
}

// ---------------------------------------------------------------- removing
void dropQueued(const char* app) {
  for (size_t i = 0; i < queue.size();) {
    const Item* it = findItem(queue[i]);
    if (!it || !strcmp(it->app(), app)) queue.erase(queue.begin() + i);
    else i++;
  }
}

// One function for every kind of removal (a template per caller cost flash): match(item, a, b).
using Match = bool (*)(const Item&, const char*, const char*);
int removeItems(Match match, const char* a = nullptr, const char* b = nullptr) {
  int n = 0;
  for (size_t i = 0; i < items.size();) {
    if (match(items[i], a, b)) {
      if (items[i].id == banItem) hideBanner(true);
      items.erase(items.begin() + i);
      n++;
    } else {
      i++;
    }
  }
  if (n) {
    ver++;
    touch();
    refillSoon();
  }
  return n;
}

bool same(const char* a, const char* b) { return a && b && !strcmp(a, b); }

void removeId(uint32_t id) {
  removeItems([](const Item& it, const char* a, const char*) { return it.id == (uint32_t)(uintptr_t)a; },
              (const char*)(uintptr_t)id);
}
bool ofApp(const Item& it, const char* app, const char*) { return same(it.app(), app); }
bool ofTag(const Item& it, const char* app, const char* tag) { return same(it.app(), app) && same(it.t[Texts::TAG], tag); }

void dropPending(const char* app, const char* tag) {
  const bool any = !tag || !*tag;
  for (size_t i = 0; i < pending.size();) {
    const Pending& p = pending[i];
    if (same(p.t[Texts::APP], app) && (any || same(p.t[Texts::TAG], tag))) {
      pending.erase(pending.begin() + i);
      touch(true);
    } else {
      i++;
    }
  }
}

// ---------------------------------------------------------------- delivering
bool deliver(const char* app, const char* title, const char* body, const char* arg, const char* tag, bool withSound,
             bool withBadge, bool withBanner, int64_t at) {
  bool replacesBanner = false;
  if (tag && *tag) {
    if (ban && !banOut)
      if (const Item* b = findItem(banItem)) replacesBanner = same(b->app(), app) && same(b->t[Texts::TAG], tag);
    if (replacesBanner) hideBanner(false);
    removeItems(ofTag, app, tag);
    dropPending(app, tag);
  }
  Item it;
  it.id = nextId++;
  it.t = Texts(app, title, body, arg, tag);
  it.at = at ? at : desk::nowMs();
  it.badge = withBadge;
  const uint32_t id = it.id;
  auto pos = items.begin();  // newest first by time (a late scheduled one goes under newer ones)
  while (pos != items.end() && pos->at > it.at) ++pos;
  const bool front = pos == items.begin();
  items.insert(pos, std::move(it));
  while (items.size() > MAX_ITEMS) items.pop_back();
  remember(app);
  ver++;
  touch();
  const bool quiet = quietNow();
  if (withBanner && !quiet && cstate == CState::CLOSED) {
    if (replacesBanner && front) {
      showBanner(items.front(), false);
    } else {
      queue.push_back(id);
      if (queue.size() > MAX_QUEUE) queue.erase(queue.begin());
    }
  }
  if (withSound && !quiet) sound::chime();
  refillSoon();
  trace::add("notif %s: %s%s", app, title, quiet ? " (quiet)" : withBanner ? "" : " (Center only)");
  return true;
}

void fireDue() {
  if (pending.empty() || !desk::nowMs()) return;  // the clock is not set yet
  const time_t now = time(nullptr);
  for (size_t i = 0; i < pending.size();) {
    if (pending[i].at > now) {
      i++;
      continue;
    }
    Pending p = std::move(pending[i]);
    pending.erase(pending.begin() + i);
    touch(true);
    const char* app = p.t[Texts::APP];
    const bool late = now - p.at > 15 * 60;  // the device was off: into the Center quietly
    if (enabled(app) && permitted(app))
      deliver(app, p.t[Texts::TITLE], p.t[Texts::BODY], p.t[Texts::ARG], p.t[Texts::TAG], p.sound && !late, p.badge,
              !late, (int64_t)p.at * 1000);
    i = 0;  // deliver() may have changed the list
  }
}

uint32_t fnv(const char* s, uint32_t h = 2166136261u) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

// The server's notes of the dashboard become notifications of "desk" once each.
void importDesk() {
  if (seenDash == desk::dashVersion()) return;
  seenDash = desk::dashVersion();
  struct New {
    int64_t at;
    String title;
    uint32_t h;
  };
  std::vector<New> fresh;
  const int64_t now = desk::nowMs();
  popnote::sync(desk::dash()["notes"].as<JsonArrayConst>());  // notes for an app: its own notifications and pop-ups
  for (JsonObjectConst n : desk::dash()["notes"].as<JsonArrayConst>()) {
    const char* title = n["title"] | "";
    const char* hm = n["time"] | "";
    if (!*title || n["app"].is<const char*>()) continue;
    const uint32_t h = fnv(hm, fnv(title));
    if (std::find(deskSeen.begin(), deskSeen.end(), h) != deskSeen.end()) continue;
    int64_t at = 0;
    if (now && strlen(hm) >= 4) {
      const time_t ns = now / 1000;
      tm t;
      localtime_r(&ns, &t);
      t.tm_hour = atoi(hm);
      t.tm_min = atoi(strchr(hm, ':') ? strchr(hm, ':') + 1 : "0");
      t.tm_sec = 0;
      at = (int64_t)mktime(&t) * 1000;
      if (at > now + 60000) at -= 86400000LL;  // later than now: it was yesterday
    }
    fresh.push_back({at, title, h});
  }
  for (size_t i = 1; i < fresh.size(); i++)  // oldest first (a few at most; no std::sort, flash is short)
    for (size_t j = i; j > 0 && fresh[j - 1].at > fresh[j].at; j--) std::swap(fresh[j - 1], fresh[j]);
  const bool off = !enabled(DESK);
  for (const New& n : fresh) {
    deskSeen.push_back(n.h);
    const bool recent = n.at && now - n.at < 30 * 60000;
    if (!off) deliver(DESK, n.title.c_str(), "", nullptr, nullptr, !firstRun && recent, false, !firstRun && recent, n.at);
  }
  while (deskSeen.size() > MAX_DESK) deskSeen.erase(deskSeen.begin());
  if (!fresh.empty()) touch();
  firstRun = false;
}

// ---------------------------------------------------------------- Notification Center
lv_obj_t *ctrList, *ctrClock, *ctrClearAll, *ctrShot;
uint16_t* shotPx;
int shotW = 0, shotH = 0;
lv_image_dsc_t shotDsc;
bool usingShot = false, pulling = false, refillQueued = false, animOpen = false;
int ctrY = 0;
int lastMinute = -1;
std::vector<String> expanded;  // groups showing all their notifications

struct Swipe {
  lv_obj_t* card = nullptr;
  lv_point_t start;
  bool hor = false, moved = false;
} sw;

void fillList();

void refillSoon() {
  if (refillQueued || !ctr) return;
  refillQueued = true;
  lv_async_call([](void*) {
    refillQueued = false;
    if (ctr && cstate == CState::OPEN && !sw.hor) fillList();
  }, nullptr);
}

lv_obj_t* closeMark(lv_obj_t* parent, int size, uint32_t bg, uint32_t fg) {
  lv_obj_t* x = box(parent, bg, size / 2);
  lv_obj_set_size(x, size, size);
  if (bg == 0) lv_obj_set_style_bg_opa(x, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(x, 8);
  lv_obj_set_style_opa(x, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_t* l = lv_label_create(x);
  lv_label_set_text(l, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, C(fg), 0);
  lv_obj_center(l);
  return x;
}

void flyOut(lv_obj_t* card) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_values(&a, lv_obj_get_style_translate_x(card, 0), -os::W());
  lv_anim_set_duration(&a, 160);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); });
  lv_anim_set_completed_cb(&a, [](lv_anim_t* a) { removeId((uint32_t)(uintptr_t)lv_obj_get_user_data((lv_obj_t*)a->var)); });
  lv_anim_start(&a);
}

void snapBack(lv_obj_t* card) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_values(&a, lv_obj_get_style_translate_x(card, 0), 0);
  lv_anim_set_duration(&a, 150);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); });
  lv_anim_start(&a);
}

void onCard(lv_event_t* e) {
  lv_obj_t* c = (lv_obj_t*)lv_event_get_current_target(e);
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) {
    sw.card = c;
    sw.start = p;
    sw.hor = sw.moved = false;
  } else if (code == LV_EVENT_PRESSING && sw.card == c) {
    const int dx = p.x - sw.start.x, dy = p.y - sw.start.y;
    if (abs(dx) > 8 || abs(dy) > 8) sw.moved = true;
    if (!sw.hor && dx < -12 && abs(dx) > 2 * abs(dy) && !lv_indev_get_scroll_obj(lv_indev_active())) {
      sw.hor = true;
      if (ctrList) lv_obj_remove_flag(ctrList, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (sw.hor) lv_obj_set_style_translate_x(c, min(0, dx), 0);
  } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && sw.card == c) {
    if (sw.hor) {
      if (ctrList) lv_obj_add_flag(ctrList, LV_OBJ_FLAG_SCROLLABLE);
      if (p.x - sw.start.x < -os::W() / 3) flyOut(c);
      else snapBack(c);
      sw.hor = false;
      sw.moved = true;  // no click after a swipe
    }
  } else if (code == LV_EVENT_CLICKED) {
    if (sw.moved) return;
    openItem((uint32_t)(uintptr_t)lv_obj_get_user_data(c));
  }
}

void addCard(const Item& it, int cw) {
  const Pal& p = pal();
  lv_obj_t* c = box(ctrList, p.cell, 14);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(c, 12, 0);
  lv_obj_set_style_pad_top(c, 9, 0);
  lv_obj_set_style_pad_bottom(c, 10, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(c, C(p.press), LV_STATE_PRESSED);
  lv_obj_set_user_data(c, (void*)(uintptr_t)it.id);
  for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST, LV_EVENT_CLICKED})
    lv_obj_add_event_cb(c, onCard, code, nullptr);
  const int inner = cw - 24;
  lv_obj_t* x = closeMark(c, 22, 0, p.faint);
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 6, -2);
  lv_obj_add_event_cb(x, [](lv_event_t* e) {
    removeId((uint32_t)(uintptr_t)lv_obj_get_user_data(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e))));
  }, LV_EVENT_CLICKED, nullptr);
  const String when = whenText(it.at);
  const int tw = when.length() ? textW(when.c_str(), 12) : 0;
  if (tw) {
    lv_obj_t* tl = label(c, when.c_str(), 12, false, p.faint);
    lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, -20, 2);
  }
  const char* title = it.t[Texts::TITLE];
  const char* body = it.t[Texts::BODY];
  if (!*title) title = body, body = "";
  lv_obj_t* t = label(c, "", 15, true, p.fg);
  const int n = wrapText(t, title, inner - tw - 28, 15, true, 1, 0);
  lv_obj_set_pos(t, 0, 0);
  if (*body) {
    const int th = n * fonts::text(15, true)->line_height + (n - 1);
    lv_obj_t* b = label(c, "", 14, false, p.dark ? 0xc9d0d8 : 0x3a3f4a);
    wrapText(b, body, inner, 14, false, 1, 0);
    lv_obj_set_pos(b, 0, th + 2);
  }
}

void fillList() {
  if (!ctrList) return;
  const Pal& p = pal();
  const int32_t sy = lv_obj_get_scroll_y(ctrList);
  lv_obj_clean(ctrList);
  sw.card = nullptr;
  const int cw = os::W() - 24;
  std::vector<String> order;  // apps by their newest notification
  for (const Item& it : items)
    if (std::find(order.begin(), order.end(), String(it.app())) == order.end()) order.push_back(it.app());
  for (const String& app : order) {
    lv_obj_t* h = kit::clear(ctrList);
    lv_obj_set_size(h, lv_pct(100), 34);
    lv_obj_t* ic = appIcon(h, app.c_str(), 28);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_t* nm = label(h, "", 14, true, p.fg);
    fitText(nm, appName(app.c_str()).c_str(), cw - 36 - 40, 32, 14, 12, true, 0);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 38, 0);
    lv_obj_t* x = closeMark(h, 26, p.chip, p.dim);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, -2, 0);
    String* key = new String(app);
    own<String>(x, key);
    lv_obj_add_event_cb(x, [](lv_event_t* e) {
      const String app = *(String*)lv_event_get_user_data(e);
      removeItems(ofApp, app.c_str());
    }, LV_EVENT_CLICKED, key);
    const bool all = std::find(expanded.begin(), expanded.end(), app) != expanded.end();
    int shown = 0, total = 0;
    for (const Item& it : items) {
      if (app != it.app()) continue;
      total++;
      if (!all && shown >= 3) continue;
      addCard(it, cw);
      shown++;
    }
    if (total > shown) {
      lv_obj_t* m = kit::clear(ctrList);
      lv_obj_set_size(m, lv_pct(100), 28);
      lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(m, LV_OPA_50, LV_STATE_PRESSED);
      lv_obj_t* l = label(m, i18n::plural(total - shown, NP_("{n} more notification|{n} more notifications")).c_str(), 13, true,
                          p.dark ? 0x5aa9ff : p.link);
      lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
      String* k2 = new String(app);
      own<String>(m, k2);
      lv_obj_add_event_cb(m, [](lv_event_t* e) {
        expanded.push_back(*(String*)lv_event_get_user_data(e));
        refillSoon();
      }, LV_EVENT_CLICKED, k2);
    }
  }
  if (items.empty()) {
    lv_obj_t* em = kit::clear(ctrList);
    lv_obj_set_size(em, lv_pct(100), max<int32_t>(120, lv_obj_get_height(ctrList) - 60));
    lv_obj_set_flex_flow(em, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(em, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(em, 10, 0);
    glyph(em, &gl_bell_22, p.faint);
    label(em, N_("No notifications"), 16, false, p.faint);
  }
  if (ctrClearAll) items.empty() ? lv_obj_add_flag(ctrClearAll, LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(ctrClearAll, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(ctrList);
  lv_obj_scroll_to_y(ctrList, sy, LV_ANIM_OFF);
}

void setY(int y) {
  ctrY = y;
  if (usingShot && cstate == CState::SLIDING) {
    if (ctrShot) lv_obj_set_y(ctrShot, y);
  } else if (ctr) {
    lv_obj_set_y(ctr, y);
  }
}

void freeShot() {
  if (ctrShot) {
    lv_obj_add_flag(ctrShot, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(ctrShot, nullptr);
  }
  lv_image_cache_drop(&shotDsc);  // the next picture lives at another address
  heap_caps_free(shotPx);
  shotPx = nullptr;
  shotW = shotH = 0;
}

// While it slides, the Center is a picture of itself (as the Control Center): one copy a frame.
bool snapCenter() {
  if (!ctr) return false;
  const int w = os::W(), h = os::H();
  if (!shotPx || shotW != w || shotH != h) {
    heap_caps_free(shotPx);
    shotPx = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
    shotW = w, shotH = h;
  }
  if (!shotPx) return false;
  if (!ctrShot) {
    keep(&ctrShot, lv_image_create(lv_layer_top()));
    lv_obj_add_flag(ctrShot, LV_OBJ_FLAG_HIDDEN);
  }
  lv_draw_buf_t buf;
  lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, w * 2, shotPx, w * h * 2);
  const bool hidden = lv_obj_has_flag(ctr, LV_OBJ_FLAG_HIDDEN);
  if (hidden) lv_obj_remove_flag(ctr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(ctr);
  const bool ok = lv_snapshot_take_to_draw_buf(ctr, LV_COLOR_FORMAT_RGB565, &buf) == LV_RESULT_OK;
  if (hidden) lv_obj_add_flag(ctr, LV_OBJ_FLAG_HIDDEN);
  if (!ok) return false;
  shotDsc = {};
  shotDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  shotDsc.header.cf = LV_COLOR_FORMAT_RGB565;
  shotDsc.header.w = w;
  shotDsc.header.h = h;
  shotDsc.header.stride = w * 2;
  shotDsc.data_size = w * h * 2;
  shotDsc.data = (const uint8_t*)shotPx;
  lv_image_cache_drop(&shotDsc);
  lv_image_set_src(ctrShot, &shotDsc);
  return true;
}

void beginSlide() {
  usingShot = snapCenter();
  cstate = CState::SLIDING;
  if (usingShot) {
    lv_obj_add_flag(ctr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ctrShot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctrShot);
  } else {
    lv_obj_remove_flag(ctr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctr);
  }
  setY(ctrY);
}

void endSlide(bool open) {
  if (!ctr) {
    cstate = CState::CLOSED;
    freeShot();
    return;
  }
  if (open) {
    cstate = CState::OPEN;
    if (ctrShot) lv_obj_add_flag(ctrShot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(ctr, 0);
    lv_obj_remove_flag(ctr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctr);
    ctrY = 0;
  } else {
    closeCenterNow();
  }
}

void animTo(bool open) {
  lv_anim_delete(&ctrY, nullptr);
  animOpen = open;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &ctrY);
  lv_anim_set_values(&a, ctrY, open ? 0 : -os::H());
  lv_anim_set_duration(&a, 220);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void*, int32_t v) { setY(v); });
  lv_anim_set_completed_cb(&a, [](lv_anim_t*) { endSlide(animOpen); });
  lv_anim_start(&a);
  trace::add("notification center %s", open ? "open" : "close");
}

// The strip at the bottom: tap or pull up to close.
int handleStart = 0;
bool handleMoved = false;
void onHandle(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) {
    handleStart = p.y;
    handleMoved = false;
  } else if (code == LV_EVENT_PRESSING) {
    const int dy = p.y - handleStart;
    if (!handleMoved && dy < -6 && cstate == CState::OPEN) {
      handleMoved = true;
      ctrY = 0;
      beginSlide();
    }
    if (handleMoved) setY(min(0, dy));
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (!handleMoved) {
      if (code == LV_EVENT_RELEASED && cstate == CState::OPEN) closeCenter();
      return;
    }
    handleMoved = false;
    animTo(p.y - handleStart > -os::H() / 6);
  }
}

void clearAll() {
  hideBanner(true);
  queue.clear();
  expanded.clear();
  if (items.empty()) return;
  items.clear();
  ver++;
  touch();
  refillSoon();
  trace::add("notif: Center cleared");
}

void buildCenter() {
  if (ctr) {
    lv_obj_t* old = ctr;
    ctr = nullptr;
    lv_obj_delete_async(old);
  }
  const Pal& p = pal();
  const int w = os::W(), h = os::H();
  keep(&ctr, box(lv_layer_top(), p.bg, 0));
  lv_obj_set_size(ctr, w, h);
  lv_obj_add_flag(ctr, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
  keep(&ctrClock, label(ctr, "", 14, true, p.fg));
  lv_obj_set_pos(ctrClock, 14, 4);
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  setText(ctrClock, hhmm(t.tm_hour, t.tm_min).c_str());
  lastMinute = t.tm_min;
  lv_obj_t* title = label(ctr, N_("Notifications"), 24, true, p.fg);
  lv_obj_set_pos(title, 12, os::SB_H + 6);
  keep(&ctrClearAll, box(ctr, p.chip, 14));
  lv_obj_set_size(ctrClearAll, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(ctrClearAll, 12, 0);
  lv_obj_add_flag(ctrClearAll, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(ctrClearAll, 6);
  lv_obj_set_style_bg_color(ctrClearAll, C(p.press), LV_STATE_PRESSED);
  lv_obj_t* cl = label(ctrClearAll, N_("Clear all"), 13, true, p.chipFg);
  lv_obj_center(cl);
  lv_obj_align(ctrClearAll, LV_ALIGN_TOP_RIGHT, -12, os::SB_H + 9);
  lv_obj_add_event_cb(ctrClearAll, [](lv_event_t*) { clearAll(); }, LV_EVENT_CLICKED, nullptr);
  const int top = os::SB_H + 46, bottom = 34;
  keep(&ctrList, kit::clear(ctr));
  lv_obj_set_pos(ctrList, 0, top);
  lv_obj_set_size(ctrList, w, h - top - bottom);
  lv_obj_set_style_pad_hor(ctrList, 12, 0);
  lv_obj_set_style_pad_bottom(ctrList, 12, 0);
  lv_obj_set_style_pad_row(ctrList, 6, 0);
  lv_obj_set_flex_flow(ctrList, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(ctrList, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(ctrList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(ctrList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t* strip = kit::clear(ctr);
  lv_obj_set_size(strip, w, bottom);
  lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(strip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* bar = box(strip, p.dark ? 0x3a4250 : 0xc3c6ce, 3);
  lv_obj_set_size(bar, 40, 5);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -12);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(strip, onHandle, c, nullptr);
  expanded.clear();
  fillList();
  lv_obj_set_y(ctr, -h);
  ctrY = -h;
}

// The Center is about to show: no banners over it, none waiting.
void prepareOpen() {
  load();
  hideBanner(false);
  queue.clear();
  buildCenter();
  beginSlide();
}

void closeCenterNow() {
  lv_anim_delete(&ctrY, nullptr);
  pulling = handleMoved = false;
  sw = Swipe();
  cstate = CState::CLOSED;
  if (ctr) {
    lv_obj_t* old = ctr;
    ctr = nullptr;
    lv_obj_add_flag(old, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(old);  // often called from one of its own buttons
  }
  freeShot();
}

void openItem(uint32_t id) {
  const Item* it = findItem(id);
  if (!it) return;
  const String app = it->app(), arg = it->t[Texts::ARG];
  if (app == DESK) return;  // a note from the web: nothing to open
  removeId(id);
  closeCenterNow();
  os::wake();
  if (!apps::find(app.c_str())) {
    os::toast(N_("This app is no longer on the device"));
    return;
  }
  trace::add("notif: open %s %s", app.c_str(), arg.c_str());
  apps::open(app.c_str(), arg.length() ? arg.c_str() : nullptr);
}

// ---------------------------------------------------------------- settings page
void setEnabled(const String& id, bool on) {
  JsonDocument d(&spiram);
  const String raw = desk::setting("s3.notif", "");
  if (raw.length()) deserializeJson(d, raw);
  if (!d.is<JsonObject>()) d.to<JsonObject>();
  if (on) d.remove(id);
  else d[id] = false;
  String out;
  serializeJson(d, out);
  desk::setSetting("s3.notif", out);
  readSwitches();
  if (!on) dropQueued(id.c_str());
  trace::add("notif: %s %s", id.c_str(), on ? "on" : "off");
}

int countOf(const String& app) {
  int n = 0;
  for (const Item& it : items) n += app == it.app();
  return n;
}

bool isOff(const String& app) {
  for (const String& s : offApps)
    if (s == app) return true;
  return false;
}

String rowSub(const String& id) {
  if (isOff(id)) return TC_("notifications", "Off");  // i18n: an app's notifications are turned off
  if (id == DESK) return T_("From the EspOS web");
  const int n = countOf(id);
  return n ? i18n::plural(n, NP_("{n} notification|{n} notifications")) : String(TC_("notifications", "On"));
}

// Apps that posted or may post, in the order of the App Library, then the rest.
std::vector<String> candidates() {
  std::vector<String> want;
  auto add = [&](const String& s) {
    if (s.length() && std::find(want.begin(), want.end(), s) == want.end()) want.push_back(s);
  };
  for (const String& k : known) add(k);
  for (const Item& it : items) add(it.app());
  for (const Pending& p : pending) add(p.t[Texts::APP]);
  for (const String& k : offApps) add(k);
  add("clock");
  add("pet");
  for (const pkg::Package& p : pkg::list())
    if (permitted(p.id.c_str())) add(p.id);
  std::vector<String> out;
  for (const apps::App& a : apps::all())
    if (std::find(want.begin(), want.end(), String(a.id)) != want.end()) out.push_back(a.id);
  if (std::find(want.begin(), want.end(), String(DESK)) != want.end()) out.push_back(DESK);
  return out;
}

struct Row {
  String id;
  lv_obj_t* sub;
};

void appRow(lv_obj_t* body, const String& id) {
  const Pal& p = pal();
  lv_obj_t* r = box(body, p.cell, 12);
  lv_obj_set_size(r, lv_pct(100), 54);
  lv_obj_set_style_pad_hor(r, 12, 0);
  lv_obj_t* ic = appIcon(r, id.c_str(), 28);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* nm = label(r, "", 15, false, p.fg);
  fitText(nm, appName(id.c_str()).c_str(), os::W() - 24 - 24 - 38 - 58, 20, 15, 12, false, 0);
  lv_obj_set_pos(nm, 38, 7);
  Row* row = new Row{id, label(r, rowSub(id).c_str(), 12, false, p.faint)};
  lv_obj_set_pos(row->sub, 38, 29);
  lv_obj_t* s = toggle(r, !isOff(id), [](lv_event_t* e) {
    Row* row = (Row*)lv_event_get_user_data(e);
    setEnabled(row->id, lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
    setText(row->sub, rowSub(row->id).c_str());
  }, row);
  own<Row>(s, row);
  lv_obj_align(s, LV_ALIGN_RIGHT_MID, 0, 0);
}

}  // namespace

// ================================================================== public
bool post(const char* app, const char* title, const char* body, const Opts& o) {
  led::receive();  // the LED's soft green (when the person allows it, src/led.h)
  title = i18n::trl(title), body = i18n::trl(body);  // N_ marked text
  load();
  if (!app || !*app) return false;
  if (!enabled(app)) {
    trace::add("notif %s: off, not shown", app);
    return false;
  }
  if (!permitted(app)) {
    trace::add("notif %s: no \"notify\" permission", app);
    return false;
  }
  return deliver(app, title ? title : "", body ? body : "", o.arg, o.tag, o.sound, o.badge, o.banner, 0);
}

bool quiet() { return quietNow(); }

bool schedule(const char* app, time_t at, const char* title, const char* body, const Opts& o) {
  title = i18n::trl(title), body = i18n::trl(body);
  load();
  if (!app || !*app || !enabled(app) || !permitted(app)) return false;
  if (desk::nowMs() && at <= time(nullptr)) return post(app, title, body, o);
  if (o.tag && *o.tag) dropPending(app, o.tag);
  if (pending.size() >= MAX_SCHED) {
    trace::add("notif %s: too many scheduled", app);
    return false;
  }
  Pending p;
  p.t = Texts(app, title, body, o.arg, o.tag);
  p.at = at;
  p.sound = o.sound;
  p.badge = o.badge;
  pending.push_back(std::move(p));
  remember(app);
  touch(true);
  trace::add("notif %s: scheduled in %ld s", app, (long)(at - time(nullptr)));
  return true;
}

void cancel(const char* app, const char* tag) {
  load();
  if (!app) return;
  dropPending(app, tag);
  if (tag && *tag) removeItems(ofTag, app, tag);
}

void clear(const char* app) {
  load();
  if (!app) return;
  dropQueued(app);
  removeItems(ofApp, app);
  if (ownBadge.erase(app)) {
    ver++;
    touch();
  }
}

void seen(const char* app) {
  load();
  if (!app) return;
  bool changed = false;
  for (Item& it : items)
    if (!it.read && same(it.app(), app)) it.read = changed = true;
  if (changed) {
    ver++;
    touch();
  }
}

int badge(const char* app) {
  if (!app || !*app) return 0;
  load();
  if (!enabled(app)) return 0;
  auto o = ownBadge.find(app);
  if (o != ownBadge.end() && o->second >= 0) return o->second;
  int unread = 0;
  for (const Item& it : items) unread += !it.read && it.badge && same(it.app(), app);
  const apps::App* a = apps::find(app);
  const int builtin = a && a->badge ? a->badge() : 0;
  return max(unread, builtin);
}

void setBadge(const char* app, int n) {
  load();
  if (!app || !*app) return;
  auto o = ownBadge.find(app);
  if (n < 0) {
    if (o == ownBadge.end()) return;
    ownBadge.erase(o);
  } else {
    n = min(n, 9999);
    if (o != ownBadge.end() && o->second == n) return;
    ownBadge[app] = n;
  }
  ver++;
  touch();
}

bool enabled(const char* app) {
  if (!app || !*app) return false;
  readSwitches();
  return !isOff(app);
}

void openCenter() {
  if (ctr && cstate == CState::OPEN) return;
  if (!ctr || cstate == CState::CLOSED) prepareOpen();
  animTo(true);
}

void closeCenter() {
  if (!ctr || cstate == CState::CLOSED) return;
  if (cstate == CState::OPEN) {
    ctrY = 0;
    beginSlide();
  }
  animTo(false);
}

bool centerShown() { return ctr && cstate != CState::CLOSED; }

void pullCenter(int dy, bool released) {
  if (!pulling) {
    if (released || (ctr && cstate == CState::OPEN)) return;
    if (!ctr || cstate == CState::CLOSED) prepareOpen();
    lv_anim_delete(&ctrY, nullptr);
    pulling = true;
  }
  if (!released) {
    setY(constrain(-os::H() + dy, -os::H(), 0));
  } else {
    pulling = false;
    animTo(dy > os::H() / 5);
  }
}

void openSettings() {
  load();
  readSwitches();
  lv_obj_t* body;
  lv_obj_t* scr = os::screen(N_("Notifications"), &body);
  lv_obj_t* r = listRow(body, N_("Show previews"), "", nullptr);
  lv_obj_t* s = toggle(r, preview, [](lv_event_t* e) {
    desk::setSetting("s3.notif_preview", lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? "1" : "0");
  });
  lv_obj_align(s, LV_ALIGN_RIGHT_MID, 0, 0);
  hint(body, N_("Off: banners show only the app's name, not what it says."));
  caption(body, N_("Apps"));
  for (const String& id : candidates()) appRow(body, id);
  const String q = i18n::fmt(N_("With Do Not Disturb on and in the quiet hours ({0} - {1}): no banners, no sound; "
                                "notifications still go to the Notification Center."),
                             desk::setting("dash.quiet_from", "23:00"), desk::setting("dash.quiet_to", "06:00"));
  hint(body, q.c_str());
  os::open(scr);
}

int count() {
  load();
  return items.size();
}

std::vector<Shown> latest(int n) {
  load();
  std::vector<Shown> out;
  for (const Item& it : items) {
    if ((int)out.size() >= n) break;
    Shown s;
    s.app = it.t[Texts::APP];
    s.title = it.t[Texts::TITLE];
    s.body = it.t[Texts::BODY];
    s.arg = it.t[Texts::ARG];
    s.at = (time_t)(it.at / 1000);
    s.unread = !it.read;
    out.push_back(s);
  }
  return out;
}

String name(const char* app) { return appName(app); }
lv_obj_t* icon(lv_obj_t* parent, const char* app, int size) { return appIcon(parent, app, size); }
String when(time_t at) { return whenText((int64_t)at * 1000); }

void tick() {
  popnote::tick();  // the pop-ups' countdowns and expiry
  load();
  if (!ban) banOut = banHeld = false;
  if (!ctr && cstate != CState::CLOSED) {  // the OS rebuilt its layers
    cstate = CState::CLOSED;
    pulling = false;
    lv_anim_delete(&ctrY, nullptr);
    freeShot();
  }
  // A system gesture (swipe up home, edge back) can take a touch away: LVGL then never says "released".
  if (!os::fingerDown()) {
    if (ban && banHeld && !banOut) {
      banHeld = false;
      slideBanner(banTop, 150, false);
      banAt = millis();
    }
    if (sw.hor) {
      sw.hor = false;
      if (ctrList) lv_obj_add_flag(ctrList, LV_OBJ_FLAG_SCROLLABLE);
      if (sw.card && lv_obj_is_valid(sw.card)) snapBack(sw.card);
    }
    if (handleMoved) {
      handleMoved = false;
      animTo(false);  // it was a swipe up
    }
    if (pulling) {
      pulling = false;
      animTo(ctrY > -os::H() * 4 / 5);
    }
  }
  if (ban && !banOut && !banHeld && millis() - banAt > (queue.empty() ? SHOW_MS : SHOW_QUEUED_MS)) hideBanner(true);
  while (!ban && !queue.empty() && cstate == CState::CLOSED) {
    const uint32_t id = queue.front();
    queue.erase(queue.begin());
    if (const Item* it = findItem(id)) showBanner(*it, true);
  }
  if (banMore) queue.empty() ? lv_obj_add_flag(banMore, LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(banMore, LV_OBJ_FLAG_HIDDEN);
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  readSwitches();
  fireDue();
  importDesk();
  if (dirty && millis() - dirtyAt > 4000 && !os::fingerDown()) save();
  if (cstate == CState::OPEN && os::saverOn()) closeCenterNow();
  if (cstate == CState::OPEN && ctrClock) {
    const time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    if (t.tm_min != lastMinute && !os::fingerDown() && !sw.hor) {
      lastMinute = t.tm_min;
      setText(ctrClock, hhmm(t.tm_hour, t.tm_min).c_str());
      fillList();  // "12 phút trước" moves on
    }
  }
}

uint32_t version() {
  load();
  readSwitches();
  return ver;
}

}  // namespace notif
