// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
// The Storage page, like a phone's storage screen: a bar per volume (games, apps, other, free),
// the installed apps by size, and a sheet to remove an app or move it between Máy and Thẻ nhớ.
// Drawn after ~/projects/xt/mockups/s3-store.html. Also the small pieces the store shares (ui.h).
#include <lvgl.h>

#include <algorithm>

#include "apps.h"
#include "art.h"
#include "fonts.h"
#include "internal.h"
#include "os.h"
#include "store.h"
#include "ui.h"
#include "uikit.h"

#ifndef SIM
#include "esp_system.h"
#endif

namespace pkg {
namespace ui {

using namespace kit;

constexpr uint32_t GAMES = 0xff9500, APPS = 0x0a84ff, OTHER = 0xb0b5bf, PENDING = 0xf5c58a;

Usage usage(Vol v) {
  Usage u;
  const VolInfo i = volume(v);
  u.present = i.present;
  if (!i.present) return u;
  u.total = (uint64_t)i.totalKB * 1024;
  u.freeB = (uint64_t)i.freeKB * 1024;
  for (const Package& p : list())
    if (p.vol == v) (p.category == "game" ? u.games : u.apps) += p.bytes;
  if (v == FLASH) u.pending = (uint64_t)restartKB() * 1024;
  const uint64_t used = u.total > u.freeB ? u.total - u.freeB : 0;
  u.other = used > u.games + u.apps + u.pending ? used - u.games - u.apps - u.pending : 0;
  return u;
}

lv_obj_t* bar(lv_obj_t* parent, const Usage& u, int h) {
  lv_obj_t* b = box(parent, pal().line, h / 2);
  lv_obj_set_size(b, lv_pct(100), h);
  lv_obj_set_style_clip_corner(b, true, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  if (!u.total) return b;
  lv_obj_update_layout(parent);
  const int w = lv_obj_get_content_width(parent);
  const uint64_t parts[4] = {u.games, u.apps, u.other, u.pending};
  const uint32_t cols[4] = {GAMES, APPS, OTHER, PENDING};
  for (int k = 0; k < 4; k++) {
    if (!parts[k]) continue;
    lv_obj_t* s = box(b, cols[k], 0);
    lv_obj_set_size(s, max<int>(2, (int)((double)parts[k] * w / u.total)), h);
  }
  return b;
}

String permsText(const std::vector<String>& perms) {
  String s;
  for (const String& p : perms) {
    const char* t = p == "sound" ? T_("Play sounds") : p == "notify" ? T_("Send notifications") : p == "net" ? T_("Use the network")
                    : p == "bg" ? T_("Run when closed") : nullptr;
    if (!t || s.indexOf(t) >= 0) continue;
    if (s.length()) s += " · ";
    s += t;
  }
  return s.length() ? s : String(T_("No permissions needed"));
}

const char* categoryName(const String& id) {
  if (id == "game") return T_("Games");
  if (id == "study") return T_("Study");
  if (id == "health") return T_("Health");
  if (id == "widget") return T_("Widgets");
  return T_("Tools");
}

const char* volName(Vol v) { return v == SD ? T_("Memory card") : T_("Device"); }

lv_obj_t* pill(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* data, int h) {
  lv_obj_t* o = box(parent, bg, h / 2);
  lv_obj_set_size(o, LV_SIZE_CONTENT, h);
  lv_obj_set_style_pad_hor(o, 14, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(o, 8);
  lv_obj_set_style_bg_opa(o, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t* l = label(o, text, 13, true, fg);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, data);
  return o;
}

String glue(const String& s) {
  String g = s;
  g.replace(" ", "\xC2\xA0");
  return g;
}

int wrapWords(lv_obj_t* l, const String& text, int w, int px, bool bold) {
  const lv_font_t* f = fonts::text(px, bold);
  auto width = [&](const String& t) {
    String v = t;
    v.replace("\xC2\xA0", " ");
    return (int)lv_text_get_width(v.c_str(), v.length(), f, 0);
  };
  String out, line;
  int n = 0;
  for (unsigned i = 0, start = 0; i <= text.length(); i++) {
    if (i < text.length() && text[i] != ' ' && text[i] != '\n') continue;
    const String word = text.substring(start, i);
    const String cand = line.length() ? line + " " + word : word;
    if (!line.length() || width(cand) <= w) {
      line = cand;
    } else {
      out += (n++ ? "\n" : "") + line;
      line = word;
    }
    if (i < text.length() && text[i] == '\n') out += (n++ ? "\n" : "") + line, line = "";
    start = i + 1;
  }
  if (line.length()) out += (n++ ? "\n" : "") + line;
  out.replace("\xC2\xA0", " ");
  // A word wider than the line (Chinese and Japanese have no spaces): LVGL's own breaking instead.
  for (int a = 0, b; a <= (int)out.length(); a = b + 1) {
    b = out.indexOf('\n', a);
    if (b < 0) b = out.length();
    if (width(out.substring(a, b)) > w) {
      String plain = text;
      plain.replace("\xC2\xA0", " ");
      return wrapText(l, plain.c_str(), w, px, bold, 2);
    }
  }
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_line_space(l, 2, 0);
  setText(l, out.c_str());
  return n;
}

const char* appOf(const String& pkgId) {
  for (const apps::App& a : apps::all())
    if (a.pkg && pkgId == a.pkg) return a.id;
  return nullptr;
}

void restartDevice() {
#ifdef SIM
  os::toast(N_("The device would restart now (simulator)"));
#else
  os::toast(N_("Restarting"));
  lv_timer_create([](lv_timer_t*) { esp_restart(); }, 800, nullptr);
#endif
}

}  // namespace ui

namespace {

using namespace kit;
namespace st = store;

lv_obj_t *scr_ = nullptr, *body_ = nullptr, *sheet = nullptr;
std::vector<String> ids;
String sheetId;
bool confirmRm = false;
uint32_t seenPkg = 0, seenJob = 0;
st::State shownState = st::IDLE;

void fill();
void openSheet(const String& id);

void refillSoon() {
  lv_async_call([](void*) { fill(); }, nullptr);
}

void volCard(lv_obj_t* parent, Vol v) {
  const Pal& p = pal();
  const ui::Usage u = ui::usage(v);
  lv_obj_t* c = cell(parent, 12);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(c, 8, 0);
  lv_obj_t* top = clear(c);
  lv_obj_set_size(top, lv_pct(100), 20);
  label(top, ui::volName(v), 15, true, p.fg);
  const String used = sizeText(u.total - u.freeB) + " / " + sizeText(u.total);
  lv_obj_t* r = label(top, used.c_str(), 13, false, p.faint);
  lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
  ui::bar(c, u, 10);
  // Legend.
  lv_obj_t* leg = clear(c);
  lv_obj_set_size(leg, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(leg, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(leg, 12, 0);
  lv_obj_set_style_pad_row(leg, 4, 0);
  struct L {
    uint64_t n;
    uint32_t col;
    const char* name;
  };
  const L ls[] = {{u.games, ui::GAMES, N_("Games")}, {u.apps, ui::APPS, N_("Apps")}, {u.other, ui::OTHER, N_("Other")},
                  {u.pending, ui::PENDING, N_("After restart")}, {u.freeB, p.line, N_("Free")}};
  for (const L& l : ls) {
    if (!l.n && l.col != p.line) continue;
    lv_obj_t* it = stack(leg, true, 4);
    lv_obj_t* dot = box(it, l.col, 2);
    lv_obj_set_size(dot, 8, 8);
    label(it, (String(T_(l.name)) + " " + sizeText(l.n)).c_str(), 12, false, p.dim);
  }
}

void onRow(lv_event_t* e) {
  const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i < ids.size()) openSheet(ids[i]);
}

void fill() {
  if (!body_) return;
  const Pal& p = pal();
  const int32_t sy = lv_obj_get_scroll_y(body_);
  lv_obj_clean(body_);
  ids.clear();
  seenPkg = version();
  const st::Status js = st::status();
  seenJob = js.seq;
  shownState = st::busy() ? js.state : st::IDLE;

  volCard(body_, FLASH);
  if (volume(SD).present) {
    volCard(body_, SD);
  } else {
    lv_obj_t* c = cell(body_, 12);
    lv_obj_t* l = label(c, "", 13, false, p.faint);
    wrapText(l, in::card::bad() ? N_("The memory card is in but cannot be read. It needs to be FAT32.")
                                : N_("No memory card. Insert one (FAT32) to install big games and move apps to it."),
             os::W() - 48, 13);
  }
  // Room that comes back after a restart (apps removed while in use).
  if (const uint32_t kb = restartKB()) {
    lv_obj_t* c = cell(body_, 12);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(c, 8, 0);
    lv_obj_t* l = label(c, "", 13, false, p.dim);
    ui::wrapWords(l, i18n::fmt(N_("{0} of removed apps will be free after a restart."), ui::glue(sizeText((uint64_t)kb * 1024))),
                  os::W() - 48, 13);
    lv_obj_t* b = button(c, N_("Restart"), col::IOS_BLUE, 0xffffff, [](lv_event_t*) { ui::restartDevice(); });
    lv_obj_set_width(b, lv_pct(100));
  }
  // The running job.
  if (st::busy() && js.state != st::DONE && js.state != st::ERROR) {
    lv_obj_t* c = cell(body_, 12);
    String t = String(st::stateText(js.state)) + " " + js.name;
    if (js.state != st::REMOVE) t += " " + String(js.pct) + "%";
    lv_obj_t* l = label(c, "", 13, true, p.fg);
    fitText(l, t.c_str(), os::W() - 48, 20, 13, 11, true);
  }
  // The apps by size (with their add-ons), each add-on right under its app.
  std::vector<const Package*> top;
  auto total = [](const Package* a) {
    uint64_t n = a->bytes;
    for (const Package& q : list())
      if (q.addonOf == a->id) n += q.bytes;
    return n;
  };
  for (const Package& pk : list())
    if (!pk.addonOf.length() || !find(pk.addonOf.c_str())) top.push_back(&pk);
  std::sort(top.begin(), top.end(), [&](const Package* a, const Package* b) { return total(a) > total(b); });
  std::vector<const Package*> all;
  for (const Package* t : top) {
    all.push_back(t);
    for (const Package& q : list())
      if (q.addonOf == t->id) all.push_back(&q);
  }
  caption(body_, N_("Installed apps"));
  if (all.empty()) hint(body_, N_("No apps installed yet. Open the Store to get some."));
  for (const Package* pk : all) {
    ids.push_back(pk->id);
    const bool sub = pk->addonOf.length() && find(pk->addonOf.c_str());
    lv_obj_t* r = box(body_, p.cell, 14);
    lv_obj_set_size(r, sub ? lv_pct(90) : lv_pct(100), 54);
    if (sub) lv_obj_set_style_margin_left(r, os::W() / 10 - 4, 0);  // under its app
    lv_obj_set_style_pad_hor(r, 10, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, C(p.press), LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, onRow, LV_EVENT_CLICKED, (void*)(intptr_t)(ids.size() - 1));
    lv_obj_t* ic = apps::packageIcon(r, pk->id.c_str(), 36);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
    if (!pk->available) lv_obj_set_style_opa(ic, LV_OPA_40, 0);
    lv_obj_t* n = label(r, "", 14, true, p.fg);
    fitText(n, pk->name.c_str(), os::W() - 24 - 20 - 46 - 90 - (sub ? os::W() / 10 : 0), 18, 14, 11, true);
    lv_obj_set_pos(n, 46, 7);
    lv_obj_t* tag = box(r, p.cell2, 6);
    lv_obj_set_size(tag, LV_SIZE_CONTENT, 18);
    lv_obj_set_style_pad_hor(tag, 6, 0);
    lv_obj_t* tl = label(tag, pk->available ? ui::volName(pk->vol) : N_("Card not inserted"), 11, true, p.dim);
    lv_obj_center(tl);
    lv_obj_set_pos(tag, 46, 28);
    lv_obj_t* z = label(r, sizeText(pk->bytes).c_str(), 13, false, p.faint);
    lv_obj_align(z, LV_ALIGN_RIGHT_MID, -18, 0);
    lv_obj_t* ch = glyph(r, &gl_chev_16, p.faint);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  lv_obj_update_layout(body_);
  lv_obj_scroll_to_y(body_, sy, LV_ANIM_OFF);
}

// ---------------------------------------------------------------- the sheet of one app
void closeSheet() {
  if (sheet) lv_obj_delete(sheet);
  sheet = nullptr;
  sheetId = "";
  confirmRm = false;
}

void sheetDone() {
  lv_async_call([](void*) {
    closeSheet();
    fill();
  }, nullptr);
}

void openSheet(const String& id) {
  const Package* pk = find(id.c_str());
  if (!pk || !scr_) return;
  if (sheet) lv_obj_delete(sheet);
  sheetId = id;
  const Pal& p = pal();
  // A dim layer over the page; a tap on it closes the sheet.
  sheet = lv_obj_create(scr_);
  lv_obj_remove_style_all(sheet);
  lv_obj_set_size(sheet, os::W(), os::H());
  lv_obj_set_style_bg_color(sheet, C(0x000000), 0);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_40, 0);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sheet, [](lv_event_t* e) {
    if (lv_event_get_target(e) == sheet) lv_async_call([](void*) { closeSheet(); }, nullptr);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* s = box(sheet, p.cell, 18);
  lv_obj_set_size(s, os::W(), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(s, 14, 0);
  lv_obj_set_style_pad_bottom(s, 24, 0);
  lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(s, 8, 0);
  lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);  // taps on the sheet stay on it
  lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, 18);
  lv_obj_t* hdr = clear(s);
  lv_obj_set_size(hdr, lv_pct(100), 56);
  lv_obj_t* ic = apps::packageIcon(hdr, id.c_str(), 52);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* n = label(hdr, "", 18, true, p.fg);
  fitText(n, pk->name.c_str(), os::W() - 28 - 64, 24, 18, 13, true);
  lv_obj_set_pos(n, 64, 6);
  String sub = i18n::fmt(N_("{0} · on {1}"), sizeText(pk->bytes), ui::volName(pk->vol));
  if (pk->version.length()) sub += i18n::fmt(N_(" · version {0}"), pk->version);
  lv_obj_t* sl = label(hdr, "", 13, false, p.faint);
  fitText(sl, sub.c_str(), os::W() - 28 - 64, 18, 13, 11);
  lv_obj_set_pos(sl, 64, 32);
  const st::State state = st::stateOf(id.c_str());
  if (state != st::IDLE) {
    lv_obj_t* l = label(s, "", 14, false, p.dim);
    wrapText(l, N_("This app is being installed, removed or moved. Please wait."), os::W() - 28, 14);
  } else {
    if (volume(SD).present && pk->available) {
      lv_obj_t* mv = button(s, pk->vol == SD ? N_("Move to device") : N_("Move to memory card"), p.cell2, p.fg, [](lv_event_t*) {
        const Package* q = find(sheetId.c_str());
        String why;
        if (q && !st::move(sheetId.c_str(), q->vol == SD ? FLASH : SD, why)) os::toast(why.c_str());
        sheetDone();
      });
      lv_obj_set_width(mv, lv_pct(100));
    }
    lv_obj_t* rm = button(s, N_("Remove app"), p.dark ? 0x3a1d1d : 0xffe5e3, col::RED, [](lv_event_t* e) {
      if (!confirmRm) {
        confirmRm = true;
        lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
        lv_obj_set_style_bg_color(b, C(col::RED), 0);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        lv_label_set_text(l, T_("Tap again to remove"));
        lv_obj_set_style_text_color(l, C(0xffffff), 0);
        return;
      }
      String why;
      if (!st::remove(sheetId.c_str(), why)) os::toast(why.c_str());
      sheetDone();
    });
    lv_obj_set_width(rm, lv_pct(100));
    // An app with add-ons: remove them too, in one tap.
    String names;
    for (const Package& q : list())
      if (q.addonOf == id) names += String(names.length() ? ", " : "") + q.name;
    if (names.length()) {
      lv_obj_t* all = button(s, "", p.dark ? 0x3a1d1d : 0xffe5e3, col::RED, [](lv_event_t* e) {
        static lv_obj_t* asked = nullptr;  // tapped once already (same two taps as "Remove app")
        lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
        if (asked != b) {
          asked = b;
          lv_obj_set_style_bg_color(b, C(col::RED), 0);
          lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C(0xffffff), 0);
          ui::wrapWords(lv_obj_get_child(b, 0), T_("Tap again to remove all"), os::W() - 60, 15, true);
          return;
        }
        asked = nullptr;
        String why;
        std::vector<String> drop = {sheetId};
        for (const Package& q : list())
          if (q.addonOf == sheetId) drop.push_back(q.id);
        for (const String& d : drop)
          if (!st::remove(d.c_str(), why)) os::toast(why.c_str());
        sheetDone();
      });
      lv_obj_set_width(all, lv_pct(100));
      lv_obj_set_height(all, LV_SIZE_CONTENT);
      lv_obj_set_style_pad_ver(all, 10, 0);
      lv_obj_t* l = lv_obj_get_child(all, 0);
      ui::wrapWords(l, i18n::fmt(N_("Also remove {0}"), names), os::W() - 60, 15, true);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
  }
  lv_obj_t* cl = button(s, N_("Close"), p.cell2, p.link, [](lv_event_t*) { lv_async_call([](void*) { closeSheet(); }, nullptr); });
  lv_obj_set_width(cl, lv_pct(100));
  lv_obj_t* h = label(s, "", 12, false, p.faint);
  wrapText(h, N_("Removing an app keeps its data (levels, coins, notes) on the device; install it again to carry on."), os::W() - 28, 12);
}

void watch(lv_timer_t*) {
  if (!body_) return;
  const st::Status js = st::status();
  const st::State now = st::busy() ? js.state : st::IDLE;
  if (seenPkg != version() || now != shownState || (now != st::IDLE && js.seq != seenJob)) {
    fill();
    if (sheet && sheetId.length() && !find(sheetId.c_str())) closeSheet();
  }
}

}  // namespace

void openStorage() {
  begin();
  st::refresh();
  lv_obj_t* body;
  scr_ = os::screen(N_("Storage"), &body, 0);
  body_ = body;
  sheet = nullptr;
  lv_obj_add_event_cb(scr_, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == scr_) scr_ = body_ = sheet = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fill();
  every(scr_, 300, watch);
  os::open(scr_);
}

}  // namespace pkg
