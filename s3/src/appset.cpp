#pragma GCC optimize("Os")  // cold screens: built small (the app slot is nearly full)
// A package's settings (appset.h): the value store over desk::setting("app.<id>.<key>"), the form drawn from the
// manifest's schema with the OS's own rows (mockup mockups/app-slots/settings-form.html), Settings › Apps,
// the device's secrets through the schema (secret.h), and the sync's secret report and delivery.
#include "appset.h"

#include <lvgl.h>

#include <cmath>
#include <vector>

#include "apps.h"
#include "appopts.h"
#include "art.h"
#include "desk.h"
#include "fonts.h"
#include "i18n.h"
#include "keyboard.h"
#if __has_include("lanform.h")
#include "lanform.h"
#endif
#include "os.h"
#include "pkg/pkg.h"
#include "secret.h"
#include "trace.h"
#include "uikit.h"

namespace appset {
namespace {

using namespace kit;

struct Spiram : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} spiram;

// ---------------------------------------------------------------- the schema (one package's, kept while it is read)
struct Cache {
  String id;
  uint32_t ver = 0;
  bool ok = false;
  JsonDocument doc{&spiram};
} cache;

JsonArrayConst schema(const char* id) {
  if (!id || !*id) return JsonArrayConst();
  if (cache.id != id || cache.ver != pkg::version()) {
    cache.id = id;
    cache.ver = pkg::version();
    cache.doc.clear();
    const String m = pkg::manifestJson(id);
    cache.ok = m.length() && deserializeJson(cache.doc, m.c_str()) == DeserializationError::Ok &&
               cache.doc["settings"].is<JsonArrayConst>();
  }
  return cache.ok ? cache.doc["settings"].as<JsonArrayConst>() : JsonArrayConst();
}

bool is(JsonObjectConst f, const char* type) { return !strcmp(f["type"] | "", type); }

JsonObjectConst field(const char* id, const char* key) {
  for (JsonObjectConst f : schema(id))
    if (!is(f, "section") && !strcmp(f["key"] | "", key)) return f;
  return JsonObjectConst();
}

String fullKey(const char* id, const char* key) { return String("app.") + id + "." + key; }

// The label, help or an option's label in the language shown (the field's "i18n"), else the manifest's own.
const char* text(JsonObjectConst f, const char* what) {
  JsonVariantConst t = f["i18n"][i18n::lang()][what];
  return t.is<const char*>() ? t.as<const char*>() : (f[what] | "");
}
const char* optionLabel(JsonObjectConst f, JsonObjectConst opts, const char* v) {
  JsonVariantConst t = f["i18n"][i18n::lang()]["options"][v];
  if (t.is<const char*>()) return t.as<const char*>();
  for (JsonObjectConst o : opts["options"].as<JsonArrayConst>())
    if (!strcmp(o["v"] | "", v)) return o["label"] | v;
  return v;
}

String valueOf(const char* id, JsonObjectConst f) {
  if (is(f, "secret") || is(f, "section")) return "";
  const String v = desk::setting(fullKey(id, f["key"] | "").c_str(), "");
  if (v.length() && check(f, v.c_str())) return v;
  const size_t n = defaultText(f, nullptr, 0);
  if (!n) return "";
  std::vector<char> b(n + 1);
  defaultText(f, b.data(), b.size());
  return b.data();
}

// A number as the language writes it ("2,5" in German).
String shownNum(const String& s) {
  String o = s;
  o.replace(".", i18n::decSep());
  return o;
}

String reason(const Check& c) {
  char n[32];
  numText(c.n, n, sizeof n);
  const String ns = shownNum(n);
  switch (c.err) {
    case E_LONG: return i18n::fmt(N_("At most {0} characters"), ns);
    case E_CONTROL: return T_("No line breaks or control characters");
    case E_NUMBER: return T_("Type a number, like 4 or 2.5");
    case E_MIN: return i18n::fmt(N_("At least {0}"), ns);
    case E_MAX: return i18n::fmt(N_("At most {0}"), ns);
    case E_INT: return T_("Type a whole number");
    case E_STEP: return i18n::fmt(N_("In steps of {0}"), ns);
    case E_ITEMS: return i18n::fmt(N_("At most {0} items"), ns);
    case E_URL: return T_("Type an https:// address, like https://shop.example.com");
    case E_REALDATE: return T_("This date does not exist");
    default: return T_("This value cannot be used");
  }
}

// The send_to patterns of the manifest's "auth" bindings that use the secret `key` (docs/app-slots.md §15.1:
// {"<name>": {"basic": [user, pass]} | {"bearer": key} | {"header": {...}}, "query": {...}, "send_to": [...]}).
std::vector<String> sendTos(const char* id, const char* key) {
  std::vector<String> out;
  if (schema(id).isNull()) return out;
  for (JsonPairConst b : cache.doc["auth"].as<JsonObjectConst>()) {
    JsonObjectConst o = b.value();
    bool uses = !strcmp(o["bearer"] | "", key);
    for (JsonVariantConst k : o["basic"].as<JsonArrayConst>()) uses |= !strcmp(k | "", key);
    for (const char* part : {"header", "query"})
      for (JsonPairConst kv : o[part].as<JsonObjectConst>()) uses |= !strcmp(kv.value() | "", key);
    if (uses)
      for (JsonVariantConst p : o["send_to"].as<JsonArrayConst>()) out.push_back(p | "");
  }
  return out;
}

// What a device secret is bound to: the url settings its bindings' "{key}" patterns expand to (re-pointing, below).
String tagOf(const char* id, const char* key) {
  String s;
  for (const String& pat : sendTos(id, key)) {
    const int close = pat.indexOf('}');
    if (pat[0] == '{' && close > 1) s += get(id, pat.substring(1, close).c_str()) + "|";
  }
  if (!s.length()) return "";
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < s.length(); i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  char b[12];
  snprintf(b, sizeof b, "%08lx", (unsigned long)h);
  return b;
}

String appName(const char* id) {
  const pkg::Package* p = pkg::find(id);
  return p ? p->name : String(id);
}

// A device secret whose url setting changed is cleared, so a key never follows the address to another host (only
// apps whose schema could be read: a card that is out clears nothing).
void repoint(const char* only) {
  for (const secret::Entry& e : secret::list()) {
    if (only && e.app != only) continue;
    if (schema(e.app.c_str()).isNull()) continue;
    JsonObjectConst f = field(e.app.c_str(), e.key.c_str());
    if (!f.isNull() && is(f, "secret") && strcmp(f["where"] | "", "device")) continue;  // delivered from the server
    if (!f.isNull() && is(f, "secret") && tagOf(e.app.c_str(), e.key.c_str()) == secret::tag(e.app.c_str(), e.key.c_str()))
      continue;
    secret::clear(e.app.c_str(), e.key.c_str());
    trace::add("appset: %s %s cleared (its address changed, or the field is gone)", e.app.c_str(), e.key.c_str());
    if (!f.isNull()) os::toast(i18n::fmt(N_("{0}: {1} was cleared, the address it was for changed"), appName(e.app.c_str()),
                                         text(f, "label")).c_str());
  }
}

struct Ack {
  String app, key;
  int64_t ms;
};
std::vector<Ack> acks;  // delivered secrets, acknowledged in the next sync body

}  // namespace

