#pragma GCC optimize("Os")  // editor screens: cold, built small
// A widget's options as pill rows from the registry (widopts.h). Used by Home's edit mode and the saver editor.
#include "widopts.h"

#include <ArduinoJson.h>

#include <deque>

#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "keyboard.h"
#include "os.h"
#include "uikit.h"

namespace widopts {
namespace {

using namespace kit;

// The state of one set of rows. Two sets can be on screen at once (a Home sheet left open under the Markets
// app's picker), so each pill carries its own set's state: the event loads it into these globals, runs the
// handler, and stores it back.
wid::Item cur;
Look lk;
Changed onChanged;
int rowW = 296;
int codesMost = 10;
bool forcedCustom = false;  // "Pick your own" tapped while the value was still a quick set
String lastOut;             // the value this picker made last (another value = another widget: start over)
void (*codesCb)(const String&) = nullptr;

struct Ctx {
  wid::Item cur;
  Look lk;
  Changed cb;
  int rowW, most;
  bool forced;
  String lastOut;
  void (*codesCb)(const String&);
};
struct Val {
  Ctx* ctx;
  String v;  // what the pill stands for
  lv_event_cb_t fn;
};
// Freed with the rows (a hidden child of the parent), so a pill's value lives exactly as long as the pill.
struct Pool {
  Ctx ctx;
  std::deque<Val> vals;
};
Pool* pool = nullptr;

void storeCtx(Ctx& c) { c = {cur, lk, onChanged, rowW, codesMost, forcedCustom, lastOut, codesCb}; }
void loadCtx(const Ctx& c) {
  cur = c.cur, lk = c.lk, onChanged = c.cb, rowW = c.rowW, codesMost = c.most, forcedCustom = c.forced;
  lastOut = c.lastOut, codesCb = c.codesCb;
}

void tramp(lv_event_t* e) {
  Val* v = (Val*)lv_event_get_user_data(e);
  loadCtx(*v->ctx);
  Ctx* c = v->ctx;
  v->fn(e);
  storeCtx(*c);  // callers rebuild later, never inside the callback, so the rows are still here
}

Val* keep_(const char* s, lv_event_cb_t fn) {
  pool->vals.push_back({&pool->ctx, s ? s : "", fn});
  return &pool->vals.back();
}

void newPool(lv_obj_t* parent) {
  lv_obj_t* a = lv_obj_create(parent);
  lv_obj_remove_style_all(a);
  lv_obj_set_size(a, 0, 0);
  lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
  pool = new Pool();
  own(a, pool);
}

void changed(bool size) {
  if (onChanged) onChanged(cur, size);
}

const char* arg(lv_event_t* e) { return ((Val*)lv_event_get_user_data(e))->v.c_str(); }

void caption(lv_obj_t* p, const char* t) {
  lv_obj_t* l = label(p, t, 13, true, lk.dim);
  lv_obj_set_style_pad_top(l, 4, 0);
}

lv_obj_t* row(lv_obj_t* p) {
  lv_obj_t* r = kit::clear(p);
  lv_obj_set_size(r, rowW, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(r, 6, 0);
  return r;
}

lv_obj_t* pill(lv_obj_t* r, const String& text, bool on, lv_event_cb_t fn, const char* data) {
  lv_obj_t* b = box(r, on ? lk.on : lk.pill, 14);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(b, 28, 0);
  lv_obj_set_style_pad_hor(b, 12, 0);
  lv_obj_set_style_pad_ver(b, 4, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(b, 3);
  lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t* l = label(b, "", 13, on, on ? lk.onText : lk.pillText);
  fitText(l, text.c_str(), rowW - 24, 20, 13, 12, on, 0);  // a long name wraps (never "…")
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(b, tramp, LV_EVENT_CLICKED, keep_(data, fn));
  return b;
}
// Text from the firmware image (N_ marked literals and tables) is translated; names made at run time stay as they are.
lv_obj_t* pill(lv_obj_t* r, const char* text, bool on, lv_event_cb_t fn, const char* data) {
  return pill(r, String(i18n::trl(text)), on, fn, data);
}

// "a,b,c" lists (flags, cities): whether id is in, and the list with id added or taken out.
bool hasItem(const String& list, const String& id) {
  for (const String& s : data::codesOf(list, "")) if (s == id) return true;
  return false;
}
String toggled(const String& list, const String& id, size_t most) {
  String out;
  bool had = false;
  for (const String& s : data::codesOf(list, "")) {
    if (s == id) {
      had = true;
      continue;
    }
    out += (out.length() ? "," : "") + s;
  }
  if (!had && data::codesOf(out, "").size() < most) out += (out.length() ? "," : "") + id;
  return out;
}

const char* const CITY_NAMES[] = {"Hà Nội",    "Tokyo",  "Seoul",    "Bắc Kinh", "Singapore", "Sydney",      "Dubai",
                                  "Paris",     "London", "Berlin",   "New York", "Los Angeles", "Bangkok", "Moskva"};
const char* const SETS[][2] = {{"", N_("Mixed")},
                               {"gold:SJC,gold:N24K,gold:PNJ,gold:KB,gold:TL", N_("Gold")},
                               {"stocks:VNINDEX,stocks:VN30,stocks:HNX,stocks:UPCOM", N_("Stocks")},
                               {"fx:USD,fx:EUR,fx:JPY,fx:CNY,fx:GBP,fx:AUD,fx:SGD", N_("Exchange rates")},
                               {"crypto:BTC,crypto:ETH,crypto:BNB,crypto:SOL", N_("Crypto")}};

// One value of the option: the pill sets it.
// A photo widget's option is "N" or "N|caption" (frame style): picking another picture keeps the caption.
void pickPhoto(lv_event_t* e) {
  const int bar = cur.opt.indexOf('|');
  cur.opt = String(arg(e)) + (bar >= 0 ? cur.opt.substring(bar) : String(""));
  changed(false);
}

void pickValue(lv_event_t* e) {
  cur.opt = arg(e);
  changed(false);
}

// ---------------------------------------------------------------- market codes (as the web's MarketCodes)
// "gold:SJC,stocks:FPT,fx:USD,crypto:BTC" in the order picked, or a quick set ("" = the built-in mix).
const char* const GOLD[] = {"SJC", "N24K", "PNJ", "KB", "TL", "24K", "999", "9920", "99",
                            "22K", "75",   "68",  "65", "61", "58.5", "41", "37.5", "33"};
const char* const INDEX[] = {"VNINDEX", "VN30", "HNX", "UPCOM"};
const char* const FX[] = {"USD", "EUR", "JPY", "CNY", "GBP", "AUD", "SGD", "KRW", "THB"};
const char* const COIN[] = {"BTC", "ETH", "BNB", "SOL", "XRP", "DOGE", "TON"};
bool isSet(const String& v) {
  for (const auto& s : SETS)
    if (v == s[0]) return true;
  return false;
}

void codeChanged() {
  lastOut = cur.opt;
  changed(false);
}

void toggleCode(lv_event_t* e) {
  const String tok = arg(e);
  if (!hasItem(cur.opt, tok) && (int)data::codesOf(cur.opt, "").size() >= codesMost)
    return os::toast(i18n::fmt(N_("At most {0} codes"), String(codesMost)).c_str());
  cur.opt = toggled(cur.opt, tok, codesMost);
  if (!cur.opt.length()) cur.opt = tok;  // the last one stays (empty would be the built-in mix)
  codeChanged();
}

void codeGroup(lv_obj_t* p, const char* title, const char* kind, const char* const* codes, int n) {
  caption(p, title);
  lv_obj_t* r = row(p);
  for (int i = 0; i < n; i++) {
    const String tok = String(kind) + ":" + codes[i];
    const String name = !strcmp(codes[i], "N24K") ? T_("Ring 9999") : !strcmp(codes[i], "VNINDEX") ? "VN-Index" : codes[i];  // i18n: gold rings, 99.99% (Nhẫn 9999)
    pill(r, name, hasItem(cur.opt, tok), toggleCode, tok.c_str());
  }
}

// only: a one-market widget (gold, stocks, fx, crypto): that market's codes, its defaults picked when empty.
void codeRows(lv_obj_t* p, const String& only = "") {
  if (only.length()) {
    if (!cur.opt.length()) cur.opt = wid::marketDefault(only);
  } else {
    if (cur.opt != lastOut) forcedCustom = false, lastOut = cur.opt;
    const bool custom = forcedCustom || !isSet(cur.opt);
    caption(p, N_("Quick sets"));
    lv_obj_t* r = row(p);
    for (const auto& s : SETS)
      pill(r, s[1], !custom && cur.opt == s[0], [](lv_event_t* e) {
        forcedCustom = false;
        cur.opt = arg(e);
        codeChanged();
      }, s[0]);
    pill(r, N_("Pick your own"), custom, [](lv_event_t*) {
      if (!cur.opt.length()) cur.opt = "stocks:VNINDEX,gold:SJC";
      forcedCustom = true;
      codeChanged();
    }, nullptr);
    if (!custom) return;
  }
  caption(p, i18n::fmt(N_("{0}/{1} picked, shown in this order"), String(data::codesOf(cur.opt, "").size()),
                       String(codesMost)).c_str());
  if (!only.length() || only == "gold") codeGroup(p, N_("Gold"), "gold", GOLD, sizeof GOLD / sizeof *GOLD);
  if (only.length() && only != "stocks") {
    if (only == "fx") codeGroup(p, N_("Exchange rates"), "fx", FX, sizeof FX / sizeof *FX);
    if (only == "crypto") codeGroup(p, N_("Crypto"), "crypto", COIN, sizeof COIN / sizeof *COIN);
    return;
  }
  codeGroup(p, N_("Indices"), "stocks", INDEX, 4);
  caption(p, N_("Shares"));
  lv_obj_t* st = row(p);
  for (const String& t : data::codesOf(cur.opt, "")) {
    if (!t.startsWith("stocks:")) continue;
    const String c = t.substring(7);
    bool index = false;
    for (const char* x : INDEX) index |= c == x;
    if (!index) pill(st, c, true, toggleCode, t.c_str());
  }
  pill(st, N_("+ Other code"), false, [](lv_event_t* e) {
    // A field in its own holder (the keyboard pads the field's first scrolling parent).
    lv_obj_t* host = lv_obj_get_parent(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e)));
    lv_obj_t* holder = kit::clear(host);
    lv_obj_set_size(holder, rowW, 52);
    lv_obj_move_to_index(holder, lv_obj_get_index(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e))) + 1);
    lv_obj_add_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(holder, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(holder, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t* ta = lv_textarea_create(holder);
    lv_obj_set_width(ta, rowW);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 10);
    lv_textarea_set_accepted_chars(ta, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    lv_obj_set_style_text_font(ta, fonts::get(16), 0);
    lv_obj_set_style_radius(ta, 12, 0);
    lv_textarea_set_placeholder_text(ta, T_("Code, e.g. FPT"));
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    lv_obj_scroll_to_view_recursive(holder, LV_ANIM_OFF);
    kb::open(ta, kb::Mode::Plain, [](lv_obj_t* t) {
      String c = lv_textarea_get_text(t);
      c.toUpperCase();
      kb::close();
      lv_obj_add_flag(lv_obj_get_parent(t), LV_OBJ_FLAG_HIDDEN);  // the caller's redraw deletes it
      if (!c.length()) return;
      const String tok = "stocks:" + c;
      if (hasItem(cur.opt, tok)) return;
      if ((int)data::codesOf(cur.opt, "").size() >= codesMost)
        return os::toast(i18n::fmt(N_("At most {0} codes"), String(codesMost)).c_str());
      cur.opt += (cur.opt.length() ? "," : "") + tok;
      codeChanged();
    }, T_("Add"));
  }, nullptr);
  if (only.length()) return;
  codeGroup(p, N_("Exchange rates"), "fx", FX, sizeof FX / sizeof *FX);
  codeGroup(p, N_("Crypto"), "crypto", COIN, sizeof COIN / sizeof *COIN);
}

// count: "YYYY-MM-DD|label".
struct DateOpt {
  int y, m, d;
  String label;
};
DateOpt dateOf(const String& opt) {
  const data::Day t = data::today();
  DateOpt o{t.y, t.m, t.d, ""};
  if (opt.length() >= 10 && opt[4] == '-') {
    o.y = opt.substring(0, 4).toInt(), o.m = opt.substring(5, 7).toInt(), o.d = opt.substring(8, 10).toInt();
    const int bar = opt.indexOf('|');
    if (bar >= 0) o.label = opt.substring(bar + 1);
  }
  return o;
}
String dateText(const DateOpt& o) {
  char b[16];
  snprintf(b, sizeof b, "%04d-%02d-%02d", o.y, o.m, o.d);
  return String(b) + (o.label.length() ? "|" + o.label : String(""));
}
int daysIn(int y, int m) {
  static const int D[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : D[m - 1];
}
void stepDate(lv_event_t* e) {
  DateOpt o = dateOf(cur.opt);
  const String a = arg(e);  // "d+" "m-" ...
  const int dir = a[1] == '+' ? 1 : -1;
  if (a[0] == 'd') o.d += dir;
  else if (a[0] == 'm') o.m += dir;
  else o.y += dir;
  if (o.m < 1) o.m = 12;
  if (o.m > 12) o.m = 1;
  o.y = constrain(o.y, 2000, 2099);
  if (o.d < 1) o.d = daysIn(o.y, o.m);
  if (o.d > daysIn(o.y, o.m)) o.d = a[0] == 'd' ? 1 : daysIn(o.y, o.m);
  cur.opt = dateText(o);
  changed(false);
}

void dateRows(lv_obj_t* p) {
  const DateOpt o = dateOf(cur.opt);
  caption(p, N_("Count down to"));
  lv_obj_t* r = row(p);
  const struct {
    char k;
    int v;
  } parts[3] = {{'d', o.d}, {'m', o.m}, {'y', o.y}};
  for (const auto& q : parts) {
    lv_obj_t* g = kit::clear(r);
    lv_obj_set_size(g, LV_SIZE_CONTENT, 28);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(g, 4, 0);
    pill(g, "−", false, stepDate, (String(q.k) + "-").c_str());
    label(g, String(q.v).c_str(), 14, true, lk.text);
    pill(g, "+", false, stepDate, (String(q.k) + "+").c_str());
  }
  caption(p, N_("Name"));
  lv_obj_t* r2 = row(p);
  pill(r2, o.label.length() ? o.label : String(T_("Add a name")), false, [](lv_event_t* e) {
    // The keyboard pads the field's first scrolling parent: give the field a holder of its own.
    lv_obj_t* host = lv_obj_get_parent(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e)));
    lv_obj_t* holder = kit::clear(host);
    lv_obj_set_size(holder, rowW, 52);
    lv_obj_add_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(holder, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(holder, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t* ta = lv_textarea_create(holder);
    lv_obj_set_width(ta, rowW);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 40);
    lv_obj_set_style_text_font(ta, fonts::get(16), 0);
    lv_obj_set_style_radius(ta, 12, 0);
    lv_textarea_set_text(ta, dateOf(cur.opt).label.c_str());
    lv_textarea_set_placeholder_text(ta, T_("e.g. Mum's birthday"));
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    lv_obj_scroll_to_view_recursive(holder, LV_ANIM_OFF);
    kb::open(ta, kb::Mode::Text, [](lv_obj_t* t) {
      DateOpt o = dateOf(cur.opt);
      o.label = lv_textarea_get_text(t);
      o.label.replace("|", "/");
      kb::close();
      lv_obj_add_flag(lv_obj_get_parent(t), LV_OBJ_FLAG_HIDDEN);  // the caller's redraw deletes it
      cur.opt = dateText(o);
      changed(false);
    });
  }, nullptr);
}

void optionRows(lv_obj_t* p, const wid::Kind& k) {
  switch (k.opt) {
    case wid::Opt::Codes:
      codesMost = 10;
      codeRows(p, wid::marketDefault(k.id).length() ? String(k.id) : String(""));
      break;
    case wid::Opt::Note: {
      caption(p, N_("Note"));
      lv_obj_t* r = row(p);
      pill(r, N_("Latest"), !cur.opt.length(), pickValue, "");
      JsonDocument doc;
      if (data::loadJson("s3.notes", doc))
        for (JsonObjectConst n : doc.as<JsonArrayConst>()) {
          const String id = n["id"] | "", title = n["title"] | "";
          String name = title.length() ? title : String(n["body"] | "");
          if (name.indexOf('\n') > 0) name = name.substring(0, name.indexOf('\n'));  // the first line of a note
          if (id.length()) pill(r, name.length() ? name : String(T_("Note")), cur.opt == id, pickValue, id.c_str());
        }
      break;
    }
    case wid::Opt::Photo: {
      caption(p, N_("Photo"));
      lv_obj_t* r = row(p);
      const auto ids = data::codesOf(desk::setting("s3.photos", ""), "");
      if (ids.empty()) label(r, N_("Add photos on the web, under Photos"), 13, false, lk.dim);
      for (size_t i = 0; i < ids.size(); i++)
        pill(r, i18n::fmt(N_("Photo {0}"), String(i + 1)), cur.opt.toInt() == (int)i, pickPhoto, String(i).c_str());
      break;
    }
    case wid::Opt::Mine: {
      caption(p, N_("My widget"));
      lv_obj_t* r = row(p);
      for (const wid::Mine& m : wid::mine())
        pill(r, m.title.length() ? m.title : m.text.length() ? m.text.substring(0, 20) : m.type, cur.opt == m.id, pickValue,
             m.id.c_str());
      break;
    }
    case wid::Opt::Cities: {
      caption(p, N_("Cities (up to 4)"));
      lv_obj_t* r = row(p);
      const String now = cur.opt.length() ? cur.opt : desk::setting("s3.cities", "Tokyo,Paris,New York,Sydney");
      for (const char* c : CITY_NAMES)
        pill(r, c, hasItem(now, c), [](lv_event_t* e) {
          const String now = cur.opt.length() ? cur.opt : desk::setting("s3.cities", "Tokyo,Paris,New York,Sydney");
          cur.opt = toggled(now, arg(e), 4);
          changed(false);
        }, c);
      break;
    }
    case wid::Opt::Section: {
      caption(p, N_("News section"));
      lv_obj_t* r = row(p);
      pill(r, N_("All"), !cur.opt.length(), pickValue, "");
      for (JsonObjectConst s : desk::dash()["news"]["sections"].as<JsonArrayConst>()) {
        const String slug = s["slug"] | "";
        if (slug.length()) pill(r, s["name"] | slug.c_str(), cur.opt == slug, pickValue, slug.c_str());
      }
      break;
    }
    case wid::Opt::Date:
      dateRows(p);
      break;
    default:
      break;
  }
}

}  // namespace

void codes(lv_obj_t* parent, const String& value, const Look& look, int width, void (*cb)(const String& value), int most) {
  cur = wid::Item();
  cur.opt = value;
  lk = look;
  rowW = width;
  newPool(parent);
  codesCb = cb;
  codesMost = most;
  onChanged = [](const wid::Item& it, bool) {
    if (codesCb) codesCb(it.opt);
  };
  codeRows(parent);
  storeCtx(pool->ctx);
}

void build(lv_obj_t* parent, const wid::Item& it, const Look& look, int width, Changed cb, bool sizes, int cols, int rows) {
  cur = it;
  lk = look;
  onChanged = cb;
  rowW = width;
  codesCb = nullptr;
  newPool(parent);
  const wid::Kind* k = wid::kind(it.kind);
  if (!k) return;
  int cw, ch;
  wid::spanOf(it, cw, ch);
  const bool other = !wid::legacy(cw, ch);
  if (!k->styles.empty()) {
    caption(parent, N_("Style"));
    lv_obj_t* r = row(parent);
    const String st = other ? wid::styleAt(it) : wid::styleOf(it);
    for (const wid::Style& s : k->styles)
      if (!other || (s.spans & wid::SPAN(cw, ch)))
        pill(r, s.name, st == s.id, [](lv_event_t* e) {
          cur.style = arg(e);
          changed(false);
        }, s.id);
  }
  if (sizes && cols > 0) {  // Home: every span the look draws ("Small", "3 × 1"...)
    caption(parent, N_("Size"));
    lv_obj_t* r = row(parent);
    const String st = wid::styleOf(it);
    static const char* const SZ[3] = {N_("Small"), N_("Medium"), N_("Large")};
    for (int b = 1; b <= 4 && b <= rows; b++)
      for (int a = 1; a <= 4 && a <= cols; a++) {
        if (!wid::draws(it.kind, a, b, st.c_str())) continue;
        wid::Size l;
        const char id[3] = {(char)('0' + a), (char)('0' + b), 0};
        const String name = wid::legacy(a, b, &l) ? String(i18n::trl(SZ[l])) : String(a) + " × " + b;
        pill(r, name, a == cw && b == ch, [](lv_event_t* e) {
          const char* v = arg(e);
          int a0, b0;
          wid::spanOf(cur, a0, b0);
          if (v[0] - '0' == a0 && v[1] - '0' == b0) return;
          wid::setSpan(cur, v[0] - '0', v[1] - '0');
          changed(true);
        }, id);
      }
  } else if (sizes && (k->sizes & (k->sizes - 1))) {  // more than one size
    caption(parent, N_("Size"));
    lv_obj_t* r = row(parent);
    static const char* const SZ[3] = {N_("Small"), N_("Medium"), N_("Large")};
    static const char* const ID[3] = {"0", "1", "2"};
    for (int i = 0; i < 3; i++)
      if (k->sizes & (1 << i))
        pill(r, SZ[i], it.size == i, [](lv_event_t* e) {
          const wid::Size s = (wid::Size)atoi(arg(e));
          if (s == cur.size) return;
          cur.size = s;
          changed(true);
        }, ID[i]);
  }
  if (!k->flags.empty()) {
    caption(parent, N_("Options"));
    lv_obj_t* r = row(parent);
    for (const wid::Style& f : k->flags)
      pill(r, f.name, wid::flag(it, f.id), [](lv_event_t* e) {
        cur.opt = toggled(cur.opt, arg(e), 8);
        changed(false);
      }, f.id);
  }
  optionRows(parent, *k);
  caption(parent, N_("Card"));  // i18n: the look of a widget's card (Thẻ)
  lv_obj_t* r = row(parent);
  for (const wid::Style& s : wid::skins())
    pill(r, s.name, it.skin == s.id, [](lv_event_t* e) {
      cur.skin = arg(e);
      changed(false);
    }, s.id);
  lv_obj_t* sw = row(parent);
  for (uint32_t c : wid::skinColours()) {
    char b[10];
    snprintf(b, sizeof b, "c:%06lx", (unsigned long)c);
    const bool on = it.skin == b;
    lv_obj_t* s = box(sw, c, 12);
    lv_obj_set_size(s, 24, 24);
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_border_color(s, C(on ? lk.text : lk.pill), 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s, 3);
    lv_obj_add_event_cb(s, tramp, LV_EVENT_CLICKED, keep_(b, [](lv_event_t* e) {
      cur.skin = arg(e);
      changed(false);
    }));
  }
  storeCtx(pool->ctx);
}

}  // namespace widopts
