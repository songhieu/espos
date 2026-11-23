#pragma GCC optimize("Os")  // cold UI: size first
// Settings › Default apps and the screen saver's slot (slots.h).
#include "slots.h"

#include <vector>

#include "apps.h"
#include "art.h"
#include "assistant.h"
#include "desk.h"
#include "i18n.h"
#include "js/jssaver.h"
#include "notif.h"
#include "os.h"
#include "pkg/pkg.h"
#include "trace.h"
#include "uikit.h"
#ifndef NO_PET
#include "pet/dogs.h"
#include "pet/pet.h"
#endif

namespace slots {

using namespace kit;

namespace {

// What the aquarium draws as the saver: its tank, alive, with the time and the date (the web's "tank" template).
const char* const TANK_DOC =
    R"({"v":1,"p":[{"bg":{"t":"tank"},"e":[{"t":"time","x":0,"y":48,"w":320,"h":68,"c":"ffffff","px":64},)"
    R"({"t":"date","f":"wdm","x":0,"y":116,"w":320,"h":24,"c":"ffffff","px":16}]}],)"
    R"("l":[{"bg":{"t":"tank"},"e":[{"t":"time","x":0,"y":48,"w":480,"h":68,"c":"ffffff","px":64},)"
    R"({"t":"date","f":"wdm","x":0,"y":116,"w":480,"h":24,"c":"ffffff","px":16}]}]})";

// The aquarium (engine "tank": its pages, TANK_DOC) or a JavaScript saver app (jssaver.h: it draws the whole saver).
bool canSave(const pkg::Package* p) { return p && !p->addonOf.length() && (p->engine == "tank" || jssaver::runs(p)); }

lv_obj_t* daBody = nullptr;  // Settings › Default apps, filled again after a choice
void fill();

int saverMinutes() { return desk::settingInt("s3.saver", 120) / 60; }

// A row: a 36 px picture holder (child 0), the name, a line under it, a check or a chevron.
lv_obj_t* row(lv_obj_t* parent, const String& name, const String& sub, bool check, bool chevron, lv_event_cb_t cb,
              void* data = nullptr) {
  const Pal& p = pal();
  const int W = (os::landscape() ? min(os::W() - 24, 456) : os::W() - 24) - 24;
  lv_obj_t* r = box(parent, p.cell, 12);
  lv_obj_set_size(r, lv_pct(100), 58);
  lv_obj_set_style_pad_hor(r, 12, 0);
  lv_obj_t* ic = clear(r);
  lv_obj_set_size(ic, 36, 36);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  const int tw = W - 48 - (check || chevron ? 26 : 0);
  lv_obj_t* n = label(r, "", 15, true, p.fg);
  fitText(n, name.c_str(), tw, 20, 15, 12, true);
  lv_obj_set_pos(n, 48, 8);
  lv_obj_t* s = label(r, "", 12, false, p.dim);
  fitText(s, sub.c_str(), tw, 18, 12, 11);
  lv_obj_set_pos(s, 48, 30);
  if (check) {
    lv_obj_t* k = label(r, LV_SYMBOL_OK, 16, true, col::IOS_BLUE);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_20, 0);
    lv_obj_align(k, LV_ALIGN_RIGHT_MID, 0, 0);
  } else if (chevron) {
    lv_obj_align(glyph(r, &gl_chev_16, p.faint), LV_ALIGN_RIGHT_MID, 0, 0);
  }
  if (cb) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, C(p.press), LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, data);
  }
  return r;
}

void builtinIcon(lv_obj_t* holder) {  // "Clock and photos": a dark square with "12"
  lv_obj_t* b = box(holder, 0x1c1c1e, 9);
  lv_obj_set_size(b, 36, 36);
  lv_obj_center(label(b, "12", 14, true, 0xffffff));
}

void plainIcon(lv_obj_t* holder, const char* text) {  // "None" and "Get more": a grey square
  lv_obj_t* b = box(holder, 0xd1d5db, 9);
  lv_obj_set_size(b, 36, 36);
  lv_obj_center(label(b, text, 16, true, 0x374151));
}