// ---------------------------------------------------------------- the value store
bool hasForm(const char* id) {
  for (JsonObjectConst f : schema(id))
    if (!is(f, "section")) return true;
  return false;
}

String get(const char* id, const char* key) {
  JsonObjectConst f = field(id, key);
  return f.isNull() ? String() : valueOf(id, f);
}

std::vector<String> keys(const char* id) {
  std::vector<String> out;
  for (JsonObjectConst f : schema(id))
    if (!is(f, "section") && (f["key"] | "")[0]) out.push_back(f["key"] | "");
  return out;
}

std::vector<Field> fields(const char* id) {
  std::vector<Field> out;
  for (JsonObjectConst f : schema(id))
    if (!is(f, "section") && (f["key"] | "")[0]) out.push_back({f["key"] | "", f["type"] | "", text(f, "label"), f["where"] | ""});
  return out;
}

bool set(const char* id, const char* key, const String& raw, String& why) {
  JsonObjectConst f = field(id, key);
  if (f.isNull() || is(f, "secret")) return why = T_("This value cannot be used"), false;
  const Check c = check(f, raw.c_str());
  if (!c) return why = reason(c), false;
  size_t total = raw.length();
  for (const String& k : keys(id))
    if (k != key) total += desk::setting(fullKey(id, k.c_str()).c_str(), "").length();
  if (total > (size_t)MAX_APP_BYTES) return why = T_("This app's settings are full (8 KB in all)"), false;
  desk::setSetting(fullKey(id, key).c_str(), raw);
  repoint(id);
  return true;
}

String jsValue(const char* id, const char* key) {
  JsonObjectConst f = field(id, key);
  if (f.isNull()) return "null";
  if (is(f, "secret")) {  // {set, hint}: whether it is kept here and its last 4 characters, never the value
    JsonDocument d;
    d["set"] = secret::has(id, key);
    d["hint"] = secret::hint(id, key);
    String out;
    serializeJson(d, out);
    return out;
  }
  const String v = valueOf(id, f);
  if (!v.length()) return "null";
  if (is(f, "bool")) return v == "1" ? "true" : "false";
  if (is(f, "list") || is(f, "city")) return v;  // JSON already (checked strictly)
  if (is(f, "number")) {
    char b[32];
    numText(strtod(v.c_str(), nullptr), b, sizeof b);
    return b;
  }
  JsonDocument d;
  d.set(v);
  String out;
  serializeJson(d, out);
  return out;
}

bool setSecret(const char* id, const char* key, const String& value, String& why) {
  JsonObjectConst f = field(id, key);
  if (f.isNull() || !is(f, "secret") || strcmp(f["where"] | "", "device")) return why = T_("This value cannot be used"), false;
  JsonDocument rule;
  rule["type"] = "string", rule["max"] = MAX_VALUE;
  const Check c = check(rule.as<JsonObjectConst>(), value.c_str());
  if (!c) return why = reason(c), false;
  String t = value;
  t.trim();
  if (t != value) return why = T_("No spaces at the start or the end"), false;  // the server's rule too (secret_invalid)
  if (!secret::set(id, key, value, tagOf(id, key).c_str())) return why = T_("Could not save it. Try again."), false;
  return true;
}

