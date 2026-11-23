#pragma GCC optimize("Os")  // cold UI (a tile, a card, two store hooks): size first
// The assistant slot (assistant.h, docs/app-slots.md §16).
#include "assistant.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include <map>

#include "apps.h"
#include "desk.h"
#include "i18n.h"
#include "lock.h"
#include "notif.h"
#include "os.h"
#include "pkg/pkg.h"
#include "pkg/store.h"
#include "trace.h"
#include "uikit.h"
#ifndef NO_PET
#include "pet/packs.h"
#include "pet/pet.h"
#endif
#include "turn.h"

namespace assistant {

using namespace kit;

namespace {

lv_obj_t* card = nullptr;
uint32_t cardAt = 0;

// An assistant app: PawBoop (the dog's engine), or a JavaScript app whose manifest says "provides": ["assistant"] (its
// brain is its provider: the server routes the turn; the OS client and pop-up do the rest). Never an add-on.
bool runs(const pkg::Package* p) {
  if (!p || p->addonOf.length()) return false;
  if (p->engine == "pet") return true;
  if (p->kind != "js") return false;
  static std::map<String, bool> seen;  // the manifest read once per package list
  static uint32_t ver = 0xffffffffu;
  if (ver != pkg::version()) seen.clear(), ver = pkg::version();
  auto it = seen.find(p->id);
  if (it != seen.end()) return it->second;
  JsonDocument d;
  bool yes = false;
  if (!deserializeJson(d, pkg::manifestJson(p->id.c_str())))
    for (JsonVariantConst x : d["provides"].as<JsonArrayConst>()) yes |= !strcmp(x | "", "assistant");
  seen[p->id] = yes;
  return yes;
}

// id() asked on every touch and every saver check: worked out again only when the packages or the settings changed.
String cur = "none";
uint32_t seenPkg = 0xffffffffu, seenSet = 0xffffffffu;
void slotChanged(void*);
const String& resolve() {
  const uint32_t pv = pkg::version(), sv = desk::settingsVersion();
  if (pv == seenPkg && sv == seenSet) return cur;
  const bool first = seenPkg == 0xffffffffu;
  seenPkg = pv, seenSet = sv;
  const String was = cur;
  const String v = desk::setting(KEY, "");
  if (v == "none") cur = "none";
  else if (v.length()) cur = runs(pkg::find(v.c_str())) ? v : String("none");
  else cur = runs(pkg::find(DOG)) ? String(DOG) : String("none");
  // A pop-up open for the one before (a sync or Settings changed the slot under it) goes once this call returns: its
  // mic would otherwise record for an assistant that is not there any more (0.6.1).
  if (!first && was != cur) lv_async_call(slotChanged, nullptr);
  return cur;
}

// PawBoop's add-ons that come back when it is installed again (owner: "cài lại thì chó đã mua quay về"): those removed
// with it that were bought (entitlements are the account's, lock.h) and the dog that was chosen. NVS "asst": "back" (a
// comma list), "chose" (the chosen dog's key at the removal).
String nvsGet(const char* k) {
  Preferences pr;
  if (!pr.begin("asst", true)) return String();
  const String v = pr.getString(k, "");
  pr.end();
  return v;
}
void nvsPut(const char* k, const String& v) {
  Preferences pr;
  if (pr.begin("asst", false)) pr.putString(k, v), pr.end();
}
bool dogGone = false;  // PawBoop removed this boot (the package list may not show it yet while its add-ons go)
void rememberAddon(const String& id, const String& addonOf) {
  if (addonOf != DOG || (!dogGone && pkg::find(DOG))) return;  // only an add-on going while PawBoop is gone
  const String chose = nvsGet("chose");
  if (!lock::bought(id.c_str()) && !(chose.length() && id == "vn.pawboop.dog." + chose)) return;
  String back = nvsGet("back");
  if (("," + back + ",").indexOf("," + id + ",") >= 0 || back.length() > 600) return;
  nvsPut("back", back.length() ? back + "," + id : id);
}
void bringBack(pkg::Vol vol) {
  const String back = nvsGet("back");
  nvsPut("back", ""), nvsPut("chose", "");
  int n = 0;
  for (int at = 0; at < (int)back.length();) {
    int c = back.indexOf(',', at);
    if (c < 0) c = back.length();
    const String id = back.substring(at, c);
    at = c + 1;
    const pkg::store::Entry* e = pkg::store::entry(id.c_str());
    String why;
    if (e && !pkg::find(id.c_str()) && pkg::store::install(*e, pkg::store::defaultParts(*e), vol, why)) n++;
    else trace::add("assistant: %s does not come back: %s", id.c_str(), e ? why.c_str() : "not in the catalog");
  }
  if (n) trace::add("assistant: %d of PawBoop's add-ons coming back (%s)", n, back.c_str());
}

void onCardTap(lv_event_t* e) {
  if (lv_event_get_target(e) == lv_event_get_current_target(e)) closeCard();  // outside the card
}

}  // namespace

// ---------------------------------------------------------------- the OS pop-up (any assistant app but the dog)
// The slot app's icon and name, what it says (big) and what it heard (small), the round mic button (hold to talk; after
// the wake word a tap ends it), the unlock code when talking is locked. A tap outside closes it (the turn stops); 20 s
// untouched with nothing going on, it goes by itself. Its voice is the server's (the turn's stream).
namespace {

lv_obj_t *pop = nullptr, *popSaid = nullptr, *popHeard = nullptr, *popMic = nullptr, *popHalo = nullptr,
         *popHint = nullptr, *popTrial = nullptr, *popCol = nullptr;
uint32_t popTouched = 0, popHold = 0;
int popW = 280;

void popClose() {
  if (!pop) return;
  lv_obj_t* o = pop;
  pop = nullptr;
  lv_obj_delete_async(o);  // its delete event stops a turn
}

void popHintText(const char* t) {
  if (popHint) lv_label_set_text(popHint, i18n::trl(t));
}

bool popOpen() {
  if (os::saverOn()) os::wake();
  closeCard();
  popTouched = millis();
  if (pop) return lv_obj_move_foreground(pop), true;
  const String aid = resolve();
  const Pal& p = pal();
  const bool land = os::landscape();
  pop = clear(lv_layer_top());
  lv_obj_set_size(pop, os::W(), os::H());
  lv_obj_add_flag(pop, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(pop, C(0x000000), 0);
  lv_obj_set_style_bg_opa(pop, LV_OPA_30, 0);
  lv_obj_add_event_cb(pop, [](lv_event_t* e) {
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) popClose();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(pop, [](lv_event_t*) {
    pop = popSaid = popHeard = popMic = popHalo = popHint = popTrial = popCol = nullptr;
    turn::closed();
  }, LV_EVENT_DELETE, nullptr);
  every(pop, 500, [](lv_timer_t*) {
    if (pop && millis() - popTouched > 20000 && (int32_t)(millis() - popHold) > 0 && !turn::busy()) popClose();
  });
  const int cw = land ? min(440, os::W() - 24) : os::W() - 24;
  popW = cw - 24;
  lv_obj_t* c = box(pop, p.dark ? 0x1f2631 : 0xffffff, 18);
  lv_obj_set_size(c, cw, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, 12, 0);
  lv_obj_set_style_shadow_width(c, 24, 0);
  lv_obj_set_style_shadow_opa(c, LV_OPA_30, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);  // a tap on it stays on it
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 8, 0);
  popCol = c;
  // the app: its icon and its name (and a trial's talks left)
  lv_obj_t* head = clear(c);
  lv_obj_set_size(head, popW, 40);
  lv_obj_t* ic = apps::packageIcon(head, aid.c_str(), 40);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  const pkg::Package* pk = pkg::find(aid.c_str());
  lv_obj_t* nm = label(head, "", 16, true, p.fg);
  fitText(nm, pk ? pk->name.c_str() : aid.c_str(), popW - 52 - 100, 40, 16, 12, true);  // a name: as it is
  lv_obj_align(nm, LV_ALIGN_LEFT_MID, 50, 0);
  popTrial = label(head, "", 12, true, 0x6a3df0);
  lv_obj_align(popTrial, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(popTrial, LV_OBJ_FLAG_HIDDEN);
  popSaid = label(c, "", 15, false, p.fg);
  lv_obj_set_width(popSaid, popW);
  lv_label_set_long_mode(popSaid, LV_LABEL_LONG_WRAP);
  popHeard = label(c, "", 12, false, p.dim);
  lv_obj_set_width(popHeard, popW);
  lv_label_set_long_mode(popHeard, LV_LABEL_LONG_WRAP);
  lv_obj_add_flag(popHeard, LV_OBJ_FLAG_HIDDEN);
  // the mic: a halo for the voice's level, the button, the hint beside it
  lv_obj_t* row = clear(c);
  lv_obj_set_size(row, popW, 64);
  popHalo = box(row, 0xff7a45, 32);
  lv_obj_set_size(popHalo, 64, 64);
  lv_obj_align(popHalo, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(popHalo, LV_OPA_TRANSP, 0);
  popMic = box(row, 0xff7a45, 26);
  lv_obj_set_size(popMic, 52, 52);
  lv_obj_align(popMic, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_add_flag(popMic, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(micIcon(popMic, 0xffffff));
  lv_obj_add_event_cb(popMic, [](lv_event_t*) {
    popTouched = millis();
    turn::press();
  }, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(popMic, [](lv_event_t*) { turn::release(); }, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(popMic, [](lv_event_t*) { turn::release(); }, LV_EVENT_PRESS_LOST, nullptr);
  popHint = label(row, "", 13, false, p.dim);
  lv_obj_align(popHint, LV_ALIGN_LEFT_MID, 76, 0);
  popHintText(N_("Hold to talk"));
  lv_obj_update_layout(c);
  if (land) lv_obj_center(c);
  else lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -14);
  trace::add("assistant: %s's pop-up", aid.c_str());
  return true;
}

void popShow(const char*, const String& big, const String& small) {
  if (!pop) return;
  popTouched = millis();
  if (!big.length() && !small.length()) return;
  wrapText(popSaid, big.c_str(), popW, 15, false, 2, 6);
  if (small.length()) {
    lv_obj_remove_flag(popHeard, LV_OBJ_FLAG_HIDDEN);
    wrapText(popHeard, small.c_str(), popW, 12, false, 2, 3);
  } else {
    lv_obj_add_flag(popHeard, LV_OBJ_FLAG_HIDDEN);
  }
}

void popLevel(int pct) {
  if (!popHalo) return;
  static const lv_opa_t OPA[5] = {LV_OPA_TRANSP, LV_OPA_20, LV_OPA_30, LV_OPA_40, LV_OPA_50};
  lv_obj_set_style_bg_opa(popHalo, OPA[pct < 0 ? 0 : 1 + min(3, pct / 25)], 0);
}

void popButton(int state) {
  if (!popMic) return;
  lv_obj_set_style_bg_color(popMic, C(state == 1 || state == 3 ? 0xe5484d : state == 2 ? 0xb8bcc6 : 0xff7a45), 0);
  popHintText(state == 1 ? N_("Let go to send") : state == 3 ? N_("Tap when done") : N_("Hold to talk"));
}

void popTouch() { popTouched = millis(); }

// Talking is locked (the trial is over, or no AI on the account): the code that unlocks it in place of the button; a
// minute open (time to take a phone out).
void popLock(const char* url) {
  if (!pop || !popCol) return;
  popHold = millis() + 60000;
  if (popMic) lv_obj_add_flag(lv_obj_get_parent(popMic), LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* r = clear(popCol);
  lv_obj_set_size(r, popW, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 12, 0);
  qr(r, url, 104);
  lv_obj_t* t = label(r, "", 14, true, pal().fg);
  wrapText(t, T_("Scan to keep talking"), popW - 116, 14, true);
  lv_obj_update_layout(popCol);
}

void popTrialLeft(int left) {
  if (!popTrial) return;
  if (left < 0) return (void)lv_obj_add_flag(popTrial, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(popTrial, i18n::plural(left, NP_("{n} talk left|{n} talks left")).c_str());
  lv_obj_remove_flag(popTrial, LV_OBJ_FLAG_HIDDEN);
}

uint32_t noLine(const char*) { return 0; }  // no recorded words: the words alone (a beep for the call)
bool silent() { return false; }
void nothing() {}
uint32_t longAgo() { return 0x3fffffff; }
String serverVoice() { return String(); }

const Face OS_FACE = {popOpen, popShow, popLevel, popButton, popTouch, popLock, popTrialLeft, noLine, silent, nothing,
                      longAgo, serverVoice};

}  // namespace

const Face& face() {
#ifndef NO_PET
  if (dog()) return pet::face();
#endif
  return OS_FACE;
}

String id() { return resolve(); }
bool any() { return resolve() != "none"; }
bool dog() { return resolve() == DOG; }
bool runs(const char* pkgId) { return runs(pkg::find(pkgId)); }

std::vector<String> installed() {
  std::vector<String> out;
  for (const pkg::Package& p : pkg::list())
    if (runs(&p)) out.push_back(p.id);
  return out;
}

String name() {
#ifndef NO_PET
  if (dog()) return pet::name();  // a name: as it is
#endif
  const pkg::Package* p = any() ? pkg::find(resolve().c_str()) : nullptr;
  return p ? p->name : String();
}

bool listens() { return any(); }  // the OS client calls whichever app is in the slot (turn.cpp)

bool open() {
#ifndef NO_PET
  if (pet::summoned()) return true;
#endif
  return card != nullptr || pop != nullptr;
}

bool busy() { return turn::busy(); }

void summon(const char* reason) {
#ifndef NO_PET
  if (dog()) return pet::summon(reason);
#endif
  if (!any()) return noneCard();
  if (popOpen()) popShow(nullptr, T_("How can I help?"), T_(turn::canTalk() ? N_("Hold the button and speak") : N_("Link this device to your account first")));
}

void closeCard() {
  if (!card) return;
  lv_obj_t* c = card;
  card = nullptr;
  lv_obj_delete_async(c);
}

void close() {
#ifndef NO_PET
  if (pet::summoned()) pet::dismiss();
#endif
  popClose();
  closeCard();
}

namespace {
// The slot changed (resolve): the dog's pop-up goes when the dog is no longer the assistant, the OS's when the slot is
// now the dog or none (another app keeps it: the OS's pop-up serves every app but the dog).
void slotChanged(void*) {
#ifndef NO_PET
  if (pet::summoned() && !dog()) {
    trace::add("assistant: the slot is now %s: the dog's pop-up closes", id().c_str());
    pet::dismiss();
  }
#endif
  if (pop && (dog() || !any())) {
    trace::add("assistant: the slot is now %s: its pop-up closes", id().c_str());
    popClose();
  }
}
}  // namespace

// The approved mockup (mockups/app-slots/assistant-install.html, "No assistant installed"): a card at the bottom over
// the screen, a grey mic, two lines and "Store" (its Assistants). A tap outside closes it; it goes by itself in 10 s.
void noneCard() {
  if (os::saverOn()) os::wake();
  closeCard();
  const bool land = os::landscape();
  lv_obj_t* scrim = clear(lv_layer_top());
  card = scrim;
  cardAt = millis();
  lv_obj_set_size(scrim, os::W(), os::H());
  lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scrim, onCardTap, LV_EVENT_CLICKED, nullptr);
  every(scrim, 500, [](lv_timer_t*) {
    if (card && millis() - cardAt > 10000) closeCard();
  });
  const Pal& p = pal();
  const int cw = land ? min(440, os::W() - 24) : os::W() - 24;
  lv_obj_t* c = box(scrim, p.dark ? 0x1f2631 : 0xffffff, 18);
  lv_obj_set_size(c, cw, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, 12, 0);
  lv_obj_set_style_shadow_width(c, 24, 0);
  lv_obj_set_style_shadow_opa(c, LV_OPA_30, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);  // a tap on it stays on it
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(c, 10, 0);
  lv_obj_t* ic = box(c, p.dark ? 0x3a4150 : 0xd1d5db, 12);
  lv_obj_set_size(ic, 40, 40);
  lv_obj_center(micIcon(ic, p.dark ? 0xe5e7eb : 0x374151));
  const int bw = 76, tw = cw - 24 - 40 - bw - 20;
  lv_obj_t* tc = stack(c, false, 2);
  lv_obj_set_width(tc, tw);
  lv_obj_t* t = label(tc, "", 14, true, p.fg);
  fitText(t, N_("No assistant installed"), tw, 40, 14, 12, true);
  lv_obj_t* s = label(tc, "", 12, false, p.dim);
  wrapText(s, N_("Get one in the Store: the PawBoop dog, or another."), tw, 12);
  lv_obj_t* b = button(c, N_("Store"), col::IOS_BLUE, 0xffffff, [](lv_event_t*) {
    closeCard();
    apps::open("store", "assistant");  // the Store's Assistants (app_more.cpp)
  });
  lv_obj_set_size(b, bw, 34);
  lv_obj_update_layout(c);
  lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -14);
  trace::add("assistant: none installed (card)");
}

lv_obj_t* micIcon(lv_obj_t* parent, uint32_t color) {
  lv_obj_t* m = clear(parent);
  lv_obj_set_size(m, 18, 26);
  lv_obj_t* cap = box(m, color, 5);  // the capsule
  lv_obj_set_size(cap, 10, 15);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);
  // the U around it: three bars (an arc widget would cost 3 KB of firmware)
  for (int k = 0; k < 3; k++) {
    lv_obj_t* b = box(m, color, 1);
    lv_obj_set_size(b, k == 2 ? 16 : 2, k == 2 ? 2 : 8);
    lv_obj_set_pos(b, k == 1 ? 16 : 1, k == 2 ? 17 : 9);
  }
  lv_obj_t* stem = box(m, color, 1);
  lv_obj_set_size(stem, 2, 5);
  lv_obj_align(stem, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_t* base = box(m, color, 1);
  lv_obj_set_size(base, 10, 2);
  lv_obj_align(base, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(m, LV_OBJ_FLAG_EVENT_BUBBLE);
  return m;
}

void removed(const char* pkgId, const String& appName, const String& addonOf) {
  const String rid(pkgId);
#ifndef NO_PET
  if (rid == DOG) {  // the first of its jobs (the app, then its add-ons): which dog it was, before its data goes
    // The dog shown until now (its packs are still loaded: the dogs' own removals may have run already), else the
    // setting's.
    String chose = pet::pk::dogKey();
    if (!chose.length()) chose = desk::setting("s3.pet_dog", "");
    nvsPut("back", ""), nvsPut("chose", chose);
    dogGone = true;
  }
#endif
  if (rid != DOG) rememberAddon(rid, addonOf);
#ifndef NO_PET
  if (rid == DOG) pet::uninstalled();  // its game, moments, chosen dog and notifications (pet.h)
  if (rid == "vn.pawboop.run") {       // the run game's best distance
    Preferences pr;
    if (pr.begin("run", false)) pr.clear(), pr.end();
  }
#endif
  const String v = desk::setting(KEY, "");
  if (!(v == rid || (!v.length() && rid == DOG))) return;  // not the assistant
  desk::setSetting(KEY, "none");  // the server answers no turn for this device now (409 no_assistant)
  closeCard();
  notif::Opts o;
  o.arg = "assistant", o.tag = "assistant", o.sound = false, o.badge = false, o.banner = false;  // the Center only
  notif::post("store", T_("No assistant on this device"),
              i18n::fmt(N_("{0} was removed. Get an assistant in the Store."), appName).c_str(), o);
  trace::add("assistant: %s removed, the slot is empty", pkgId);
}

void installed(const pkg::Package& p) {
  if (p.id == DOG) {  // its add-ons once the package list shows it (the store's install of an add-on asks for its app)
    dogGone = false;
    static pkg::Vol vol;
    vol = p.vol;
    lv_timer_t* t = lv_timer_create([](lv_timer_t* t) {
      if (!pkg::find(DOG) && lv_timer_get_user_data(t) != (void*)1) return lv_timer_set_user_data(t, (void*)1);  // once more
      lv_timer_delete(t);
      bringBack(vol);
    }, 400, nullptr);
    (void)t;
  }
  if (!runs(&p) || desk::setting(KEY, "") != "none") return;
  desk::setSetting(KEY, p.id);  // an assistant came while there was none: it is the assistant (a reinstalled dog)
  trace::add("assistant: %s is the assistant", p.id.c_str());
}

}  // namespace assistant
