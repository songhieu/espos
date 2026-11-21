// ESPOS_ONBOARD (off by default until the owner approves the first-boot setup, onboard.h): with it, the file below;
// without it, the file as it was before the setup (the #else part; drop it once the setup ships).
#if ESPOS_ONBOARD
// "Choose a dog" (chooser.h): a card a dog, two columns (three or more when the screen is turned), the chosen dog
// with an orange ring. Installed: tap = this dog. Not installed: tap = download it (the store's installer; on the
// card when one is in), its progress on its card; once in, it becomes the dog.
#pragma GCC optimize("Os")
#include "chooser.h"

#include <Arduino.h>

#include "app.h"
#include "apps.h"
#include "dogs.h"
#include "i18n.h"
#include "lock.h"
#include "os.h"
#include "packs.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "trace.h"
#include "uikit.h"

namespace pet {
namespace chooser {
namespace {

using namespace kit;
namespace st = pkg::store;

constexpr uint32_t ORANGE = 0xff9a1f, GREEN = 0x1f9d4a;
constexpr int ICON = 60;

// What a card shows that changes while the screen is open (updated in place: rebuilding the ten cards for every
// progress step or arriving picture stalled the board while a dog downloaded).
struct Card {
  lv_obj_t *card = nullptr, *icon = nullptr, *state = nullptr, *track = nullptr, *fill = nullptr;
  bool drawnIcon = false;  // the placeholder until the store's picture arrives
  int cw = 0;
};
struct Screen {
  lv_obj_t *scr = nullptr, *body = nullptr, *grid = nullptr;
  uint32_t seenPkg = 0, seenStore = 0, seenCat = 0, seenDog = 0;
  Card cards[dogs::COUNT];
  String pending;     // the dog being downloaded for the user: chosen once it is in
};
Screen* S = nullptr;

String sizeOf(const char* key) {
  const st::Entry* e = st::entry(dogs::pkgId(key).c_str());
  return e ? pkg::sizeText(st::partsBytes(*e, st::defaultParts(*e))) : String();
}

String descOf(const char* key) {
  const String id = dogs::pkgId(key);
  if (const pkg::Package* p = pkg::find(id.c_str())) {
    if (const st::Entry* e = st::entry(id.c_str())) return e->desc;  // the language shown
    return p->desc;
  }
  const st::Entry* e = st::entry(id.c_str());
  return e ? e->desc : String();
}

void onCard(lv_event_t* e) {
  if (!S) return;
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  const dogs::Info& d = dogs::ALL[i];
  const int r = pick(i);
  if (r == 1) {
    S->pending = d.key;
    return;
  }
  if (r) return;
  if (dogs::chosen() == &d && !strcmp(pk::dogKey(), d.key)) {  // the dog already: back to the game
    if (app::isOpen()) return os::back();
    os::back();
    return apps::open("pet");
  }
  trace::add("pet: chose %s", d.key);
  switchDog(d.key, true);  // closes this screen too, then the game with the new dog
}

}  // namespace

int pick(int i) {
  const dogs::Info& d = dogs::ALL[i];
  const String id = dogs::pkgId(d.key);
  const pkg::Package* p = pkg::find(id.c_str());
  if (lock::locked(id.c_str())) return lock::sheet(d.name, lock::Look::Pet), -1;  // paid, after the trial (lock.h)
  if (p && !p->available) return os::toast(N_("This dog is on the memory card: put the card back in")), -1;
  if (p) return 0;
  if (st::stateOf(id.c_str()) != st::IDLE) return 1;  // already coming
  const st::Entry* en = st::entry(id.c_str());
  if (!en) {
    st::refresh(true);
    return os::toast(N_("Getting the dogs from the Store, try again in a moment")), -1;
  }
  const pkg::Vol v = pkg::volume(pkg::SD).present ? pkg::SD : pkg::FLASH;
  String why;
  if (!st::install(*en, st::defaultParts(*en), v, why)) return os::toast(why.c_str()), -1;
  trace::add("pet: downloading %s", d.key);
  return 1;
}

String desc(int i) { return descOf(dogs::ALL[i].key); }

// A card's state line: the dog now / tap to choose / on the card that is out / the download and its percent.
void state(int i, String& what, uint32_t& col, int& pct) {
  const Pal& p = pal();
  const dogs::Info& d = dogs::ALL[i];
  const String id = dogs::pkgId(d.key);
  const pkg::Package* inst = pkg::find(id.c_str());
  const st::State state = st::stateOf(id.c_str());
  pct = -1;
  col = p.dim;
  if (state != st::IDLE) {
    const st::Status ss = st::status();
    what = st::stateText(state);
    if (ss.id == id && (state == st::DOWNLOAD || state == st::WRITE)) what += " " + String(ss.pct) + "%", pct = ss.pct;
    col = ORANGE;
  } else if (lock::locked(id.c_str())) {  // paid, after the trial: on the device still, or "Buy" with its size
    const String sz = sizeOf(d.key);
    what = inst ? String(T_("Locked now")) : sz.length() ? i18n::fmt(N_("Buy · {0}"), sz) : String(TC_("store", "Buy"));
    col = inst ? 0xc2410c : p.link;
  } else if (dogs::chosen() == &d && inst) {
    what = T_("Your dog"), col = GREEN;
  } else if (inst && !inst->available) {
    what = T_("Card out");
  } else if (inst) {
    what = T_("Tap to choose"), col = p.link;
  } else {
    const String sz = sizeOf(d.key);
    const bool tryIt = lock::trial() && !lock::freeAnyway(id.c_str());  // the trial gives it: "Try · 6.4 MB"
    what = sz.length() ? i18n::fmt(tryIt ? N_("Try · {0}") : N_("Download · {0}"), sz) : String(T_("In the Store"));
    col = p.link;
  }
}

namespace {

void showState(int i) {
  Card& k = S->cards[i];
  String what;
  uint32_t col;
  int pct;
  state(i, what, col, pct);
  lv_obj_set_style_text_color(k.state, C(col), 0);
  fitText(k.state, what.c_str(), k.cw - 16, 18, 13, 11, true, 0);
  lv_obj_align(k.state, LV_ALIGN_TOP_MID, 0, ICON + 40);
  if (pct < 0) {
    lv_obj_add_flag(k.track, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(k.track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(k.fill, lv_pct(max(2, pct)), 6);
  }
}

void card(lv_obj_t* grid, int i, int cw) {
  const Pal& p = pal();
  const dogs::Info& d = dogs::ALL[i];
  const String id = dogs::pkgId(d.key);
  const pkg::Package* inst = pkg::find(id.c_str());
  const dogs::Info* cur = dogs::chosen();
  const bool chosen = cur == &d && inst;

  lv_obj_t* c = box(grid, p.cell, 18);
  lv_obj_set_size(c, cw, 216);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
  if (chosen) {
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_set_style_border_color(c, C(ORANGE), 0);
  }
  lv_obj_add_event_cb(c, onCard, LV_EVENT_CLICKED, (void*)(intptr_t)i);

  // The phone game's portrait (the package's icon.jpg, or the store's copy until it is installed).
  Card& k = S->cards[i];
  k = Card();
  k.card = c, k.cw = cw;
  lv_obj_t* ic = nullptr;
  if (inst) {
    ic = apps::packageIcon(c, id.c_str(), ICON);
  } else if (const lv_image_dsc_t* d2 = app_store::catalogIcon(id.c_str(), ICON)) {
    ic = img(c, d2);
  } else {
    ic = apps::drawnIcon(c, 0xffcf7a, 0xe8892c, "play", ICON), k.drawnIcon = true;
  }
  k.icon = ic;
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 10);
  if (inst && !inst->available) lv_obj_set_style_opa(ic, LV_OPA_40, 0);
  if (lock::locked(id.c_str())) {  // a lock on the portrait's corner (lock.h)
    lv_obj_update_layout(c);
    lv_obj_align_to(lock::badge(c, 22), ic, LV_ALIGN_BOTTOM_RIGHT, 6, 6);
  }

  lv_obj_t* n = label(c, d.name, 17, true, p.fg);  // a name: as it is
  lv_obj_align(n, LV_ALIGN_TOP_MID, 0, ICON + 16);
  // Its state line and the download's bar (hidden unless downloading): showState() keeps them current.
  k.state = label(c, "", 13, true, p.dim);
  lv_obj_set_style_text_align(k.state, LV_TEXT_ALIGN_CENTER, 0);
  k.track = box(c, p.cell2, 3);
  lv_obj_set_size(k.track, cw - 32, 6);
  lv_obj_align(k.track, LV_ALIGN_TOP_MID, 0, ICON + 62);
  k.fill = box(k.track, ORANGE, 3);
  lv_obj_set_size(k.fill, 0, 6);
  showState(i);
  // Who it is (the dog's character, from the store).
  const String desc = descOf(d.key);
  if (desc.length()) {
    lv_obj_t* t = label(c, "", 12, false, p.dim);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    fitText(t, desc.c_str(), cw - 16, 216 - ICON - 80, 12, 11, false, 1);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, ICON + 72);
  }
}

void build() {
  if (!S) return;
  const int32_t sy = S->body ? lv_obj_get_scroll_y(S->body) : 0;
  lv_obj_clean(S->body);
  S->seenPkg = pkg::version(), S->seenStore = st::version(), S->seenCat = st::catalogVersion();
  S->seenDog = dogs::version();
  hint(S->body, N_("Your dog is your pet, runs in Coin Run and is the voice of the assistant. Each dog has its own character."));
  const int cols = max(2, (os::W() - 16) / 150);
  const int cw = (os::W() - 24 - 8 * (cols - 1)) / cols;
  lv_obj_t* grid = clear(S->body);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, 8, 0);
  lv_obj_set_style_pad_row(grid, 8, 0);
  for (int i = 0; i < dogs::COUNT; i++) card(grid, i, cw);
  if (!pkg::volume(pkg::SD).present) hint(S->body, N_("A dog takes about 7 MB: put a memory card in to keep several."));
  lv_obj_update_layout(S->body);
  lv_obj_scroll_to_y(S->body, sy, LV_ANIM_OFF);
}

void watch(lv_timer_t*) {
  if (!S) return;
  // The dog downloaded for the user is in: it becomes the dog (and the game opens with it).
  if (S->pending.length() && dogs::installed(S->pending.c_str()) && st::stateOf(dogs::pkgId(S->pending.c_str()).c_str()) == st::IDLE) {
    const String k = S->pending;
    S->pending = "";
    return switchDog(k.c_str(), true);
  }
  static uint32_t seenLock = 0;
  if (seenLock != lock::version()) return seenLock = lock::version(), build();
  if (S->seenPkg != pkg::version() || S->seenStore != st::version() || S->seenCat != st::catalogVersion() ||
      S->seenDog != dogs::version())
    return build();  // a dog in or out, a job started or ended, the catalog, the chosen dog
  for (int i = 0; i < dogs::COUNT; i++) {
    Card& k = S->cards[i];
    if (!k.card) continue;
    if (k.drawnIcon) {  // the store's picture came: in place of the placeholder
      if (const lv_image_dsc_t* d = app_store::catalogIcon(dogs::pkgId(dogs::ALL[i].key).c_str(), ICON)) {
        lv_obj_delete(k.icon);
        k.icon = img(k.card, d), k.drawnIcon = false;
        lv_obj_align(k.icon, LV_ALIGN_TOP_MID, 0, 10);
      }
    }
    if (st::busy()) showState(i);  // the download's percent
  }
}

}  // namespace

void open() {
  if (S && S->scr && lv_obj_is_valid(S->scr)) return;
  st::refresh(false);
  S = new Screen();
  lv_obj_t* body;
  S->scr = os::screen(N_("Choose a dog"), &body);
  S->body = body;
  lv_obj_add_event_cb(S->scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != S->scr) return;
    delete S;
    S = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  every(S->scr, 700, watch);
  build();
  os::open(S->scr);
}

}  // namespace chooser
}  // namespace pet
#else  // !ESPOS_ONBOARD: as before the setup
// "Choose a dog" (chooser.h): a card a dog, two columns (three or more when the screen is turned), the chosen dog
// with an orange ring. Installed: tap = this dog. Not installed: tap = download it (the store's installer; on the
// card when one is in), its progress on its card; once in, it becomes the dog.
#pragma GCC optimize("Os")
#include "chooser.h"