void forget(const char* id) {
  desk::dropSettings((String("app.") + id + ".").c_str());
  secret::clearApp(id);
  trace::add("appset: %s uninstalled, its settings and secrets removed", id);
}

// ---------------------------------------------------------------- sync
void syncBody(JsonObject body) {
  JsonObject s = body["app_secrets"].to<JsonObject>();
  for (const secret::Entry& e : secret::list()) {
    JsonArray a = s[e.app].is<JsonArray>() ? s[e.app].as<JsonArray>() : s[e.app].to<JsonArray>();
    a.add(e.key);
  }
  if (acks.empty()) return;
  JsonArray a = body["secret_acks"].to<JsonArray>();
  for (const Ack& k : acks) {
    JsonObject o = a.add<JsonObject>();
    o["app"] = k.app, o["key"] = k.key, o["ms"] = k.ms;
  }
}

void fromSync(JsonObjectConst answer) {
  // The server sends a secret until it sees its ack; an answer without it means it did: the acks start afresh.
  acks.clear();
  for (JsonObjectConst s : answer["secrets"].as<JsonArrayConst>()) {
    const char* app = s["app"] | "";
    const char* key = s["key"] | "";
    const char* v = s["v"] | "";
    if (!*app || !*key) continue;
    acks.push_back({app, key, s["ms"] | (int64_t)0});
    if (!pkg::find(app)) continue;  // not installed (any more): acknowledged, not kept
    if (*v) secret::set(app, key, v);
    else secret::clear(app, key);
  }
  repoint(nullptr);  // the answer's settings are in: a url that changed on the web clears its secrets
}

// ---------------------------------------------------------------- the form
namespace {

struct Form {
  String id;
  lv_obj_t *scr = nullptr, *body = nullptr;
  bool changed = false;
  int rowW = 296;
  uint32_t ver = 0;  // the settings and secrets it was drawn from
} F;
uint32_t verNow() { return desk::settingsVersion() * 7919u + secret::version(); }
String edKey;            // the field an editor screen changes
lv_obj_t *edA, *edB, *edC;  // its widgets (field, rollers)

JsonObjectConst edField() { return field(F.id.c_str(), edKey.c_str()); }
JsonObjectConst fieldAt(int i) {
  int n = 0;
  for (JsonObjectConst f : schema(F.id.c_str()))
    if (n++ == i) return f;
  return JsonObjectConst();
}

void fill();
void refill() {
  lv_async_call([](void*) { fill(); }, nullptr);
}

// Stores `raw` for the field being edited; a refusal is said in a toast. true: stored.
bool save(const String& raw) {
  String why;
  if (!set(F.id.c_str(), edKey.c_str(), raw, why)) return os::toast(why.c_str()), false;
  F.changed = true;
  refill();
  return true;
}

void saveBack(const String& raw) {
  if (save(raw)) kb::close(), os::back();
}

// What a row shows for a value.
String shown(JsonObjectConst f, const String& v) {
  if (is(f, "secret")) return "";
  if (!v.length()) return T_("Not set");
  if (is(f, "enum")) return optionLabel(f, f, v.c_str());
  if (is(f, "number")) return shownNum(v);
  if (is(f, "photo")) return T_("Chosen on the web");
  if (is(f, "list") || is(f, "city")) {
    JsonDocument d;
    deserializeJson(d, v.c_str());
    if (is(f, "city")) return d["name"] | "";
    String s;
    for (JsonVariantConst x : d.as<JsonArrayConst>()) {
      if (s.length()) s += ", ";
      s += is(f["of"], "enum") ? optionLabel(f, f["of"], x | "") : (x | "");
    }
    return s.length() ? s : String(T_("None"));
  }
  if (is(f, "date")) {
    const int y = v.toInt(), m = v.substring(5).toInt(), d = v.substring(8).toInt();
    return i18n::date(N_("{M} {d}, {y}"), d, m, y);
  }
  return v;
}

// A form row: the label, the value under it (wrapped in full), the help in small grey; on the right `rightW` px kept
// for a chevron, a switch or an action. Returns the row; its text column is child 0.
lv_obj_t* row(lv_obj_t* parent, JsonObjectConst f, const char* value, int idx, lv_event_cb_t cb, int rightW) {
  const Pal& p = pal();
  lv_obj_t* r = box(parent, p.cell, 12);
  lv_obj_set_size(r, F.rowW, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(r, 14, 0);
  lv_obj_set_style_pad_ver(r, 9, 0);
  lv_obj_t* col = clear(r);
  const int tw = F.rowW - 28 - rightW;
  lv_obj_set_size(col, tw, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 2, 0);
  const char* lab = text(f, "label");
  if (value) {
    wrapText(label(col, "", 12, false, p.faint), lab, tw, 12);
    if (*value) wrapText(label(col, "", 15, true, p.fg), value, tw, 15, true);
  } else {
    wrapText(label(col, "", 15, true, p.fg), lab, tw, 15, true);
  }
  const char* help = text(f, "help");
  if (*help) wrapText(label(col, "", 12, false, p.faint), help, tw, 12);
  lv_obj_update_layout(col);
  lv_obj_set_height(r, max(46, (int)lv_obj_get_height(col) + 18));
  lv_obj_align(col, LV_ALIGN_LEFT_MID, 0, 0);
  if (cb) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, C(p.press), LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    if (rightW == 16) lv_obj_align(glyph(r, &gl_chev_16, p.faint), LV_ALIGN_RIGHT_MID, 4, 0);
  }
  return r;
}

// ---- editors (one at a time: edKey)
void openText(lv_event_t* e);
void openPick(lv_event_t* e);
void openList(lv_event_t* e);
void openTime(lv_event_t* e);
void openDate(lv_event_t* e);
void openColor(lv_event_t* e);
void openCity(lv_event_t* e);
void openSecret(lv_event_t* e);

const char* keyAt(lv_event_t* e) {
  JsonObjectConst f = fieldAt((int)(intptr_t)lv_event_get_user_data(e));
  edKey = f["key"] | "";
  return edKey.c_str();
}

lv_obj_t* editor(const char* title, lv_obj_t** body, int flags) {
  return os::screen(title, body, flags);  // a runtime label: shown as it is
}

void textDone(lv_obj_t* ta) {
  String v = lv_textarea_get_text(ta);
  JsonObjectConst f = edField();
  if (f.isNull()) return kb::close();
  if (!is(f, "text")) v.trim();
  if (is(f, "number")) v.replace(",", ".");
  saveBack(v);
}

// A string, url, text or number typed with the keyboard.
void openText(lv_event_t* e) {
  keyAt(e);
  JsonObjectConst f = edField();
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, os::NOSCROLL);
  const String v = valueOf(F.id.c_str(), f);
  lv_obj_t* ta = app_settings::field(body, is(f, "number") ? shownNum(v).c_str() : v.c_str(), false);
  if (is(f, "text")) {
    lv_textarea_set_one_line(ta, false);
    lv_obj_set_height(ta, os::H() - os::SB_H - os::NAV_H - kb::height() - 24);
  } else if (*text(f, "help")) {
    hint(body, text(f, "help"));
  }
  os::open(s);
  kb::open(ta, is(f, "url") || is(f, "number") ? kb::Mode::Plain : kb::Mode::Text, textDone, N_("Save"));
}

