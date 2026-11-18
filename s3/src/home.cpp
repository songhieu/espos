#pragma GCC optimize("Os")  // UI building and editing, not drawing (LVGL is apart): built small, the firmware must fit
#include "home.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <vector>

#include "apps.h"
#include "art.h"
#include "data.h"
#include "desk.h"
#include "desksaver.h"
#include "fonts.h"
#include "mywid.h"
#include "os.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "search.h"
#include "sound.h"
#include "standby.h"
#include "theme.h"
#include "trace.h"
#include "uikit.h"
#include "wid.h"
#include "widopts.h"

namespace home {
namespace {

using namespace kit;

// The grid of the orientation shown (setGeo): portrait 4 x 5 cells of 78 x 74 under the status bar, the dock under
// them; landscape 5 x 4 cells of 78 x 72, the dock standing on the right (x 404), the dots under the grid. The same
// 20 cells and the same widget boxes (wid::boxSize) either way. Landscape pages are their own (s3.home_l, SEED).
int COLS = 4, ROWS = 5, CW = 78, CH = 74, GX = 4, GY = 28, WY = 4;  // WY: a widget's box below its cell's top
int PAGER_H = 400;
bool land = false;
constexpr int DOCK_H = 62;                    // portrait: across the bottom
constexpr int LCOLS = 5, LROWS = 4;           // landscape grid
constexpr int LDOCK_X = 404, LDOCK_Y = 27, LDOCK_W = 68, LDOCK_H = 284, LDOTS_Y = 313;

void setGeo(bool l) {
  land = l;
  COLS = l ? LCOLS : 4, ROWS = l ? LROWS : 5, CH = l ? 72 : 74, GY = l ? 24 : 28, WY = l ? 3 : 4;
  PAGER_H = l ? os::H() : GY + ROWS * CH + 2;  // portrait 400
}

struct Entry {
  bool app = true;
  String id;     // app id
  wid::Item w;   // widget
  int x = 0, y = 0;
  int cw() const { return app ? 1 : wid::cols(w.size); }
  int ch() const { return app ? 1 : wid::rows(w.size); }
};
using Page = std::vector<Entry>;
struct Model {
  std::vector<Page> pages;  // the pages of the orientation shown (portrait: s3.home; landscape: s3.home_l)
  std::vector<PageBg> bg;  // aligned with pages (syncBg after every change of the page list)
  std::vector<String> dock;
  std::vector<String> lib;  // the App Library's order (apps not listed follow in the registry's order)
} model;
String applied;           // what the shown orientation's setting was read as (its serialize())
String rawP, rawL;        // s3.home and s3.home_l as last read (a change from the web is noticed by these)
// Landscape: the portrait pages, kept to write s3.home again when the dock or the App Library's order changes.
std::vector<Page> portPages;
std::vector<PageBg> portBg;
String portSer;

lv_obj_t *root, *tiles, *dots, *dockBox, *todayTile, *libTile, *editBar, *wallImg, *wallStrip;
void syncBg() { model.bg.resize(model.pages.size()); }
std::vector<lv_obj_t*> pageTiles;

// Every tile holds its live content (child 0) and a picture of it (child 1, hidden). While the
// pages slide, only the pictures are drawn: a copy from PSRAM instead of every label, icon and
// glass card (the board drew a full page in ~80 ms, too slow for a swipe).
lv_obj_t* liveOf(lv_obj_t* tile) { return lv_obj_get_child(tile, 0); }
lv_obj_t* shotOf(lv_obj_t* tile) { return lv_obj_get_child(tile, 1); }
lv_obj_t* addTile(int col, lv_dir_t dir, int h, bool scroll);

struct Snap {
  uint16_t* px = nullptr;
  lv_image_dsc_t dsc = {};
  int h = 0;
  bool fresh = false;  // up to date
  bool valid = false;  // same layout (a clock a minute behind is fine for a swipe)
};
std::vector<Snap> snaps;  // by tile index (0 Today, 1..N pages, N+1 library)
bool shotsOn = false;

void freeSnap(Snap& s) {
  heap_caps_free(s.px);
  s = Snap();
}

// Content changed (new layout, new data): the picture may not be used any more.
void dirtyTile(int idx) {
  if (idx >= 0 && idx < (int)snaps.size() && snaps[idx].valid) trace::add("home tile %d changed", idx);
  if (idx >= 0 && idx < (int)snaps.size()) snaps[idx].fresh = snaps[idx].valid = false;
}
bool edit = false;
int curIndex = 1;  // tile index (0 = Today)

// ---------------------------------------------------------------- model
wid::Size sizeOf(const char* s) { return s && s[0] == 'm' ? wid::M : s && s[0] == 'l' ? wid::L : wid::S; }
const char* sizeName(wid::Size s) { return s == wid::M ? "m" : s == wid::L ? "l" : "s"; }

bool overlaps(const Entry& a, const Entry& b) {
  return a.x < b.x + b.cw() && b.x < a.x + a.cw() && a.y < b.y + b.ch() && b.y < a.y + a.ch();
}

bool fitsIn(const Page& p, const Entry& e, int skip, int cols, int rows) {
  if (e.x < 0 || e.y < 0 || e.x + e.cw() > cols || e.y + e.ch() > rows) return false;
  for (int i = 0; i < (int)p.size(); i++)
    if (i != skip && overlaps(p[i], e)) return false;
  return true;
}
bool fits(const Page& p, const Entry& e, int skip) { return fitsIn(p, e, skip, COLS, ROWS); }

// The first free place row by row; apps step 1 cell, widgets 2 (SEED's FIND).
bool findIn(const Page& p, Entry& e, int cols, int rows) {
  for (int y = 0; y + e.ch() <= rows; y++)
    for (int x = 0; x + e.cw() <= cols; x += (e.app ? 1 : 2)) {
      e.x = x, e.y = y;
      if (fitsIn(p, e, -1, cols, rows)) return true;
    }
  return false;
}
bool findFree(const Page& p, Entry& e) { return findIn(p, e, COLS, ROWS); }

Entry appEntry(const char* id, int x, int y) {
  Entry e;
  e.app = true, e.id = id, e.x = x, e.y = y;
  return e;
}

Entry widEntry(const char* kind, int x, int y, wid::Size s, const char* style = "", const char* opt = "") {
  Entry e;
  e.app = false, e.x = x, e.y = y;
  e.w.kind = kind, e.w.size = s, e.w.style = style, e.w.opt = opt;
  return e;
}

void defaults() {
  model = {};
  Page p1 = {widEntry("clock", 0, 0, wid::M), widEntry("cal", 0, 2, wid::S), widEntry("markets", 2, 2, wid::S),
             appEntry("news", 0, 4), appEntry("weather", 1, 4), appEntry("cal", 2, 4), appEntry("photos", 3, 4)};
  Page p2 = {appEntry("store", 0, 0), appEntry("lunar", 1, 0), appEntry("remind", 2, 0), appEntry("calc", 3, 0),
             widEntry("news", 0, 1, wid::M), widEntry("notes", 0, 3, wid::S), widEntry("count", 2, 3, wid::S)};
  model.pages = {p1, p2};
  model.dock = {"clock", "notes", "markets", "settings"};
}

// "pages" and "bg" of s3.home or s3.home_l into pages/bg, each entry checked against a grid of cols x rows.
void readPages(JsonDocument& d, int cols, int rows, std::vector<Page>& pages, std::vector<PageBg>& bgs) {
  for (JsonArrayConst pg : d["pages"].as<JsonArrayConst>()) {
    Page p;
    for (JsonArrayConst e : pg) {
      Entry en;
      en.app = !strcmp(e[0] | "", "a");
      en.x = e[2] | 0, en.y = e[3] | 0;
      if (en.app) {
        en.id = e[1] | "";
        if (!apps::find(en.id.c_str())) continue;
      } else {
        en.w.kind = e[1] | "";
        en.w.size = sizeOf(e[4] | "s");
        en.w.style = e[5] | "";
        en.w.opt = e[6] | "";
        en.w.open = e[7] | "";
        en.w.skin = e[8] | "";
        if (!wid::kind(en.w.kind)) continue;
      }
      if (fitsIn(p, en, -1, cols, rows)) p.push_back(en);
    }
    pages.push_back(p);
    if (pages.size() >= 8) break;
  }
  JsonArrayConst b = d["bg"];
  bgs.assign(pages.size(), PageBg());
  for (size_t i = 0; i < b.size() && i < pages.size(); i++)
    if (b[i].is<JsonObjectConst>()) {
      bgs[i].own = true;
      desksaver::readBg(b[i], bgs[i].bg);
    }
}

void parse(const String& json) {
  model = {};
  JsonDocument d;
  if (json.length() && deserializeJson(d, json) == DeserializationError::Ok && (d["v"] | 0) == 2) {
    readPages(d, 4, 5, model.pages, model.bg);
    for (JsonVariantConst v : d["dock"].as<JsonArrayConst>())
      if (apps::find(v | "") && model.dock.size() < 4) model.dock.push_back(v.as<const char*>());
    for (JsonVariantConst v : d["lib"].as<JsonArrayConst>())
      if (apps::find(v | "") && model.lib.size() < 64) model.lib.push_back(v.as<const char*>());
  }
  if (model.pages.empty()) defaults();
  syncBg();
}

// "pages" (each entry in its page's order) and "bg" (up to the last page with its own background) into d.
void writePages(JsonDocument& d, const std::vector<Page>& list, const std::vector<PageBg>& bgs) {
  JsonArray pages = d["pages"].to<JsonArray>();
  for (const Page& p : list) {
    JsonArray pa = pages.add<JsonArray>();
    for (const Entry& e : p) {
      JsonArray a = pa.add<JsonArray>();
      if (e.app) {
        a.add("a"), a.add(e.id), a.add(e.x), a.add(e.y);
      } else {
        a.add("w"), a.add(e.w.kind), a.add(e.x), a.add(e.y), a.add(sizeName(e.w.size)), a.add(e.w.style),
            a.add(e.w.opt);
        if (e.w.open.length() || e.w.skin.length()) a.add(e.w.open);
        if (e.w.skin.length()) a.add(e.w.skin);  // index 8 (docs/widgets.md §2)
      }
    }
  }
  int last = -1;
  for (int i = 0; i < (int)bgs.size() && i < (int)list.size(); i++)
    if (bgs[i].own) last = i;
  if (last >= 0) {
    JsonArray bg = d["bg"].to<JsonArray>();
    for (int i = 0; i <= last; i++)
      if (bgs[i].own) desksaver::writeBg(bg.add<JsonObject>(), bgs[i].bg);
      else bg.add(nullptr);
  }
}

// s3.home: v 2, the dock, the App Library's order, the portrait pages.
String serializePort(const std::vector<Page>& list, const std::vector<PageBg>& bgs) {
  JsonDocument d;
  d["v"] = 2;
  JsonArray dk = d["dock"].to<JsonArray>();
  for (const String& s : model.dock) dk.add(s);
  if (!model.lib.empty()) {
    JsonArray lb = d["lib"].to<JsonArray>();
    for (const String& s : model.lib) lb.add(s);
  }
  writePages(d, list, bgs);
  String s;
  serializeJson(d, s);
  return s;
}

// s3.home_l: v 1, the landscape pages (the dock and the App Library's order stay in s3.home).
String serializeLand(const std::vector<Page>& list, const std::vector<PageBg>& bgs) {
  JsonDocument d;
  d["v"] = 1;
  writePages(d, list, bgs);
  String s;
  serializeJson(d, s);
  return s;
}

// The shown orientation's setting as the model would write it.
String serialize() { return land ? serializeLand(model.pages, model.bg) : serializePort(model.pages, model.bg); }

// SEED: the first landscape pages, made from the portrait ones while s3.home_l is empty or not readable. The web
// (web/app/src/os/model.ts) does exactly the same, so both write the same s3.home_l (mockups/s3-landscape.html):
//   for each portrait page P, in page order (stop once there are 8 landscape pages):
//     append a new empty landscape page (its background: P's own, or none); `first` = its index
//     items = P's widgets ordered by (y, then x), then P's apps ordered by (y, then x)
//     for each item e (every field kept but x, y):
//       for k = first .. the last page: if FIND(page k, e): append e to page k, next item
//       else, with fewer than 8 pages: append a new empty page (P's background), FIND on it (always fits), append e
//       else: drop e (an app stays in the App Library)
//   trailing empty pages go; none at all: one empty page
//   FIND(page, e): y = 0 .. 4 - rows(e), x = 0 .. 5 - cols(e) in steps of 1 (an app) or 2 (a widget): the first
//   (x, y) that overlaps nothing on the page (an app 1 x 1, S 2 x 2, M 4 x 2, L 4 x 4)
//   written by serializeLand: {"v":1,"pages":[...],"bg":[...]}, entries in the order they were appended, each as
//   s3.home writes it; "bg" up to the last page with its own background (null before it), left out when none
void seed(const std::vector<Page>& from, const std::vector<PageBg>& fromBg, std::vector<Page>& out,
          std::vector<PageBg>& outBg) {
  out.clear(), outBg.clear();
  for (size_t i = 0; i < from.size() && out.size() < 8; i++) {
    const PageBg b = i < fromBg.size() ? fromBg[i] : PageBg();
    const size_t first = out.size();
    out.push_back({}), outBg.push_back(b);
    std::vector<Entry> items;
    for (int apps = 0; apps < 2; apps++) {
      const size_t at = items.size();
      for (const Entry& e : from[i])
        if (e.app == (apps == 1)) items.push_back(e);
      std::stable_sort(items.begin() + at, items.end(),
                       [](const Entry& a, const Entry& c) { return a.y != c.y ? a.y < c.y : a.x < c.x; });
    }
    for (Entry e : items) {
      bool done = false;
      for (size_t k = first; k < out.size() && !done; k++)
        if (findIn(out[k], e, LCOLS, LROWS)) out[k].push_back(e), done = true;
      if (!done && out.size() < 8) {
        out.push_back({}), outBg.push_back(b);
        if (findIn(out.back(), e, LCOLS, LROWS)) out.back().push_back(e);
      }
    }
  }
  while (out.size() > 1 && out.back().empty()) out.pop_back(), outBg.pop_back();
  if (out.empty()) out.push_back({}), outBg.push_back(PageBg());
}

// Both settings into the model for the orientation shown (the grid set by setGeo first).
void load() {
  rawP = desk::setting("s3.home", ""), rawL = desk::setting("s3.home_l", "");
  parse(rawP);
  if (!land) {
    applied = serialize();
    return;
  }
  portPages = model.pages, portBg = model.bg;
  portSer = serializePort(portPages, portBg);
  model.pages.clear(), model.bg.clear();
  JsonDocument d;
  if (rawL.length() && deserializeJson(d, rawL) == DeserializationError::Ok && (d["v"] | 0) == 1 &&
      d["pages"].as<JsonArrayConst>().size() > 0)
    readPages(d, LCOLS, LROWS, model.pages, model.bg);
  if (model.pages.empty()) seed(portPages, portBg, model.pages, model.bg);
  syncBg();
  applied = serialize();
}

// Changed since load(): the shown pages, or (landscape) the dock or the App Library's order, kept in s3.home.
bool changed() { return serialize() != applied || (land && serializePort(portPages, portBg) != portSer); }

void save() {
  // Trailing empty pages go.
  while (model.pages.size() > 1 && model.pages.back().empty()) model.pages.pop_back();
  syncBg();
  const String s = serialize();
  applied = s;
  desk::setSetting(land ? "s3.home_l" : "s3.home", s);
  (land ? rawL : rawP) = s;
  if (land) {
    const String p = serializePort(portPages, portBg);
    if (p != portSer) portSer = rawP = p, desk::setSetting("s3.home", p);
  }
  trace::add("home saved (%s, %u pages, %u bytes)", land ? "landscape" : "portrait", (unsigned)model.pages.size(),
             (unsigned)s.length());
}

// ---------------------------------------------------------------- drawing
// Runs fn after the current event: anything that deletes the object being touched goes here.
void later(void (*fn)()) {
  lv_async_call([](void* f) { ((void (*)())f)(); }, (void*)fn);
}

uint32_t onWall() { return theme::home().fg; }

// ---------------------------------------------------------------- page backgrounds (docs/widgets.md §4)
// The page shown by tile idx (-1: Today, the App Library: they keep the look's wallpaper).
int pageOfTile(int idx) { return idx >= 1 && idx <= (int)model.pages.size() ? idx - 1 : -1; }

// A page's background as one object at 0,0 of parent: its own (desksaver::drawBg, whose picture is held while the
// object lives and shared by equal backgrounds), else the look's wallpaper.
lv_obj_t* pageBg(lv_obj_t* parent, int page, int w, int h) {
  if (page >= 0 && page < (int)model.bg.size() && model.bg[page].own) return desksaver::drawBg(parent, model.bg[page].bg, w, h);
  if (const lv_image_dsc_t* wp = theme::wallpaper(w, h)) {
    lv_obj_t* i = img(parent, wp);
    lv_obj_set_pos(i, 0, 0);
    return i;
  }
  lv_obj_t* b = box(parent, 0, 0);
  lv_obj_set_size(b, w, h);
  grad(b, theme::wall().top, theme::wall().bottom);
  return b;
}

// At rest the shown page's background lies behind the live tiles (wallImg). While the pages slide as pictures,
// only the part under them shows (wallStrip, behind the dots and the dock): one strip per tile around the shown
// one, the nearest visible. The neighbours' strips are made at idle, so their pictures are painted before a swipe
// needs them; far pages get theirs only while their swipe picture is taken.
int restFor = -1;          // the tile the at-rest background shows
bool nearMade = false;     // the neighbours' strips exist
int stripShown = -1;
lv_obj_t* strips[3];       // tiles restFor-1, restFor, restFor+1

lv_obj_t* stripOf(int idx) {
  if (PAGER_H >= os::H()) return nullptr;  // landscape: the pages cover the whole screen, dock and dots included
  lv_obj_t* s = clear(wallStrip);
  lv_obj_set_size(s, os::W(), os::H() - PAGER_H);
  lv_obj_set_pos(pageBg(s, pageOfTile(idx), os::W(), os::H()), 0, -PAGER_H);
  lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  return s;
}

void showStrip(int idx) {
  if (idx < restFor - 1 || idx > restFor + 1 || !strips[idx - restFor + 1]) idx = restFor;  // not made yet: the shown one's
  if (idx == stripShown) return;
  stripShown = idx;
  for (int k = 0; k < 3; k++)
    if (strips[k]) (restFor - 1 + k == idx) ? lv_obj_remove_flag(strips[k], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(strips[k], LV_OBJ_FLAG_HIDDEN);
}

void restBg() {
  if (!wallImg || !wallStrip) return;
  const int r = curIndex;
  // The strips still around the new page stay; new objects come before the old ones go, so a picture both use is
  // never freed and painted again.
  lv_obj_t* next[3] = {nullptr, nullptr, nullptr};
  for (int k = 0; k < 3; k++) {
    const int nk = restFor < 0 || !strips[k] ? -1 : restFor - 1 + k - (r - 1);
    if (nk >= 0 && nk < 3) next[nk] = strips[k], strips[k] = nullptr;
  }
  if (!next[1]) next[1] = stripOf(r);
  lv_obj_t* old = lv_obj_get_child_count(wallImg) ? lv_obj_get_child(wallImg, 0) : nullptr;
  pageBg(wallImg, pageOfTile(r), os::W(), os::H());
  if (old) lv_obj_delete(old);
  for (int k = 0; k < 3; k++) {
    if (strips[k]) lv_obj_delete(strips[k]);
    strips[k] = next[k];
  }
  restFor = r;
  nearMade = false;
  stripShown = -1;
  showStrip(r);
}

// A page's background changed: everything drawn again.
void rebuildBgs() {
  for (lv_obj_t*& s : strips)
    if (s) lv_obj_delete(s), s = nullptr;
  restFor = -1;
  restBg();
}

// Idle: the neighbours' strips (their pictures painted now, not while swiping).
void nearStep() {
  if (nearMade || !wallStrip || restFor < 0) return;
  nearMade = true;
  const uint32_t t0 = millis();
  for (int k : {0, 2}) {
    const int idx = restFor - 1 + k;
    if (!strips[k] && idx >= 1 && idx <= (int)pageTiles.size() + 1) strips[k] = stripOf(idx);
  }
  trace::add("home backgrounds near %d in %lu ms (%d KB held)", restFor, millis() - t0, desksaver::heldPicKB());
}

// The shown tile changed: its background now, unless the pages are still sliding (then when they stop).
void tileChanged() {
  if (curIndex != restFor && !shotsOn) restBg();
}

void enterEdit();
void exitEdit();
void appSheetFor(int page, const String& app, lv_obj_t* cell = nullptr);
void prune();
void openBgSheet(int page);
void renderPage(int pi);
void openGallery();
void openConfig(int pi, int idx);
void updateDots();

lv_obj_t* appCell(lv_obj_t* parent, const char* id, bool withLabel) {
  lv_obj_t* c = clear(parent);
  lv_obj_set_size(c, CW, CH);
  lv_obj_t* ic = apps::icon(c, id, 52);
  lv_obj_set_pos(ic, 9, 0);
  if (withLabel) {
    const apps::App* a = apps::find(id);
    lv_obj_t* l = label(c, "", 12, false, onWall());
    fitText(l, a ? a->name : id, CW - 2, 16, 12, 12, false, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, CH - 16);  // inside the cell: 58 in portrait, 56 in landscape
  }
  const int n = apps::badgeOf(id);
  if (n > 0) {
    lv_obj_t* b = badge(c, n);
    lv_obj_set_pos(b, 9 + 4 + 52 - 14, -4);
  }
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_user_data(ic, (void*)id);
  return c;
}

void openApp(lv_event_t* e) {
  if (edit) return;
  const char* id = (const char*)lv_event_get_user_data(e);
  sound::click();
  apps::open(id);
}

void onLong(lv_event_t*) {
  if (!edit) later(enterEdit);
}

// ---------------------------------------------------------------- dragging in edit mode
struct Drag {
  lv_obj_t* obj = nullptr;
  int page = -1, index = -1;   // where the entry lives in the model
  int curPage = -1;            // page it hovers now
  lv_point_t start, orig;
  bool moving = false;
  uint32_t edgeSince = 0;
  int edgeDir = 0;
  int hx = -1, hy = -1, hp = -1;  // the cell it hovers
  uint32_t hoverAt = 0;
  bool planned = false;           // the others moved to make room there
} drag;
int pendA = -1, pendB = -1;  // pages to redraw after a drop

int tileOfPage(int pi) { return pi + 1; }

void setTile(int idx, bool anim) {
  idx = constrain(idx, 0, (int)pageTiles.size() + 1);
  lv_tileview_set_tile_by_index(tiles, idx, 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
  curIndex = idx;
  tileChanged();
}

// ---- iPhone-like placing: an app dropped on another app takes its cell and the apps after it move on
bool nextCell(Entry& e) {
  const int i = e.y * COLS + e.x + 1;
  if (i >= COLS * ROWS) return false;
  e.x = i % COLS, e.y = i / COLS;
  return true;
}

// Puts app `m` at its cell on page p (m is not in p yet). An app in the way moves to the next cell in reading order,
// the app there to the next, until a free cell takes the last one; widgets stay and apps skip their cells.
// false when they do not all fit on the page. On success p is the new layout (m last, others keep their index).
bool cascade(Page& p, const Entry& m) {
  if (!m.app) return false;
  Page q = p;
  q.push_back(m);
  int mover = q.size() - 1;
  for (int guard = 0; guard < 2 * COLS * ROWS; guard++) {
    int hit = -1;
    for (int i = 0; i < (int)q.size() && hit < 0; i++)
      if (i != mover && overlaps(q[i], q[mover])) hit = i;
    if (hit < 0) {
      p = q;
      return true;
    }
    if (q[hit].app && mover != hit) mover = hit;  // the app in the way gives up its cell
    if (!nextCell(q[mover])) return false;
  }
  return false;
}

// Where the dragged app would land now, and where the others go: shown while it hovers, kept on release.
bool planDrop(const Entry& en, int cx, int cy, Page& out) {
  out = model.pages[drag.curPage];
  if (drag.curPage == drag.page) out.erase(out.begin() + drag.index);
  Entry m = en;
  m.x = cx, m.y = cy;
  return cascade(out, m);
}

void slideTo(lv_obj_t* o, int x, int y) {
  if (lv_obj_get_x(o) == x && lv_obj_get_y(o) == y) return;
  for (int k = 0; k < 2; k++) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, k ? lv_obj_get_y(o) : lv_obj_get_x(o), k ? y : x);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, k ? (lv_anim_exec_xcb_t)lv_obj_set_y : (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_start(&a);
  }
}

// Moves the page's icons to a plan (or back to the model when plan is null).
void showPlan(const Page* plan) {
  if (drag.curPage < 0 || drag.curPage >= (int)pageTiles.size()) return;
  lv_obj_t* t = liveOf(pageTiles[drag.curPage]);
  for (uint32_t c = 0; c < lv_obj_get_child_count(t); c++) {
    lv_obj_t* o = lv_obj_get_child(t, c);
    if (o == drag.obj) continue;
    const intptr_t tag = (intptr_t)lv_obj_get_user_data(o);
    const int pi = tag >> 8, i = tag & 0xff;
    if (pi != drag.curPage || i >= (int)model.pages[pi].size() || !model.pages[pi][i].app) continue;
    const Entry* e = &model.pages[pi][i];
    if (plan) {
      const int j = drag.curPage == drag.page && i > drag.index ? i - 1 : i;  // index in the plan
      if (drag.curPage == drag.page && i == drag.index) continue;
      if (j < (int)plan->size()) e = &(*plan)[j];
    }
    slideTo(o, GX + e->x * CW, GY + e->y * CH);
  }
}

void onItem(lv_event_t* e) {
  if (!edit) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  const intptr_t tag = (intptr_t)lv_obj_get_user_data(o);
  if (code == LV_EVENT_PRESSED) {
    drag = {};
    drag.obj = o;
    drag.page = drag.curPage = tag >> 8;
    drag.index = tag & 0xff;
    drag.start = p;
    drag.orig = {lv_obj_get_x(o), lv_obj_get_y(o)};
    return;
  }
  if (drag.obj != o) return;
  if (code == LV_EVENT_PRESSING) {
    const int dx = p.x - drag.start.x, dy = p.y - drag.start.y;
    if (!drag.moving && abs(dx) + abs(dy) > 8) {
      drag.moving = true;
      lv_obj_move_foreground(o);
      lv_obj_set_style_opa(o, LV_OPA_90, 0);
    }
    if (!drag.moving) return;
    lv_obj_set_pos(o, drag.orig.x + dx, drag.orig.y + dy);
    // Held still over a cell for a moment: the other apps make room there (as on an iPhone).
    const Entry& en = model.pages[drag.page][drag.index];
    if (en.app) {
      const int cx = constrain((lv_obj_get_x(o) - GX + CW / 2) / CW, 0, COLS - 1);
      const int cy = constrain((lv_obj_get_y(o) - GY + CH / 2) / CH, 0, ROWS - 1);
      if (cx != drag.hx || cy != drag.hy || drag.curPage != drag.hp) {
        if (drag.planned) showPlan(nullptr);
        drag.hx = cx, drag.hy = cy, drag.hp = drag.curPage, drag.hoverAt = millis(), drag.planned = false;
      } else if (!drag.planned && millis() - drag.hoverAt > 220) {
        Page plan;
        drag.planned = true;
        if (planDrop(en, cx, cy, plan)) showPlan(&plan);
      }
    }
    // Hold at an edge to move to the next or previous page (a new page after the last).
    // Only once the icon was really dragged: a long press that starts at the edge must not turn the page.
    const int dir = !drag.moving ? 0 : p.x > os::W() - 14 ? 1 : p.x < 14 ? -1 : 0;
    if (dir != drag.edgeDir) drag.edgeDir = dir, drag.edgeSince = millis();
    else if (dir && millis() - drag.edgeSince > 650) {
      int np = drag.curPage + dir;
      if (np < 0) return;
      if (np >= (int)model.pages.size()) {
        if (!model.pages.back().empty() && model.pages.size() < 8) {
          model.pages.push_back({});
          syncBg();
          // A new tile before the App Library.
          lv_obj_t* t = addTile(model.pages.size(), LV_DIR_HOR, PAGER_H, false);
          lv_obj_move_to_index(t, lv_obj_get_index(libTile));
          pageTiles.push_back(t);
          snaps.resize(pageTiles.size() + 2);
          lv_obj_set_pos(libTile, (model.pages.size() + 1) * os::W(), 0);
        } else {
          return;
        }
      }
      drag.curPage = np;
      lv_obj_set_parent(o, liveOf(pageTiles[np]));
      setTile(tileOfPage(np), true);
      drag.edgeSince = millis();
      drag.start = p;
      drag.orig = {dir > 0 ? 6 : os::W() - 6 - lv_obj_get_width(o), lv_obj_get_y(o)};
      lv_obj_set_pos(o, drag.orig.x, drag.orig.y);
    }
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (!drag.moving) {
      Entry& en = model.pages[drag.page][drag.index];
      drag.obj = nullptr;
      if (!en.app) openConfig(drag.page, drag.index);
      return;
    }
    Entry en = model.pages[drag.page][drag.index];
    const int px = lv_obj_get_x(o) - (en.app ? 0 : 6) - GX, py = lv_obj_get_y(o) - (en.app ? 0 : WY) - GY;
    Entry moved = en;
    moved.x = constrain((px + CW / 2) / CW, 0, COLS - en.cw());
    // Portrait: widgets snap to the left or right half. Landscape (5 wide): any column that fits.
    if (!en.app && !land) moved.x = moved.x >= 1 && en.cw() == 2 ? (moved.x >= 2 ? 2 : (px + CW) / (2 * CW) * 2) : 0;
    moved.y = constrain((py + CH / 2) / CH, 0, ROWS - en.ch());
    Page plan;
    if (en.app && planDrop(en, moved.x, moved.y, plan)) {
      // An app: it takes the cell, the apps after it move on (iPhone-like).
      if (drag.curPage != drag.page) model.pages[drag.page].erase(model.pages[drag.page].begin() + drag.index);
      model.pages[drag.curPage] = plan;
    } else {
      Page& from = model.pages[drag.page];
      from.erase(from.begin() + drag.index);
      Page& to = model.pages[drag.curPage];
      bool ok = fits(to, moved, -1);
      if (!ok) {
        Entry alt = moved;
        if (findFree(to, alt)) moved = alt, ok = true;
      }
      if (ok) {
        to.push_back(moved);
      } else {
        from.insert(from.begin() + drag.index, en);
        os::toast(N_("No room on that page"));
      }
    }
    pendA = drag.page, pendB = drag.curPage;
    drag = {};
    later([] {
      renderPage(pendA);
      if (pendB != pendA) renderPage(pendB);
    });
  }
}

// Swipes on empty space change the page while editing (the pager does not scroll then).
lv_point_t bgStart;
void onTileBg(lv_event_t* e) {
  if (!edit) return;
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) bgStart = p;
  else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    const int dx = p.x - bgStart.x;
    if (dx < -50 && curIndex <= (int)pageTiles.size()) setTile(curIndex + 1, true);
    else if (dx > 50 && curIndex > 1) setTile(curIndex - 1, true);
    updateDots();
  }
}

lv_obj_t* minusBadge(lv_obj_t* parent, int x, int y) {
  // The owner found "−" very hard to hit (2026-11-17): it was 22 px with a 2 px margin, and the part sticking out
  // of the icon was clipped, so it took clicks only on a few pixels. Now 26 px drawn with a ~44 px touch target
  // (iPhone size), and the icon lets its children overflow so the whole badge takes the tap.
  lv_obj_add_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_t* m = box(parent, 0x3a3a42, 13);
  lv_obj_set_size(m, 26, 26);
  lv_obj_set_style_border_width(m, 2, 0);
  lv_obj_set_style_border_color(m, C(0xffffff), 0);
  lv_obj_set_style_border_opa(m, LV_OPA_80, 0);
  lv_obj_set_style_shadow_width(m, 6, 0);
  lv_obj_set_style_shadow_opa(m, LV_OPA_40, 0);
  lv_obj_set_pos(m, x - 2, y - 2);
  lv_obj_t* bar = box(m, 0xffffff, 2);
  lv_obj_set_size(bar, 12, 3);
  lv_obj_center(bar);
  lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(m, 9);
  return m;
}

lv_obj_t* makeEntry(lv_obj_t* tile, int pi, int i) {
  Entry& e = model.pages[pi][i];
  lv_obj_t* o;
  if (e.app) {
    o = appCell(tile, e.id.c_str(), true);
    lv_obj_set_pos(o, GX + e.x * CW, GY + e.y * CH);
    if (!edit) lv_obj_add_event_cb(o, openApp, LV_EVENT_SHORT_CLICKED, (void*)apps::find(e.id.c_str())->id);
  } else {
    int w, h;
    wid::boxSize(e.w.size, w, h);
    o = wid::create(tile, e.w, w, h, !edit);
    lv_obj_set_pos(o, GX + e.x * CW + 6, GY + e.y * CH + WY);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_set_user_data(o, (void*)(intptr_t)((pi << 8) | i));
  lv_obj_add_event_cb(o, onLong, LV_EVENT_LONG_PRESSED, nullptr);
  if (edit) {
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
      lv_obj_add_event_cb(o, onItem, c, nullptr);
    lv_obj_t* m = minusBadge(o, e.app ? 4 : -6, e.app ? -6 : -6);
    lv_obj_add_event_cb(m, [](lv_event_t* ev) {
      lv_obj_t* item = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(ev));
      const intptr_t tag = (intptr_t)lv_obj_get_user_data(item);
      const int pi = tag >> 8, idx = tag & 0xff;
      if (pi < (int)model.pages.size() && idx < (int)model.pages[pi].size()) {
        const Entry& en = model.pages[pi][idx];
        const apps::App* a = en.app ? apps::find(en.id.c_str()) : nullptr;
        if (a && a->pkg) {  // an installed app: remove it from the page, or uninstall it (a sheet)
          appSheetFor(pi, en.id, item);
          return;
        }
        Page& p = model.pages[pi];
        p.erase(p.begin() + idx);
        pendA = pendB = pi;
        later([] { renderPage(pendA); });
      }
    }, LV_EVENT_CLICKED, nullptr);
  }
  return o;
}

void renderPage(int pi) {
  if (pi < 0 || pi >= (int)pageTiles.size()) return;
  lv_obj_t* t = liveOf(pageTiles[pi]);
  lv_obj_clean(t);
  for (int i = 0; i < (int)model.pages[pi].size(); i++) makeEntry(t, pi, i);
  dirtyTile(pi + 1);
}

void updateDots() {
  if (!dots) return;
  const int n = pageTiles.size() + 1;  // pages + library
  if ((int)lv_obj_get_child_count(dots) != n) {
    lv_obj_clean(dots);
    for (int i = 0; i < n; i++) {
      lv_obj_t* d = box(dots, onWall(), 3);
      lv_obj_set_size(d, 6, 6);
    }
  }
  for (int i = 0; i < n; i++) {
    lv_obj_t* d = lv_obj_get_child(dots, i);
    const bool on = i + 1 == curIndex;
    lv_obj_set_width(d, on ? 16 : 6);
    lv_obj_set_style_bg_opa(d, on ? LV_OPA_COVER : 115, 0);
  }
}

// ---------------------------------------------------------------- Today and the App Library
void fillToday() {
  if (!todayTile) return;
  dirtyTile(0);
  lv_obj_t* box_ = liveOf(todayTile);
  standby::reset();
  lv_obj_clean(box_);
  if (land) return standby::build(box_);  // landscape: StandBy's two stacks instead of Today
  const uint32_t fg = onWall();
  lv_obj_t* t = label(box_, N_("Today"), 24, true, fg);
  lv_obj_set_pos(t, 14, 30);
  const data::Day d = data::today();
  const data::Lunar ld = data::lunar(d);
  // i18n: the lunar date beside today's date (vi "{0} âm"); "{0}" alone on the 1st and 15th, already worded
  const String lt = i18n::fmt(ld.d == 1 || ld.d == 15 ? TC_("lunar-day", "{0}") : T_("Lunar {0}"), data::lunarText(ld));
  String ds = i18n::date(N_("{w}, {M} {d}"), d.d, d.m, d.y, d.wday) + " · " + lt;
  lv_obj_t* r = label(box_, ds.c_str(), 13, false, theme::home().dim);
  lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -14, 38);
  struct W {
    const char* kind;
    wid::Size size;
    int h;
  };
  const W list[] = {{"weather", wid::M, 138}, {"cal", wid::M, 138}, {"news", wid::L, 214},
                    {"markets", wid::M, 138}, {"lunar", wid::M, 138}, {"remind", wid::M, 138}};
  int y = 70;
  for (const W& w : list) {
    wid::Item it;
    it.kind = w.kind;
    it.size = w.size;
    lv_obj_t* o = wid::create(box_, it, os::W() - 16, w.h, true);
    lv_obj_set_pos(o, 8, y);
    y += w.h + 8;
  }
  lv_obj_t* pad = clear(box_);
  lv_obj_set_size(pad, 10, 20);
  lv_obj_set_pos(pad, 0, y);
}