// The assistant's picture: the chosen dog (round) for PawBoop, else the app's icon.
void assistantIcon(lv_obj_t* holder, const String& id) {
#ifndef NO_PET
  if (id == assistant::DOG) {
    lv_obj_t* w = clear(holder);
    lv_obj_set_size(w, 36, 36);
    lv_obj_set_style_radius(w, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(w, true, 0);
    lv_obj_set_style_bg_color(w, C(0xf3e6d3), 0);
    lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
    const pet::dogs::Info* d = pet::dogs::chosen();
    lv_obj_center(apps::packageIcon(w, d ? pet::dogs::pkgId(d->key).c_str() : id.c_str(), 36));
    return;
  }
#endif
  lv_obj_center(apps::packageIcon(holder, id.c_str(), 36));
}

String assistantName(const String& id) {
#ifndef NO_PET
  if (id == assistant::DOG) return pet::name();  // the dog's own name
#endif
  const pkg::Package* p = pkg::find(id.c_str());
  return p ? p->name : id;
}

String assistantSub(const String& id) {
  if (id == assistant::DOG) return T_("PawBoop dog · answers when called");
  const pkg::Package* p = pkg::find(id.c_str());
  return p && p->author.length() ? p->author : String(T_("Assistant app"));
}

String saverSub(const String& id) {
  const String when = saverMinutes() ? i18n::fmt(N_("starts after {0} min"), String(saverMinutes())) : String(T_("off"));
  if (id == "builtin") return String(T_("Built in")) + " · " + when;
  const pkg::Package* p = pkg::find(id.c_str());
  return (p && p->author.length() ? p->author + " · " : String()) + when;
}

// ---------------------------------------------------------------- choosing
void choseSaver(lv_event_t* e) {
  const char* id = (const char*)lv_event_get_user_data(e);
  desk::setSetting(SAVER_KEY, id);
  trace::add("slots: the saver is %s", id);
  jssaver::unblock();  // chosen again: a saver app stopped too often may start again
  os::back();
  fill();
}

std::vector<String> saverIds;  // the ids the saver rows point at (alive while the picker is)

void openSaverPick() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Screen saver"), &body);
  lv_obj_set_style_pad_row(body, 8, 0);
  const String cur = saverId();
  builtinIcon(lv_obj_get_child(row(body, T_("Clock and photos"), T_("Built in · your saver pages"), cur == "builtin", false,
                                   choseSaver, (void*)"builtin"), 0));
  saverIds.clear();
  for (const pkg::Package& p : pkg::list())
    if (canSave(&p)) saverIds.push_back(p.id);
  for (const String& id : saverIds) {
    const pkg::Package* p = pkg::find(id.c_str());
    lv_obj_t* r = row(body, p ? p->name : id, p ? p->author : String(), cur == id, false, choseSaver, (void*)id.c_str());
    lv_obj_center(apps::packageIcon(lv_obj_get_child(r, 0), id.c_str(), 36));
  }
  plainIcon(lv_obj_get_child(row(body, T_("Get more in the Store"), T_("Apps that replace the screen saver"), false, true,
                                 [](lv_event_t*) { apps::open("store", "saver"); }), 0), "+");
  lv_obj_t* pv = row(body, T_("Preview"), T_("Shows the saver now; touch to come back"), false, true, [](lv_event_t*) { os::showSaver(); });
  lv_obj_add_flag(lv_obj_get_child(pv, 0), LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(lv_obj_get_child(pv, 1), 0, 8), lv_obj_set_pos(lv_obj_get_child(pv, 2), 0, 30);
  os::open(s);
}

void choseAssistant(lv_event_t* e) {
  const char* id = (const char*)lv_event_get_user_data(e);
  desk::setSetting(assistant::KEY, id);  // "none": nothing listens (wake.cpp); the server answers no turn (409)
  trace::add("slots: the assistant is %s", id);
  os::back();
  fill();
}

std::vector<String> keepIds;  // the ids the rows point at (alive while the picker is)

void openAssistantPick() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Assistant"), &body);
  lv_obj_set_style_pad_row(body, 8, 0);
  const String cur = assistant::id();
  keepIds = assistant::installed();
  for (const String& id : keepIds) {
    lv_obj_t* r = row(body, assistantName(id), assistantSub(id), cur == id, false, choseAssistant, (void*)id.c_str());
    assistantIcon(lv_obj_get_child(r, 0), id);
  }
  plainIcon(lv_obj_get_child(row(body, T_("None"), T_("The wake word does nothing"), cur == "none", false, choseAssistant,
                                 (void*)"none"), 0), "-");
  plainIcon(lv_obj_get_child(row(body, T_("Get more in the Store"), T_("Assistant apps"), false, true,
                                 [](lv_event_t*) { apps::open("store", "assistant"); }), 0), "+");
  hint(body, N_("What an assistant app may know is what you allowed when you installed it (Settings, Apps)."));
  os::open(s);
}