// The list being edited with its item `at` taken out (>= 0) or `add` added: stored, then the list screen again, the
// `backs` screens on top closed first.
void listEdit(int at, const char* add, int backs) {
  const String v = valueOf(F.id.c_str(), edField());
  JsonDocument d;
  deserializeJson(d, v.c_str());
  JsonArray a = d.is<JsonArray>() ? d.as<JsonArray>() : d.to<JsonArray>();
  if (at >= 0) a.remove(at);
  if (add) a.add(add);
  String out;
  serializeJson(d, out);
  if (!save(out)) return;
  kb::close();
  while (backs--) os::back();
  lv_async_call([](void*) { openList(nullptr); }, nullptr);
}

// ---- an enum's options (more than 3), or one to add to a list of enum items
bool pickForList = false;
void picked(lv_event_t* e) {
  JsonObjectConst f = edField();
  JsonObjectConst opts = pickForList ? f["of"].as<JsonObjectConst>() : f;
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  const char* v = opts["options"][i]["v"] | "";
  if (!pickForList) return saveBack(v);
  listEdit(-1, v, 2);  // the options and the list as it was close
}

void pickScreen(bool forList) {
  pickForList = forList;
  JsonObjectConst f = edField();
  JsonObjectConst opts = forList ? f["of"].as<JsonObjectConst>() : f;
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, os::TWO);
  const String v = valueOf(F.id.c_str(), f);
  int i = 0;
  for (JsonObjectConst o : opts["options"].as<JsonArrayConst>()) {
    const char* ov = o["v"] | "";
    const bool on = forList ? v.indexOf(String("\"") + ov + "\"") >= 0 : v == ov;
    if (!(forList && on)) {
      lv_obj_t* r = listRow(body, optionLabel(f, opts, ov), "", picked, (void*)(intptr_t)i, false);
      if (on) lv_obj_align(glyph(r, &gl_check_16, col::IOS_BLUE), LV_ALIGN_RIGHT_MID, 0, 0);
    }
    i++;
  }
  os::open(s);
}

void openPick(lv_event_t* e) {
  keyAt(e);
  pickScreen(false);
}

// ---- a list: its items (each with Remove) and Add
void listRemove(lv_event_t* e) { listEdit((int)(intptr_t)lv_event_get_user_data(e), nullptr, 1); }

void itemDone(lv_obj_t* ta) {
  String v = lv_textarea_get_text(ta);
  v.trim();
  if (!v.length()) return kb::close(), os::back();
  JsonObjectConst f = edField();
  const Check c = check(f["of"].isNull() ? JsonObjectConst() : f["of"].as<JsonObjectConst>(), v.c_str());
  if (!c && !f["of"].isNull()) return os::toast(reason(c).c_str());
  listEdit(-1, v.c_str(), 2);  // the item's field and the list as it was close
}

void listAdd(lv_event_t*) {
  JsonObjectConst f = edField();
  if (is(f["of"], "enum")) return pickScreen(true);
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, os::NOSCROLL);
  lv_obj_t* ta = app_settings::field(body, "", false);
  os::open(s);
  kb::open(ta, kb::Mode::Text, itemDone, N_("Add"));
}