// The App Library: a search field, then every app in the user's order (long press: edit mode, then drag one to
// a new place; the others make room, as on Home).
int LIB_Y = 116;  // landscape: 70 (the search field beside the title)
constexpr int LIB_ROW = 80;
std::vector<lv_obj_t*> libCells;

std::vector<String> libOrder() {
  std::vector<String> ids;
  for (const String& s : model.lib)
    if (apps::find(s.c_str()) && std::find(ids.begin(), ids.end(), s) == ids.end()) ids.push_back(s);
  for (const apps::App& a : apps::all())
    if (std::find(ids.begin(), ids.end(), String(a.id)) == ids.end()) ids.push_back(a.id);
  return ids;
}

lv_point_t libPos(int i) { return {GX + (i % COLS) * CW, LIB_Y + (i / COLS) * LIB_ROW}; }

struct LibDrag {
  lv_obj_t* obj = nullptr;
  int from = -1, hover = -1;
  lv_point_t start, orig;
  bool moving = false, planned = false;
  uint32_t hoverAt = 0;
} ld;

void fillLibrary();

void libPlan(int to) {
  for (lv_obj_t* c : libCells) {
    if (c == ld.obj) continue;
    const int i = (int)(intptr_t)lv_obj_get_user_data(c);
    int j = i;
    if (to >= 0 && ld.from < to && i > ld.from && i <= to) j = i - 1;
    if (to >= 0 && ld.from > to && i >= to && i < ld.from) j = i + 1;
    const lv_point_t p = libPos(j);
    slideTo(c, p.x, p.y);
  }
}

