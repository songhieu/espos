#pragma GCC optimize("Os")  // screens made once a change: size over speed
#include "lock.h"
#if !ESPOS_NO_LOCK

#include <Preferences.h>
#include <time.h>

#include <mutex>
#include <vector>

#include "apps.h"
#include "claim.h"
#include "desk.h"
#include "lic.h"
#include "notif.h"
#include "os.h"
#include "pet/dogs.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "trace.h"
#include "uikit.h"


namespace lock {
namespace {

using namespace kit;
namespace st = pkg::store;

constexpr const char* TANK = "vn.pawboop.tank";
constexpr const char* DOG = "vn.pawboop.dog.";
constexpr const char* MAP = "vn.pawboop.map.";

// Plain strings, no JSON or maps (the app slot is nearly full): the entitlements ",a,b," and the catalog's marks
// ";<paid id>=,<free id>,<free id>,;" (kept in NVS as they are).
std::mutex mu;  // may() is also asked by the run's decoder task
Plan plan_ = Plan::None;
int64_t trialEnd = 0;  // ms
String ent = ",", marks = ";";
bool marksLoaded = false;
uint32_t seenLic = 0, ver = 1;
String acct;                  // a blocked account: "" normal, else the server's state ("blocked", "deleting", "deleted")
lv_obj_t* acctObj = nullptr;  // its screen (shown over Home only: an alarm or the saver still come first)
void acctShow();

// ",x," inside [s, e) (a list ",a,b,"), x being n characters.
bool inList(const char* s, const char* e, const char* x, size_t n) {
  for (const char* p = s; p && p < e; p = strchr(p + 1, ','))
    if (*p == ',' && p + n + 1 < e && !strncmp(p + 1, x, n) && p[n + 1] == ',') return true;
  return false;
}
bool inList(const String& l, const char* x, size_t n) { return inList(l.c_str(), l.c_str() + l.length(), x, n); }

// The free list ",a,b," of a paid package (its end in `end`), nullptr when the catalog does not mark it paid.
const char* markOf(const char* id, size_t n, const char*& end) {
  if (!marksLoaded) {
    marksLoaded = true;
    Preferences p;
    if (p.begin("lock", true)) marks = p.getString("marks", ";"), p.end();
  }
  for (const char* p = strchr(marks.c_str(), ';'); p; p = strchr(p + 1, ';'))
    if (!strncmp(p + 1, id, n) && p[n + 1] == '=') {
      end = strchr(p + n + 2, ';');
      return end ? p + n + 2 : nullptr;
    }
  return nullptr;
}

// The value after "key": in a JSON object's text (the server's "lic", kept by lic.h), nullptr when absent.
const char* after(const char* j, const char* key) {
  const size_t n = strlen(key);
  for (const char* p = strchr(j, '"'); p; p = strchr(p + 1, '"'))
    if (!strncmp(p + 1, key, n) && p[n + 1] == '"') {
      p += n + 2;
      while (*p == ' ' || *p == ':') p++;
      return p;
    }
  return nullptr;
}

Plan parsePlan(const char* s) {
  if (!s || *s != '"') return Plan::Owned;  // no plan said: nothing locks
  if (!strncmp(s, "\"free\"", 6)) return Plan::Free;
  if (!strncmp(s, "\"trial\"", 7)) return Plan::Trial;
  if (!strncmp(s, "\"sub\"", 5)) return Plan::Sub;
  return Plan::Owned;  // "owned", or a word this firmware does not know: nothing locks
}

int prefInt(const char* k, int def = 0) {
  Preferences p;
  if (!p.begin("lock", true)) return def;
  const int v = p.getInt(k, def);
  p.end();
  return v;
}
void prefPut(const char* k, int v) {
  Preferences p;
  if (!p.begin("lock", false)) return;
  p.putInt(k, v);
  p.end();
}

// The server's "lic" (lic.h keeps the last one as its JSON text): read again when it changed.
void refresh() {
  if (seenLic == lic::version()) return;
  seenLic = lic::version();
  const char* raw = lic::raw().c_str();
  Plan pl = Plan::None;
  int64_t end = 0;
  String e = ",";
  if (*raw == '{') {
    pl = parsePlan(after(raw, "plan"));
    if (const char* t = after(raw, "trial_end_ms")) end = strtoll(t, nullptr, 10);
    if (const char* a = after(raw, "ent"))
      if (*a == '[')
        for (const char* q = strchr(a, '"'); q && q < strchr(a, ']');) {  // ["id", "id"]: ids have no quotes
          const char* z = strchr(q + 1, '"');
          if (!z) break;
          e += String(q + 1).substring(0, z - q - 1) + ",";
          q = strchr(z + 1, '"');
        }
  }
  std::lock_guard<std::mutex> l(mu);
  if (pl == plan_ && end == trialEnd && e == ent) return;
  plan_ = pl, trialEnd = end, ent = e;
  ver++;
  trace::add("lock: plan %d end %lld ent %s", (int)pl, (long long)end, ent.c_str());
}

// asFree: as on the free plan without anything bought (what the trial gives on top: the store's "Try").
bool mayLocked(const char* id, bool asFree = false) {  // mu held
  if (!asFree && plan_ != Plan::Free) return true;  // none, trial, owned, sub
  const size_t n = strlen(id);
  if (!asFree && inList(ent, id, n)) return true;
  const char* end;
  if (const char* plus = strchr(id, '+')) {  // a part of a package
    if (!asFree && inList(ent, id, plus - id)) return true;
    const char* m = markOf(id, plus - id, end);
    return !m || inList(m, end, plus + 1, strlen(plus + 1));
  }
  const char* m = markOf(id, n, end);
  if (!m || end - m > 1) return true;  // not paid, or paid with a free part: the package itself opens
  return inList(marks, id, n);  // an add-on its main package lists as free (vn.pawboop.pet: "vn.pawboop.dog.bao")
}

// ---------------------------------------------------------------- the notices and the end card
bool syncAsked = false;

int64_t nowMs() { return desk::nowMs(); }

int dayOf(int64_t ms) {  // a local calendar day number
  const time_t t = ms / 1000;
  tm lt;
  localtime_r(&t, &lt);
  return lt.tm_year * 400 + lt.tm_yday;
}

void notices() {
  const int endS = (int)(trialEnd / 1000);
  // A day before the trial ends (scheduled: it comes even when the device was off in between; kept by notif).
  if (plan_ == Plan::Trial && trialEnd > 0) {
    if (prefInt("n1") != endS && nowMs() && nowMs() < trialEnd) {
      prefPut("n1", endS);
      notif::Opts o;
      o.tag = "trial_end";
      // Less than a day left when first heard of: it posts at once, and the end may be today.
      const bool today = trialEnd - nowMs() < 86400000 && dayOf(trialEnd) == dayOf(nowMs());
      notif::schedule("store", (time_t)(endS - 86400), today ? N_("Your trial ends today") : N_("Your trial ends tomorrow"),
                      N_("Then the extra dogs, maps and fish lock. Nothing you made is lost."), o);
    }
  } else if (prefInt("n1")) {
    prefPut("n1", 0);
    notif::cancel("store", "trial_end");
  }
  // The trial's AI turns nearly used up (lic.h counts them down between syncs).
  const int left = lic::aiLeft();
  if (lic::ai() == lic::Ai::Trial && left >= 1 && left <= 10) {
    if (prefInt("n10") != endS + 1) {
      prefPut("n10", endS + 1);
      notif::Opts o;
      o.tag = "ai_left";
      notif::post("store", i18n::plural(left, NP_("{n} AI turn left|{n} AI turns left")).c_str(),
                  T_("The trial's turns with the assistant are almost used up."), o);
    }
  }
}

// The end card: once, the first time the plan turns "free" after a trial (or after a subscription).
void planChanged(Plan was) {
  if (plan_ == Plan::Free) {
    const int endS = (int)(trialEnd / 1000);
    if (was == Plan::Owned || was == Plan::Sub) prefPut("card", -1), prefPut("due", 2);
    else if (trialEnd > 0 && prefInt("card") != endS) prefPut("card", endS), prefPut("due", 1);
  }
  prefPut("plan", (int)plan_);
}

// ---------------------------------------------------------------- pieces
struct Colors {
  uint32_t bg, fg, dim, bubble, bubbleFg, btn, btnFg;
  lv_opa_t bgOpa, bubbleOpa, btnOpa;
};
Colors colors(Look k) {
  const Pal& p = pal();
  if (k == Look::Pet) return {0xfff8ee, 0x7a3e00, 0x7a5a36, 0xffe6c4, 0x5b3200, 0xffe2b8, 0x7a3e00, 255, 255, 255};
  if (k == Look::Tank) return {0x102c3a, 0xe8f6ff, 0xa9c6d6, 0xffffff, 0xe8f6ff, 0xffffff, 0xffffff, 240, 30, 36};
  return {p.cell, p.fg, p.dim, p.cell2, p.fg, p.chip, p.link, 255, 255, 255};
}

}  // namespace
lv_obj_t* glyph(lv_obj_t* parent, int s, uint32_t color) {  // a padlock in a box s x s
  lv_obj_t* g = clear(parent);
  lv_obj_set_size(g, s, s);
  lv_obj_center(g);
  // The shackle: a tall rounded loop whose legs go behind the body (a round ring above it read as a person).
  const int bw = s * 8 / 10, bh = s * 55 / 100, sw = s * 56 / 100, shh = s * 7 / 10;
  lv_obj_t* sh = clear(g);
  lv_obj_set_size(sh, sw, shh);
  lv_obj_set_style_radius(sh, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(sh, max(1, s / 7), 0);
  lv_obj_set_style_border_color(sh, C(color), 0);
  lv_obj_set_style_border_opa(sh, LV_OPA_COVER, 0);
  lv_obj_align(sh, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t* b = box(g, color, max(1, s / 10));
  lv_obj_set_size(b, bw, bh);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
  return g;
}
namespace {

lv_obj_t* portrait(lv_obj_t* parent, int size) {  // the chosen dog (else PawBoop's own icon), round
  lv_obj_t* w = clear(parent);
  lv_obj_set_size(w, size, size);
  lv_obj_set_style_radius(w, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(w, true, 0);
  lv_obj_set_style_bg_color(w, C(0xffcf7a), 0);
  lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
  const pet::dogs::Info* d = pet::dogs::chosen();
  lv_obj_t* ic = d ? apps::packageIcon(w, pet::dogs::pkgId(d->key).c_str(), size)
                   : pkg::find("vn.pawboop.pet") ? apps::packageIcon(w, "vn.pawboop.pet", size)
                                                 : apps::drawnIcon(w, 0xffcf7a, 0xe8892c, "play", size);
  lv_obj_center(ic);
  lv_obj_remove_flag(w, LV_OBJ_FLAG_CLICKABLE);
  return w;
}

String buyUrl() {
  const String& u = lic::buyUrl();
  return u.length() ? u : String(ESPOS_WEB);
}

lv_obj_t* qrBlock(lv_obj_t* parent, int size, uint32_t fg) {  // the code and "Scan to buy" under it
  lv_obj_t* c = stack(parent, false, 4);
  lv_obj_set_width(c, size + 20);  // (a column stack is as wide as its parent otherwise)
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  qr(c, buyUrl().c_str(), size);
  lv_obj_t* l = label(c, "", 12, false, fg);
  fitText(l, N_("Scan to buy"), size + 20, 34, 12, 12, false, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return c;
}

// ---------------------------------------------------------------- the lock sheet
// The lock sheet and the end card share one frame (built below): what is up, and the screen it went up over.
lv_obj_t* topObj = nullptr;
lv_obj_t* topScr = nullptr;
bool topFull = false;

void closeTop() {
  if (topObj && lv_obj_is_valid(topObj)) lv_obj_delete(topObj);
  topObj = topScr = nullptr;
}

// ---------------------------------------------------------------- the end card

String joined(const std::vector<String>& v) {
  String s;
  for (size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + v[i];
  return s;
}

void bullet(lv_obj_t* parent, const String& text, uint32_t dot, uint32_t fg, int w) {
  lv_obj_t* r = clear(parent);
  lv_obj_set_size(r, w, LV_SIZE_CONTENT);
  lv_obj_t* d = box(r, dot, 3);
  lv_obj_set_size(d, 6, 6);
  lv_obj_set_pos(d, 0, 5);
  lv_obj_t* l = label(r, "", 12, false, fg);
  wrapText(l, text.c_str(), w - 13, 12, false, 1);
  lv_obj_set_pos(l, 13, 0);
}

// What stays free and what is locked now, from the catalog's marks (the names from the catalog, else the packages).
void lists(std::vector<String>& free, std::vector<String>& locked) {
  std::vector<String> dogsFree;
  int dogsLocked = 0;
  for (const pet::dogs::Info& d : pet::dogs::ALL) {
    if (may(pet::dogs::pkgId(d.key).c_str())) dogsFree.push_back(d.name);
    else dogsLocked++;
  }
  std::vector<String> mapsFree, others;
  int mapsLocked = 0;
  bool anyMap = false;
  for (const st::Entry& e : st::catalog()) {
    if (e.set == "map") {
      anyMap = true;
      if (may(e.id.c_str())) mapsFree.push_back(e.name);
      continue;
    }
  }
  const char* end;
  markOf("", 0, end);  // (loads the kept marks)
  for (const char* q = strchr(marks.c_str(), ';'); q && q[1]; q = strchr(q + 1, ';')) {  // ";id=,free,;"
    const char* eq = strchr(q, '=');
    if (!eq) break;
    const String id = String(q + 1).substring(0, eq - q - 1);
    if (id.startsWith(MAP)) {
      if (!may(id.c_str())) mapsLocked++;
    } else if (!id.startsWith(DOG) && id != TANK && !may(id.c_str())) {
      const st::Entry* e = st::entry(id.c_str());
      const pkg::Package* p = pkg::find(id.c_str());
      others.push_back(e ? e->name : p ? p->name : id);
    }
    q = strchr(eq, ';');
    if (!q) break;
    q--;
  }
  if (dogsLocked && dogsFree.size()) free.push_back(i18n::fmt(N_("Dogs: {0}"), joined(dogsFree)));
  if (anyMap && mapsLocked && mapsFree.size()) free.push_back(i18n::fmt(N_("Coin Run maps: {0}"), joined(mapsFree)));
  const char* tank = markOf(TANK, strlen(TANK), end);
  const bool tankLocked = tank && !may("vn.pawboop.tank+full");
  if (tankLocked && end - tank > 1) {
    int n = -inList(tank, end, "planted", 7) - 1;  // its free ids but the look ("planted")
    for (const char* c = tank; c < end; c++) n += *c == ',';
    free.push_back(i18n::plural(n, NP_("Aquarium: the planted tank and {n} kind of fish|Aquarium: the planted tank and {n} kinds of fish")));
  }
  free.push_back(T_("Every other app"));
  if (dogsLocked) locked.push_back(i18n::plural(dogsLocked, dogsFree.size() ? NP_("{n} more dog|{n} more dogs") : NP_("{n} dog|{n} dogs")));
  if (mapsLocked) locked.push_back(i18n::plural(mapsLocked, mapsFree.size() ? NP_("{n} more map|{n} more maps") : NP_("{n} map|{n} maps")));
  if (tankLocked) locked.push_back(T_("Aquarium: every fish, tank look and the sea tank"));
  for (size_t i = 0; i < others.size() && i < 3; i++) locked.push_back(others[i]);
  if (lic::aiLocked()) locked.push_back(T_("The AI assistant"));
}

}  // namespace

// ---------------------------------------------------------------- state
Plan plan() {
  refresh();
  return plan_;
}
bool trial() { return plan() == Plan::Trial; }
int daysLeft() {
  refresh();
  const int64_t n = nowMs();
  if (!trialEnd || !n || n >= trialEnd) return 0;
  return max(0, dayOf(trialEnd) - dayOf(n));
}
bool paid(const char* id) {
  std::lock_guard<std::mutex> l(mu);
  const char* plus = strchr(id, '+');
  const char* end;
  return markOf(id, plus ? plus - id : strlen(id), end) != nullptr;
}
bool may(const char* id) {
  if (!id || !*id) return true;
  std::lock_guard<std::mutex> l(mu);
  return mayLocked(id);
}
bool freeAnyway(const char* id) {
  std::lock_guard<std::mutex> l(mu);
  return mayLocked(id, true);
}
bool bought(const char* id) {
  refresh();
  if (!paid(id)) return false;
  if (plan_ == Plan::Owned || plan_ == Plan::Sub) return true;
  std::lock_guard<std::mutex> l(mu);
  return plan_ == Plan::Free && inList(ent, id, strlen(id));
}
uint32_t version() {
  refresh();
  return ver;
}

void catalog(JsonArrayConst packages) {
  String m = ";";
  for (JsonObjectConst a : packages) {
    if (!(a["paid"] | false)) continue;
    m += String(a["id"] | "") + "=,";
    for (JsonVariantConst x : a["free_ids"].as<JsonArrayConst>()) m += String(x | "") + ",";
    m += ";";
  }
  {
    std::lock_guard<std::mutex> l(mu);
    const char* e;
    markOf("", 0, e);  // (loads the kept ones first)
    if (m == marks) return;
    marks = m;
    ver++;
  }
  Preferences p;
  if (p.begin("lock", false)) p.putString("marks", m), p.end();
  trace::add("lock: paid %s", m.c_str());
}

void tick() {
#if !ESPOS_ONBOARD
  claim::tick();  // an unclaimed unit's first boot (claim.h; with the setup, onboard.cpp does it)
#endif
  refresh();
  static uint32_t seenVer = 0, seenAi = 0;
  if (seenVer != ver) {
    seenVer = ver;
    const int last = prefInt("plan", -1);
    if (last != (int)plan_) planChanged(last < 0 ? Plan::None : (Plan)last);
  }
  if (seenAi != lic::version()) seenAi = lic::version(), notices();
  // The trial's end passed on this clock: ask the server now (it decides; nothing locks here on its own).
  if (plan_ == Plan::Trial && trialEnd && nowMs() > trialEnd + 5000 && !syncAsked) {
    syncAsked = true;
    trace::add("lock: trial over, sync");
    desk::syncNow();
  }
  if (plan_ != Plan::Trial) syncAsked = false;
  if (topObj && !topFull && lv_screen_active() != topScr) closeTop();  // the sheet's screen went away
  if (acct.length()) {  // a blocked account: its screen over Home (hidden while an app, an alarm or the saver is up)
    const bool home = os::atHome() && !os::saverOn();
    if (home) acctShow();
    if (acctObj && lv_obj_is_valid(acctObj)) home ? lv_obj_remove_flag(acctObj, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(acctObj, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  // The end card: on Home, nobody touching it, no saver, nothing else on top.
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 1000) return;
  lastTry = millis();
  if (plan_ != Plan::Free || topObj || !prefInt("due")) return;
  if (!os::atHome() || os::saverOn() || os::fingerDown() || os::idleMs() < 1500 || notif::centerShown()) return;
  endCard();
}

// ---------------------------------------------------------------- the chip
String chipText() {
  if (!trial()) return String();
  const int d = daysLeft();
  return d <= 0 ? String(T_("Trial · ends today")) : i18n::plural(d, NP_("Trial · {n} day left|Trial · {n} days left"));
}

lv_obj_t* chip(lv_obj_t* parent, bool onDark) {
  const String t = chipText();
  if (!t.length()) return nullptr;
  lv_obj_t* c = box(parent, onDark ? 0x000000 : 0xefe9ff, 11);
  if (onDark) lv_obj_set_style_bg_opa(c, LV_OPA_50, 0);
  lv_obj_set_size(c, LV_SIZE_CONTENT, 22);
  lv_obj_set_style_pad_left(c, 9, 0);
  lv_obj_set_style_pad_right(c, 10, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(c, 5, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* dot = box(c, onDark ? 0xc9b8ff : 0x7c4dff, 3);
  lv_obj_set_size(dot, 6, 6);
  label(c, t.c_str(), 12, true, onDark ? 0xffffff : 0x6a3df0);
  return c;
}

bool chipPicture(bool onDark, uint16_t*& rgb, uint8_t*& alpha, int& w, int& h) {
  rgb = nullptr, alpha = nullptr, w = h = 0;
  lv_obj_t* tmp = lv_obj_create(nullptr);  // a screen never shown: nothing on the panel is touched
  lv_obj_t* c = chip(tmp, onDark);
  if (!c) return lv_obj_delete(tmp), false;
  lv_obj_update_layout(tmp);
  lv_draw_buf_t* d = lv_snapshot_take(c, LV_COLOR_FORMAT_ARGB8888);
  lv_obj_delete(tmp);
  if (!d) return false;
  w = d->header.w, h = d->header.h;
  uint8_t* mem = (uint8_t*)heap_caps_malloc(w * h * 3, MALLOC_CAP_SPIRAM);
  if (mem) {
    rgb = (uint16_t*)mem, alpha = mem + w * h * 2;
    for (int y = 0; y < h; y++) {
      const uint8_t* s = d->data + y * d->header.stride;  // B, G, R, A
      for (int x = 0; x < w; x++, s += 4)
        rgb[y * w + x] = (uint16_t)((s[2] >> 3) << 11 | (s[1] >> 2) << 5 | s[0] >> 3), alpha[y * w + x] = s[3];
    }
  }
  lv_draw_buf_destroy(d);
  return mem != nullptr;
}

lv_obj_t* badge(lv_obj_t* parent, int size) {
  lv_obj_t* b = box(parent, 0x1d1f24, size / 2);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C(0xffffff), 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(glyph(b, size / 2, 0xffffff));
  return b;
}

// ---------------------------------------------------------------- the lock sheet
bool sheetShown() { return topObj && !topFull && lv_obj_is_valid(topObj); }

namespace {
// One frame for both: the words in a column; the code under them (upright) or on the right (on its side); "Later".
// A sheet is a panel over a dim layer (a tap beside it closes it); the end card is the whole screen, its words
// scrolling when a long language needs it, "Later" at the bottom.
struct Frame {
  lv_obj_t *panel, *col;
  int colW, qr;
  bool land, full;
};
Frame frameOpen(bool full, const Colors& k, int dimOpa) {
  closeTop();
  Frame f;
  f.full = full, f.land = os::landscape();
  const int W = os::W(), H = os::H();
  lv_obj_t* root = box(lv_layer_top(), full ? k.bg : 0x000000, 0);
  lv_obj_set_size(root, W, H);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  topObj = root, topScr = lv_screen_active(), topFull = full;
  int inner;
  if (full) {
    f.panel = root;
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(root, 18, 0);
    lv_obj_set_style_pad_top(root, os::SB_H + 6, 0);
    lv_obj_set_style_pad_bottom(root, 14, 0);
    inner = W - 36;
  } else {
    lv_obj_set_style_bg_opa(root, dimOpa, 0);
    lv_obj_add_event_cb(root, [](lv_event_t* e) {
      if (lv_event_get_target(e) == lv_event_get_current_target(e)) lv_async_call([](void*) { closeTop(); }, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    const int pw = f.land ? min(W - 32, 452) : W;
    f.panel = box(root, k.bg, 20);
    lv_obj_set_style_bg_opa(f.panel, k.bgOpa, 0);
    lv_obj_set_size(f.panel, pw, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(f.panel, 14, 0);
    lv_obj_set_style_pad_bottom(f.panel, f.land ? 14 : 18, 0);
    lv_obj_add_flag(f.panel, LV_OBJ_FLAG_CLICKABLE);  // a tap on it stays on it
    lv_obj_remove_flag(f.panel, LV_OBJ_FLAG_SCROLLABLE);
    inner = pw - 28;
  }
  f.qr = f.land ? (full ? 112 : 120) : (full ? 96 : 112);
  f.colW = f.land ? inner - f.qr - 30 : inner;
  f.col = stack(f.panel, false, full ? 4 : 10);
  lv_obj_set_width(f.col, f.colW);
  if (full) {  // the words take the room above "Later", and scroll when they need more
    lv_obj_set_flex_grow(f.col, 1);
    lv_obj_add_flag(f.col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(f.col, LV_DIR_VER);
  }
  return f;
}
// The code (in `beside` when given: upright, next to a list) and "Later".
void frameClose(const Frame& f, const Colors& k, lv_obj_t* beside) {
  lv_obj_t* q = qrBlock(f.land ? f.panel : beside ? beside : f.col, f.qr, k.dim);
  if (f.land) lv_obj_add_flag(q, LV_OBJ_FLAG_IGNORE_LAYOUT), lv_obj_align(q, LV_ALIGN_TOP_RIGHT, 0, 0);
  else if (beside) lv_obj_align(q, LV_ALIGN_TOP_RIGHT, 0, 12);
  else lv_obj_set_width(q, f.colW);
  lv_obj_t* later = button(f.full ? f.panel : f.col, N_("Later"), k.btn, k.btnFg,
                           [](lv_event_t*) { lv_async_call([](void*) { closeTop(); }, nullptr); });
  lv_obj_set_style_bg_opa(later, k.btnOpa, 0);
  lv_obj_set_size(later, f.full ? lv_pct(100) : f.colW, 42);
  if (f.full && f.land)  // on its side: under the code, the words keep the whole height
    lv_obj_add_flag(later, LV_OBJ_FLAG_IGNORE_LAYOUT), lv_obj_set_width(later, f.qr + 20), lv_obj_align(later, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  if (f.full) return;
  lv_obj_update_layout(f.panel);
  if (f.land) lv_obj_center(f.panel);
  else lv_obj_align(f.panel, LV_ALIGN_BOTTOM_MID, 0, 20);  // the lower corners under the edge
}
}  // namespace

void sheet(const char* name, Look look) {
  const Colors k = colors(look);
  Frame f = frameOpen(false, k, look == Look::Tank ? LV_OPA_40 : LV_OPA_50);
  lv_obj_t* t = label(f.col, "", 18, true, k.fg);
  fitText(t, i18n::fmt(N_("{0} is locked"), name).c_str(), f.colW, 48, 18, 14, true, 0);
  lv_obj_t* row = clear(f.col);
  lv_obj_set_size(row, f.colW, LV_SIZE_CONTENT);
  portrait(row, 52);
  lv_obj_t* bub = box(row, k.bubble, 14);
  lv_obj_set_style_bg_opa(bub, k.bubbleOpa, 0);
  lv_obj_set_size(bub, f.colW - 62, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(bub, 10, 0);
  lv_obj_set_pos(bub, 62, 0);
  lv_obj_t* say = label(bub, "", 13, false, k.bubbleFg);
  wrapText(say, i18n::fmt(N_("{0} is in the full version. Nothing you made is lost: buy it and everything comes back just as it was."), name).c_str(),
           f.colW - 82, 13, false, 2);
  frameClose(f, k, nullptr);
  trace::add("lock: sheet %s", name);
}

// ---------------------------------------------------------------- the end card
void endCard() {
  const int due = prefInt("due");
  prefPut("due", 0);
  const Colors k = colors(Look::Os);
  const Pal& pp = pal();
  Frame f = frameOpen(true, k, 0);
  lv_obj_t* t = label(f.col, "", 22, true, k.fg);
  fitText(t, due == 2 ? N_("Your subscription has ended") : N_("Your trial has ended"), f.colW, 30, 22, 16, true, 0);
  lv_obj_t* s = label(f.col, "", 13, false, k.dim);
  wrapText(s, T_("Nothing you made is lost: buy it and everything comes back just as it was."), f.colW, 13, false, 1);
  std::vector<String> fr, lk;
  lists(fr, lk);
  // What stays free, then what is locked (upright: beside the code); on its side the two lists side by side.
  lv_obj_t* both = stack(f.col, f.land, 12);
  lv_obj_set_width(both, f.colW);
  lv_obj_set_flex_align(both, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  const int half = f.land ? (f.colW - 12) / 2 : f.colW;
  auto group = [&](lv_obj_t* parent, const char* text, uint32_t color, const std::vector<String>& items, uint32_t dot, int w) {
    lv_obj_t* g = stack(parent, false, 4);
    lv_obj_set_width(g, w);
    lv_obj_t* h = label(g, "", 12, true, color);
    fitText(h, upperVi(T_(text)).c_str(), w, 18, 12, 12, true, 0);
    lv_obj_set_style_margin_top(h, 6, 0);
    for (const String& x : items) bullet(g, x, dot, pp.fg, w);
  };
  group(both, N_("Still free"), pp.dark ? 0x4cd28a : 0x1f9d4c, fr, 0x34c759, half);
  lv_obj_t* lrow = f.land ? both : clear(both);
  if (!f.land) lv_obj_set_size(lrow, f.colW, LV_SIZE_CONTENT);
  if (lk.size()) group(lrow, N_("Locked now"), pp.dark ? 0xff9f5a : 0xc2410c, lk, 0xf97316, f.land ? half : f.colW - f.qr - 30);
  frameClose(f, k, f.land ? nullptr : lrow);
  trace::add("lock: end card %u/%u", (unsigned)fr.size(), (unsigned)lk.size());
}

// ---------------------------------------------------------------- a blocked account
namespace {
void acctShow() {
  if (acctObj && lv_obj_is_valid(acctObj)) return;
  closeTop();
  const Pal& p = pal();
  lv_obj_t* c = box(lv_layer_top(), p.bg, 0);
  acctObj = c;
  lv_obj_set_size(c, os::W(), os::H());
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);  // Home stays out of reach under it
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 12, 0);
  lv_obj_t* ring = box(c, p.cell, 36);
  lv_obj_set_size(ring, 72, 72);
  glyph(ring, 30, p.dim);
  const int w = os::W() - 48;
  lv_obj_t* t = label(c, "", 20, true, p.fg);
  fitText(t, acct == "blocked" ? N_("Your account is locked") : N_("Your account is being deleted"), w, 54, 20, 16, true);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* l = label(c, "", 14, false, p.dim);
  wrapText(l, T_("Contact support@espos.dev"), w, 14);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}
}  // namespace

void account(const char* state) {
  const String s = state ? state : "";
  if (s == acct) return;
  acct = s;
  trace::add("lock: account %s", acct.length() ? acct.c_str() : "ok");
  if (!acct.length() && acctObj && lv_obj_is_valid(acctObj)) lv_obj_delete(acctObj);
  if (!acct.length()) acctObj = nullptr;
  else if (acctObj && lv_obj_is_valid(acctObj)) lv_obj_delete(acctObj), acctObj = nullptr;  // its words change
  ver++;
}
bool accountBlocked() { return acct.length() > 0; }

// ---------------------------------------------------------------- tests
bool dev(const char* cmd) {
  if (!strncmp(cmd, "lic ", 4)) {  // as a sync's "lic" would come; "none": no licensing at all
    JsonDocument d;
    String a = cmd + 4;  // (a String, not const: the very parser desk.cpp already has for the sync answers)
    if (a != "none" && deserializeJson(d, a)) return false;
    lic::fromSync(d.as<JsonVariantConst>());
    tick();
    return true;
  }
  if (!strcmp(cmd, "card")) return endCard(), true;
  if (!strncmp(cmd, "account ", 8)) return account(strcmp(cmd + 8, "ok") ? cmd + 8 : nullptr), true;
#ifdef SIM  // the simulator's tests (the app slot is full)
  if (!strncmp(cmd, "install ", 8)) {  // install ID: the store's install as a tap would (paid parts: pkg-link)
    const st::Entry* e = st::entry(cmd + 8);
    String why;
    if (!e) return false;
    if (!st::install(*e, st::defaultParts(*e), pkg::volume(pkg::SD).present ? pkg::SD : pkg::FLASH, why))
      trace::add("lock: install refused: %s", why.c_str());
    return true;
  }
  if (!strcmp(cmd, "close")) return closeTop(), true;
  if (!strncmp(cmd, "sheet ", 6)) {
    const char* n = cmd + 6;
    Look k = Look::Os;
    if (!strncmp(n, "pet ", 4)) k = Look::Pet, n += 4;
    else if (!strncmp(n, "tank ", 5)) k = Look::Tank, n += 5;
    return sheet(n, k), true;
  }
  if (!strcmp(cmd, "state")) {
    refresh();
    trace::add("lock: plan %d, %d days left, chip '%s', tank+full %d, momo %d, frost %d", (int)plan_, daysLeft(),
               chipText().c_str(), may("vn.pawboop.tank+full"), may("vn.pawboop.dog.momo"), may("vn.pawboop.dog.husky"));
    return true;
  }
#endif
  return false;
}

}  // namespace lock
#endif  // !ESPOS_NO_LOCK