void openList(lv_event_t* e) {
  if (e) keyAt(e);
  JsonObjectConst f = edField();
  if (f.isNull()) return;
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, os::TWO);
  const String cur = valueOf(F.id.c_str(), f);
  JsonDocument d;
  deserializeJson(d, cur.c_str());
  int i = 0;
  for (JsonVariantConst x : d.as<JsonArrayConst>()) {
    const char* v = x | "";
    lv_obj_t* r = listRow(body, is(f["of"], "enum") ? optionLabel(f, f["of"], v) : v, N_("Remove"), listRemove,
                          (void*)(intptr_t)i++, false);
    lv_obj_set_style_text_color(lv_obj_get_child(r, 1), C(col::RED), 0);
  }
  const int max = f["max_items"].is<int>() ? f["max_items"].as<int>() : LIST_MAX_ITEMS;
  if (!i) hint(body, N_("Nothing here yet."));
  if (i < max) lv_obj_set_width(button(body, N_("Add"), col::IOS_BLUE, 0xffffff, listAdd), lv_pct(100));
  else hint(body, i18n::fmt(N_("At most {0} items"), String(max)).c_str());
  os::open(s);
}

// ---- time and date: rollers, Save at the top right
lv_obj_t* roller(lv_obj_t* parent, const String& opts, int sel, int w) {
  lv_obj_t* r = lv_roller_create(parent);
  lv_roller_set_options(r, opts.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(r, 3);
  lv_roller_set_selected(r, sel, LV_ANIM_OFF);
  lv_obj_set_width(r, w);
  lv_obj_set_style_text_font(r, fonts::get(24, true), 0);
  lv_obj_set_style_text_font(r, fonts::get(24, true), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, C(pal().bg), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_text_color(r, C(pal().faint), 0);
  lv_obj_set_style_text_color(r, C(pal().fg), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, C(pal().cell), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
  lv_obj_set_style_radius(r, 12, LV_PART_SELECTED);
  lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
  return r;
}

String numbers(int from, int to) {
  String s;
  for (int i = from; i <= to; i++) s += (i > from ? "\n" : "") + String(i < 10 && from < 10 ? "0" : "") + i;
  return s;
}

lv_obj_t* rollerRow(lv_obj_t* body) {
  lv_obj_t* r = clear(body);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(r, 24, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 8, 0);
  return r;
}

void openTime(lv_event_t* e) {
  keyAt(e);
  JsonObjectConst f = edField();
  const String v = valueOf(F.id.c_str(), f);
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, os::NOSCROLL);
  os::navAction(s, N_("Save"), [](lv_event_t*) {
    if (save(hhmm(lv_roller_get_selected(edA), lv_roller_get_selected(edB)))) os::back();
  });
  lv_obj_t* r = rollerRow(body);
  edA = roller(r, numbers(0, 23), constrain(v.toInt(), 0, 23), 96);
  label(r, ":", 24, true, pal().fg);
  edB = roller(r, numbers(0, 59), constrain(v.substring(3).toInt(), 0, 59), 96);
  os::open(s);
}

int yearNow() {
  const time_t t = desk::nowMs() ? (time_t)(desk::nowMs() / 1000) : time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  return tm.tm_year + 1900 > 2000 ? tm.tm_year + 1900 : 2026;
}

void openDate(lv_event_t* e) {
  keyAt(e);
  JsonObjectConst f = edField();
  String v = valueOf(F.id.c_str(), f);
  const int y0 = yearNow();
  if (!v.length()) {
    char b[12];
    snprintf(b, sizeof b, "%04d-01-01", y0);
    v = b;
  }
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, os::NOSCROLL);
  os::navAction(s, N_("Save"), [](lv_event_t*) {
    char b[12];
    snprintf(b, sizeof b, "%04d-%02d-%02d", yearNow() - 100 + (int)lv_roller_get_selected(edC),
             (int)lv_roller_get_selected(edB) + 1, (int)lv_roller_get_selected(edA) + 1);
    if (save(b)) os::back();
  });
  lv_obj_t* r = rollerRow(body);
  String months;
  for (int m = 0; m < 12; m++) months += (m ? "\n" : "") + String(i18n::MON_SHORT[m]);
  edA = roller(r, numbers(1, 31), constrain(v.substring(8).toInt(), 1, 31) - 1, 64);
  edB = roller(r, months, constrain(v.substring(5).toInt(), 1, 12) - 1, 90);
  edC = roller(r, numbers(y0 - 100, y0 + 50), constrain(v.toInt() - (y0 - 100), 0, 150), 96);
  os::open(s);
}

// ---- a colour from swatches
const uint32_t SWATCH[] = {0xff3b30, 0xff9500, 0xffcc00, 0x34c759, 0x00c7be, 0x30b0c7, 0x0a84ff, 0x5856d6,
                           0xaf52de, 0xff2d55, 0xa2845e, 0x8e8e93, 0x1c1c1e, 0xffffff, 0xfde68a, 0xbfdbfe};
void openColor(lv_event_t* e) {
  keyAt(e);
  JsonObjectConst f = edField();
  const uint32_t now = strtoul(valueOf(F.id.c_str(), f).c_str() + 1, nullptr, 16);
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, os::NOSCROLL);
  lv_obj_t* g = clear(body);
  lv_obj_set_size(g, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(g, 12, 0);
  lv_obj_set_style_pad_top(g, 12, 0);
  lv_obj_set_flex_align(g, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (uint32_t c : SWATCH) {
    lv_obj_t* b = box(g, c, 26);
    lv_obj_set_size(b, 52, 52);
    lv_obj_set_style_border_width(b, c == now ? 4 : 1, 0);
    lv_obj_set_style_border_color(b, C(c == now ? col::IOS_BLUE : pal().line), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      char h[8];
      snprintf(h, sizeof h, "#%06lx", (unsigned long)(uintptr_t)lv_event_get_user_data(e));
      if (save(h)) os::back();
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)c);
  }
  os::open(s);
}

// ---- a city from the OS's list (the same as Settings › Weather)
void openCity(lv_event_t* e) {
  keyAt(e);
  JsonObjectConst f = edField();
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, 0);
  app_weather::placePicker(body, shown(f, valueOf(F.id.c_str(), f)).c_str(), [](const char* name, double lat, double lon) {
    JsonDocument d;
    d["name"] = name, d["lat"] = lat, d["lon"] = lon;
    String out;
    serializeJson(d, out);
    saveBack(out);
  });
  os::open(s);
}