void onLibItem(lv_event_t* e) {
  if (!edit) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) {
    ld = LibDrag();
    ld.obj = o, ld.from = (int)(intptr_t)lv_obj_get_user_data(o), ld.start = p;
    ld.orig = {lv_obj_get_x(o), lv_obj_get_y(o)};
    return;
  }
  if (ld.obj != o) return;
  if (code == LV_EVENT_PRESSING) {
    const int dx = p.x - ld.start.x, dy = p.y - ld.start.y;
    if (!ld.moving && abs(dx) + abs(dy) > 8) {
      ld.moving = true;
      lv_obj_move_foreground(o);
      lv_obj_set_style_opa(o, LV_OPA_90, 0);
    }
    if (!ld.moving) return;
    lv_obj_set_pos(o, ld.orig.x + dx, ld.orig.y + dy);
    const int n = libCells.size();
    const int col = constrain((lv_obj_get_x(o) - GX + CW / 2) / CW, 0, COLS - 1);
    const int row = max(0, (int)(lv_obj_get_y(o) - LIB_Y + LIB_ROW / 2) / LIB_ROW);
    const int k = min(row * COLS + col, n - 1);
    if (k != ld.hover) {
      if (ld.planned) libPlan(-1);
      ld.hover = k, ld.hoverAt = millis(), ld.planned = false;
    } else if (!ld.planned && millis() - ld.hoverAt > 220) {
      ld.planned = true;
      libPlan(k);
    }
    return;
  }
  // Released: keep the new order.
  if (ld.moving && ld.hover >= 0 && ld.hover != ld.from) {
    std::vector<String> ids = libOrder();
    const String id = ids[ld.from];
    ids.erase(ids.begin() + ld.from);
    ids.insert(ids.begin() + min(ld.hover, (int)ids.size()), id);
    model.lib = ids;
  }
  ld = LibDrag();
  later(fillLibrary);
}

