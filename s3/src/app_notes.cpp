#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Ghi chú: notes typed with the Vietnamese keyboard, synced with the web (s3.notes), pinned to
// the home screen as a widget. Nhắc việc: a simple to-do list (s3.todo).
#include <Arduino.h>
#include <ArduinoJson.h>

#include <ctime>

#include "apps.h"
#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "home.h"
#include "keyboard.h"
#include "os.h"
#include "uikit.h"

namespace app_notes {
std::vector<String> noteIds();
namespace {

using namespace kit;

const uint32_t COLORS[] = {0xffd84a, 0x7cc4ff, 0xc4a7ff, 0x7ee2a8, 0xff9b9b};

lv_obj_t *listBox;
struct Ed {
  String id;
  uint32_t color = COLORS[0];
  lv_obj_t *title, *body;
} ed;

void fillList();

void saveNote() {
  JsonDocument doc;
  data::loadJson("s3.notes", doc);
  if (!doc.is<JsonArray>()) doc.to<JsonArray>();
  const String title = lv_textarea_get_text(ed.title), body = lv_textarea_get_text(ed.body);
  JsonArray a = doc.as<JsonArray>();
  JsonObject o;
  for (JsonObject x : a)
    if (ed.id == (x["id"] | "")) o = x;
  if (!title.length() && !body.length()) {
    // Empty: nothing to keep (an existing one is removed).
    for (size_t i = 0; i < a.size(); i++)
      if (ed.id == (a[i]["id"] | "")) a.remove(i);
  } else {
    if (o.isNull()) {
      // Newest first.
      JsonDocument fresh;
      JsonArray f = fresh.to<JsonArray>();
      o = f.add<JsonObject>();
      o["id"] = ed.id;
      for (JsonObject x : a) f.add(x);
      doc.set(fresh);
      a = doc.as<JsonArray>();
      for (JsonObject x : a)
        if (ed.id == (x["id"] | "")) o = x;
    }
    o["title"] = title;
    o["body"] = body;
    char c[8];
    snprintf(c, sizeof c, "%06lx", (unsigned long)ed.color);
    o["c"] = c;
    o["t"] = (int64_t)time(nullptr) * 1000;
  }
  data::saveJson("s3.notes", doc);
}

void openEditor(const String& id) {
  ed = {};
  ed.id = id.length() ? id : "n" + data::newId();
  String title, body;
  JsonDocument doc;
  data::loadJson("s3.notes", doc);
  for (JsonObjectConst x : doc.as<JsonArrayConst>())
    if (ed.id == (x["id"] | "")) {
      title = x["title"] | "";
      body = x["body"] | "";
      ed.color = strtoul(x["c"] | "ffd84a", nullptr, 16);
    }
  lv_obj_t* b;
  lv_obj_t* s = os::screen(id.length() ? N_("Note") : N_("New note"), &b, os::NOSCROLL);
  os::navAction(s, N_("Done"), [](lv_event_t*) {
    kb::close();
    saveNote();
    os::back();
    fillList();
  });
  auto field = [](lv_obj_t* parent, const String& text, bool big, const char* ph) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, lv_pct(100));
    lv_textarea_set_one_line(ta, big);
    lv_textarea_set_text(ta, text.c_str());
    lv_textarea_set_placeholder_text(ta, ph);
    lv_obj_set_style_text_font(ta, fonts::get(big ? 20 : 16, big), 0);
    lv_obj_set_style_text_color(ta, C(pal().fg), 0);
    lv_obj_set_style_text_color(ta, C(pal().faint), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, 4, 0);
    lv_obj_set_style_bg_color(ta, C(col::ORANGE), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
      lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
      lv_obj_add_state(t, LV_STATE_FOCUSED);
      kb::open(t, kb::Mode::Text, [](lv_obj_t*) { kb::close(); });
    }, LV_EVENT_CLICKED, nullptr);
    return ta;
  };
  ed.title = field(b, title, true, T_("Title"));
  ed.body = field(b, body, false, T_("Note"));
  lv_obj_set_height(ed.body, os::H() - os::SB_H - os::NAV_H - kb::height() - 100);
  lv_obj_t* row = stack(b, true, 10);
  for (uint32_t c : COLORS) {
    lv_obj_t* sw = box(row, c, 12);
    lv_obj_set_size(sw, 24, 24);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(sw, 4);
    lv_obj_set_style_border_width(sw, c == ed.color ? 3 : 0, 0);
    lv_obj_set_style_border_color(sw, C(pal().fg), 0);
    lv_obj_add_event_cb(sw, [](lv_event_t* e) {
      ed.color = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
      lv_obj_t* row = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
      for (uint32_t i = 0; i < 5 && i < lv_obj_get_child_count(row); i++)
        lv_obj_set_style_border_width(lv_obj_get_child(row, i), COLORS[i] == ed.color ? 3 : 0, 0);
    }, LV_EVENT_CLICKED, (void*)(uintptr_t)c);
  }
  lv_obj_t* pin = label(row, N_("Pin to Home"), 13, true, pal().link);
  lv_obj_add_flag(pin, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(pin, 8);
  lv_obj_add_event_cb(pin, [](lv_event_t*) {
    kb::close();
    saveNote();
    const String id = ed.id;
    os::goHome();
    home::addWidget("notes", 0, id.c_str());
  }, LV_EVENT_CLICKED, nullptr);
  os::open(s);
  lv_obj_add_state(id.length() ? ed.body : ed.title, LV_STATE_FOCUSED);
  kb::open(id.length() ? ed.body : ed.title, kb::Mode::Text, [](lv_obj_t*) { kb::close(); });
}