// ---- a device secret: typed as dots, never shown again
void secretDone(lv_obj_t* ta) {
  const String v = lv_textarea_get_text(ta);
  if (!v.length()) return os::toast(N_("Type it first"));
  String why;
  if (!setSecret(F.id.c_str(), edKey.c_str(), v, why)) return os::toast(why.c_str());
  lv_textarea_set_text(ta, "");
  F.changed = true;
  refill();
  kb::close();
  os::back();
}

void openSecret(lv_event_t* e) {
  keyAt(e);
  JsonObjectConst f = edField();
  lv_obj_t* body;
  lv_obj_t* s = editor(text(f, "label"), &body, 0);
  lv_obj_t* ta = app_settings::field(body, "", true);
  lv_textarea_set_placeholder_text(ta, secret::has(F.id.c_str(), edKey.c_str()) ? T_("Type a new one") : "");
  // The keyboard on a tap (not at once): "From a computer" and "Clear" stay in sight, also in landscape.
  lv_obj_add_event_cb(ta, [](lv_event_t* e) {
    kb::open((lv_obj_t*)lv_event_get_target(e), kb::Mode::Password, secretDone, N_("Save"));
  }, LV_EVENT_CLICKED, nullptr);
  // Where the OS sends it: the hosts of its bindings' send_to (a "{key}" one as the address set now).
  String hosts;
  for (String h : sendTos(F.id.c_str(), edKey.c_str())) {
    if (h[0] == '{') h = get(F.id.c_str(), h.substring(1, h.indexOf('}')).c_str()) + h.substring(h.indexOf('}') + 1);
    if (!h.startsWith("https://")) continue;
    h = h.substring(8, strcspn(h.c_str() + 8, "/?#*") + 8);
    if (h.length() && hosts.indexOf(h) < 0) hosts += (hosts.length() ? ", " : "") + h;
  }
  hint(body, (hosts.length() ? i18n::fmt(N_("{0} can use it only for {1}. The app never sees it, and it is never sent to espos.dev."),
                                          appName(F.id.c_str()), hosts)
                             : i18n::fmt(N_("{0} never sees it, and it is never sent to espos.dev."), appName(F.id.c_str())))
                 .c_str());
#if __has_include("lanform.h")
  // From a computer on the same Wi-Fi: a one-time local page with a QR and a PIN (lane 1f).
  lv_obj_set_width(button(body, N_("From a computer"), pal().chip, pal().chipFg, [](lv_event_t*) {
    kb::close();
    lanform::open(F.id.c_str());
  }), lv_pct(100));
#endif
  if (secret::has(F.id.c_str(), edKey.c_str()))
    lv_obj_set_width(button(body, N_("Clear"), col::RED, 0xffffff, [](lv_event_t*) {
      secret::clear(F.id.c_str(), edKey.c_str());
      F.changed = true;
      refill();
      kb::close();
      os::back();
    }), lv_pct(100));
  os::open(s);
  if (!secret::has(F.id.c_str(), edKey.c_str()) && !os::landscape()) kb::open(ta, kb::Mode::Password, secretDone, N_("Save"));
}

// ---- the form's rows
void onBool(lv_event_t* e) {
  keyAt(e);
  save(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? "1" : "0");
}

void onChip(lv_event_t* e) {
  const intptr_t u = (intptr_t)lv_event_get_user_data(e);
  edKey = fieldAt((int)u)["key"] | "";
  const int i = lv_obj_get_index((lv_obj_t*)lv_event_get_current_target(e));
  save(edField()["options"][i]["v"] | "");
}

// v on the field's grid (min + n * step) as text, without float noise (0.3, not 0.30000000000000004).
String stepText(JsonObjectConst f, double v, double step) {
  const double base = f["min"].is<double>() ? num(f["min"]) : 0;
  char b[32];
  snprintf(b, sizeof b, "%.12g", base + std::round((v - base) / step) * step);
  numText(strtod(b, nullptr), b, sizeof b);
  return b;
}