void fillLibrary() {
  if (!libTile) return;
  dirtyTile(pageTiles.size() + 1);
  lv_obj_t* lib = liveOf(libTile);
  lv_obj_clean(lib);
  libCells.clear();
  lv_obj_t* t = label(lib, N_("App Library"), 22, true, onWall());
  lv_obj_set_pos(t, 14, 30);
  lv_obj_t* pill = box(lib, 0xffffff, 18);
  lv_obj_set_style_bg_opa(pill, theme::home().light ? 150 : 60, 0);
  // Landscape: the field beside the title, the grid's width (the dock stands at the right).
  lv_obj_update_layout(t);
  const int px = land ? max(150, 14 + (int)lv_obj_get_width(t) + 14) : 12;
  lv_obj_set_size(pill, land ? GX + COLS * CW - px : os::W() - 24, land ? 34 : 36);
  lv_obj_set_pos(pill, px, land ? 28 : 66);
  lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* g = glyph(pill, &gl_search_16, theme::home().dim);
  lv_obj_align(g, LV_ALIGN_LEFT_MID, 12, 0);
  lv_obj_t* ph = label(pill, N_("Search apps"), 15, false, theme::home().dim);
  lv_obj_align(ph, LV_ALIGN_LEFT_MID, 36, 0);
  lv_obj_add_event_cb(pill, [](lv_event_t*) {
    if (!edit) search::open();
  }, LV_EVENT_CLICKED, nullptr);
  const std::vector<String> ids = libOrder();
  for (int i = 0; i < (int)ids.size(); i++) {
    const apps::App* a = apps::find(ids[i].c_str());
    if (!a) continue;
    lv_obj_t* c = appCell(lib, a->id, true);
    const lv_point_t p = libPos(i);
    lv_obj_set_pos(c, p.x, p.y);
    lv_obj_set_user_data(c, (void*)(intptr_t)i);
    lv_obj_add_event_cb(c, onLong, LV_EVENT_LONG_PRESSED, nullptr);
    if (edit) {
      for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
        lv_obj_add_event_cb(c, onLibItem, code, nullptr);
      if (a->pkg) {  // an installed app: "−" to uninstall it (built-in apps stay)
        lv_obj_t* m = minusBadge(c, 4, -6);
        lv_obj_add_event_cb(m, [](lv_event_t* ev) {
          lv_obj_t* cell = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(ev));
          const int i = (int)(intptr_t)lv_obj_get_user_data(cell);
          const std::vector<String> ids = libOrder();
          if (i >= 0 && i < (int)ids.size()) appSheetFor(-1, ids[i], cell);
        }, LV_EVENT_CLICKED, nullptr);
      }
    } else {
      lv_obj_add_event_cb(c, openApp, LV_EVENT_SHORT_CLICKED, (void*)a->id);
    }
    libCells.push_back(c);
  }
  lv_obj_t* pad = clear(lib);  // room to scroll the last row above the dots and the dock
  lv_obj_set_size(pad, 10, 16);
  lv_obj_set_pos(pad, 0, libPos(ids.size() + COLS - 1).y);
  // While editing the list does not scroll (a drag moves an app instead).
  edit ? lv_obj_remove_flag(lib, LV_OBJ_FLAG_SCROLLABLE) : lv_obj_add_flag(lib, LV_OBJ_FLAG_SCROLLABLE);
}

void fillDock() {
  lv_obj_clean(dockBox);
  const int n = model.dock.size();
  // Four slots across (portrait) or down (landscape); an icon is 60 with its shadow.
  const int slot = land ? LDOCK_H / 4 : (os::W() - 16) / 4;
  for (int i = 0; i < n; i++) {
    lv_obj_t* ic = apps::icon(dockBox, model.dock[i].c_str(), 52);
    const int x = land ? (LDOCK_W - 60) / 2 : i * slot + (slot - 60) / 2, y = land ? i * slot + (slot - 60) / 2 : 1;
    lv_obj_set_pos(ic, x, y);
    lv_obj_add_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ic, openApp, LV_EVENT_SHORT_CLICKED, (void*)apps::find(model.dock[i].c_str())->id);
    const int b = apps::badgeOf(model.dock[i].c_str());
    if (b > 0) {
      lv_obj_t* bd = badge(dockBox, b);
      lv_obj_set_pos(bd, x + 46, y - 5);
    }
  }
}

int dockShown = -1;  // the dock's slide-away amount last set (reset with every build)

void onScroll(lv_event_t*) {
  // Sliding towards Today hides the dock and the dots.
  const int x = lv_obj_get_scroll_x(tiles);
  const int w = os::W();
  if (shotsOn) showStrip(constrain((x + w / 2) / w, 1, (int)pageTiles.size() + 1));
  const int hide = x >= w ? 0 : (w - x) * 100 / w;
  // Only when it moves: setting the same value still redraws the dock (11 ms a frame).
  if (hide == dockShown) return;
  dockShown = hide;
  if (land) lv_obj_set_style_translate_x(dockBox, hide * 90 / 100, 0);  // off to the right
  else lv_obj_set_style_translate_y(dockBox, hide * 90 / 100, 0);
  lv_obj_set_style_translate_y(dots, hide * 90 / 100, 0);
}

lv_obj_t* addTile(int col, lv_dir_t dir, int h, bool scroll) {
  lv_obj_t* t = lv_tileview_add_tile(tiles, col, 0, dir);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(t, h);
  lv_obj_t* l = clear(t);
  lv_obj_set_size(l, os::W(), h);
  if (scroll) {
    lv_obj_add_flag(l, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(l, [](lv_event_t* e) {
      dirtyTile(lv_obj_get_index(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e))));
    }, LV_EVENT_SCROLL_END, nullptr);
  }
  lv_obj_t* shot = lv_image_create(t);
  lv_obj_add_flag(shot, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(shot, 0, 0);
  return t;
}

lv_obj_t* tileAt(int idx) { return idx >= 0 && idx < (int)lv_obj_get_child_count(tiles) ? lv_obj_get_child(tiles, idx) : nullptr; }

// One picture of a tile with the wallpaper behind (UI loop, when nobody is touching).
bool takeSnap(int idx) {
  lv_obj_t* t = tileAt(idx);
  if (!t || idx >= (int)snaps.size()) return false;
  Snap& sn = snaps[idx];
  const int w = os::W(), h = lv_obj_get_height(t);
  if (!sn.px || sn.h != h) {
    heap_caps_free(sn.px);
    sn.px = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
    sn.h = h;
    if (!sn.px) return false;
  }
  lv_draw_buf_t buf;
  lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, w * 2, sn.px, w * h * 2);
  const uint32_t t0 = millis();
  lv_obj_t* back = pageBg(t, pageOfTile(idx), w, os::H());  // painted now if far (freed again below)
  lv_obj_move_to_index(back, 0);
  const bool ok = lv_snapshot_take_to_draw_buf(t, LV_COLOR_FORMAT_RGB565, &buf) == LV_RESULT_OK;
  lv_obj_delete(back);
  sn.dsc = {};
  sn.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  sn.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  sn.dsc.header.w = w;
  sn.dsc.header.h = h;
  sn.dsc.header.stride = w * 2;
  sn.dsc.data_size = w * h * 2;
  sn.dsc.data = (const uint8_t*)sn.px;
  sn.fresh = sn.valid = ok;
  lv_image_set_src(shotOf(t), &sn.dsc);
  trace::add("home snap %d in %lu ms", idx, millis() - t0);
  return ok;
}