void fillList() {
  if (!listBox) return;
  lv_obj_clean(listBox);
  JsonDocument doc;
  data::loadJson("s3.notes", doc);
  int n = 0;
  const int w = twoCols(listBox);  // landscape: notes two a line
  for (JsonObjectConst x : doc.as<JsonArrayConst>()) {
    lv_obj_t* c = cell(listBox, 0);
    lv_obj_set_width(c, w);
    lv_obj_set_style_clip_corner(c, true, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c, C(pal().press), LV_STATE_PRESSED);
    lv_obj_t* bar = box(c, strtoul(x["c"] | "ffd84a", nullptr, 16), 0);
    lv_obj_set_size(bar, 5, lv_pct(100));
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_t* inner = stack(c, false, 3);
    lv_obj_set_style_pad_all(inner, 12, 0);
    lv_obj_set_style_pad_left(inner, 16, 0);
    const String title = x["title"] | "", body = x["body"] | "";
    lv_obj_t* t = label(inner, "", 16, true, pal().fg);
    fitText(t, (title.length() ? title : body).c_str(), w - 30, 24, 16, 13, true);
    if (title.length() && body.length()) {
      lv_obj_t* p = label(inner, "", 13, false, pal().dim);
      wrapText(p, body.c_str(), w - 30, 13, false, 2, 2);
    }
    lv_obj_add_event_cb(c, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      const auto ids = noteIds();
      if (i < (int)ids.size()) openEditor(ids[i]);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)n);
    n++;
  }
  if (!n) hint(listBox, N_("No notes yet. Tap + to write one (the keyboard types Vietnamese with Telex too)."));
  hint(listBox, N_("Notes typed here sync to the web. Pin a note to Home as a widget."));
}

}  // namespace

std::vector<String> noteIds() {
  std::vector<String> out;
  JsonDocument doc;
  data::loadJson("s3.notes", doc);
  for (JsonObjectConst x : doc.as<JsonArrayConst>()) out.push_back(x["id"] | "");
  return out;
}

void open(const char* arg) {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Notes"), &body, os::ROOT);
  os::navAction(s, "+", [](lv_event_t*) { openEditor(""); });
  listBox = stack(body, false, 8);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (listBox && lv_obj_get_screen(listBox) == lv_event_get_current_target(e)) listBox = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillList();
  os::open(s);
  if (arg && *arg) openEditor(arg);
}

}  // namespace app_notes