void stepBy(lv_event_t* e) {
  const intptr_t u = (intptr_t)lv_event_get_user_data(e);
  edKey = fieldAt((int)(u >> 1))["key"] | "";
  JsonObjectConst f = edField();
  const double step = f["step"].is<double>() && num(f["step"]) > 0 ? num(f["step"]) : 1;
  double v = strtod(valueOf(F.id.c_str(), f).c_str(), nullptr) + (u & 1 ? step : -step);
  if (f["min"].is<double>() && v < num(f["min"])) v = num(f["min"]);
  if (f["max"].is<double>() && v > num(f["max"])) v = num(f["max"]);
  save(stepText(f, v, step));
}

void onSlider(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  edKey = fieldAt((int)(intptr_t)lv_event_get_user_data(e))["key"] | "";
  JsonObjectConst f = edField();
  const double step = f["step"].is<double>() && num(f["step"]) > 0 ? num(f["step"]) : 1;
  save(stepText(f, num(f["min"]) + lv_slider_get_value(sl) * step, step));
}

lv_obj_t* small(lv_obj_t* parent, const char* t, lv_event_cb_t cb, intptr_t data) {
  lv_obj_t* b = button(parent, t, pal().chip, pal().chipFg, cb, (void*)data);
  lv_obj_set_size(b, 40, 34);
  lv_obj_set_style_pad_hor(b, 0, 0);
  return b;
}

void addRow(lv_obj_t* body, JsonObjectConst f, int i) {
  const char* key = f["key"] | "";
  const String v = valueOf(F.id.c_str(), f);
  if (is(f, "section")) {
    caption(body, text(f, "label"));
    return;
  }
  if (is(f, "bool")) {
    lv_obj_t* r = row(body, f, nullptr, i, nullptr, 56);
    lv_obj_align(toggle(r, v == "1", onBool, (void*)(intptr_t)i), LV_ALIGN_RIGHT_MID, 0, 0);
    return;
  }
  if (is(f, "enum") && f["options"].size() <= 3) {
    lv_obj_t* r = row(body, f, "", i, nullptr, 0);
    lv_obj_t* col = lv_obj_get_child(r, 0);
    std::vector<String> names;
    for (JsonObjectConst o : f["options"].as<JsonArrayConst>()) names.push_back(optionLabel(f, f, o["v"] | ""));
    std::vector<const char*> ptrs;
    int sel = 0;
    for (size_t k = 0; k < names.size(); k++) {
      ptrs.push_back(names[k].c_str());
      if (v == (f["options"][k]["v"] | "")) sel = k;
    }
    lv_obj_t* bar = chips(col, ptrs.data(), ptrs.size(), sel, nullptr);
    lv_obj_move_to_index(bar, 1);  // under the label, over the help
    for (uint32_t k = 0; k < lv_obj_get_child_count(bar); k++)
      lv_obj_add_event_cb(lv_obj_get_child(bar, k), onChip, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_update_layout(col);
    lv_obj_set_height(r, lv_obj_get_height(col) + 18);
    return;
  }
  if (is(f, "number")) {
    const bool hasRange = f["min"].is<double>() && f["max"].is<double>();
    const double step = f["step"].is<double>() && num(f["step"]) > 0 ? num(f["step"]) : 1;
    const double span = hasRange ? (num(f["max"]) - num(f["min"])) / step : 1e9;
    if (span <= 100 && span >= 1 && std::fabs(span - std::round(span)) < 1e-6) {
      lv_obj_t* r = row(body, f, shown(f, v).c_str(), i, nullptr, 0);
      lv_obj_t* col = lv_obj_get_child(r, 0);
      lv_obj_t* sl = lv_slider_create(col);
      lv_obj_set_size(sl, lv_pct(94), 8);
      lv_obj_set_style_margin_ver(sl, 10, 0);
      lv_slider_set_range(sl, 0, (int)std::round(span));
      lv_slider_set_value(sl, (int)std::round((strtod(v.c_str(), nullptr) - num(f["min"])) / step), LV_ANIM_OFF);
      lv_obj_set_style_bg_color(sl, C(col::IOS_BLUE), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(sl, C(pal().line), LV_PART_MAIN);
      lv_obj_set_style_bg_color(sl, C(0xffffff), LV_PART_KNOB);
      lv_obj_set_style_border_width(sl, 1, LV_PART_KNOB);  // a white knob on a white row: its rim shows it
      lv_obj_set_style_border_color(sl, C(0xb0b6c0), LV_PART_KNOB);
      lv_obj_set_style_pad_all(sl, 7, LV_PART_KNOB);
      lv_obj_move_to_index(sl, 2);
      lv_obj_add_event_cb(sl, [](lv_event_t* e) {  // the value under the label follows the finger
        lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
        JsonObjectConst f = fieldAt((int)(intptr_t)lv_event_get_user_data(e));
        const double step = f["step"].is<double>() && num(f["step"]) > 0 ? num(f["step"]) : 1;
        setText(lv_obj_get_child(lv_obj_get_parent(sl), 1), shownNum(stepText(f, num(f["min"]) + lv_slider_get_value(sl) * step, step)).c_str());
      }, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
      lv_obj_add_event_cb(sl, onSlider, LV_EVENT_RELEASED, (void*)(intptr_t)i);
      lv_obj_update_layout(col);
      lv_obj_set_height(r, lv_obj_get_height(col) + 18);
      return;
    }
    lv_obj_t* r = row(body, f, shown(f, v).c_str(), i, openText, 92);
    lv_obj_align(small(r, "+", stepBy, i << 1 | 1), LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_align(small(r, "-", stepBy, i << 1), LV_ALIGN_RIGHT_MID, -46, 0);
    return;
  }
  if (is(f, "secret")) {
    const bool dev = !strcmp(f["where"] | "", "device"), has = secret::has(F.id.c_str(), key);
    const char* state = dev ? (has ? TC_("secret state", "Set") : T_("Not set"))
                            : (has ? T_("Set on the web") : T_("Kept on espos.dev"));
    lv_obj_t* r = row(body, f, state, i, dev ? openSecret : nullptr, dev ? 80 : 0);
    if (dev) lv_obj_align(label(r, has ? N_("Change") : N_("Set"), 14, true, col::IOS_BLUE), LV_ALIGN_RIGHT_MID, 0, 0);
    return;
  }
  lv_event_cb_t cb = is(f, "enum") ? openPick : is(f, "list") ? openList : is(f, "time") ? openTime
                   : is(f, "date") ? openDate : is(f, "color") ? openColor : is(f, "city") ? openCity
                   : is(f, "photo") ? nullptr : openText;
  lv_obj_t* r = row(body, f, shown(f, v).c_str(), i, cb, cb ? 16 : 0);
  if (is(f, "color") && v.length()) {
    lv_obj_t* dot = box(r, strtoul(v.c_str() + 1, nullptr, 16), 11);
    lv_obj_set_size(dot, 22, 22);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, C(pal().line), 0);
    lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -18, 0);
  }
}

void fill() {
  if (!F.body) return;
  F.ver = verNow();
  const int32_t sy = lv_obj_get_scroll_y(F.body);
  lv_obj_clean(F.body);
  lv_obj_update_layout(F.body);
  const int w = lv_obj_get_content_width(F.body);
  F.rowW = lv_obj_has_flag(F.body, LV_OBJ_FLAG_USER_1) ? (w - 8) / 2 : w;
  JsonArrayConst s = schema(F.id.c_str());
  const pkg::Package* p = pkg::find(F.id.c_str());
  if (s.isNull()) {
    hint(F.body, p && !p->available ? N_("Needs the memory card") : N_("This app has no settings."));
    return;
  }
  int i = 0;
  for (JsonObjectConst f : s) {
    if (i >= MAX_FIELDS) break;
    addRow(F.body, f, i++);
  }
  lv_obj_update_layout(F.body);
  lv_obj_scroll_to_y(F.body, sy, LV_ANIM_OFF);
}

std::vector<String> appIds;  // Settings › Apps' rows

}  // namespace

void openForm(const char* id) {
  F = Form();
  F.id = id;
  lv_obj_t* body;
  lv_obj_t* s = os::screen(appName(id).c_str(), &body, os::TWO);
  F.scr = s, F.body = body;
  lv_obj_set_style_pad_row(body, 8, 0);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != F.scr) return;  // an older form's screen going late
    F.scr = F.body = nullptr;
    if (F.changed) desk::syncNow();  // the changes go up now (the web shows them)
  }, LV_EVENT_DELETE, nullptr);
  fill();
  // A value that arrives from the web (a sync) while the form is open shows at once; never under a finger.
  every(s, 500, [](lv_timer_t*) {
    if (F.body && F.ver != verNow() && !os::fingerDown()) fill();
  });
  os::open(s);
}