void fill() {
  if (!daBody) return;
  lv_obj_clean(daBody);
  caption(daBody, N_("Screen saver"));
  const String sv = saverId();
  lv_obj_t* r = row(daBody, saverName(), saverSub(sv), false, true, [](lv_event_t*) { openSaverPick(); });
  if (sv == "builtin") builtinIcon(lv_obj_get_child(r, 0));
  else lv_obj_center(apps::packageIcon(lv_obj_get_child(r, 0), sv.c_str(), 36));
  caption(daBody, N_("Assistant"));
  const String as = assistant::id();
  if (as == "none") {
    plainIcon(lv_obj_get_child(row(daBody, T_("None"), T_("The wake word does nothing"), false, true,
                                   [](lv_event_t*) { openAssistantPick(); }), 0), "-");
  } else {
    lv_obj_t* a = row(daBody, assistantName(as), assistantSub(as), false, true, [](lv_event_t*) { openAssistantPick(); });
    assistantIcon(lv_obj_get_child(a, 0), as);
  }
  hint(daBody, N_("An app you remove, or one that stops working, hands its place back to the built-in one."));
}

}  // namespace

bool saverApp(const char* pkgId) { return canSave(pkg::find(pkgId)); }

String saverId() {
  const String v = desk::setting(SAVER_KEY, "");
  return v.length() && v != "builtin" && saverApp(v.c_str()) ? v : String("builtin");
}

bool saverBuiltin() { return saverId() == "builtin"; }

String saverName() {
  const String id = saverId();
  if (id == "builtin") return T_("Clock and photos");
  const pkg::Package* p = pkg::find(id.c_str());
  return p ? p->name : id;
}

const char* saverDoc() {
  const pkg::Package* p = saverBuiltin() ? nullptr : pkg::find(saverId().c_str());
  return p && p->engine == "tank" ? TANK_DOC : nullptr;
}

bool saverJs() {
  const pkg::Package* p = saverBuiltin() ? nullptr : pkg::find(saverId().c_str());
  return p && p->engine != "tank";
}

String summary() {
  const String a = assistant::any() ? assistantName(assistant::id()) : String(T_("None"));
  return saverName() + " · " + a;
}

void openDefaultApps() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Default apps"), &body);
  lv_obj_set_style_pad_row(body, 8, 0);
  keep(&daBody, body);
  fill();
  os::open(s);
}

void removed(const char* pkgId, const String& name) {
  if (desk::setting(SAVER_KEY, "") != pkgId) return;
  desk::setSetting(SAVER_KEY, "builtin");  // the built-in saver is back
  notif::Opts o;
  o.arg = "defaults", o.tag = "saver", o.sound = false, o.badge = false, o.banner = false;
  notif::post("settings", T_("The built-in screen saver is back"),
              i18n::fmt(N_("{0} was removed. Choose another one in Default apps, in Settings."), name).c_str(), o);
  trace::add("slots: %s removed, the built-in saver is back", pkgId);
}

}  // namespace slots