// ====================================================================== Nhắc việc
namespace app_remind {
namespace {

using namespace kit;
lv_obj_t* listBox;

void fill();

void add(const String& text) {
  JsonDocument doc;
  data::loadJson("s3.todo", doc);
  if (!doc.is<JsonArray>()) doc.to<JsonArray>();
  JsonObject o = doc.as<JsonArray>().add<JsonObject>();
  o["t"] = text;
  o["done"] = false;
  data::saveJson("s3.todo", doc);
}

void fill() {
  if (!listBox) return;
  lv_obj_clean(listBox);
  JsonDocument doc;
  data::loadJson("s3.todo", doc);
  int i = 0, open_ = 0;
  for (JsonObjectConst t : doc.as<JsonArrayConst>()) {
    const bool done = t["done"] | false;
    open_ += !done;
    lv_obj_t* r = box(listBox, pal().cell, 12);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(r, 46, 0);
    lv_obj_set_style_pad_hor(r, 12, 0);
    lv_obj_set_style_pad_ver(r, 10, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* ring = clear(r);
    lv_obj_set_size(ring, 22, 22);
    lv_obj_set_style_radius(ring, 11, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, C(done ? col::ORANGE : 0xc7cad1), 0);
    if (done) {
      lv_obj_set_style_bg_color(ring, C(col::ORANGE), 0);
      lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    }
    lv_obj_align(ring, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* l = label(r, "", 15, false, done ? pal().faint : pal().fg);
    wrapText(l, t["t"] | "", os::W() - 24 - 24 - 34 - 8 - textW(N_("Delete"), 13, true), 15, false, 2, 3);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 34, 0);
    lv_obj_t* x = label(r, N_("Delete"), 13, true, col::RED);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(x, 8);
    lv_obj_add_event_cb(x, [](lv_event_t* e) {
      JsonDocument doc;
      data::loadJson("s3.todo", doc);
      doc.as<JsonArray>().remove((int)(intptr_t)lv_event_get_user_data(e));
      data::saveJson("s3.todo", doc);
      lv_async_call([](void*) { fill(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      JsonDocument doc;
      data::loadJson("s3.todo", doc);
      JsonObject o = doc.as<JsonArray>()[(int)(intptr_t)lv_event_get_user_data(e)];
      o["done"] = !(o["done"] | false);
      data::saveJson("s3.todo", doc);
      lv_async_call([](void*) { fill(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    i++;
  }
  // New item.
  lv_obj_t* ta = lv_textarea_create(listBox);
  lv_obj_set_width(ta, lv_pct(100));
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, T_("+ Add a task"));
  lv_obj_set_style_text_font(ta, fonts::get(16), 0);
  lv_obj_set_style_text_color(ta, C(pal().fg), 0);
  lv_obj_set_style_text_color(ta, C(pal().link), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(ta, C(pal().cell), 0);
  lv_obj_set_style_border_width(ta, 0, 0);
  lv_obj_set_style_radius(ta, 12, 0);
  lv_obj_set_style_pad_all(ta, 12, 0);
  lv_obj_add_event_cb(ta, [](lv_event_t* e) {
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_add_state(t, LV_STATE_FOCUSED);
    kb::open(t, kb::Mode::Text, [](lv_obj_t* t) {
      const String s = lv_textarea_get_text(t);
      if (s.length()) add(s);
      kb::close();
      lv_async_call([](void*) { fill(); }, nullptr);
    }, N_("Add"));
  }, LV_EVENT_CLICKED, nullptr);
  if (!i) hint(listBox, N_("No tasks left. Add one above; the Reminders widget shows the open ones."));
}

}  // namespace

int badge() {
  JsonDocument doc;
  if (!data::loadJson("s3.todo", doc)) return 0;
  int n = 0;
  for (JsonObjectConst t : doc.as<JsonArrayConst>()) n += !(t["done"] | false);
  return min(n, 99);
}

void open(const char*) {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Reminders"), &body, os::ROOT);
  listBox = stack(body, false, 8);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (listBox && lv_obj_get_screen(listBox) == lv_event_get_current_target(e)) listBox = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fill();
  os::open(s);
}

}  // namespace app_remind