void openApp(const char* id) {
  const pkg::Package* p = pkg::find(id);
  if (!p) return;
  static String appId;
  appId = id;
  lv_obj_t* body;
  lv_obj_t* s = os::screen(p->name.c_str(), &body, os::TWO);
  listRow(body, N_("Version"), p->version.c_str(), nullptr);
  if (p->author.length()) listRow(body, N_("Developer"), p->author.c_str(), nullptr);
  if (hasForm(id)) {
    // The first labels as a summary, like the mockup's "Brew time, sound".
    String sum;
    int n = 0;
    for (JsonObjectConst f : schema(id))
      if (!is(f, "section") && n++ < 2) sum += (sum.length() ? ", " : "") + String(text(f, "label"));
    listRow(body, N_("Settings"), sum.c_str(), [](lv_event_t*) { openForm(appId.c_str()); });
  } else {
    hint(body, p->available ? N_("This app has no settings.") : N_("Needs the memory card"));
  }
  os::open(s);
}

void openApps() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Apps"), &body, os::TWO);
  appIds.clear();
  for (const pkg::Package& p : pkg::list()) {
    if (p.type == "pack" || p.addonOf.length() || p.engine == "lang" || p.engine == "font") continue;
    appIds.push_back(p.id);
    listRow(body, p.name.c_str(), p.version.c_str(), [](lv_event_t* e) {
      const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
      if (i < appIds.size()) openApp(appIds[i].c_str());
    }, (void*)(intptr_t)(appIds.size() - 1));
  }
  if (appIds.empty()) hint(body, N_("No apps installed yet. The Store has some."));
  os::open(s);
}

}  // namespace appset
