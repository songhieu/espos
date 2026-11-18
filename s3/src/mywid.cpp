#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "mywid.h"

#include <Arduino.h>
#include <lvgl.h>

#include "data.h"
#include "fonts.h"
#include "keyboard.h"
#include "os.h"
#include "uikit.h"
#include "wid.h"

namespace mywid {
namespace {

using namespace kit;

struct State {
  wid::Mine m;
  int size = 0;
  bool isNew = true;
  Done done = nullptr;
  lv_obj_t *scr, *body, *preview, *ta, *title, *list, *dateLbl;
} st;

const uint32_t SWATCH_BG[] = {0xffe066, 0xffb4c6, 0xb9f2c9, 0xa7d8ff, 0xd8c8ff, 0x16181d, 0xe2401f};
const uint32_t SWATCH_FG[] = {0x3b2f00, 0x5a1a2c, 0x0f4a26, 0x0b3a63, 0x2e1a63, 0xffffff, 0xffffff};

void redrawPreview() {
  if (!st.preview) return;
  lv_obj_clean(st.preview);
  wid::setPreview(&st.m);  // drawn from the edits, saved only with "Save"
  wid::Item it;
  it.kind = "my";
  it.opt = st.m.id;
  it.size = wid::S;
  lv_obj_t* w = wid::create(st.preview, it, 144, 138, false);
  lv_obj_set_pos(w, 0, 0);
}

void save(lv_event_t*) {
  kb::close();
  if (st.ta) {
    if (st.m.type == "count" || st.m.type == "list") st.m.title = lv_textarea_get_text(st.ta);
    else st.m.text = lv_textarea_get_text(st.ta);
  }
  wid::saveMine(st.m);
  const Done d = st.done;
  const String id = st.m.id;
  const int size = st.size;
  os::back();
  if (d) d(id.c_str(), size);
}

void swatches(lv_obj_t* parent) {
  caption(parent, N_("Colour"));
  lv_obj_t* row = stack(parent, true, 10);
  for (int i = 0; i < 7; i++) {
    lv_obj_t* s = box(row, SWATCH_BG[i], 14);
    lv_obj_set_size(s, 28, 28);
    lv_obj_set_style_border_width(s, st.m.bg == SWATCH_BG[i] ? 3 : 1, 0);
    lv_obj_set_style_border_color(s, C(st.m.bg == SWATCH_BG[i] ? pal().fg : 0xd0d3da), 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s, 4);
    lv_obj_add_event_cb(s, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      st.m.bg = SWATCH_BG[i];
      st.m.fg = SWATCH_FG[i];
      lv_obj_t* row = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
      for (int k = 0; k < 7; k++) {
        lv_obj_t* s = lv_obj_get_child(row, k);
        lv_obj_set_style_border_width(s, k == i ? 3 : 1, 0);
        lv_obj_set_style_border_color(s, C(k == i ? pal().fg : 0xd0d3da), 0);
      }
      redrawPreview();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

lv_obj_t* field(lv_obj_t* parent, const String& text, bool multi, const char* placeholder) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_obj_set_width(ta, lv_pct(100));
  lv_textarea_set_one_line(ta, !multi);
  if (multi) lv_obj_set_height(ta, 76);
  lv_textarea_set_text(ta, text.c_str());
  lv_textarea_set_placeholder_text(ta, i18n::trl(placeholder));  // N_ marked by the callers
  lv_obj_set_style_text_font(ta, fonts::get(16), 0);
  lv_obj_set_style_text_color(ta, C(pal().fg), 0);
  lv_obj_set_style_text_color(ta, C(pal().faint), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(ta, C(pal().cell), 0);
  lv_obj_set_style_border_color(ta, C(col::ACC), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_radius(ta, 12, 0);
  lv_obj_set_style_pad_all(ta, 10, 0);
  lv_obj_set_style_bg_color(ta, C(col::ACC), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ta, [](lv_event_t* e) {
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    if (!kb::isOpen()) kb::open(t, kb::Mode::Text, [](lv_obj_t*) { kb::close(); });
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(ta, [](lv_event_t* e) {
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    if (st.m.type == "count" || st.m.type == "list") st.m.title = lv_textarea_get_text(t);
    else st.m.text = lv_textarea_get_text(t);
    redrawPreview();
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  return ta;
}

// Countdown date: day, month and year steppers.
void showDate() {
  if (!st.dateLbl) return;
  const String& d = st.m.date;
  // Between the steppers (~116 px): shrinks for longer forms ("18. Nov. 2026").
  fitText(st.dateLbl,
          i18n::date(N_("{M} {d}, {y}"), d.substring(8, 10).toInt(), d.substring(5, 7).toInt(), d.substring(0, 4).toInt())
              .c_str(),
          os::W() - 204, 24, 18, 13, true);
}

void stepDate(int days) {
  data::Day d = {(int)st.m.date.substring(8, 10).toInt(), (int)st.m.date.substring(5, 7).toInt(),
                 (int)st.m.date.substring(0, 4).toInt(), 0};
  d = data::addDays(d, days);
  st.m.date = data::isoDate(d);
  showDate();
  redrawPreview();
}

void dateRow(lv_obj_t* parent) {
  caption(parent, N_("Date"));
  lv_obj_t* r = cell(parent, 8);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  static const int STEPS[] = {-30, -1, 1, 30};
  static const char* NAMES[] = {"−30", "−1", "+1", "+30"};
  for (int i = 0; i < 4; i++) {
    if (i == 2) {
      st.dateLbl = label(r, "", 18, true, pal().fg);
    }
    lv_obj_t* b = button(r, NAMES[i], pal().chip, pal().chipFg, [](lv_event_t* e) {
      stepDate((int)(intptr_t)lv_event_get_user_data(e));
    }, (void*)(intptr_t)STEPS[i]);
    lv_obj_set_style_pad_hor(b, 10, 0);
  }
  showDate();
}

void fillList();

void listEditor(lv_obj_t* parent) {
  caption(parent, N_("Items (tap to tick off)"));
  st.list = stack(parent, false, 6);
  fillList();
  lv_obj_t* add = listRow(parent, N_("+ Add item"), "", [](lv_event_t*) {
    // One field for the new item, typed with the keyboard.
    lv_obj_t* ta = lv_textarea_create(st.body);
    lv_obj_set_width(ta, lv_pct(100));
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_text_font(ta, fonts::get(16), 0);
    lv_obj_set_style_radius(ta, 12, 0);
    lv_obj_set_style_border_color(ta, C(col::ACC), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_move_to_index(ta, lv_obj_get_index(st.list) + 1);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
    kb::open(ta, kb::Mode::Text, [](lv_obj_t* t) {
      const String s = lv_textarea_get_text(t);
      if (s.length()) st.m.items.push_back({s, false});
      kb::close();
      lv_obj_delete_async(t);
      fillList();
      redrawPreview();
    }, T_("Add"));
  });
  (void)add;
}

void fillList() {
  if (!st.list) return;
  lv_obj_clean(st.list);
  for (int i = 0; i < (int)st.m.items.size(); i++) {
    const auto& e = st.m.items[i];
    lv_obj_t* r = box(st.list, pal().cell, 12);
    lv_obj_set_size(r, lv_pct(100), 42);
    lv_obj_set_style_pad_hor(r, 12, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* ring = clear(r);
    lv_obj_set_size(ring, 18, 18);
    lv_obj_set_style_radius(ring, 9, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, C(e.second ? col::GREEN : 0xc7cad1), 0);
    if (e.second) lv_obj_set_style_bg_color(ring, C(col::GREEN), 0), lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_align(ring, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* l = label(r, "", 15, false, e.second ? pal().faint : pal().fg);
    fitText(l, e.first.c_str(), os::W() - 24 - 24 - 30 - 8 - textW(N_("Delete"), 13, true), 36, 15, 12);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_t* x = label(r, N_("Delete"), 13, true, col::RED);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(x, 8);
    lv_obj_add_event_cb(x, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      if (i < (int)st.m.items.size()) st.m.items.erase(st.m.items.begin() + i);
      lv_async_call([](void*) {
        fillList();
        redrawPreview();
      }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      if (i < (int)st.m.items.size()) st.m.items[i].second = !st.m.items[i].second;
      lv_async_call([](void*) {
        fillList();
        redrawPreview();
      }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

void fontSizes(lv_obj_t* parent) {
  lv_obj_t* r = stack(parent, true, 6);
  static const int PX[] = {13, 15, 18};
  for (int i = 0; i < 3; i++) {
    const bool on = st.m.px == PX[i];
    lv_obj_t* b = box(r, on ? pal().chipOn : pal().chip, 8);
    lv_obj_set_size(b, 44, 30);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = label(b, "A", PX[i], true, on ? pal().chipOnFg : pal().chipFg);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      st.m.px = PX[i];
      lv_obj_t* row = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
      for (int k = 0; k < 3; k++) {
        lv_obj_t* b = lv_obj_get_child(row, k);
        lv_obj_set_style_bg_color(b, C(k == i ? pal().chipOn : pal().chip), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C(k == i ? pal().chipOnFg : pal().chipFg), 0);
      }
      redrawPreview();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

void openBuilder() {
  static const char* TITLES[][2] = {{"note", N_("Sticky note")}, {"count", N_("Countdown")}, {"quote", N_("Quotes")},
                                    {"list", N_("List")}};
  const char* title = N_("My widget");
  for (auto& t : TITLES)
    if (st.m.type == t[0]) title = t[1];
  st.scr = os::screen(title, &st.body);
  os::navAction(st.scr, N_("Save"), save);
  lv_obj_add_event_cb(st.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != st.scr) return;
    st.scr = st.body = st.preview = st.ta = st.list = st.dateLbl = nullptr;
    wid::setPreview(nullptr);
  }, LV_EVENT_DELETE, nullptr);
  // Preview beside the size note.
  lv_obj_t* top = clear(st.body);
  lv_obj_set_size(top, lv_pct(100), 140);
  st.preview = clear(top);
  lv_obj_set_size(st.preview, 144, 138);
  lv_obj_t* note = label(top, "", 13, false, pal().faint);
  fitText(note, N_("Preview at its real size on Home."), os::W() - 24 - 160, 40, 13, 12);
  lv_obj_set_pos(note, 156, 4);
  if (st.m.type == "note") {
    lv_obj_t* fs = label(top, N_("Text size"), 13, true, pal().faint);
    lv_obj_set_pos(fs, 156, 62);
    lv_obj_t* box_ = clear(top);
    lv_obj_set_size(box_, 150, 32);
    lv_obj_set_pos(box_, 156, 84);
    fontSizes(box_);
  }
  swatches(st.body);
  st.ta = nullptr;
  if (st.m.type == "note") {
    st.ta = field(st.body, st.m.text, true, N_("Write a note"));
  } else if (st.m.type == "quote") {
    caption(st.body, N_("One per line; a new one each day"));
    st.ta = field(st.body, st.m.text, true, N_("Quotes"));
  } else if (st.m.type == "count") {
    caption(st.body, N_("Name"));
    st.ta = field(st.body, st.m.title, false, N_("e.g. Mum's birthday"));
    dateRow(st.body);
  } else {
    caption(st.body, N_("List name"));
    st.ta = field(st.body, st.m.title, false, N_("e.g. Shopping"));
    listEditor(st.body);
  }
  redrawPreview();
  os::open(st.scr);
  if (st.ta && (st.m.type == "note" || st.m.type == "quote") && !st.m.text.length())
    kb::open(st.ta, kb::Mode::Text, [](lv_obj_t*) { kb::close(); });
}

}  // namespace

void create(const char* type, Done done, int size) {
  st = {};
  st.m.id = "w" + data::newId();
  st.m.type = type;
  st.isNew = true;
  st.done = done;
  st.size = size;
  if (st.m.type == "count") {
    st.m.bg = 0xe2401f, st.m.fg = 0xffffff;
    st.m.date = data::isoDate(data::addDays(data::today(), 30));
    st.m.title = "";
  } else if (st.m.type == "quote") {
    st.m.bg = 0x16181d, st.m.fg = 0xffffff;
  } else if (st.m.type == "list") {
    st.m.bg = 0xffffff, st.m.fg = 0x16181d;
  }
  openBuilder();
}

void edit(const char* id, Done done) {
  const wid::Mine* m = wid::findMine(id);
  if (!m) return os::toast(N_("Widget not found"));
  st = {};
  st.m = *m;
  st.isNew = false;
  st.done = done;
  openBuilder();
}

}  // namespace mywid
