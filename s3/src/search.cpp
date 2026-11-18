#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "search.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <vector>

#include "apps.h"
#include "art.h"
#include "data.h"
#include "fonts.h"
#include "keyboard.h"
#include "os.h"
#include "trace.h"
#include "uikit.h"

namespace search {
namespace {

using namespace kit;

// ---------------------------------------------------------------- folding Vietnamese letters
struct Fold {
  uint32_t cp;
  char base;
};
std::vector<Fold> folds;

uint32_t nextCp(const char*& p) {
  const uint8_t c = *p;
  if (c < 0x80) return p++, c;
  int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
  uint32_t cp = c & (0x3F >> n);
  p++;
  while (n-- && (*p & 0xC0) == 0x80) cp = cp << 6 | (*p++ & 0x3F);
  return cp;
}

void buildFolds() {
  static const struct {
    char base;
    const char* all;
  } T[] = {
      {'a', "àáảãạăằắẳẵặâầấẩẫậÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬ"}, {'e', "èéẻẽẹêềếểễệÈÉẺẼẸÊỀẾỂỄỆ"},
      {'i', "ìíỉĩịÌÍỈĨỊ"}, {'o', "òóỏõọôồốổỗộơờớởỡợÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢ"},
      {'u', "ùúủũụưừứửữựÙÚỦŨỤƯỪỨỬỮỰ"}, {'y', "ỳýỷỹỵỲÝỶỸỴ"}, {'d', "đĐ"},
  };
  for (const auto& t : T)
    for (const char* p = t.all; *p;) folds.push_back({nextCp(p), t.base});
  std::sort(folds.begin(), folds.end(), [](const Fold& a, const Fold& b) { return a.cp < b.cp; });
}

// ---------------------------------------------------------------- what can be found
struct Item {
  String name, sub, key;  // key: folded name (shown language and English) + extra words
  uint8_t kind = 0;       // 0 app, 1 settings page, 2 reminder, 3 note (the groups, in this order)
  String app, arg;        // opens apps::open(app, arg)
  uint32_t color = 0;
  const lv_image_dsc_t* glyph = nullptr;  // settings, reminders, notes; apps draw their icon
};

struct SettingsPage {
  const char* name;
  const char* words;
  const char* arg;
  uint32_t color;
  uint16_t glyph;  // art.h image number (the table stays in flash)
};
// Names and search words are translated where they are used. i18n: the second text of each row is search words:
// lower case, space-separated, what people would type to find that settings page (accents are ignored).
const SettingsPage PAGES[] = {
    {"WiFi", N_("network internet connect wireless"), "wifi", 0x0a84ff, ART_gl_wifi3_16},
    {N_("EspOS account"), N_("link web sync account"), "account", 0x34c759, ART_gl_sync_16},
    {N_("Appearance"), N_("wallpaper colour color light dark theme"), "look", 0x5e5ce6, ART_gl_photo_16},
    {N_("Home Screen"), N_("home page reset"), "home", 0x64748b, ART_gl_grid_16},
    {N_("Display"), N_("brightness rotate landscape portrait"), "display", 0x0a84ff, ART_gl_sun_16},
    {N_("Screen saver"), N_("screen off picture clock"), "saver", 0x5e5ce6, ART_gl_moon_16},
    {N_("Sound"), N_("volume speaker sound"), "sound", 0xff3b30, ART_gl_speaker_16},
    {N_("Notifications"), N_("notifications alerts on off"), "notif", 0xff3b30, ART_gl_bell_16},
    {N_("Storage"), N_("memory card space full remove apps"), "storage", 0x8e8e93, ART_gl_list_16},
    {N_("Software update"), N_("version firmware"), "update", 0x8e8e93, ART_gl_gear_16},
    {N_("Device name"), N_("name rename"), "name", 0x8e8e93, ART_gl_edit_16},
    {N_("About"), N_("version memory battery"), "about", 0x8e8e93, ART_gl_power_16},
    {N_("Time and data"), N_("time zone night quiet hours data refresh"), "time", 0x0a84ff, ART_gl_timer_16},
    {N_("Weather city"), N_("weather city location"), "city", 0x3b82f6, ART_gl_sunrise_16},
    {N_("Linked calendars"), N_("calendar google icloud outlook ics"), "calendars", 0xff3b30, ART_gl_flag_16},
    {N_("News sections"), N_("news papers sections"), "news", 0xc2410c, ART_gl_globe_16},
    {N_("Market codes"), N_("gold stocks exchange rates crypto codes"), "markets", 0x16a34a, ART_gl_list_16},
    {N_("Photos on device"), N_("photos pictures"), "photos", 0x34c759, ART_gl_photo_16},
};

std::vector<Item> items;     // everything, built when Search opens
std::vector<String> recent;  // apps opened from Search, newest first

void collect() {
  items.clear();
  for (const apps::App& a : apps::all()) {
    Item it;
    it.name = i18n::trl(a.name), it.sub = T_("App"), it.app = a.id;  // built-in names are translated, packages' not
    it.key = fold(it.name.c_str()) + " " + fold(a.name) + " " + a.id;
    items.push_back(it);
  }
  for (const SettingsPage& p : PAGES) {
    Item it;
    it.name = T_(p.name), it.sub = T_("Settings"), it.app = "settings", it.arg = p.arg, it.kind = 1;
    it.color = p.color, it.glyph = art_dsc(p.glyph);
    // The shown language's words, and the English ones too.
    it.key = fold(it.name.c_str()) + " " + fold(it.sub.c_str()) + " " + fold(T_(p.words)) + " " + fold(p.name) + " settings " +
             p.words;
    items.push_back(it);
  }
  JsonDocument doc;
  if (data::loadJson("s3.todo", doc))
    for (JsonObjectConst t : doc.as<JsonArrayConst>()) {
      Item it;
      it.name = t["t"] | "";
      if (!it.name.length()) continue;
      it.sub = (t["done"] | false) ? T_("Reminder, done") : T_("Reminder");
      it.kind = 2;
      it.app = "remind", it.color = 0xff9f0a, it.glyph = &gl_list_16;
      it.key = fold(it.name.c_str());
      items.push_back(it);
    }
  doc.clear();
  if (data::loadJson("s3.notes", doc))
    for (JsonObjectConst n : doc.as<JsonArrayConst>()) {
      Item it;
      const String title = n["title"] | "", body = n["body"] | "";
      it.name = title.length() ? title : body.substring(0, 40);
      if (!it.name.length()) continue;
      it.sub = T_("Note"), it.kind = 3, it.app = "notes", it.arg = n["id"] | "";
      it.color = 0xffcc00, it.glyph = &gl_edit_16;
      it.key = fold((title + " " + body).c_str());
      items.push_back(it);
    }
}

// ---------------------------------------------------------------- the screen
lv_obj_t *scr, *field, *results;
String pendApp, pendArg;

void openItem(const String& app, const String& arg) {
  if (!apps::find(app.c_str())) return;
  if (arg.length() == 0 || app != "settings") {
    recent.erase(std::remove(recent.begin(), recent.end(), app), recent.end());
    recent.insert(recent.begin(), app);
    if (recent.size() > 8) recent.pop_back();
  }
  pendApp = app, pendArg = arg;
  // After this event: opening an app closes Search (and the object being tapped).
  lv_async_call([](void*) {
    kb::close();
    apps::open(pendApp.c_str(), pendArg.length() ? pendArg.c_str() : nullptr);
  }, nullptr);
}

void onRow(lv_event_t* e) {
  const Item* it = (const Item*)lv_event_get_user_data(e);
  openItem(it->app, it->arg);
}

lv_obj_t* row(lv_obj_t* parent, const Item& it) {
  lv_obj_t* r = box(parent, pal().cell, 12);
  lv_obj_set_size(r, lv_pct(100), 50);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(r, C(pal().press), LV_STATE_PRESSED);
  if (it.glyph) {
    lv_obj_t* ic = box(r, it.color, 8);
    lv_obj_set_size(ic, 32, 32);
    lv_obj_set_pos(ic, 9, 9);
    lv_obj_center(glyph(ic, it.glyph, 0xffffff));
  } else {
    lv_obj_t* ic = apps::icon(r, it.app.c_str(), 28);
    lv_obj_set_pos(ic, 7, 7);
  }
  lv_obj_t* n = label(r, "", 15, true, pal().fg);
  fitText(n, it.name.c_str(), os::W() - 24 - 60, 20, 15, 12, true, 0);
  lv_obj_set_pos(n, 52, 7);
  lv_obj_t* s = label(r, it.sub.c_str(), 12, false, pal().dim);
  lv_obj_set_pos(s, 52, 28);
  lv_obj_add_event_cb(r, onRow, LV_EVENT_CLICKED, (void*)&it);
  return r;
}

void suggestions() {
  caption(results, N_("Suggestions"));
  lv_obj_t* grid = clear(results);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  std::vector<String> ids = recent;
  for (const apps::App& a : apps::all())
    if (ids.size() < 8 && std::find(ids.begin(), ids.end(), a.id) == ids.end()) ids.push_back(a.id);
  const int cw = (os::W() - 24) / 4;
  for (size_t i = 0; i < ids.size() && i < 8; i++) {
    const apps::App* a = apps::find(ids[i].c_str());
    if (!a) continue;
    lv_obj_t* c = clear(grid);
    lv_obj_set_size(c, cw, 78);
    lv_obj_set_pos(c, (i % 4) * cw, (i / 4) * 80);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* ic = apps::icon(c, a->id, 52);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* l = label(c, "", 12, false, pal().fg);
    fitText(l, a->name, cw - 2, 16, 12, 11, false, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_event_cb(c, [](lv_event_t* e) { openItem((const char*)lv_event_get_user_data(e), ""); },
                        LV_EVENT_CLICKED, (void*)a->id);
  }
}

void refill() {
  if (!results) return;
  lv_obj_clean(results);
  const String q = fold(lv_textarea_get_text(field));
  std::vector<String> words;
  for (int i = 0; i < (int)q.length();) {
    int j = q.indexOf(' ', i);
    if (j < 0) j = q.length();
    if (j > i) words.push_back(q.substring(i, j));
    i = j + 1;
  }
  if (words.empty()) return suggestions();
  // Every word must be found; a name that starts with the first word comes first.
  std::vector<std::pair<int, const Item*>> hits;
  for (const Item& it : items) {
    bool all = true;
    for (const String& w : words) all = all && it.key.indexOf(w) >= 0;
    if (!all) continue;
    const String nm = fold(it.name.c_str());
    const int score = (nm.startsWith(words[0]) ? 0 : nm.indexOf(" " + words[0]) >= 0 ? 1 : 2) * 10 + (it.kind < 2 ? it.kind : 2);
    hits.push_back({score, &it});
  }
  std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  if (hits.empty()) {
    lv_obj_t* l = label(results, N_("No results"), 15, false, pal().dim);
    lv_obj_set_style_pad_top(l, 24, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return;
  }
  static const char* const GROUPS[] = {N_("Apps"), N_("Settings"), N_("Reminders"), N_("Notes")};
  int group = -1;
  int shown = 0;
  for (const auto& h : hits) {
    if (++shown > 20) break;
    if (h.second->kind != group) caption(results, GROUPS[group = h.second->kind]);
    row(results, *h.second);
  }
}

void onDone(lv_obj_t*) {
  // "Search" (the keyboard's OK key): opens the first result.
  for (uint32_t i = 0; results && i < lv_obj_get_child_count(results); i++) {
    lv_obj_t* c = lv_obj_get_child(results, i);
    if (lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE)) {
      lv_obj_send_event(c, LV_EVENT_CLICKED, nullptr);
      return;
    }
  }
  kb::close();
}

}  // namespace

String fold(const char* s) {
  if (folds.empty()) buildFolds();
  String out;
  out.reserve(strlen(s));
  for (const char* p = s; *p;) {
    const uint32_t cp = nextCp(p);
    if (cp < 0x80) {
      out += (char)tolower(cp);
      continue;
    }
    auto it = std::lower_bound(folds.begin(), folds.end(), cp, [](const Fold& f, uint32_t c) { return f.cp < c; });
    if (it != folds.end() && it->cp == cp) out += it->base;
  }
  return out;
}

void open() {
  collect();
  lv_obj_t* body;
  keep(&scr, os::screen(N_("Search"), &body, os::BARE | os::NOSCROLL));
  const int w = os::W();
  lv_obj_t* pill = box(body, pal().cell, 18);
  lv_obj_set_size(pill, w - 12 - 64, 36);
  lv_obj_set_pos(pill, 12, 8);  // the body starts under the status bar
  lv_obj_t* g = glyph(pill, &gl_search_16, pal().dim);
  lv_obj_align(g, LV_ALIGN_LEFT_MID, 10, 0);
  keep(&field, lv_textarea_create(pill));
  lv_textarea_set_one_line(field, true);
  lv_textarea_set_placeholder_text(field, T_("Search"));
  lv_obj_set_size(field, w - 12 - 64 - 34, 34);
  lv_obj_set_pos(field, 30, 1);
  lv_obj_set_style_text_font(field, fonts::get(16), 0);
  lv_obj_set_style_text_color(field, C(pal().fg), 0);
  lv_obj_set_style_text_color(field, C(pal().faint), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_opa(field, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(field, 0, 0);
  lv_obj_set_style_pad_hor(field, 4, 0);
  lv_obj_set_style_pad_ver(field, 7, 0);
  lv_obj_set_style_bg_color(field, C(col::ACC), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_state(field, LV_STATE_FOCUSED);
  lv_obj_set_scrollbar_mode(field, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(field, [](lv_event_t*) { refill(); }, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t* cancel = label(body, N_("Cancel"), 16, true, pal().link);
  lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(cancel, 12);
  lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -14, 16);
  lv_obj_add_event_cb(cancel, [](lv_event_t*) { lv_async_call([](void*) { kb::close(); os::back(); }, nullptr); },
                      LV_EVENT_CLICKED, nullptr);

  keep(&results, stack(body, false, 6));
  lv_obj_set_size(results, w, os::H() - os::SB_H - 52 - kb::height());
  lv_obj_set_pos(results, 0, 52);
  lv_obj_set_style_pad_hor(results, 12, 0);
  lv_obj_set_style_pad_bottom(results, 12, 0);
  lv_obj_add_flag(results, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(results, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(results, LV_SCROLLBAR_MODE_OFF);
  // Scrolling the results puts the keyboard away (more room), as on a phone.
  lv_obj_add_event_cb(results, [](lv_event_t*) {
    if (kb::isOpen()) {
      kb::close();
      if (results) lv_obj_set_height(results, os::H() - os::SB_H - 52);
    }
  }, LV_EVENT_SCROLL_BEGIN, nullptr);
  os::barDarkFor(scr, !pal().dark);
  refill();
  os::open(scr);
  kb::open(field, kb::Mode::Text, onDone, N_("keyboard" "\x04" "Search"));  // i18n: the keyboard's OK key (short)
  trace::add("search open (%u items)", (unsigned)items.size());
}

}  // namespace search