// Pictures of every tile, nearest first: missing or changed ones before merely old ones.
void snapStep() {
  if (!tiles || edit || shotsOn || snaps.empty()) return;
  for (int pass = 0; pass < 2; pass++)
    for (int d = 0; d < (int)snaps.size(); d++)
      for (int sgn : {1, -1}) {
        const int i = curIndex + d * sgn;
        if (i < 0 || i >= (int)snaps.size()) continue;
        if (pass == 0 ? !snaps[i].valid : !snaps[i].fresh) {
          takeSnap(i);
          return;  // one a tick: never a long stall
        }
        if (!d) break;
      }
}

void useShots(bool on) {
  if (on == shotsOn || !tiles) return;
  if (on) {
    // Only when the page and both neighbours have a fresh picture.
    for (int d : {-1, 0, 1}) {
      const int i = curIndex + d;
      if (i >= 0 && i < (int)snaps.size() && !snaps[i].valid) {
        trace::add("swipe live: tile %d has no picture", i);
        return;
      }
    }
  }
  shotsOn = on;
  trace::add("home pictures %s", on ? "on" : "off");
  for (int i = 0; i < (int)snaps.size(); i++) {
    lv_obj_t* t = tileAt(i);
    if (!t) continue;
    const bool pic = on && snaps[i].valid;
    pic ? lv_obj_add_flag(liveOf(t), LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(liveOf(t), LV_OBJ_FLAG_HIDDEN);
    pic ? lv_obj_remove_flag(shotOf(t), LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(shotOf(t), LV_OBJ_FLAG_HIDDEN);
  }
  // The pictures carry the wallpaper: only the strip under the pages is still needed.
  if (wallImg) on ? lv_obj_add_flag(wallImg, LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(wallImg, LV_OBJ_FLAG_HIDDEN);
  if (wallStrip) on ? lv_obj_remove_flag(wallStrip, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(wallStrip, LV_OBJ_FLAG_HIDDEN);
  if (on) stripShown = -1, onScroll(nullptr);
  else tileChanged();  // stopped on another page: its background
}

void buildPager() {
  keep(&tiles, lv_tileview_create(root));
  lv_obj_set_pos(tiles, 0, 0);
  lv_obj_set_size(tiles, os::W(), os::H());
  lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tiles, LV_SCROLLBAR_MODE_OFF);
  pageTiles.clear();
  for (Snap& sn : snaps) freeSnap(sn);
  snaps.assign(model.pages.size() + 2, Snap());
  shotsOn = false;
  keep(&todayTile, addTile(0, LV_DIR_RIGHT, os::H(), !land));  // Today scrolls; StandBy's stacks do
  for (int i = 0; i < (int)model.pages.size(); i++) {
    lv_obj_t* t = addTile(i + 1, LV_DIR_HOR, PAGER_H, false);
    lv_obj_add_event_cb(t, onLong, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(t, onTileBg, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(t, onTileBg, LV_EVENT_RELEASED, nullptr);
    pageTiles.push_back(t);
  }
  keep(&libTile, addTile(model.pages.size() + 1, LV_DIR_LEFT, PAGER_H, true));
  // Its list takes the touches (it scrolls): swipes there change the page while editing.
  lv_obj_add_event_cb(liveOf(libTile), onTileBg, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(liveOf(libTile), onTileBg, LV_EVENT_RELEASED, nullptr);
  for (int i = 0; i < (int)model.pages.size(); i++) renderPage(i);
  fillToday();
  fillLibrary();
  lv_obj_add_event_cb(tiles, [](lv_event_t*) {
    lv_obj_t* act = lv_tileview_get_tile_active(tiles);
    curIndex = lv_obj_get_index(act);
    updateDots();
    tileChanged();
    trace::add("home page %d", curIndex);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(tiles, onScroll, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(tiles, [](lv_event_t*) { useShots(true); }, LV_EVENT_SCROLL_BEGIN, nullptr);
  // The scroll "ends" when the finger lifts, but the page still glides to its place: pictures
  // stay until nothing moves any more.
  lv_obj_add_event_cb(tiles, [](lv_event_t*) {
    lv_timer_t* t = lv_timer_create([](lv_timer_t* t) {
      if (!tiles || (!lv_anim_get(tiles, nullptr) && !os::fingerDown())) {
        lv_timer_delete(t);
        if (tiles && !lv_anim_get(tiles, nullptr)) useShots(false);
      }
    }, 40, nullptr);
    (void)t;
  }, LV_EVENT_SCROLL_END, nullptr);
}

void buildAll(int index) {
  prune();
  lv_obj_clean(root);
  wallImg = wallStrip = nullptr;
  restFor = -1;
  strips[0] = strips[1] = strips[2] = nullptr;
  keep(&wallImg, clear(root));
  lv_obj_set_size(wallImg, os::W(), os::H());
  // The backgrounds under the pages (dots, dock) for while the pages are pictures.
  keep(&wallStrip, clear(root));
  lv_obj_set_size(wallStrip, os::W(), os::H() - PAGER_H);
  lv_obj_set_pos(wallStrip, 0, PAGER_H);
  lv_obj_add_flag(wallStrip, LV_OBJ_FLAG_HIDDEN);
  buildPager();
  keep(&dots, clear(root));
  lv_obj_set_size(dots, land ? GX + COLS * CW + 4 : os::W(), 6);  // landscape: under the grid
  lv_obj_set_pos(dots, 0, land ? LDOTS_Y : PAGER_H + 2);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dots, 6, 0);
  // While editing, the dots change the page (a tap on either side, or a swipe along them): a full page has no
  // empty space to swipe on.
  lv_obj_add_flag(dots, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(dots, 10);
  lv_obj_add_event_cb(dots, onTileBg, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(dots, onTileBg, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(dots, [](lv_event_t*) {
    if (!edit) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (abs(p.x - bgStart.x) > 20) return;  // a swipe (onTileBg)
    setTile(max(1, curIndex + (p.x > lv_obj_get_width(dots) / 2 ? 1 : -1)), true);
    updateDots();
  }, LV_EVENT_CLICKED, nullptr);
  keep(&dockBox, box(root, theme::home().dock, 22));
  lv_obj_set_style_bg_opa(dockBox, theme::home().dockOpa, 0);
  if (land) {  // standing at the right, as phones do in landscape
    lv_obj_set_size(dockBox, LDOCK_W, LDOCK_H);
    lv_obj_set_pos(dockBox, LDOCK_X, LDOCK_Y);
  } else {
    lv_obj_set_size(dockBox, os::W() - 16, DOCK_H);
    lv_obj_set_pos(dockBox, 8, os::H() - 6 - DOCK_H);
  }
  fillDock();
  setTile(constrain(index, 0, (int)pageTiles.size() + 1), false);  // and its background (tileChanged)
  updateDots();
  dockShown = -1;
  onScroll(nullptr);
}

// ---------------------------------------------------------------- edit mode
lv_timer_t* wiggle = nullptr;

void wiggleStep(lv_timer_t*) {
  static bool phase = false;
  phase = !phase;
  if (curIndex == (int)pageTiles.size() + 1) {  // the App Library
    for (size_t i = 0; i < libCells.size(); i++) {
      if (libCells[i] == ld.obj) continue;
      const bool odd = (i & 1) ^ phase;
      lv_obj_set_style_translate_x(libCells[i], odd ? 1 : -1, 0);
      lv_obj_set_style_translate_y(libCells[i], odd ? -1 : 1, 0);
    }
    return;
  }
  if (curIndex < 1 || curIndex > (int)pageTiles.size()) return;
  lv_obj_t* t = liveOf(pageTiles[curIndex - 1]);
  for (uint32_t i = 0; i < lv_obj_get_child_count(t); i++) {
    lv_obj_t* o = lv_obj_get_child(t, i);
    if (o == drag.obj) continue;
    const bool odd = (i & 1) ^ phase;
    lv_obj_set_style_translate_x(o, odd ? 1 : -1, 0);
    lv_obj_set_style_translate_y(o, odd ? -1 : 1, 0);
  }
}

void enterEdit() {
  if (edit) return;
  edit = true;
  trace::add("home edit on");
  lv_obj_remove_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < (int)pageTiles.size(); i++) renderPage(i);
  fillLibrary();
  os::setStatusHidden(true);
  keep(&editBar, clear(root));
  lv_obj_set_size(editBar, os::W(), 26);
  lv_obj_set_pos(editBar, 0, 0);
  auto pill = [](lv_obj_t* parent, const char* text, bool ok) {
    lv_obj_t* b = box(parent, ok ? col::ACC : 0xffffff, 13);
    if (!ok) lv_obj_set_style_bg_opa(b, 60, 0);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_t* l = label(b, text, 14, true, ok ? 0x1b1408 : 0xffffff);
    lv_obj_center(l);
    return b;
  };
  lv_obj_t* add = pill(editBar, "+ Widget", false);
  lv_obj_align(add, LV_ALIGN_LEFT_MID, 8, 1);
  lv_obj_add_event_cb(add, [](lv_event_t*) { openGallery(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* wall = pill(editBar, N_("Wallpaper"), false);
  lv_obj_align(wall, LV_ALIGN_CENTER, 0, 1);
  lv_obj_add_event_cb(wall, [](lv_event_t*) {
    if (pageOfTile(curIndex) < 0) return os::toast(N_("The App Library uses the theme's wallpaper"));
    openBgSheet(pageOfTile(curIndex));
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* done = pill(editBar, N_("Done"), true);
  lv_obj_align(done, LV_ALIGN_RIGHT_MID, -8, 1);
  lv_obj_add_event_cb(done, [](lv_event_t*) { exitEdit(); }, LV_EVENT_CLICKED, nullptr);
  wiggle = lv_timer_create(wiggleStep, 160, nullptr);
}

void exitEdit() {
  if (!edit) return;
  edit = false;
  if (wiggle) lv_timer_delete(wiggle), wiggle = nullptr;
  if (editBar) lv_obj_delete(editBar);
  os::setStatusHidden(false);
  trace::add("home edit off");
  const bool onLib = curIndex == (int)pageTiles.size() + 1;
  if (changed()) save();  // may drop trailing empty pages
  buildAll(onLib ? (int)model.pages.size() + 1 : min(curIndex, (int)model.pages.size()));
}

}  // namespace

// ---------------------------------------------------------------- sheets (gallery, widget settings)
namespace {
lv_obj_t *sheetBack, *sheet;
}  // namespace

void closeSheet() {
  if (sheetBack) lv_obj_delete_async(sheetBack);
  sheetBack = sheet = nullptr;
}

lv_obj_t* openSheet(int h, const char* title) {
  h = min(h, os::H() - 16);  // landscape: the screen is 320 high; a sheet's body scrolls
  closeSheet();
  setDark(false);
  keep(&sheetBack, box(lv_layer_top(), 0x000000, 0));
  lv_obj_set_style_bg_opa(sheetBack, LV_OPA_40, 0);
  lv_obj_set_size(sheetBack, os::W(), os::H());
  lv_obj_add_flag(sheetBack, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sheetBack, [](lv_event_t* e) {
    if (lv_event_get_target(e) == sheetBack) closeSheet();
  }, LV_EVENT_CLICKED, nullptr);
  keep(&sheet, box(sheetBack, 0xf4f4f7, 20));
  lv_obj_set_size(sheet, os::W(), h + 20);
  lv_obj_set_pos(sheet, 0, os::H() - h);
  lv_obj_set_style_pad_hor(sheet, 12, 0);
  lv_obj_set_style_pad_top(sheet, 10, 0);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* handle = box(sheet, 0xc3c6ce, 2);
  lv_obj_set_size(handle, 40, 4);
  lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t* t = label(sheet, title, 18, true, 0x16181d);
  lv_obj_set_pos(t, 0, 12);
  // Slide up.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sheet);
  lv_anim_set_values(&a, os::H(), os::H() - h);
  lv_anim_set_duration(&a, 200);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_y((lv_obj_t*)o, v); });
  lv_anim_start(&a);
  return sheet;
}

namespace {
void restorePalette() { theme::refresh(); }

// Gallery state.
struct Gal {
  int cat = 0;
  String kind, opt;
  int size = 0;
  lv_obj_t *grid, *sizes, *addBtn;
  bool free;  // the saver's own elements first ("Basic")
  AddFn add;
} gal;

// The tabs. Compared untranslated with the registry's categories (wid.cpp: the same English), shown translated by chips().
const char* const CATS[] = {N_("Basic"),    N_("Suggested"), N_("Custom"),  N_("Clock"),   N_("Weather"),
                            N_("Calendar"), N_("News"),      N_("Markets"), N_("Reading"), N_("Photos")};
// The saver's own elements (docs/saver.md §2): kind "el:<t>", a sample and a name.
const char* const FREE[][3] = {{"time", "12:30", N_("Time")},
                               {"date", N_("Tue, Nov 17"), N_("Date")},          // i18n: a sample date
                               {"lunar", N_("Lunar 9/10"), N_("Lunar")},         // i18n: a sample lunar date
                               {"text", "Aa", N_("Your text")},
                               {"next", N_("08:30 Meeting"), N_("Next event")},  // i18n: a sample event
                               {"alarm", "06:30", N_("Alarm")},
                               {"temp", N_("29° Sunny"), N_("Temperature")},
                               {"box", N_("frames, lines"), N_("Shapes")},
                               {"photo", N_("one or more photos"), N_("Photo")},
                               {"caption", N_("on a blurred band"), N_("Text on photo")}};

void fillGalleryGrid();

void paintSizes() {
  const wid::Kind* k = wid::kind(gal.kind);
  for (int i = 0; i < 3; i++) {
    lv_obj_t* b = lv_obj_get_child(gal.sizes, i);
    const bool can = k && (k->sizes & (1 << i));
    const bool on = can && gal.size == i;
    lv_obj_set_style_bg_color(b, C(on ? col::ACC : 0xe4e6ec), 0);
    lv_obj_set_style_opa(b, can ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C(on ? 0x1b1408 : 0x3a3f4a), 0);
  }
  if (gal.addBtn) lv_obj_set_style_opa(gal.addBtn, k || gal.kind.startsWith("el:") ? LV_OPA_COVER : LV_OPA_40, 0);
}

int galCols() { return os::landscape() ? 4 : 3; }

lv_obj_t* galTile(lv_obj_t* parent, uint32_t bg, uint32_t fg, const char* name, const char* desc, bool selected) {
  const int w = (os::W() - 24 - 7 * (galCols() - 1)) / galCols();
  lv_obj_t* t = box(parent, bg, 12);
  lv_obj_set_size(t, w, 66);
  lv_obj_set_style_pad_all(t, 8, 0);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  if (bg == 0xffffff || selected) {
    lv_obj_set_style_border_width(t, selected ? 3 : 1, 0);
    lv_obj_set_style_border_color(t, C(selected ? col::ACC : 0xe1e3e8), 0);
  }
  // A long name takes two lines; the description goes under it (they never overlap).
  lv_obj_t* a = label(t, "", 13, true, fg);
  const int px = fitText(a, name, w - 16, 18, 13, 12, true, 0);
  const int nameH = lineH(px, true) * lines(lv_label_get_text(a));
  lv_obj_t* b = label(t, "", 12, false, fg);
  fitText(b, desc, w - 16, max(14, 50 - nameH - 1), 12, 12, false, 0);
  lv_obj_set_style_opa(b, LV_OPA_80, 0);
  lv_obj_set_pos(b, 0, nameH + 1);
  return t;
}

uint32_t fgOn(uint32_t bg) {
  const int r = (bg >> 16) & 255, g = (bg >> 8) & 255, b = bg & 255;
  return (r * 299 + g * 587 + b * 114) / 1000 > 150 ? 0x16181d : 0xffffff;
}

struct GalPick {
  const char* kind;
  const char* opt;  // "new:note" for a new my-widget, or a my-widget id (dynamic)
};
std::vector<String> galOpts;  // keeps option strings alive for the tiles

void pickTile(lv_event_t* e) {
  const intptr_t i = (intptr_t)lv_event_get_user_data(e);
  const String& s = galOpts[i];
  const int bar = s.indexOf('|');
  gal.kind = s.substring(0, bar);
  gal.opt = s.substring(bar + 1);
  const wid::Kind* k = wid::kind(gal.kind);
  if (k && !(k->sizes & (1 << gal.size))) gal.size = (k->sizes & 1) ? 0 : (k->sizes & 2) ? 1 : 2;
  if (gal.opt.startsWith("new:")) {
    // A new widget of my own: write it first, then it comes back here to be added.
    const String type = gal.opt.substring(4);
    const int size = gal.size;
    closeSheet();
    restorePalette();
    mywid::create(type.c_str(), [](const char* id, int size) { addWidget("my", size, id); }, size);
    return;
  }
  later([] {
    fillGalleryGrid();
    paintSizes();
  });
}

void fillGalleryGrid() {
  lv_obj_clean(gal.grid);
  galOpts.clear();
  auto add = [](const String& kind, const String& opt, uint32_t bg, const char* name, const char* desc) {
    const bool sel = gal.kind == kind && gal.opt == opt;
    lv_obj_t* t = galTile(gal.grid, bg, fgOn(bg), name, desc, sel);
    galOpts.push_back(kind + "|" + opt);
    lv_obj_add_event_cb(t, pickTile, LV_EVENT_CLICKED, (void*)(intptr_t)(galOpts.size() - 1));
  };
  const String cat = CATS[gal.cat + !gal.free];
  if (cat == "Basic") {
    for (const auto& f : FREE) add(String("el:") + f[0], "", 0x1c2029, f[2], f[1]);
    return;
  }
  if (cat == "Custom") {
    if (!gal.free) {  // a new one opens its builder (an app screen): from Home only, not over the saver
      add("my", "new:note", 0xffe066, N_("Sticky note"), N_("text and colour"));
      add("my", "new:count", 0xe2401f, N_("Countdown"), N_("any date"));
      add("my", "new:quote", 0x16181d, N_("Quotes"), N_("a new one each day"));
      add("my", "new:list", 0xffffff, N_("List"), N_("to-dos, shopping"));
    } else if (wid::mine().empty()) {
      wrapText(label(gal.grid, "", 13, false, 0x6b7280), N_("Make your widget on Home (+ Widget, Custom), then add it here."),
               os::W() - 24, 13);
    }
    for (const wid::Mine& m : wid::mine()) {
      // i18n: the kind of a widget made on the device, small under its name
      const char* type = m.type == "count" ? N_("countdown") : m.type == "quote" ? N_("quotes") : m.type == "list" ? N_("list") : N_("note");
      String name = m.title.length() ? m.title : m.text;
      if (name.length() > 40) name = name.substring(0, 40);
      static std::vector<String> keepNames;
      keepNames.push_back(name);
      if (keepNames.size() > 64) keepNames.erase(keepNames.begin());
      add("my", m.id, m.bg, keepNames.back().c_str(), type);
    }
    return;
  }
  // "Suggested" shows every kind once; other tabs their own.
  for (const wid::Kind& k : wid::kinds()) {
    if (!strcmp(k.id, "my")) continue;
    if (cat != "Suggested" && cat != k.cat) continue;
    add(k.id, "", k.tint, k.name, k.desc);
  }
}

}  // namespace

void gallery(const char* title, bool free, AddFn add) {
  // Landscape (the saver editor): nearly the whole screen, no "Size" caption, four tiles a row.
  const bool land = os::landscape();
  const int h = land ? os::H() - 16 : min(os::H() - 60, 400);
  lv_obj_t* s = openSheet(h, title);
  gal = {};
  gal.free = free;
  gal.add = add;
  lv_obj_t* ch = chips(s, CATS + !free, sizeof(CATS) / sizeof(CATS[0]) - !free, 0, [](int i) {
    gal.cat = i;
    fillGalleryGrid();
  });
  lv_obj_set_pos(ch, 0, 42);
  lv_obj_set_width(ch, os::W() - 24);
  gal.grid = clear(s);
  lv_obj_set_pos(gal.grid, 0, 80);
  lv_obj_set_size(gal.grid, os::W() - 24, h - 80 - (land ? 56 : 98));
  lv_obj_set_flex_flow(gal.grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(gal.grid, 7, 0);
  lv_obj_add_flag(gal.grid, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(gal.grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(gal.grid, LV_SCROLLBAR_MODE_OFF);
  if (!land) lv_obj_set_pos(label(s, N_("Size"), 13, true, 0x6b7280), 2, h - 92);
  gal.sizes = clear(s);
  lv_obj_set_size(gal.sizes, os::W() - 24, 34);
  lv_obj_set_pos(gal.sizes, 0, h - (land ? 50 : 72));
  lv_obj_set_flex_flow(gal.sizes, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(gal.sizes, 8, 0);
  static const char* SZ[] = {N_("Small 2×2"), N_("Medium 4×2"), N_("Large 4×4")};
  const int bw = (os::W() - 24 - 16 - 84) / 3;
  for (int i = 0; i < 3; i++) {
    lv_obj_t* b = box(gal.sizes, 0xe4e6ec, 10);
    lv_obj_set_size(b, bw, 34);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = label(b, "", 13, true, 0x3a3f4a);
    fitText(l, SZ[i], bw - 6, 34, 13, 12, true, 0);  // a longer word wraps under the size
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      const wid::Kind* k = wid::kind(gal.kind);
      if (k && (k->sizes & (1 << i))) gal.size = i, paintSizes();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  gal.addBtn = button(gal.sizes, N_("Add"), 0x16181d, 0xffffff, [](lv_event_t*) {
    if (!gal.kind.length()) return;
    const String k = gal.kind, o = gal.opt;
    const int sz = gal.size;
    const AddFn fn = gal.add;
    closeSheet();
    restorePalette();
    fn(k.c_str(), sz, o.c_str());
  });
  lv_obj_set_height(gal.addBtn, 34);
  fillGalleryGrid();
  paintSizes();
}

namespace {
void openGallery() { gallery(N_("Add widget"), false, addWidget); }

// Widget settings: style, size, what a tap opens, its content (my widgets), remove.
struct Cfg {
  int page, index;
} cfg;

Entry* cfgEntry() {
  if (cfg.page < 0 || cfg.page >= (int)model.pages.size() || cfg.index >= (int)model.pages[cfg.page].size()) return nullptr;
  return &model.pages[cfg.page][cfg.index];
}

void openConfig(int pi, int idx);
void reopenConfig() { openConfig(cfg.page, cfg.index); }

void cfgRow(lv_obj_t* parent, const char* name, const String& value, lv_event_cb_t cb) {
  lv_obj_t* r = listRow(parent, name, value.c_str(), cb);
  lv_obj_set_style_bg_color(r, C(0xffffff), 0);
}

void openConfig(int pi, int idx) {
  cfg = {pi, idx};
  Entry* e = cfgEntry();
  if (!e) return;
  const wid::Kind* k = wid::kind(e->w.kind);
  const String title = i18n::fmt(N_("{0} widget"), k ? T_(k->name) : e->w.kind.c_str());
  const int h = min(330, os::H() - 16);  // landscape: 304, the rows scroll
  lv_obj_t* s = openSheet(h, title.c_str());
  lv_obj_t* body = clear(s);
  lv_obj_set_pos(body, 0, 44);
  lv_obj_set_size(body, os::W() - 24, h - 50);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 8, 0);
  lv_obj_add_flag(body, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  // Style, Size, the options and Card, from the registry (widopts.h); a change redraws the page and the sheet.
  static const widopts::Look LOOK = {0x16181d, 0x6b7280, 0xe4e6ec, 0x3a3f4a, col::ACC, 0x1b1408};
  widopts::build(body, e->w, LOOK, os::W() - 24, [](const wid::Item& it, bool sizeChanged) {
    Entry* e = cfgEntry();
    if (!e) return;
    Entry moved = *e;
    moved.w = it;
    if (sizeChanged) {  // the page must have room for the new size
      Page& p = model.pages[cfg.page];
      if (!fits(p, moved, cfg.index)) {
        Page rest = p;
        rest.erase(rest.begin() + cfg.index);
        if (!findFree(rest, moved)) {
          os::toast(N_("No room on this page for that size"));
          later(reopenConfig);
          return;
        }
      }
    }
    *e = moved;
    renderPage(cfg.page);
    later(reopenConfig);
  });
  if (e->w.kind == "my") {
    cfgRow(body, N_("Edit content"), "", [](lv_event_t*) {
      Entry* e = cfgEntry();
      if (!e) return;
      const String id = e->w.opt;
      closeSheet();
      restorePalette();
      mywid::edit(id.c_str(), [](const char*, int) { refresh(); });
    });
  }
  // Tap opens.
  const apps::App* target = apps::find(e->w.open.length() ? e->w.open.c_str() : (k ? k->app : ""));
  cfgRow(body, N_("Tap opens"), target ? T_(target->name) : T_("Nothing"), [](lv_event_t*) {
    Entry* e = cfgEntry();
    if (!e) return;
    const auto& all = apps::all();
    int cur = -1;
    for (int i = 0; i < (int)all.size(); i++)
      if (e->w.open == all[i].id) cur = i;
    e->w.open = all[(cur + 1) % all.size()].id;
    later(reopenConfig);
  });
  lv_obj_t* del = listRow(body, N_("Remove widget"), "", [](lv_event_t*) {
    Entry* e = cfgEntry();
    if (!e) return;
    Page& p = model.pages[cfg.page];
    p.erase(p.begin() + cfg.index);
    closeSheet();
    restorePalette();
    renderPage(cfg.page);
  }, nullptr, false);
  lv_obj_set_style_bg_color(del, C(0xffffff), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(del, 0), C(col::RED), 0);
}

// ---------------------------------------------------------------- "−" on an installed app (as on an iPhone)
// Uninstall it (with its add-ons), uninstall one add-on, or only take it off this page (it stays in the library).
// Uninstalling takes two taps on the same button; the app's data stays on the device.
struct RmApp {
  int page = -1;               // its Home page, -1 in the App Library
  lv_obj_t* cell = nullptr;    // its icon, hidden once it is being uninstalled
  String app;                  // its app id ("pet" for PawBoop)
  std::vector<String> pkgs;    // [0] its package, then its add-ons
  lv_obj_t* armed = nullptr;   // the button tapped once
} rmApp;

void dropFromPage() {
  if (rmApp.page < 0 || rmApp.page >= (int)model.pages.size()) return;
  Page& p = model.pages[rmApp.page];
  for (size_t i = 0; i < p.size(); i++)
    if (p[i].app && p[i].id == rmApp.app) {
      p.erase(p.begin() + i);
      break;
    }
  renderPage(rmApp.page);
}

void onUninstall(lv_event_t* e) {
  lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
  const int k = (int)(intptr_t)lv_event_get_user_data(e);
  if (rmApp.armed != b) {
    if (rmApp.armed) {  // another one was asked first: it goes back to its plain look
      lv_obj_set_style_bg_color(rmApp.armed, C(0xffe5e3), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(rmApp.armed, 0), C(col::RED), 0);
    }
    rmApp.armed = b;
    lv_obj_set_style_bg_color(b, C(col::RED), 0);
    lv_obj_t* l = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_color(l, C(0xffffff), 0);
    lv_label_set_text(l, T_("Tap again to remove"));
    return;
  }
  std::vector<String> drop;
  if (k == 0) drop = rmApp.pkgs;  // the app and its add-ons
  else if (k < (int)rmApp.pkgs.size()) drop.push_back(rmApp.pkgs[k]);
  String why, failed;
  for (const String& id : drop)
    if (!pkg::store::remove(id.c_str(), why)) failed = why;
  trace::add("home: uninstall %s (%d)%s%s", rmApp.app.c_str(), (int)drop.size(), failed.length() ? ": " : "",
             failed.c_str());
  if (failed.length()) os::toast(failed.c_str());
  else os::toast(k == 0 ? N_("Removing the app") : N_("Removing the add-on"));
  closeSheet();
  restorePalette();
  if (k == 0 && !failed.length()) {
    if (rmApp.cell) lv_obj_add_flag(rmApp.cell, LV_OBJ_FLAG_HIDDEN);
    dropFromPage();
  }
}

void openAppSheet() {
  const apps::App* a = apps::find(rmApp.app.c_str());
  const pkg::Package* p = a && a->pkg ? pkg::find(a->pkg) : nullptr;
  if (!p) return;
  rmApp.armed = nullptr;
  rmApp.pkgs = {p->id};
  uint64_t on[2] = {0, 0};  // bytes in the internal flash, on the card
  on[p->vol == pkg::SD] += p->bytes;
  std::vector<String> addNames;
  for (const pkg::Package& q : pkg::list())
    if (q.addonOf == p->id) {
      rmApp.pkgs.push_back(q.id);
      addNames.push_back(q.name + " · " + pkg::sizeText(q.bytes));
      on[q.vol == pkg::SD] += q.bytes;
    }
  bool busy = false;
  for (const String& id : rmApp.pkgs) busy |= pkg::store::stateOf(id.c_str()) != pkg::store::IDLE;
  const int W = os::W() - 24;
  const int nBtn = busy ? 1 : (int)rmApp.pkgs.size() + 1;
  const int h = min(44 + 26 + nBtn * 48 + 58, os::H() - 16);  // landscape with add-ons: the buttons scroll
  lv_obj_t* s = openSheet(h, a->name);
  lv_obj_t* body = clear(s);
  lv_obj_set_pos(body, 0, 44);
  lv_obj_set_size(body, W, h - 50);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 8, 0);
  const String sub = on[0] && on[1] ? i18n::fmt(N_("{0} on the device · {1} on the card"), pkg::sizeText(on[0]),
                                                pkg::sizeText(on[1]))
                     : i18n::fmt(on[1] ? N_("{0} · on the memory card") : N_("{0} · on the device"), pkg::sizeText(on[0] + on[1]));
  lv_obj_t* sl = label(body, "", 13, false, 0x6b7280);
  fitText(sl, sub.c_str(), W, 18, 13, 11);
  if (busy) {
    lv_obj_t* l = label(body, "", 14, false, 0x3a3f4a);
    wrapText(l, N_("This app is being installed, removed or moved. Please wait until that is done."), W, 14);
  } else {
    lv_obj_t* all = button(body, rmApp.pkgs.size() > 1 ? N_("Remove the app and add-ons") : N_("Remove the app"), 0xffe5e3,
                           col::RED, onUninstall, (void*)(intptr_t)0);
    lv_obj_set_width(all, lv_pct(100));
    for (size_t i = 1; i < rmApp.pkgs.size(); i++) {
      lv_obj_t* one = button(body, "", 0xffe5e3, col::RED, onUninstall, (void*)(intptr_t)i);
      lv_obj_set_width(one, lv_pct(100));
      fitText(lv_obj_get_child(one, 0), i18n::fmt(N_("Remove {0}"), addNames[i - 1]).c_str(), W - 32, 20, 15, 12, true);
    }
  }
  lv_obj_t* off = button(body, rmApp.page >= 0 ? N_("Remove from Home") : N_("Cancel"), 0xffffff, 0x16181d, [](lv_event_t*) {
    closeSheet();
    restorePalette();
    dropFromPage();  // nothing in the App Library (page -1)
  });
  lv_obj_set_width(off, lv_pct(100));
  lv_obj_t* hl = label(body, "", 12, false, 0x6b7280);
  wrapText(hl, N_("Removing keeps the app's data (levels, coins, notes) on the device; reinstall it to carry on."), W, 12);
}

void appSheetFor(int page, const String& app, lv_obj_t* cell) {
  rmApp.page = page;
  rmApp.app = app;
  rmApp.cell = nullptr;
  if (cell) keep(&rmApp.cell, cell);
  later(openAppSheet);
}

// An app uninstalled while its icon is on a page, in the dock or the library: its place empties.
void prune() {
  for (Page& p : portPages)  // landscape: the portrait pages too (s3.home is written again with a new dock)
    p.erase(std::remove_if(p.begin(), p.end(), [](const Entry& e) { return e.app && !apps::find(e.id.c_str()); }),
            p.end());
  for (Page& p : model.pages)
    p.erase(std::remove_if(p.begin(), p.end(), [](const Entry& e) { return e.app && !apps::find(e.id.c_str()); }),
            p.end());
  auto gone = [](const String& s) { return !apps::find(s.c_str()); };
  model.dock.erase(std::remove_if(model.dock.begin(), model.dock.end(), gone), model.dock.end());
  model.lib.erase(std::remove_if(model.lib.begin(), model.lib.end(), gone), model.lib.end());
}

// ---------------------------------------------------------------- Hình nền trang (the web's WallPicker)
const char* const COLOURS[] = {"000000", "1f2631", "0f172a", "1e3a8a", "0a84ff", "0f766e", "34c759", "ffcc00",
                               "ff9500", "d7261e", "ff2d55", "af52de", "5856d6", "f5f0e6", "ffffff"};
const uint32_t GRADS[][2] = {{0x1e3a8a, 0x0f172a}, {0xff9a5a, 0x6c2a6d}, {0x0fa59a, 0x0a3550}, {0xffd166, 0xef476f},
                             {0xa1c349, 0x1b4332}, {0xf5f0e6, 0xc9b79c}, {0x5856d6, 0xaf52de}, {0x2b2f3a, 0x000000}};
int bgPage = -1;
lv_obj_t* bgBody = nullptr;
PageBg bgCur;                       // the background the sheet edits
void (*bgSet)(const PageBg&) = nullptr;  // its owner stores and draws it (Home's page, the saver editor's page)
bool bgSlides = false;              // the saver's "Slide show" photo tile (id 0: s3.photos, one a minute)
void fillBgSheet();

struct Preview {
  lv_image_dsc_t* d;
  ~Preview() {
    lv_image_cache_drop(d);
    theme::freePreview(d);
  }
};

lv_obj_t* tile(lv_obj_t* parent, int w, int h, bool on, lv_event_cb_t cb, intptr_t data) {
  lv_obj_t* t = box(parent, 0x2a3240, 9);
  lv_obj_set_size(t, w, h);
  lv_obj_set_style_clip_corner(t, true, 0);
  if (on) {
    lv_obj_set_style_outline_width(t, 3, 0);
    lv_obj_set_style_outline_pad(t, 2, 0);
    lv_obj_set_style_outline_color(t, C(col::ACC), 0);
  }
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(t, 3);
  lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, (void*)data);
  return t;
}

void wallTile(lv_obj_t* parent, const theme::Wall& w, bool on, lv_event_cb_t cb, intptr_t data) {
  lv_obj_t* t = tile(parent, 44, 66, on, cb, data);
  if (lv_image_dsc_t* d = theme::makePreview(w, 44, 66)) {
    lv_obj_t* i = lv_image_create(t);
    lv_image_set_src(i, d);
    own(i, new Preview{d});
  }
}

lv_obj_t* tileRow(lv_obj_t* parent, const char* caption) {
  lv_obj_t* l = label(parent, caption, 13, true, 0x6b7280);
  lv_obj_set_style_pad_top(l, 4, 0);
  lv_obj_t* r = clear(parent);
  lv_obj_set_size(r, os::W() - 24, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(r, 8, 0);
  lv_obj_set_style_pad_all(r, 4, 0);  // room for the selection ring
  return r;
}

// Home's page: stored and drawn at once.
void setHomeBg(const PageBg& b) {
  if (bgPage < 0 || bgPage >= (int)model.bg.size()) return;
  model.bg[bgPage] = b;
  dirtyTile(bgPage + 1);
  rebuildBgs();
}

// A new background (own false: the look's); the sheet shows the choice after this event.
void setBg(const PageBg& b) {
  bgCur = b;
  if (bgSet) bgSet(b);
  later(fillBgSheet);
}

desksaver::Bg keepDim(desksaver::Bg b) {
  // As the web: walls and photos keep the dim already chosen.
  if (bgCur.own && (bgCur.bg.t == "wall" || bgCur.bg.t == "photo")) b.dim = bgCur.bg.dim;
  return b;
}

void pickPhotoBg(lv_event_t* e) {
  desksaver::Bg nb;
  nb.t = "photo";
  nb.id = (intptr_t)lv_event_get_user_data(e);
  nb.dim = bgCur.own && (bgCur.bg.t == "wall" || bgCur.bg.t == "photo") && bgCur.bg.dim ? bgCur.bg.dim : 20;
  setBg({true, nb});
}

void fillBgSheet() {
  if (!bgBody) return;
  const int32_t sy = lv_obj_get_scroll_y(bgBody);
  lv_obj_clean(bgBody);
  const PageBg& cur = bgCur;
  const desksaver::Bg& b = cur.bg;
  lv_obj_t* r = tileRow(bgBody, N_("From the theme"));
  wallTile(r, theme::wall(), !cur.own, [](lv_event_t*) { setBg(PageBg()); }, 0);
  r = tileRow(bgBody, N_("Built-in wallpapers"));
  for (int i = 0; i < theme::WALL_COUNT; i++)
    wallTile(r, theme::WALLS[i], cur.own && b.t == "wall" && b.w == theme::WALLS[i].id, [](lv_event_t* e) {
      desksaver::Bg nb;
      nb.t = "wall";
      nb.w = theme::WALLS[(intptr_t)lv_event_get_user_data(e)].id;
      setBg({true, keepDim(nb)});
    }, i);
  const auto ids = data::codesOf(desk::setting("s3.photos", ""), "");
  r = tileRow(bgBody, N_("Photos on the device"));
  if (ids.empty()) label(r, N_("Add photos on the EspOS web, under Photos"), 13, false, 0x6b7280);
  else if (bgSlides)  // all of them, one a minute
    lv_obj_center(label(tile(r, 44, 66, cur.own && b.t == "photo" && !b.id, pickPhotoBg, 0), N_("Slide\nshow"), 12, true, 0xffffff));  // i18n: all photos in turn, two short lines in a 44 px tile
  for (const String& id : ids) {
    lv_obj_t* t = tile(r, 44, 66, cur.own && b.t == "photo" && b.id == id.toInt(), pickPhotoBg, id.toInt());
    if (!desksaver::photoImg(t, id.toInt(), 44, 66)) lv_obj_center(glyph(t, &gl_photo_16, 0x9aa3ad));
  }
  r = tileRow(bgBody, N_("Solid colour"));
  for (int i = 0; i < 15; i++) {
    const uint32_t c = strtoul(COLOURS[i], nullptr, 16);
    lv_obj_t* t = tile(r, 26, 26, cur.own && b.t == "color" && b.c == c, [](lv_event_t* e) {
      desksaver::Bg nb;
      nb.t = "color";
      nb.c = strtoul(COLOURS[(intptr_t)lv_event_get_user_data(e)], nullptr, 16);
      setBg({true, nb});
    }, i);
    lv_obj_set_style_radius(t, 13, 0);
    lv_obj_set_style_bg_color(t, C(c), 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, C(0xd9dbe1), 0);
  }
  r = tileRow(bgBody, N_("Gradient"));
  for (int i = 0; i < 8; i++) {
    lv_obj_t* t = tile(r, 44, 44, cur.own && b.t == "grad" && b.c == GRADS[i][0] && b.c2 == GRADS[i][1], [](lv_event_t* e) {
      desksaver::Bg nb;
      nb.t = "grad";
      nb.c = GRADS[(intptr_t)lv_event_get_user_data(e)][0];
      nb.c2 = GRADS[(intptr_t)lv_event_get_user_data(e)][1];
      setBg({true, nb});
    }, i);
    grad(t, GRADS[i][0], GRADS[i][1]);
  }
  if (cur.own && (b.t == "wall" || b.t == "photo")) {
    lv_obj_t* l = label(bgBody, i18n::fmt(N_("Darken for easier reading: {0}%"), String(b.dim)).c_str(), 13, true, 0x6b7280);
    lv_obj_set_style_pad_top(l, 4, 0);
    lv_obj_t* sl = lv_slider_create(bgBody);
    lv_obj_set_width(sl, os::W() - 24 - 24);
    lv_obj_set_style_margin_hor(sl, 12, 0);
    lv_slider_set_range(sl, 0, 18);
    lv_slider_set_value(sl, b.dim / 5, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, C(0xd9dbe1), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, C(col::ACC), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, C(col::ACC), LV_PART_KNOB);
    lv_obj_add_event_cb(sl, [](lv_event_t* e) {
      PageBg nb = bgCur;
      nb.bg.dim = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)) * 5;
      if (nb.bg.dim != bgCur.bg.dim) setBg(nb);
    }, LV_EVENT_RELEASED, nullptr);
  }
  if (bgSet == setHomeBg && model.pages.size() > 1) {
    lv_obj_t* all = listRow(bgBody,
                            i18n::plural(model.pages.size(),
                                         NP_("Use this wallpaper on the {n} page|Use this wallpaper on all {n} pages"))
                                .c_str(),
                            "",
                            [](lv_event_t*) {
                              const PageBg b = model.bg[bgPage];
                              for (PageBg& x : model.bg) x = b;
                              for (int i = 0; i < (int)model.pages.size(); i++) dirtyTile(i + 1);
                              rebuildBgs();
                              os::toast(N_("Used on every page"));
                            }, nullptr, false);
    lv_obj_set_style_bg_color(all, C(0xffffff), 0);
  }
  lv_obj_update_layout(bgBody);
  lv_obj_scroll_to_y(bgBody, sy, LV_ANIM_OFF);
}

}  // namespace

void bgSheet(const char* title, const PageBg& cur, void (*set)(const PageBg&), bool slideshow) {
  bgCur = cur;
  bgSet = set;
  bgSlides = slideshow;
  const int h = os::landscape() ? os::H() - 16 : 340;
  lv_obj_t* s = openSheet(h, title);
  keep(&bgBody, clear(s));
  lv_obj_set_pos(bgBody, 0, 44);
  lv_obj_set_size(bgBody, os::W() - 24, h - 50);
  lv_obj_set_flex_flow(bgBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(bgBody, 4, 0);
  lv_obj_set_style_pad_bottom(bgBody, 16, 0);
  lv_obj_add_flag(bgBody, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(bgBody, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(bgBody, LV_SCROLLBAR_MODE_OFF);
  fillBgSheet();
}

namespace {
void openBgSheet(int page) {
  if (page < 0 || page >= (int)model.bg.size()) return;
  bgPage = page;
  bgSheet(i18n::fmt(N_("Page {0} wallpaper"), String(page + 1)).c_str(), model.bg[page], setHomeBg, false);
}
}  // namespace
// ================================================================== public
void reset() {
  // Everything here is about to be deleted (rotation, new look): no timer may outlive it.
  if (wiggle) lv_timer_delete(wiggle);
  wiggle = nullptr;
  edit = false;
  drag = {};
  pageTiles.clear();
  for (Snap& sn : snaps) freeSnap(sn);
  snaps.clear();
  shotsOn = false;
  root = tiles = dots = dockBox = todayTile = libTile = editBar = wallImg = wallStrip = nullptr;
  strips[0] = strips[1] = strips[2] = nullptr;
  restFor = -1;
  sheetBack = sheet = nullptr;
  standby::reset();
}

void build(lv_obj_t* scr) {
  reset();
  root = scr;
  setGeo(os::landscape());
  LIB_Y = land ? 70 : 116;
  load();
  buildAll(1);
}

void refresh() {
  if (!root || edit) return;
  buildAll(curIndex);
  trace::add("home refreshed (page %d)", curIndex);
}

void tick() {
  if (land) standby::tick();
  if (wid::tick()) {
    for (Snap& sn : snaps) sn.fresh = false;  // a clock changed: still fine for a swipe
  }
  if (!os::fingerDown() && os::idleMs() > 400 && os::atHome() && !os::saverOn() && !shotsOn) {
    if (!nearMade && !edit) nearStep();
    else snapStep();
  }
  static uint32_t lastBadges = 0;
  if (millis() - lastBadges > 10000 && !edit && dockBox) {
    lastBadges = millis();
    // Badges change with the data; the dock is cheap to redraw.
    fillDock();
  }
}

bool editing() { return edit; }

bool searchAt(int32_t x, int32_t y) {
  // Over a page, not on the dock (landscape: at the right; portrait: at the bottom).
  return root && !edit && !sheet && curIndex >= 1 && curIndex <= (int)pageTiles.size() && y > os::SB_H + 4 &&
         (land ? x < LDOCK_X && y < os::H() - 12 : y < os::H() - DOCK_H - 24);
}

void openSearch() { search::open(); }

bool settingsChanged() {
  // s3.home (portrait pages, dock, App Library) or, in landscape, s3.home_l changed on the web.
  if (edit || !root) return false;
  if (desk::setting("s3.home", "") == rawP && (!land || desk::setting("s3.home_l", "") == rawL)) return false;
  load();
  return true;
}

void toFirstPage() {
  if (edit) return exitEdit();
  if (tiles) setTile(1, true);
}

void bench() {
  // Dev console: frame times of Home, live and as pictures, and of each part.
  if (!tiles) return;
  auto run = [](const char* what) {
    lv_refr_now(nullptr);
    const uint32_t t0 = micros();
    for (int i = 0; i < 5; i++) {
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(nullptr);
    }
    Serial.printf("HOMEBENCH %s: %lu us\n", what, (micros() - t0) / 5);
  };
  run("live");
  for (int i = 0; i < (int)snaps.size(); i++)
    if (!snaps[i].valid) takeSnap(i);
  useShots(true);
  run("pictures");
  lv_obj_add_flag(dockBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
  run("pictures, no dock");
  if (wallStrip) lv_obj_add_flag(wallStrip, LV_OBJ_FLAG_HIDDEN);
  run("pictures only");
  lv_obj_add_flag(tiles, LV_OBJ_FLAG_HIDDEN);
  run("empty");
  lv_obj_remove_flag(tiles, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(dockBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(dots, LV_OBJ_FLAG_HIDDEN);
  useShots(false);
  run("live again");
}

void releasePictures() {
  if (shotsOn) return;
  unsigned kb = 0;
  for (Snap& sn : snaps)
    if (sn.px) {
      lv_image_cache_drop(&sn.dsc);
      kb += sn.dsc.data_size / 1024;
      freeSnap(sn);
    }
  if (kb) trace::add("home pictures let go (%u KB)", kb);
}

void bgPhotos(std::vector<int>& port, std::vector<int>& landscape) {
  // false: not a layout load() reads (unreadable, another version, no pages): no backgrounds of its own.
  auto ids = [](const char* key, int v, std::vector<int>& out) {
    JsonDocument d;
    const String json = desk::setting(key, "");
    if (!json.length() || deserializeJson(d, json) != DeserializationError::Ok || (d["v"] | 0) != v ||
        !d["pages"].as<JsonArrayConst>().size())
      return false;
    for (JsonVariantConst b : d["bg"].as<JsonArrayConst>())
      if (!strcmp(b["t"] | "", "photo") && (b["id"] | 0) > 0 && out.size() < 8) out.push_back(b["id"] | 0);
    return true;
  };
  port.clear(), landscape.clear();
  ids("s3.home", 2, port);
  if (!ids("s3.home_l", 1, landscape)) landscape = port;  // landscape seeded from portrait (load())
}

void addWidget(const char* kind, int size, const char* opt) {
  Entry e = widEntry(kind, 0, 0, (wid::Size)size, "", opt);
  int pi = constrain(curIndex - 1, 0, (int)model.pages.size() - 1);
  if (!findFree(model.pages[pi], e)) {
    pi = -1;
    for (int i = 0; i < (int)model.pages.size(); i++)
      if (findFree(model.pages[i], e)) {
        pi = i;
        break;
      }
    if (pi < 0) {
      if (model.pages.size() >= 8) return os::toast(N_("8 pages is the most"));
      model.pages.push_back({});
      syncBg();
      pi = model.pages.size() - 1;
      findFree(model.pages[pi], e);
    }
  }
  model.pages[pi].push_back(e);
  if (!root) {  // Home not built: saved, shown when it is
    save();
    os::toast(N_("Added to Home"));
    return;
  }
  if (!edit) {
    save();
    buildAll(pi + 1);
  } else if (pi < (int)pageTiles.size()) {
    renderPage(pi);
    setTile(pi + 1, true);
  } else {
    // A new page: build the pager again and stay in the edit mode.
    edit = false;
    if (wiggle) lv_timer_delete(wiggle), wiggle = nullptr;
    buildAll(pi + 1);
    enterEdit();
  }
  os::toast(N_("Widget added"));
}

}  // namespace home