#include <Arduino.h>

#include "app.h"
#include "apps.h"
#include "dogs.h"
#include "i18n.h"
#include "lock.h"
#include "os.h"
#include "packs.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "trace.h"
#include "uikit.h"

namespace pet {
namespace chooser {
namespace {

using namespace kit;
namespace st = pkg::store;

constexpr uint32_t ORANGE = 0xff9a1f, GREEN = 0x1f9d4a;
constexpr int ICON = 60;

// What a card shows that changes while the screen is open (updated in place: rebuilding the ten cards for every
// progress step or arriving picture stalled the board while a dog downloaded).
struct Card {
  lv_obj_t *card = nullptr, *icon = nullptr, *state = nullptr, *track = nullptr, *fill = nullptr;
  bool drawnIcon = false;  // the placeholder until the store's picture arrives
  int cw = 0;
};
struct Screen {
  lv_obj_t *scr = nullptr, *body = nullptr, *grid = nullptr;
  uint32_t seenPkg = 0, seenStore = 0, seenCat = 0, seenDog = 0;
  Card cards[dogs::COUNT];
  String pending;     // the dog being downloaded for the user: chosen once it is in
};
Screen* S = nullptr;

String sizeOf(const char* key) {
  const st::Entry* e = st::entry(dogs::pkgId(key).c_str());
  return e ? pkg::sizeText(st::partsBytes(*e, st::defaultParts(*e))) : String();
}

String descOf(const char* key) {
  const String id = dogs::pkgId(key);
  if (const pkg::Package* p = pkg::find(id.c_str())) {
    if (const st::Entry* e = st::entry(id.c_str())) return e->desc;  // the language shown
    return p->desc;
  }
  const st::Entry* e = st::entry(id.c_str());
  return e ? e->desc : String();
}

void onCard(lv_event_t* e) {
  if (!S) return;
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  const dogs::Info& d = dogs::ALL[i];
  const String id = dogs::pkgId(d.key);
  const pkg::Package* p = pkg::find(id.c_str());
  if (lock::locked(id.c_str())) return lock::sheet(d.name, lock::Look::Pet);  // paid, after the trial (lock.h)
  if (p && !p->available) return os::toast(N_("This dog is on the memory card: put the card back in"));
  if (p) {
    if (dogs::chosen() == &d && !strcmp(pk::dogKey(), d.key)) {  // the dog already: back to the game
      if (app::isOpen()) return os::back();
      os::back();
      return apps::open("pet");
    }
    trace::add("pet: chose %s", d.key);
    switchDog(d.key, true);  // closes this screen too, then the game with the new dog
    return;
  }
  if (st::stateOf(id.c_str()) != st::IDLE) return;  // already coming
  const st::Entry* en = st::entry(id.c_str());
  if (!en) {
    st::refresh(true);
    return os::toast(N_("Getting the dogs from the Store, try again in a moment"));
  }
  const pkg::Vol v = pkg::volume(pkg::SD).present ? pkg::SD : pkg::FLASH;
  String why;
  if (!st::install(*en, st::defaultParts(*en), v, why)) return os::toast(why.c_str());
  S->pending = d.key;
  trace::add("pet: downloading %s for the chooser", d.key);
}

// A card's state line: the dog now / tap to choose / on the card that is out / the download and its percent.
void stateOf(int i, String& what, uint32_t& col, int& pct) {
  const Pal& p = pal();
  const dogs::Info& d = dogs::ALL[i];
  const String id = dogs::pkgId(d.key);
  const pkg::Package* inst = pkg::find(id.c_str());
  const st::State state = st::stateOf(id.c_str());
  pct = -1;
  col = p.dim;
  if (state != st::IDLE) {
    const st::Status ss = st::status();
    what = st::stateText(state);
    if (ss.id == id && (state == st::DOWNLOAD || state == st::WRITE)) what += " " + String(ss.pct) + "%", pct = ss.pct;
    col = ORANGE;
  } else if (lock::locked(id.c_str())) {  // paid, after the trial: on the device still, or "Buy" with its size
    const String sz = sizeOf(d.key);
    what = inst ? String(T_("Locked now")) : sz.length() ? i18n::fmt(N_("Buy · {0}"), sz) : String(TC_("store", "Buy"));
    col = inst ? 0xc2410c : p.link;
  } else if (dogs::chosen() == &d && inst) {
    what = T_("Your dog"), col = GREEN;
  } else if (inst && !inst->available) {
    what = T_("Card out");
  } else if (inst) {
    what = T_("Tap to choose"), col = p.link;
  } else {
    const String sz = sizeOf(d.key);
    const bool tryIt = lock::trial() && !lock::freeAnyway(id.c_str());  // the trial gives it: "Try · 6.4 MB"
    what = sz.length() ? i18n::fmt(tryIt ? N_("Try · {0}") : N_("Download · {0}"), sz) : String(T_("In the Store"));
    col = p.link;
  }
}

void showState(int i) {
  Card& k = S->cards[i];
  String what;
  uint32_t col;
  int pct;
  stateOf(i, what, col, pct);
  lv_obj_set_style_text_color(k.state, C(col), 0);
  fitText(k.state, what.c_str(), k.cw - 16, 18, 13, 11, true, 0);
  lv_obj_align(k.state, LV_ALIGN_TOP_MID, 0, ICON + 40);
  if (pct < 0) {
    lv_obj_add_flag(k.track, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(k.track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(k.fill, lv_pct(max(2, pct)), 6);
  }
}

void card(lv_obj_t* grid, int i, int cw) {
  const Pal& p = pal();
  const dogs::Info& d = dogs::ALL[i];
  const String id = dogs::pkgId(d.key);
  const pkg::Package* inst = pkg::find(id.c_str());
  const dogs::Info* cur = dogs::chosen();
  const bool chosen = cur == &d && inst;

  lv_obj_t* c = box(grid, p.cell, 18);
  lv_obj_set_size(c, cw, 216);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
  if (chosen) {
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_set_style_border_color(c, C(ORANGE), 0);
  }
  lv_obj_add_event_cb(c, onCard, LV_EVENT_CLICKED, (void*)(intptr_t)i);

  // The phone game's portrait (the package's icon.jpg, or the store's copy until it is installed).
  Card& k = S->cards[i];
  k = Card();
  k.card = c, k.cw = cw;
  lv_obj_t* ic = nullptr;
  if (inst) {
    ic = apps::packageIcon(c, id.c_str(), ICON);
  } else if (const lv_image_dsc_t* d2 = app_store::catalogIcon(id.c_str(), ICON)) {
    ic = img(c, d2);
  } else {
    ic = apps::drawnIcon(c, 0xffcf7a, 0xe8892c, "play", ICON), k.drawnIcon = true;
  }
  k.icon = ic;
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 10);
  if (inst && !inst->available) lv_obj_set_style_opa(ic, LV_OPA_40, 0);
  if (lock::locked(id.c_str())) {  // a lock on the portrait's corner (lock.h)
    lv_obj_update_layout(c);
    lv_obj_align_to(lock::badge(c, 22), ic, LV_ALIGN_BOTTOM_RIGHT, 6, 6);
  }

  lv_obj_t* n = label(c, d.name, 17, true, p.fg);  // a name: as it is
  lv_obj_align(n, LV_ALIGN_TOP_MID, 0, ICON + 16);
  // Its state line and the download's bar (hidden unless downloading): showState() keeps them current.
  k.state = label(c, "", 13, true, p.dim);
  lv_obj_set_style_text_align(k.state, LV_TEXT_ALIGN_CENTER, 0);
  k.track = box(c, p.cell2, 3);
  lv_obj_set_size(k.track, cw - 32, 6);
  lv_obj_align(k.track, LV_ALIGN_TOP_MID, 0, ICON + 62);
  k.fill = box(k.track, ORANGE, 3);
  lv_obj_set_size(k.fill, 0, 6);
  showState(i);
  // Who it is (the dog's character, from the store).
  const String desc = descOf(d.key);
  if (desc.length()) {
    lv_obj_t* t = label(c, "", 12, false, p.dim);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    fitText(t, desc.c_str(), cw - 16, 216 - ICON - 80, 12, 11, false, 1);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, ICON + 72);
  }
}

void build() {
  if (!S) return;
  const int32_t sy = S->body ? lv_obj_get_scroll_y(S->body) : 0;
  lv_obj_clean(S->body);
  S->seenPkg = pkg::version(), S->seenStore = st::version(), S->seenCat = st::catalogVersion();
  S->seenDog = dogs::version();
  hint(S->body, N_("Your dog is your pet, runs in Coin Run and is the voice of the assistant. Each dog has its own character."));
  const int cols = max(2, (os::W() - 16) / 150);
  const int cw = (os::W() - 24 - 8 * (cols - 1)) / cols;
  lv_obj_t* grid = clear(S->body);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, 8, 0);
  lv_obj_set_style_pad_row(grid, 8, 0);
  for (int i = 0; i < dogs::COUNT; i++) card(grid, i, cw);
  if (!pkg::volume(pkg::SD).present) hint(S->body, N_("A dog takes about 7 MB: put a memory card in to keep several."));
  lv_obj_update_layout(S->body);
  lv_obj_scroll_to_y(S->body, sy, LV_ANIM_OFF);
}

void watch(lv_timer_t*) {
  if (!S) return;
  // The dog downloaded for the user is in: it becomes the dog (and the game opens with it).
  if (S->pending.length() && dogs::installed(S->pending.c_str()) && st::stateOf(dogs::pkgId(S->pending.c_str()).c_str()) == st::IDLE) {
    const String k = S->pending;
    S->pending = "";
    return switchDog(k.c_str(), true);
  }
  static uint32_t seenLock = 0;
  if (seenLock != lock::version()) return seenLock = lock::version(), build();
  if (S->seenPkg != pkg::version() || S->seenStore != st::version() || S->seenCat != st::catalogVersion() ||
      S->seenDog != dogs::version())
    return build();  // a dog in or out, a job started or ended, the catalog, the chosen dog
  for (int i = 0; i < dogs::COUNT; i++) {
    Card& k = S->cards[i];
    if (!k.card) continue;
    if (k.drawnIcon) {  // the store's picture came: in place of the placeholder
      if (const lv_image_dsc_t* d = app_store::catalogIcon(dogs::pkgId(dogs::ALL[i].key).c_str(), ICON)) {
        lv_obj_delete(k.icon);
        k.icon = img(k.card, d), k.drawnIcon = false;
        lv_obj_align(k.icon, LV_ALIGN_TOP_MID, 0, 10);
      }
    }
    if (st::busy()) showState(i);  // the download's percent
  }
}

}  // namespace

void open() {
  if (S && S->scr && lv_obj_is_valid(S->scr)) return;
  st::refresh(false);
  S = new Screen();
  lv_obj_t* body;
  S->scr = os::screen(N_("Choose a dog"), &body);
  S->body = body;
  lv_obj_add_event_cb(S->scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != S->scr) return;
    delete S;
    S = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  every(S->scr, 700, watch);
  build();
  os::open(S->scr);
}

}  // namespace chooser
}  // namespace pet
#endif  // ESPOS_ONBOARD
