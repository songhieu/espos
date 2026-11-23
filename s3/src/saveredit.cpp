#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit its 3.75 MB slot)
// The saver's editor (saveredit.h, docs/saver.md §3, mockup mockups/s3-saver-device.html): the real
// page full screen, one screen of plain LVGL objects over the saver. The widget gallery, the widget option rows, the
// background sheet and the sheet look are Home's (home.h, widopts.h): one copy of that code, the same widgets.
#include "saveredit.h"

#include <lvgl.h>

#include <algorithm>

#ifdef SIM
#include <fstream>
#include <iterator>
#include <string>
extern std::string simDir;
#else
#include <LittleFS.h>
#endif

#include "art.h"
#include "data.h"
#include "desk.h"
#include "desksaver.h"
#include "slots.h"
#include "fonts.h"
#include "home.h"
#include "keyboard.h"
#include "lcd.h"
#include "os.h"
#include "theme.h"
#include "trace.h"
#include "uikit.h"
#include "wid.h"
#include "widopts.h"

namespace saveredit {
namespace {

using namespace kit;
using desksaver::El;
using desksaver::Page;

// Home's sheet look (home.cpp openSheet, the widget options): light, iOS-like.
constexpr uint32_t PAPER = 0xf4f4f7, INK = 0x16181d, DIM = 0x6b7280, PILL = 0xe4e6ec, PILL_T = 0x3a3f4a,
                   ON_T = 0x1b1408, SEL = 0x4c8dff, GUIDE = 0xff3b9a;
constexpr int BAR_H = 40;
const widopts::Look LOOK = {INK, DIM, PILL, PILL_T, col::ACC, ON_T};
// Text and box colours.
const uint32_t INKS[8] = {0xe8ecf2, 0x9aa3ad, 0x16181d, 0xffb36b, 0xffe066, 0x8be29a, 0x7cc4ff, 0xd92d20};
// The shared templates (docs/saver.md §5), kept on the device for the next time (and offline).
const char* TPL_URL = ESPOS_API "/storage/bb/149/apps/saver-templates.json";

desksaver::Doc d;  // the copy being edited
bool land = false, edited = false, isOpen_ = false;
void (*onClosed)() = nullptr;
int page = 0;  // -1: the night page
int sel = -1;  // the selected element
lv_obj_t *root, *layer, *sheet, *tool, *hint_, *handle, *guideV, *guideH, *field, *setBody, *tplBody, *tplNote, *dlg;
std::vector<lv_obj_t*> boxes;  // one per element of the page, in its order

std::vector<Page>& pages() { return land ? d.l : d.p; }
bool& hasNight() { return land ? d.hasNl : d.hasNp; }
bool ownNight() { return d.night == "page" && hasNight(); }
Page& pg() {
  if (page < 0) {
    Page& n = desksaver::nightPage(d, land);  // the built-in one first, then it is ours to change
    hasNight() = true;
    return n;
  }
  return pages()[min<int>(page, pages().size() - 1)];
}
El* selEl() { return sel >= 0 && sel < (int)pg().e.size() ? &pg().e[sel] : nullptr; }
intptr_t arg(lv_event_t* e) { return (intptr_t)lv_event_get_user_data(e); }

// Changes from a button are drawn after its event (the button itself is redrawn).
enum : uint8_t { DO_SHEET = 1, DO_EL = 2, DO_ALL = 4 };
uint8_t pending = 0;
void buildAll();
void buildSheet();
void refreshEl(int i);
void soon(uint8_t what) {
  if (!pending)
    lv_async_call([](void*) {
      const uint8_t p = pending;
      pending = 0;
      if (!root) return;
      if (p & DO_ALL) return buildAll();
      if (p & DO_EL) refreshEl(sel);
      if (p & DO_SHEET) buildSheet();
    }, nullptr);
  pending |= what;
}
void changed(uint8_t what) {
  edited = true;
  soon(what);
}

// ---------------------------------------------------------------- small parts
// A pill of the dark bars (Home's edit bar): white 24 %, or the accent.
lv_obj_t* barPill(lv_obj_t* parent, const char* text, bool ok, lv_event_cb_t cb, int h = 26) {
  lv_obj_t* b = box(parent, ok ? col::ACC : 0xffffff, h / 2);
  if (!ok) lv_obj_set_style_bg_opa(b, 60, 0);
  lv_obj_set_size(b, LV_SIZE_CONTENT, h);
  lv_obj_set_style_pad_hor(b, h > 26 ? 14 : 12, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(b, 6);
  lv_obj_set_style_bg_opa(b, ok ? LV_OPA_70 : 120, LV_STATE_PRESSED);
  lv_obj_center(label(b, text, 14, true, ok ? ON_T : 0xffffff));
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

// A pill of the light sheets (the widget options' look); red: Delete.
lv_obj_t* pill(lv_obj_t* parent, const char* text, bool on, lv_event_cb_t cb, intptr_t data = 0, bool red = false) {
  lv_obj_t* p = box(parent, red ? 0xffe5e3 : on ? col::ACC : PILL, 14);
  lv_obj_set_size(p, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(p, 12, 0);
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(p, 3);
  lv_obj_set_style_bg_opa(p, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_center(label(p, text, 13, on || red, red ? col::RED : on ? ON_T : PILL_T));
  lv_obj_add_event_cb(p, cb, LV_EVENT_CLICKED, (void*)data);
  return p;
}

int innerW() { return (land ? 248 : os::W()) - 24; }
// The page's own screen (desksaver::canvas): elements, the handle and the guides live in it, in its coordinates.
int PW() { return land ? desksaver::PAGE_H : desksaver::PAGE_W; }
int PH() { return land ? desksaver::PAGE_W : desksaver::PAGE_H; }

// A caption and a wrapping row of pills under it.
lv_obj_t* row(lv_obj_t* parent, const char* caption) {
  if (caption) lv_obj_set_style_pad_top(label(parent, caption, 13, true, DIM), 4, 0);
  lv_obj_t* r = kit::clear(parent);
  lv_obj_set_size(r, innerW(), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(r, 6, 0);
  return r;
}

// − value +: dir -1 / +1 in the user data.
void stepper(lv_obj_t* r, const String& value, lv_event_cb_t cb) {
  pill(r, "−", false, cb, -1);
  lv_obj_t* v = label(r, value.c_str(), 14, true, INK);
  lv_obj_set_style_min_width(v, 30, 0);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
  pill(r, "+", false, cb, 1);
}

// Band colours (text over pictures): black and white first, then dark and light tones.
const uint32_t BANDS[8] = {0x000000, 0xffffff, 0x1c2029, 0x1e3a8a, 0x0f766e, 0x7c2d12, 0xf5efe4, 0xffb547};
void swatches(lv_obj_t* r, uint32_t on, lv_event_cb_t cb, const uint32_t* list = INKS) {
  for (int i = 0; i < 8; i++) {
    const uint32_t c = list[i];
    lv_obj_t* s = box(r, c, 12);
    lv_obj_set_size(s, land ? 22 : 26, land ? 22 : 26);  // eight on one line
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_border_color(s, C(c == on ? INK : PILL), 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s, 2);
    lv_obj_add_event_cb(s, cb, LV_EVENT_CLICKED, (void*)(uintptr_t)c);
  }
}

// A white row of a light sheet (Home's cfgRow).
lv_obj_t* whiteRow(lv_obj_t* parent, const char* name, const char* value, lv_event_cb_t cb, intptr_t data = 0,
                   bool chevron = true) {
  lv_obj_t* r = listRow(parent, name, value, cb, (void*)data, chevron);
  lv_obj_set_style_bg_color(r, C(0xffffff), 0);
  return r;
}

// A body that scrolls under a sheet's title (home::openSheet), filled by the caller.
lv_obj_t* sheetBody(lv_obj_t* s, int h) {
  lv_obj_t* b = kit::clear(s);
  lv_obj_set_pos(b, 0, 44);
  lv_obj_set_size(b, os::W() - 24, h - 50);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(b, 6, 0);
  lv_obj_set_style_pad_bottom(b, 16, 0);
  lv_obj_add_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(b, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_OFF);
  return b;
}
int sheetH(int portrait) { return os::landscape() ? os::H() - 16 : portrait; }

// A question over everything: no (keeps editing) or yes (runs fn).
void (*askYes)() = nullptr;
void ask(const char* title, const char* body, const char* no, const char* yes, void (*fn)()) {
  askYes = fn;
  if (dlg) lv_obj_delete(dlg);
  keep(&dlg, box(lv_layer_top(), 0x000000, 0));
  lv_obj_set_style_bg_opa(dlg, LV_OPA_50, 0);
  lv_obj_set_size(dlg, os::W(), os::H());
  lv_obj_add_flag(dlg, LV_OBJ_FLAG_CLICKABLE);  // nothing under it takes the touch
  const int w = min(os::W() - 48, 300);
  lv_obj_t* c = box(dlg, PAPER, 16);
  lv_obj_set_size(c, w, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, 16, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 8, 0);
  lv_obj_center(c);
  wrapText(label(c, "", 17, true, INK), title, w - 32, 17, true);
  wrapText(label(c, "", 14, false, PILL_T), body, w - 32, 14);
  lv_obj_t* r = kit::clear(c);
  lv_obj_set_size(r, w - 32, 40);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(r, 8, 0);
  lv_obj_set_style_margin_top(r, 4, 0);
  for (int k = 0; k < 2; k++) {
    lv_obj_t* b = button(r, k ? yes : no, k ? col::ACC : PILL, k ? ON_T : INK, [](lv_event_t* e) {
      if (dlg) lv_obj_delete_async(dlg), dlg = nullptr;
      if (arg(e) && askYes) askYes();
    }, (void*)(intptr_t)k);
    lv_obj_set_flex_grow(b, 1);
  }
}

std::vector<int> photoIds() {
  std::vector<int> out;
  for (const String& s : data::codesOf(desk::setting("s3.photos", ""), ""))
    if (s.toInt() > 0) out.push_back(s.toInt());
  return out;
}

// ---------------------------------------------------------------- elements on the page
struct Drag {
  int i = -1;
  lv_point_t start;
  int16_t x, y, w, h;
  bool moved = false;
} drag;

// A light page (paper, a light colour) gets dark outlines, a dark one light outlines.
bool lightPage() {
  const desksaver::Bg& b = pg().bg;
  auto light = [](uint32_t c) { return ((c >> 16) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000 > 150; };
  return (b.t == "color" || b.t == "grad") && light(b.c) && !b.dim;
}

void outline(int i) {
  if (i < 0 || i >= (int)boxes.size()) return;
  lv_obj_t* c = boxes[i];
  const bool on = i == sel;
  lv_obj_set_style_outline_width(c, on ? 2 : 1, 0);
  lv_obj_set_style_outline_color(c, C(on ? SEL : lightPage() ? 0x000000 : 0xffffff), 0);
  lv_obj_set_style_outline_opa(c, on ? LV_OPA_COVER : 90, 0);
  lv_obj_set_style_outline_pad(c, on ? 3 : 2, 0);
}

void placeHandle() {
  El* e = selEl();
  if (!handle) return;
  if (!e) return (void)lv_obj_add_flag(handle, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(handle, min(e->x + e->w - 9, PW() - 18), min(e->y + e->h - 9, PH() - 18));
  lv_obj_remove_flag(handle, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(handle);
}

void select(int i) {
  const int old = sel;
  sel = i;
  outline(old);
  outline(sel);
  placeHandle();
  buildSheet();
}

// Snaps one axis to the screen's edges and centre and to other elements' edges and centres (within 6 px, with a
// guide line), else to 4 px steps; always inside the screen.
int snapAxis(int pos, int size, int screen, bool vertical, int& guide) {
  guide = -1;
  int best = 7, out = pos;
  auto tryAt = [&](int t) {
    for (int k = 0; k < 3; k++) {
      const int mine = pos + size * k / 2;
      if (abs(mine - t) < best) best = abs(mine - t), out = pos + t - mine, guide = t;
    }
  };
  tryAt(0);
  tryAt(screen / 2);
  tryAt(screen);
  for (size_t j = 0; j < pg().e.size(); j++) {
    if ((int)j == drag.i) continue;
    const El& o = pg().e[j];
    const int a = vertical ? o.y : o.x, s = vertical ? o.h : o.w;
    tryAt(a);
    tryAt(a + s / 2);
    tryAt(a + s);
  }
  if (guide < 0) out = (pos + (pos >= 0 ? 2 : -2)) / 4 * 4;
  return constrain(out, 0, max(0, screen - size));
}

void showGuides(int gx, int gy) {
  if (!guideV || !guideH) return;
  gx >= 0 ? lv_obj_remove_flag(guideV, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(guideV, LV_OBJ_FLAG_HIDDEN);
  gy >= 0 ? lv_obj_remove_flag(guideH, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(guideH, LV_OBJ_FLAG_HIDDEN);
  if (gx >= 0) lv_obj_set_x(guideV, min(gx, PW() - 1));
  if (gy >= 0) lv_obj_set_y(guideH, min(gy, PH() - 1));
}

// While a finger moves something the page is seen whole: the bars and the sheet hide (the bottom bar comes back
// only when nothing is selected).
void chrome(bool shown) {
  for (lv_obj_t* o : {sheet, tool, hint_})
    if (o) shown && (o == sheet || sel < 0) ? lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// LVGL draws the screen under the top layer, and every child of the top layer, for each piece of the editor it
// draws again: Home, then the saver (its aquarium and widgets), then the editor. While the editor is open what it
// covers is hidden (the screen, and the top layer's children under it), and shown again when it closes. Dragging a
// widget on an aquarium page went from ~22 to ~60 pictures a second (QEMU at the board's speed, 2026-11-23).
lv_obj_t* hid[8];
uint8_t nHid = 0;
void under(bool hide) {
  if (!hide) {
    while (nHid)
      if (lv_obj_t* o = hid[--nHid]; lv_obj_is_valid(o)) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  for (int k = -1; k < (int)lv_obj_get_index(root) && nHid < 8; k++) {
    lv_obj_t* o = k < 0 ? lv_screen_active() : lv_obj_get_child(lv_layer_top(), k);
    if (!lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN), hid[nHid++] = o;
  }
}

int indexOf(lv_obj_t* c) {
  for (size_t i = 0; i < boxes.size(); i++)
    if (boxes[i] == c) return i;
  return -1;
}

void onEl(lv_event_t* ev) {
  const lv_event_code_t code = lv_event_get_code(ev);
  const int i = indexOf((lv_obj_t*)lv_event_get_current_target(ev));
  if (i < 0 || i >= (int)pg().e.size()) return;
  El& e = pg().e[i];
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) {
    drag = Drag();
    drag.i = i, drag.start = p, drag.x = e.x, drag.y = e.y;
  } else if (code == LV_EVENT_PRESSING && drag.i == i) {
    const int dx = p.x - drag.start.x, dy = p.y - drag.start.y;
    if (!drag.moved && (abs(dx) > 6 || abs(dy) > 6)) {
      drag.moved = true;
      chrome(false);
    }
    if (!drag.moved) return;
    int gx, gy;
    e.x = snapAxis(drag.x + dx, e.w, PW(), false, gx);
    e.y = snapAxis(drag.y + dy, e.h, PH(), true, gy);
    lv_obj_set_pos(boxes[i], e.x, e.y);
    showGuides(gx, gy);
    if (i == sel) placeHandle();
  } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && drag.i == i) {
    showGuides(-1, -1);
    chrome(true);
    if (drag.moved) edited = true;
    else if (code == LV_EVENT_RELEASED) select(i == sel ? -1 : i);  // a second tap lets go of it
    drag.i = -1;
  }
}

// Every child of an element is only a picture here: the element's box takes the touches.
void inert(lv_obj_t* o) {
  for (uint32_t k = 0; k < lv_obj_get_child_count(o); k++) {
    lv_obj_t* ch = lv_obj_get_child(o, k);
    lv_obj_remove_flag(ch, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    inert(ch);
  }
}

lv_obj_t* addBox(const El& e) {
  lv_obj_t* c = desksaver::drawEl(layer, e);
  if (e.t != "box" && e.t != "photo") lv_obj_set_style_radius(c, e.t == "w" ? 18 : 6, 0);  // outlines with soft corners
  inert(c);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  for (lv_event_code_t k : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(c, onEl, k, nullptr);
  return c;
}

void refreshEl(int i) {
  if (i < 0 || i >= (int)boxes.size() || i >= (int)pg().e.size()) return;
  lv_obj_t* old = boxes[i];
  boxes[i] = addBox(pg().e[i]);
  lv_obj_move_to_index(boxes[i], lv_obj_get_index(old));
  lv_obj_delete(old);
  outline(i);
  placeHandle();
}

// The corner handle: a free element takes any size (4 px steps, inside the screen); a widget the nearest of its
// sizes (Small, Medium, Large).
void onHandle(lv_event_t* ev) {
  El* e = selEl();
  if (!e) return;
  const lv_event_code_t code = lv_event_get_code(ev);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (code == LV_EVENT_PRESSED) {
    drag = Drag();
    drag.start = p, drag.w = e->w, drag.h = e->h;
    chrome(false);
    return;
  }
  const int w = drag.w + p.x - drag.start.x, h = drag.h + p.y - drag.start.y;
  if (code == LV_EVENT_PRESSING && e->free()) {
    e->w = constrain((w + 2) / 4 * 4, 16, PW() - e->x);
    e->h = constrain((h + 2) / 4 * 4, 12, PH() - e->y);
    lv_obj_set_size(boxes[sel], e->w, e->h);
    placeHandle();
  } else if (code == LV_EVENT_PRESSING) {
    const wid::Kind* k = wid::kind(e->k);
    char best = e->sz;
    int bd = 1 << 20;
    for (int s = 0; s < 3; s++) {
      if (k && !(k->sizes & (1 << s))) continue;
      int16_t bw, bh;
      desksaver::widgetBox("SML"[s], bw, bh);
      if (abs(bw - w) + abs(bh - h) < bd) bd = abs(bw - w) + abs(bh - h), best = "SML"[s];
    }
    if (best == e->sz) return;
    desksaver::setWidgetSize(*e, best);
    e->x = constrain(e->x, 0, max(0, PW() - e->w));
    e->y = constrain(e->y, 0, max(0, PH() - e->h));
    refreshEl(sel);
    edited = true;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    edited = true;
    chrome(true);
    soon(DO_EL | DO_SHEET);  // the text takes its new size; the sheet shows it
  }
}

// ---------------------------------------------------------------- the sheet of the selected element
const char* nameOf(const El& e) {
  static const char* const T[][2] = {{"time", N_("Time")},       {"date", N_("Date")},         {"lunar", N_("Lunar")},
                                     {"text", N_("Your text")}, {"next", N_("Next event")}, {"alarm", N_("Alarm")},
                                     {"temp", N_("Temperature")}, {"box", N_("Shapes")},     {"photo", N_("Photo")}};
  for (auto& t : T)
    if (e.t == t[0]) return t[1];
  const wid::Kind* k = wid::kind(e.k);
  return k ? k->name : N_("Widget");
}

// The format choices of a type: value, label.
struct Fmt {
  const char *f, *name;
};
const Fmt TIME_F[] = {{"24", "07:21"}, {"24h1", "7:21"}, {"12", N_("7:21 AM")}};
const Fmt DATE_F[] = {{"wdm", N_("Weekday, date")}, {"wdml", N_("With lunar")}, {"wdmy", N_("With year")},
                      {"full", N_("Full")},         {"fullu", N_("FULL")},      {"dm", N_("Nov 17")},  // i18n: a sample day and month
                      {"wd", N_("Weekday")},        {"wdu", N_("WEEKDAY")},     {"day", N_("Day number")},
                      {"month", N_("Month")}};
const Fmt LUNAR_F[] = {{"dm", "9/10"}, {"full", N_("Full")}, {"cc", N_("Zodiac")}};  // i18n: Zodiac = the stem-branch name (Can chi, "Fire Horse")
const Fmt TEMP_F[] = {{"t", "29°"}, {"td", N_("29° and weather")}, {"d", N_("Weather only")}};

void fmtRow(const Fmt* fs, int n, const String& cur) {
  lv_obj_t* r = row(sheet, N_("Style"));
  for (int i = 0; i < n; i++)
    pill(r, fs[i].name, cur == fs[i].f || (!cur.length() && i == 0), [](lv_event_t* e) {
      if (El* el = selEl()) el->f = (const char*)lv_event_get_user_data(e);
      changed(DO_EL | DO_SHEET);
    }, (intptr_t)fs[i].f);
}

void editText(int idx);

void moveBy(lv_event_t* e) {  // Bring forward (+1: drawn later, over the others) / Send back (-1)
  std::vector<El>& v = pg().e;
  const int to = sel + arg(e);
  if (sel < 0 || to < 0 || to >= (int)v.size()) return os::toast(arg(e) > 0 ? N_("Already at the front") : N_("Already at the back"));
  std::swap(v[sel], v[to]);
  sel = to;
  changed(DO_ALL);
}

// A photo element: its pictures (one, a set, or all of them: a slideshow, docs/saver.md §2), how they change, and a
// caption over it.
std::vector<int> setOf(const El& e) {
  return e.all ? photoIds() : e.ids.size() > 1 ? e.ids : e.id ? std::vector<int>{e.id} : std::vector<int>{};
}
// Another slideshow on this page would be one too many (docs/saver.md §2: at most 3).
bool slidesFull() {
  int n = 0;
  for (size_t i = 0; i < pg().e.size(); i++) n += (int)i != sel && pg().e[i].slides();
  if (n >= 3) os::toast(N_("At most 3 slideshows a page"));
  return n >= 3;
}
void setTo(El& e, const std::vector<int>& v) {
  e.all = false;
  e.ids = v.size() > 1 ? v : std::vector<int>();
  e.id = v.empty() ? 0 : v[0];
}
void caption();
void capOver(const El& p);
void photoRows(const El& e) {
  const std::vector<int> ids = photoIds(), set = setOf(e);
  const int mode = e.all ? 2 : e.ids.size() > 1 ? 1 : 0;  // one picture, several, all of them
  lv_obj_t* m = row(sheet, N_("Showing"));
  for (int k = 0; k < 3; k++)
    pill(m, k == 0 ? N_("One photo") : k == 1 ? N_("Several, changing") : N_("All photos, changing"), k == mode, [](lv_event_t* ev) {
      El* el = selEl();
      if (!el) return;
      const std::vector<int> all = photoIds();
      const int k = arg(ev);
      if (k && !el->slides() && slidesFull()) return;
      if (k && all.size() < 2) return os::toast(N_("Needs at least 2 photos: add photos on the EspOS web, under Photos"));
      std::vector<int> v = setOf(*el);
      if (k == 0) v.resize(min<size_t>(v.size(), 1));
      for (size_t i = 0; k == 1 && v.size() < 2 && i < all.size(); i++)  // a set starts with two pictures
        if (std::find(v.begin(), v.end(), all[i]) == v.end()) v.push_back(all[i]);
      setTo(*el, v);
      if (k == 2) el->all = true, el->ids.clear(), el->id = all[0];
      changed(DO_EL | DO_SHEET);
    }, k);
  lv_obj_t* r = row(sheet, mode == 0 ? N_("Pick a photo") : mode == 1 ? N_("Pick photos; the numbers are the order")
                                                                   : N_("All the device's photos, in order"));
  if (ids.empty()) label(r, N_("Add photos on the EspOS web, under Photos"), 13, false, DIM);
  for (int id : ids) {
    const int at = std::find(set.begin(), set.end(), id) - set.begin();
    lv_obj_t* t = box(r, 0x2a3240, 8);
    lv_obj_set_size(t, 44, 66);
    lv_obj_set_style_clip_corner(t, true, 0);
    if (!desksaver::photoImg(t, id, 44, 66)) lv_obj_center(glyph(t, &gl_photo_16, 0x9aa3ad));
    if (at < (int)set.size()) {  // picked: a ring, and its place in a set
      lv_obj_set_style_outline_width(t, 3, 0);
      lv_obj_set_style_outline_pad(t, 1, 0);
      lv_obj_set_style_outline_color(t, C(col::ACC), 0);
      if (mode) {
        lv_obj_t* n = box(t, col::ACC, 9);
        lv_obj_set_size(n, 18, 18);
        lv_obj_set_pos(n, 3, 3);
        lv_obj_center(label(n, String(at + 1).c_str(), 12, true, ON_T));
      }
    }
    if (mode == 2) continue;  // all of them: nothing to pick
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t, [](lv_event_t* ev) {
      El* el = selEl();
      if (!el) return;
      std::vector<int> v = setOf(*el);
      const auto it = std::find(v.begin(), v.end(), (int)arg(ev));
      if (!el->slides()) v = {(int)arg(ev)};  // one picture: this one
      else if (it != v.end() && v.size() <= 2) return os::toast(N_("Several photos means 2 or more; for one, pick \"One photo\""));
      else if (it != v.end()) v.erase(it);
      else if (v.size() >= 12) return os::toast(N_("At most 12 photos"));
      else v.push_back(arg(ev));
      setTo(*el, v);
      changed(DO_EL | DO_SHEET);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)id);
  }
  pill(row(sheet, nullptr), N_("+ Text on photo"), false, [](lv_event_t*) { caption(); });
  if (!mode) return;
  lv_obj_t* r2 = row(sheet, N_("Change every"));
  static const uint16_t IV[5] = {10, 30, 60, 300, 900};
  static const char* const IN[5] = {N_("10 sec"), N_("30 sec"), N_("1 min"), N_("5 min"), N_("15 min")};
  for (int k = 0; k < 5; k++)
    pill(r2, IN[k], e.iv == IV[k], [](lv_event_t* ev) {
      if (El* el = selEl()) el->iv = IV[arg(ev)];
      changed(DO_EL | DO_SHEET);
    }, k);
  lv_obj_t* r3 = row(sheet, N_("Order and change"));
  for (int k = 0; k < 4; k++)
    pill(r3, k == 0 ? N_("In order") : k == 1 ? N_("Shuffle") : k == 2 ? N_("Cut") : N_("Fade"),
         k < 2 ? e.ord == (k ? 'r' : 's') : e.tr == (k == 3 ? 'f' : 'c'), [](lv_event_t* ev) {
           El* el = selEl();
           if (!el) return;
           const int k = arg(ev);
           if (k < 2) el->ord = k ? 'r' : 's';
           else el->tr = k == 3 ? 'f' : 'c';
           changed(DO_EL | DO_SHEET);
         }, k);
  // The start picture: frames of a collage set 1, 2, 3 to show pictures that follow each other.
  lv_obj_t* r4 = row(sheet, N_("Start from photo"));
  for (int k = 0; k < (int)min<size_t>(set.size(), 12); k++)
    pill(r4, String(k + 1).c_str(), e.of % max<size_t>(set.size(), 1) == (size_t)k, [](lv_event_t* ev) {
      if (El* el = selEl()) el->of = arg(ev);
      changed(DO_EL | DO_SHEET);
    }, k);
}

void elementSheet(El& e) {
  lv_obj_set_style_pad_bottom(label(sheet, nameOf(e), 18, true, INK), 2, 0);
  lv_obj_t* a = row(sheet, nullptr);
  pill(a, N_("Bring forward"), false, moveBy, 1);
  pill(a, N_("Send back"), false, moveBy, -1);
  pill(a, N_("Delete"), false, [](lv_event_t*) {
    if (sel >= 0 && sel < (int)pg().e.size()) pg().e.erase(pg().e.begin() + sel);
    sel = -1;
    changed(DO_ALL);
  }, 0, true);
  if (e.text()) {
    if (e.t == "time") fmtRow(TIME_F, 3, e.f);
    else if (e.t == "date") fmtRow(DATE_F, 10, e.f.length() ? e.f : String("wdm"));
    else if (e.t == "lunar") fmtRow(LUNAR_F, 3, e.f.length() ? e.f : String("dm"));
    else if (e.t == "temp") fmtRow(TEMP_F, 3, e.f.length() ? e.f : String("td"));
    else if (e.t == "text") pill(row(sheet, N_("Text")), N_("Edit text"), false, [](lv_event_t*) { editText(sel); });
    lv_obj_t* r = row(sheet, N_("Text size"));
    stepper(r, String(e.px ? e.px : desksaver::sizeShown(boxes[sel])), [](lv_event_t* ev) {
      El* el = selEl();
      if (!el) return;
      const std::vector<uint8_t> list = desksaver::sizes(*el);  // largest first
      const int cur = el->px ? el->px : desksaver::sizeShown(boxes[sel]);
      size_t k = 0;
      for (size_t j = 1; j < list.size(); j++)
        if (abs(list[j] - cur) < abs(list[k] - cur)) k = j;
      k = arg(ev) > 0 ? (k ? k - 1 : 0) : min(k + 1, list.size() - 1);
      el->px = list[k];
      // A bigger font needs a taller box (one line of it), still inside the screen.
      const bool digits = el->t == "time" || (el->t == "date" && el->f == "day");
      const int need = (digits ? fonts::get(el->px, el->b) : fonts::text(el->px, el->b))->line_height;
      if (el->h < need) el->h = need, el->y = max(0, min<int>(el->y, PH() - el->h));
      changed(DO_EL | DO_SHEET);
    });
    pill(r, N_("Fit the box"), !e.px, [](lv_event_t*) {
      if (El* el = selEl()) el->px = 0;
      changed(DO_EL | DO_SHEET);
    });
    swatches(row(sheet, N_("Colour")), e.c, [](lv_event_t* ev) {
      if (El* el = selEl()) el->c = (uint32_t)(uintptr_t)lv_event_get_user_data(ev);
      changed(DO_EL | DO_SHEET);
    });
    lv_obj_t* s = row(sheet, N_("Weight and alignment"));
    for (int b = 0; b < 2; b++)
      pill(s, b ? N_("Bold") : N_("Thin"), e.b == (bool)b, [](lv_event_t* ev) {
        if (El* el = selEl()) el->b = arg(ev), el->px = 0;
        changed(DO_EL | DO_SHEET);
      }, b);
    static const char AL[3] = {'l', 'c', 'r'};
    static const char* const AN[3] = {N_("Left"), N_("Centre"), N_("Right")};
    for (int k = 0; k < 3; k++)
      pill(s, AN[k], e.a == AL[k], [](lv_event_t* ev) {
        if (El* el = selEl()) el->a = AL[arg(ev)];
        changed(DO_EL | DO_SHEET);
      }, k);
    // Easy to read over a picture (docs/saver.md §2): nothing, a soft shadow, or a band behind the text.
    lv_obj_t* ov = row(sheet, N_("Easy to read over photos"));
    for (int k = 0; k < 3; k++)
      pill(ov, k == 0 ? N_("None") : k == 1 ? N_("Shadow") : N_("Band"), k == (e.band ? 2 : e.sh ? 1 : 0), [](lv_event_t* ev) {
        El* el = selEl();
        if (!el) return;
        const int k = arg(ev);
        if (k == 2 && !el->band) el->bgc = 0, el->bo = 50, el->r = min<int>(el->r ? el->r : 10, el->h / 2);
        el->band = k == 2, el->sh = k == 1;
        changed(DO_EL | DO_SHEET);
      }, k);
    if (e.band) {
      swatches(row(sheet, N_("Band colour")), e.bgc, [](lv_event_t* ev) {
        if (El* el = selEl()) el->bgc = (uint32_t)(uintptr_t)lv_event_get_user_data(ev);
        changed(DO_EL | DO_SHEET);
      }, BANDS);
      stepper(row(sheet, N_("Band opacity")), String(e.bo) + "%", [](lv_event_t* ev) {
        if (El* el = selEl()) el->bo = constrain(el->bo + 10 * arg(ev), 10, 100);
        changed(DO_EL | DO_SHEET);
      });
      stepper(row(sheet, N_("Band corners")), String(e.r), [](lv_event_t* ev) {
        if (El* el = selEl()) el->r = constrain(el->r + 4 * arg(ev), 0, el->h / 2);
        changed(DO_EL | DO_SHEET);
      });
    }
  } else if (e.t == "box" || e.t == "photo") {
    if (e.t == "box") {
      swatches(row(sheet, N_("Colour")), e.c, [](lv_event_t* ev) {
        if (El* el = selEl()) el->c = (uint32_t)(uintptr_t)lv_event_get_user_data(ev);
        changed(DO_EL | DO_SHEET);
      });
      stepper(row(sheet, N_("Opacity")), String(e.o) + "%", [](lv_event_t* ev) {
        if (El* el = selEl()) el->o = constrain(el->o + 10 * arg(ev), 10, 100);
        changed(DO_EL | DO_SHEET);
      });
    } else {
      photoRows(e);
    }
    stepper(row(sheet, N_("Corners")), String(e.r), [](lv_event_t* ev) {
      if (El* el = selEl()) el->r = constrain(el->r + 4 * arg(ev), 0, 80);
      changed(DO_EL | DO_SHEET);
    });
  } else {  // a widget: Style, Size, its options and Card, exactly Home's rows (widopts.h)
    wid::Item it;
    it.kind = e.k;
    it.size = e.sz == 'L' ? wid::L : e.sz == 'M' ? wid::M : wid::S;
    it.style = e.st;
    it.opt = e.op;
    it.skin = e.sk;
    widopts::build(sheet, it, LOOK, innerW(), [](const wid::Item& it, bool sizeChanged) {
      El* el = selEl();
      if (!el) return;
      el->st = it.style;
      el->op = it.opt;
      el->sk = it.skin;
      if (sizeChanged) {
        desksaver::setWidgetSize(*el, "SML"[it.size]);
        el->x = constrain(el->x, 0, max(0, PW() - el->w));
        el->y = constrain(el->y, 0, max(0, PH() - el->h));
      }
      changed(DO_EL | DO_SHEET);
    });
  }
}

// Nothing selected: the bottom bar (Add, Templates, Settings) and a hint; selected: the element's sheet.
void openAdd();
void openTemplates();
void openSettings();
void buildSheet() {
  if (sheet) lv_obj_delete(sheet), sheet = nullptr;
  El* e = selEl();
  if (tool) e ? lv_obj_add_flag(tool, LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(tool, LV_OBJ_FLAG_HIDDEN);
  if (hint_) e ? lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
  if (!e) return;
  // Portrait: from the bottom, its rounded bottom under the screen's edge. Landscape: a side sheet away from the
  // selection, its rounded right side off the screen.
  keep(&sheet, box(root, PAPER, 20));
  lv_obj_set_width(sheet, land ? 248 + 20 : os::W());
  lv_obj_set_height(sheet, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_left(sheet, 12, 0);
  lv_obj_set_style_pad_right(sheet, land ? 32 : 12, 0);
  lv_obj_set_style_pad_top(sheet, land ? 12 : 8, 0);
  lv_obj_set_style_pad_bottom(sheet, 32, 0);
  lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(sheet, 4, 0);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);  // touches on it stay on it
  if (!land) {
    lv_obj_t* g = box(sheet, 0xc3c6ce, 2);
    lv_obj_set_size(g, 40, 4);
    lv_obj_set_style_margin_left(g, (innerW() - 40) / 2, 0);
  }
  elementSheet(*e);
  lv_obj_update_layout(sheet);
  const int most = land ? os::H() - BAR_H + 16 : os::H() * 9 / 20 + 20;  // portrait: most of the page stays seen
  const int sh = land ? most : min<int>(lv_obj_get_height(sheet), most);
  if (lv_obj_get_height(sheet) > most || land) {
    lv_obj_set_height(sheet, most);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(sheet, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sheet, LV_SCROLLBAR_MODE_OFF);
  }
  if (land) {
    const bool left = e->x + e->w / 2 > os::W() / 2;  // the element is on the right: the sheet on the left
    lv_obj_set_pos(sheet, left ? -20 : os::W() - 248, BAR_H + 4);  // under the bar: Huỷ and Xong stay seen
    lv_obj_set_style_pad_left(sheet, left ? 32 : 12, 0);
    lv_obj_set_style_pad_right(sheet, left ? 12 : 32, 0);
  } else if (e->y + e->h - (os::H() - sh + 20) > BAR_H + sh - 16 - e->y) {  // it hides less of the element on top
    lv_obj_set_pos(sheet, 0, BAR_H + 4);
    lv_obj_set_height(sheet, sh - 20);
    lv_obj_set_style_pad_bottom(sheet, 10, 0);
    lv_obj_add_flag(lv_obj_get_child(sheet, 0), LV_OBJ_FLAG_HIDDEN);  // no grab bar
  } else {
    lv_obj_set_pos(sheet, 0, os::H() - sh + 20);
  }
}

// ---------------------------------------------------------------- adding
// New things land high, seen above the sheet that opens for them (in landscape, the middle), on the first free
// place going down (else there anyway).
int newY(int x, int w, int h) {
  const int y0 = max(land ? 0 : BAR_H, (land ? PH() / 2 : PH() * 3 / 10) - h / 2);
  for (int y = y0; y + h <= PH(); y += 8) {
    bool free = true;
    for (const El& o : pg().e) free &= x >= o.x + o.w || o.x >= x + w || y >= o.y + o.h || o.y >= y + h;
    if (free) return y;
  }
  return y0;
}

El make(const String& t) {
  El e;
  e.t = t;
  int w = 280, h = 32;
  if (t == "time") h = 100;
  else if (t == "date") e.f = "wdm";
  else if (t == "lunar") e.f = "dm";
  else if (t == "text") h = 64;
  else if (t == "temp") w = 160, h = 40, e.f = "td";
  else if (t == "box") w = 120, h = 60, e.o = 40, e.r = 12;
  else if (t == "photo") w = h = 144, e.r = 12, e.id = photoIds().empty() ? 0 : photoIds()[0];
  e.w = min(w, PW() - 16);
  e.h = h;
  e.x = (PW() - e.w) / 2;
  e.y = newY(e.x, e.w, e.h);
  return e;
}

void add(const El& e) {
  if (pg().e.size() >= 24) return os::toast(N_("At most 24 items a page"));
  pg().e.push_back(e);
  sel = pg().e.size() - 1;
  changed(DO_ALL);
}

// From the gallery (Home's): "el:<type>" for the saver's own elements, else a widget kind.
void onAdd(const char* kind, int size, const char* opt) {
  if (!root) return;
  if (!strncmp(kind, "el:", 3)) {
    if (!strcmp(kind + 3, "text")) return editText(-1);
    if (!strcmp(kind + 3, "caption")) {  // over the top photo of the page, else low in the middle
      const El* ph = nullptr;
      for (const El& x : pg().e) if (x.t == "photo") ph = &x;
      El at = make("photo");
      if (!ph) at.x = 16, at.w = PW() - 32, at.y = PH() - 140, at.h = 132, at.r = 12;
      capOver(ph ? *ph : at);
      return editText(-2);
    }
    return add(make(kind + 3));
  }
  El e;
  e.t = "w";
  e.k = kind;
  e.op = opt;
  if (e.k == "my" && !e.op.length() && !wid::mine().empty()) e.op = wid::mine()[0].id;
  desksaver::setWidgetSize(e, "SML"[constrain(size, 0, 2)]);
  e.x = max(0, (PW() - e.w) / 2);
  e.y = newY(e.x, e.w, e.h);
  add(e);
}

void unselect() {
  const int old = sel;
  sel = -1;
  outline(old);
  placeHandle();
  buildSheet();
}

void openAdd() {
  unselect();
  home::gallery(N_("Add to the saver"), true, onAdd);
}

// ---------------------------------------------------------------- pages
void goPage(int p) {
  page = p;
  sel = -1;
  soon(DO_ALL);
}

void openPages() {
  unselect();
  const int n = pages().size(), h = sheetH(page < 0 ? 170 : 220);
  lv_obj_t* b = sheetBody(
      home::openSheet(h, page < 0 ? T_("Night page") : i18n::fmt(N_("Page {0} of {1}"), String(page + 1), String(n)).c_str()),
      h);
  if (page < 0) {
    whiteRow(b, N_("Back to the day pages"), "", [](lv_event_t*) {
      home::closeSheet();
      theme::refresh();
      goPage(0);
    });
    lv_obj_t* r = whiteRow(b, N_("Remove the night page"), "", [](lv_event_t*) {
      home::closeSheet();
      theme::refresh();
      hasNight() = false;
      (land ? d.nl : d.np).e.clear();  // the built-in red clock again
      page = 0;
      sel = -1;
      changed(DO_ALL);
    }, 0, false);
    lv_obj_set_style_text_color(lv_obj_get_child(r, 0), C(col::RED), 0);
    return;
  }
  if (n < 3) {
    whiteRow(b, N_("Add a blank page"), "", [](lv_event_t*) {
      home::closeSheet();
      theme::refresh();
      pages().emplace_back();
      if (!d.cycle) d.cycle = 60;  // more than one page: they take turns
      page = pages().size() - 1;
      sel = -1;
      changed(DO_ALL);
    });
    whiteRow(b, N_("Duplicate this page"), "", [](lv_event_t*) {
      home::closeSheet();
      theme::refresh();
      pages().insert(pages().begin() + page + 1, Page(pages()[page]));
      if (!d.cycle) d.cycle = 60;
      page++;
      sel = -1;
      changed(DO_ALL);
    });
  }
  if (n > 1) {
    lv_obj_t* r = whiteRow(b, N_("Delete this page"), "", [](lv_event_t*) {
      home::closeSheet();
      theme::refresh();
      pages().erase(pages().begin() + page);
      page = max(0, page - 1);
      sel = -1;
      changed(DO_ALL);
    }, 0, false);
    lv_obj_set_style_text_color(lv_obj_get_child(r, 0), C(col::RED), 0);
  }
  if (n >= 3) label(b, N_("At most 3 pages for each way up."), 13, false, DIM);
}

// ---------------------------------------------------------------- templates (shared with the web)
std::vector<desksaver::Template> tpls;
std::vector<String> tplGroups;  // the chips: "id|name" of the groups shown (day pages)
int tplGroup = 0;               // the chip picked: tplGroups[tplGroup] (a group at a time: ~70 pictures at once is too many)
bool fetching = false, fetchFailed = false;
uint32_t fetchedAt = 0;

String readCache() {
#ifdef SIM
  std::ifstream in(simDir + "/fs/saver_tpl.json", std::ios::binary);
  const std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return String(s.c_str());
#else
  fs::File f = LittleFS.exists("/saver_tpl.json") ? LittleFS.open("/saver_tpl.json", "r") : fs::File();
  String s = f ? f.readString() : String();
  if (f) f.close();
  return s;
#endif
}

void writeCache(const uint8_t* data, size_t len) {
#ifdef SIM
  std::ofstream out(simDir + "/fs/saver_tpl.json", std::ios::binary);
  out.write((const char*)data, len);
#else
  fs::File f = LittleFS.open("/saver_tpl.json", "w");
  if (f) f.write(data, len), f.close();
#endif
}

void fillTemplates();
void fetchTemplates() {
  if (fetching || (fetchedAt && millis() - fetchedAt < 30 * 60000)) return;
  fetching = true;
  desk::fetch(TPL_URL, true, [](void*, int status, JsonDocument*, const uint8_t* data, size_t len) {
    fetching = false;
    fetchedAt = millis();
    std::vector<desksaver::Template> t;
    String json;
    if (status == 200 && data && len < 256 * 1024) json.concat((const char*)data, len);  // ~62 KB in 2026-11
    fetchFailed = !desksaver::templates(json, false, t);
    trace::add("saver templates: %d, %u bytes%s", status, (unsigned)len, fetchFailed ? ", not used" : "");
    if (!fetchFailed && json != readCache()) {
      writeCache(data, len);
      if (tplBody) return fillTemplates();
    }
    if (tplNote) {  // the list stays; only the note changes
      if (fetchFailed) setText(tplNote, N_("Could not get new templates; these are the ones on the device."));
      else lv_obj_delete(tplNote);
    }
  }, nullptr);
}

void useTemplate() {
  const int i = (intptr_t)lv_obj_get_user_data(tplBody) - 1;
  home::closeSheet();
  theme::refresh();
  if (i < 0 || i >= (int)tpls.size()) return;
  // As the web (docs/saver.md §5): the page being edited becomes the template's (the night page when that is edited).
  const desksaver::Template& t = tpls[i];
  pg() = t.pages[0];
  sel = -1;
  trace::add("saver editor: template %s", t.name.c_str());
  changed(DO_ALL);
}

// One small picture a tick (each is the page drawn whole, then scaled to a quarter): the sheet never stalls.
void thumbStep(lv_timer_t*) {
  if (!tplBody) return;
  for (uint32_t k = 0; k < lv_obj_get_child_count(tplBody); k++) {
    lv_obj_t* frame = lv_obj_get_child(lv_obj_get_child(tplBody, k), 0);
    const int i = (intptr_t)lv_obj_get_user_data(lv_obj_get_child(tplBody, k)) - 1;  // tiles only (the chips: -1)
    if (!frame || lv_obj_get_child_count(frame) || i < 0 || i >= (int)tpls.size()) continue;
    desksaver::thumb(frame, tpls[i].pages[0], 4);
    return;
  }
}

void fillTemplates() {
  lv_obj_clean(tplBody);
  std::vector<String> groups;
  desksaver::templates(readCache(), land, tpls, &groups);
  // A chip per group that has templates here, as the web's gallery.
  tplGroups.clear();
  for (const String& g : groups) {
    const String id = g.substring(0, g.indexOf('|'));
    bool any = false;
    for (const desksaver::Template& t : tpls) any |= t.group == id;
    if (any) tplGroups.push_back(g);
  }
  if (tplGroup >= (int)tplGroups.size()) tplGroup = 0;
  if (tplGroups.size() > 1) {
    static std::vector<String> names;
    static std::vector<const char*> ptrs;
    names.clear();
    for (const String& g : tplGroups) names.push_back(g.substring(g.indexOf('|') + 1));
    ptrs.clear();
    for (const String& n : names) ptrs.push_back(n.c_str());
    lv_obj_t* ch = chips(tplBody, ptrs.data(), ptrs.size(), tplGroup, [](int i) {
      tplGroup = i;
      lv_async_call([](void*) { if (tplBody) fillTemplates(); }, nullptr);  // not inside the chip's own event
    });
    lv_obj_set_width(ch, os::W() - 24);
    lv_obj_update_layout(ch);
    if (lv_obj_t* on = lv_obj_get_child(ch, tplGroup)) lv_obj_scroll_to_view(on, LV_ANIM_OFF);  // the chip picked, seen
  }
  const String want = tplGroups.empty() ? String() : tplGroups[tplGroup].substring(0, tplGroups[tplGroup].indexOf('|'));
  const int cols = 3, tw = (os::W() - 24 - 8 * (cols - 1)) / cols;
  for (size_t i = 0; i < tpls.size(); i++) {
    if (want.length() && tpls[i].group != want) continue;
    lv_obj_t* t = kit::clear(tplBody);
    lv_obj_set_size(t, tw, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t, 2, 0);
    lv_obj_set_user_data(t, (void*)(intptr_t)(i + 1));
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t, [](lv_event_t* e) {
      lv_obj_set_user_data(tplBody, lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e)));
      const String q = i18n::fmt(page < 0 ? N_("The night page becomes the template \"{0}\". Not happy with it? Tap Cancel at the top.")
                                          : N_("This page becomes the template \"{0}\". Not happy with it? Tap Cancel at the top."),
                                 tpls[(intptr_t)lv_obj_get_user_data(tplBody) - 1].name);
      ask(N_("Replace the current saver?"), q.c_str(), N_("Not now"), N_("Replace"), useTemplate);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* f = box(t, 0x2a3240, 8);  // the picture comes later (thumbStep)
    lv_obj_set_size(f, os::W() / 4, os::H() / 4);
    lv_obj_set_style_clip_corner(f, true, 0);
    lv_obj_set_style_border_post(f, true, 0);
    lv_obj_set_style_border_width(f, 1, 0);
    lv_obj_set_style_border_color(f, C(0xd9dbe1), 0);
    fitText(label(t, "", 13, true, INK), tpls[i].name.c_str(), tw, 34, 13, 12, true, 0);
    fitText(label(t, "", 12, false, DIM), tpls[i].desc.c_str(), tw, 46, 12, 12, false, 0);
  }
  if (fetching || fetchFailed) {
    keep(&tplNote, label(tplBody, "", 13, false, DIM));
    wrapText(tplNote, fetching ? N_("Loading new templates") : N_("Could not get new templates; these are the ones on the device."),
             os::W() - 24, 13);
  }
}

void openTemplates() {
  unselect();
  fetchTemplates();
  const int h = sheetH(430);
  keep(&tplBody, sheetBody(home::openSheet(h, page < 0 ? N_("Night templates") : N_("Saver templates")), h));
  lv_obj_set_flex_flow(tplBody, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(tplBody, 8, 0);
  lv_obj_set_style_pad_row(tplBody, 12, 0);
  lv_obj_set_user_data(tplBody, nullptr);
  // The night page opens on the night group, a day page on all of them (the built-in ones have no groups).
  tplGroup = 0;
  if (page < 0) {
    std::vector<String> g;
    desksaver::templates(readCache(), land, tpls, &g);
    int n = 0;
    for (const String& x : g) {
      bool any = false;
      for (const desksaver::Template& t : tpls) any |= t.group == x.substring(0, x.indexOf('|'));
      if (any && x.startsWith("night|")) tplGroup = n;
      n += any;
    }
  }
  fillTemplates();
  every(tplBody, 40, thumbStep);
}

// ---------------------------------------------------------------- settings
void fillSettings();
const char* bgName(const desksaver::Bg& b) {
  if (b.t == "wall") {
    for (int i = 0; i < theme::WALL_COUNT; i++)
      if (b.w == theme::WALLS[i].id) return theme::WALLS[i].name;
    return N_("From the theme");
  }
  if (b.t == "tank") return N_("Aquarium");  // the live aquarium (src/tank)
  return b.t == "photo" ? (b.id ? N_("Photo") : N_("Photos in turn")) : b.t == "grad" ? N_("Gradient")
         : b.t == "color" ? N_("Solid colour")
                          : N_("Black");
}

void setPageBg(const home::PageBg& b) {
  desksaver::Bg n = b.bg;
  if (!b.own) n = desksaver::Bg(), n.t = "wall";  // the look's wallpaper
  pg().bg = n;
  changed(DO_ALL);  // drawn behind the sheet
}

int nightChoice() { return d.night == "off" ? 1 : d.night == "day" ? 2 : hasNight() ? 3 : 0; }

void pickNight(lv_event_t* e) {
  const int k = arg(e);
  d.night = k == 1 ? "off" : k == 2 ? "day" : "page";
  edited = true;
  if (k == 0 && hasNight()) {  // the built-in red clock again
    hasNight() = false;
    (land ? d.nl : d.np).e.clear();
  }
  if (k == 3) {  // straight to the night page, to make it one's own
    home::closeSheet();
    theme::refresh();
    return goPage(-1);
  }
  if (page < 0) goPage(0);
  lv_async_call([](void*) { fillSettings(); }, nullptr);
}

void fillSettings() {
  if (!setBody) return;
  lv_obj_clean(setBody);
  whiteRow(setBody, page < 0 ? N_("Night page background") : N_("This page's background"), bgName(pg().bg), [](lv_event_t*) {
    home::PageBg b;
    b.bg = pg().bg;
    b.own = !(b.bg.t == "wall" && !b.bg.w.length());
    if (b.bg.t == "black") b.bg.t = "color", b.bg.c = 0;  // the same black, as the sheet's first colour
    home::bgSheet(page < 0 ? N_("Night page background") : N_("This page's background"), b, setPageBg, true);
  });
  label(setBody, i18n::fmt(N_("Daytime brightness: {0}%"), String(d.light)).c_str(), 13, true, DIM);
  lv_obj_t* c = box(setBody, 0xffffff, 12);
  lv_obj_set_size(c, lv_pct(100), 46);
  lv_obj_t* sl = lv_slider_create(c);
  lv_obj_set_width(sl, os::W() - 24 - 40);
  lv_obj_center(sl);
  lv_slider_set_range(sl, 5, 100);
  lv_slider_set_value(sl, d.light, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, C(0xd9dbe1), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, C(col::ACC), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, C(0xffffff), LV_PART_KNOB);
  lv_obj_set_style_shadow_width(sl, 6, LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(sl, LV_OPA_30, LV_PART_KNOB);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {  // the screen shows the brightness while the finger is on it
    const int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    lcd::setBacklight(v);
    lv_label_set_text(lv_obj_get_child(setBody, 1), i18n::fmt(N_("Daytime brightness: {0}%"), String(v)).c_str());
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {
    d.light = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    edited = true;
    lcd::setBacklight(os::brightness());
  }, LV_EVENT_RELEASED, nullptr);
  label(setBody,
        i18n::fmt(N_("At night, {0} to {1}"), desk::setting("dash.quiet_from", "23:00"), desk::setting("dash.quiet_to", "06:00"))
            .c_str(),
        13, true, DIM);
  static const char* const N[4] = {N_("Dim red clock"), N_("Screen off"), N_("Same as by day"), N_("A night page of its own")};
  for (int k = 0; k < 4; k++) {
    lv_obj_t* r = whiteRow(setBody, N[k], "", pickNight, k, false);
    lv_obj_t* v = lv_obj_get_child(r, 1);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(v, C(col::IOS_BLUE), 0);
    lv_label_set_text(v, k == nightChoice() ? LV_SYMBOL_OK : "");
  }
  if (nightChoice() == 3 && page >= 0)
    whiteRow(setBody, N_("Edit the night page"), "", [](lv_event_t*) {
      home::closeSheet();
      theme::refresh();
      goPage(-1);
    });
  if (pages().size() > 1) {
    label(setBody, N_("Change pages every"), 13, true, DIM);
    lv_obj_t* r = row(setBody, nullptr);
    static const int CY[4] = {0, 60, 300, 900};
    static const char* const CN[4] = {N_("Never"), N_("1 min"), N_("5 min"), N_("15 min")};
    for (int i = 0; i < 4; i++)
      pill(r, CN[i], d.cycle == CY[i], [](lv_event_t* ev) {
        d.cycle = CY[arg(ev)];
        edited = true;
        lv_async_call([](void*) { fillSettings(); }, nullptr);
      }, i);
  }
}

void openSettings() {
  unselect();
  const int h = sheetH(440);
  keep(&setBody, sheetBody(home::openSheet(h, N_("Saver settings")), h));
  fillSettings();
}

// ---------------------------------------------------------------- the user's text, typed with the keyboard
int textIdx = -1;
El capEl;  // a caption being written (textIdx -2): over the bottom of a photo, white on a band

// Over the bottom of photo p (docs/saver.md §2, the web the same): its x and w, h = min(56, max(24, h/3)), white bold
// on a black band at 45 %, its corner radius.
void capOver(const El& p) {
  capEl = El();
  capEl.t = "text";
  capEl.h = min<int>(56, max(24, p.h / 3));
  capEl.x = p.x, capEl.w = p.w, capEl.y = p.y + p.h - capEl.h;
  capEl.c = 0xffffff, capEl.b = true, capEl.band = true, capEl.bo = 45, capEl.r = min<int>(p.r, capEl.h / 2);
}

void caption() {
  const El* p = selEl();
  if (!p) return;
  capOver(*p);
  editText(-2);
}
void editText(int idx) {
  textIdx = idx;
  if (field) return;
  // The keyboard pads the field's first scrolling parent to stay clear of it: a holder of its own (not the layer).
  lv_obj_t* holder = kit::clear(root);
  lv_obj_set_size(holder, os::W(), 64);
  lv_obj_set_pos(holder, 0, BAR_H + 4);
  lv_obj_add_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(holder, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(holder, LV_SCROLLBAR_MODE_OFF);
  keep(&field, lv_textarea_create(holder));
  lv_obj_set_size(field, os::W() - 24, LV_SIZE_CONTENT);
  lv_obj_set_pos(field, 12, 4);
  lv_textarea_set_one_line(field, true);
  lv_textarea_set_max_length(field, 80);
  lv_obj_set_style_text_font(field, fonts::get(16), 0);
  lv_obj_set_style_radius(field, 12, 0);
  lv_obj_set_style_border_color(field, C(col::ACC), 0);
  lv_obj_set_style_border_width(field, 2, 0);
  lv_textarea_set_placeholder_text(field, T_("Type your text"));
  const El* e = idx >= 0 && idx < (int)pg().e.size() ? &pg().e[idx] : nullptr;
  lv_textarea_set_text(field, e ? e->s.c_str() : idx == -2 ? T_("Our family") : "");  // a caption: the web's words
  lv_obj_add_state(field, LV_STATE_FOCUSED);
  kb::open(field, kb::Mode::Text, [](lv_obj_t* t) {
    const String s = lv_textarea_get_text(t);
    kb::close();
    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);  // the redraw below deletes it with the page
    soon(DO_ALL);
    if (!s.length()) return;
    if (textIdx >= 0 && textIdx < (int)pg().e.size()) {
      pg().e[textIdx].s = s;
      edited = true;
    } else {
      El e = textIdx == -2 ? capEl : make("text");
      e.s = s;
      add(e);
    }
  });
}

// ---------------------------------------------------------------- the screen
// The pages of this orientation in turn, then the night page when it is one's own.
void stepPage(lv_event_t* e) {
  const int n = pages().size(), dir = arg(e);
  int to = page < 0 ? (dir < 0 ? n - 1 : -2) : page + dir;
  if (to == n && ownNight()) to = -1;
  if (to < -1 || to >= n) return;
  goPage(to);
}

lv_obj_t* arrow(lv_obj_t* parent, bool next, bool can) {
  lv_obj_t* a = kit::clear(parent);
  lv_obj_set_size(a, 30, 26);
  lv_obj_t* g = glyph(a, next ? &gl_chev_16 : &gl_back_16, 0xffffff);
  lv_obj_center(g);
  lv_obj_set_style_opa(g, can ? LV_OPA_COVER : LV_OPA_30, 0);
  if (can) {
    lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(a, 6);
    lv_obj_add_event_cb(a, stepPage, LV_EVENT_CLICKED, (void*)(intptr_t)(next ? 1 : -1));
  }
  return a;
}

void buildAll() {
  if (root) lv_obj_clean(root);
  else {
    keep(&root, box(lv_layer_top(), 0x000000, 0));
    lv_obj_set_size(root, os::W(), os::H());
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, [](lv_event_t*) { unselect(); }, LV_EVENT_CLICKED, nullptr);  // a tap on the empty page
  }
  sheet = tool = hint_ = handle = guideV = guideH = field = nullptr;
  boxes.clear();
  const int w = os::W();
  desksaver::drawBg(root, pg().bg, w, os::H());
  keep(&layer, desksaver::canvas(root, land));  // the page's own screen, in the middle of a bigger one
  for (const El& e : pg().e) boxes.push_back(addBox(e));
  if (sel >= (int)boxes.size()) sel = -1;
  for (size_t i = 0; i < boxes.size(); i++) outline(i);
  keep(&guideV, box(layer, GUIDE, 0));
  lv_obj_set_size(guideV, 1, PH());
  keep(&guideH, box(layer, GUIDE, 0));
  lv_obj_set_size(guideH, PW(), 1);
  showGuides(-1, -1);
  keep(&handle, box(layer, 0xffffff, 5));
  lv_obj_set_size(handle, 18, 18);
  lv_obj_set_style_border_width(handle, 2, 0);
  lv_obj_set_style_border_color(handle, C(SEL), 0);
  lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(handle, 12);
  for (lv_event_code_t k : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(handle, onHandle, k, nullptr);
  placeHandle();
  // The bar on top (Cancel, the pages, Done) over a shade; it takes no touches itself, so what is under it still drags.
  lv_obj_t* bar = box(root, 0x000000, 0);
  lv_obj_set_size(bar, w, BAR_H);
  lv_obj_set_style_bg_grad_color(bar, C(0x000000), 0);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_opa(bar, 200, 0);
  lv_obj_set_style_bg_grad_opa(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 8, 0);
  lv_obj_t* cancelB = barPill(bar, N_("Cancel"), false, [](lv_event_t*) {
    if (!edited) return close(false);
    ask(N_("Discard changes?"), N_("What you just changed will not be saved."), N_("Keep editing"), N_("Discard"),
        [] { close(false); });
  });
  lv_obj_align(cancelB, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* doneB = barPill(bar, N_("Done"), true, [](lv_event_t*) { close(true); });
  lv_obj_align(doneB, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* pager = box(bar, 0xffffff, 13);
  lv_obj_set_style_bg_opa(pager, 60, 0);
  lv_obj_set_size(pager, LV_SIZE_CONTENT, 26);
  lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(pager, LV_ALIGN_CENTER, 0, 0);
  const int n = pages().size();
  arrow(pager, false, page != 0);
  lv_obj_t* name = kit::clear(pager);
  lv_obj_set_size(name, LV_SIZE_CONTENT, 26);
  lv_obj_set_style_pad_hor(name, 2, 0);
  lv_obj_add_flag(name, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(name, [](lv_event_t*) { openPages(); }, LV_EVENT_CLICKED, nullptr);
  // Landscape: the number alone (the bar holds every control there).
  lv_obj_center(label(name, page < 0  ? T_("Night page")
                            : land ? (String(page + 1) + "/" + n).c_str()
                                   : i18n::fmt(N_("Page {0}/{1}"), String(page + 1), String(n)).c_str(),
                      14, true, 0xffffff));
  arrow(pager, true, page >= 0 && (page < n - 1 || ownNight()));
  // The bottom bar (Add, Templates, Settings) and the hint over it, while nothing is selected. Landscape: Add,
  // Templates and Settings join the top bar (a bottom bar would lie on the page's right-hand widgets), unless a
  // longer language makes the row too wide: then they stay at the bottom.
  auto tools = [](lv_obj_t* parent, int h) {
    keep(&tool, box(parent, 0x000000, 20));
    lv_obj_set_style_bg_opa(tool, h > 26 ? 170 : 0, 0);
    lv_obj_set_size(tool, LV_SIZE_CONTENT, h > 26 ? 44 : 26);
    lv_obj_set_style_pad_hor(tool, h > 26 ? 6 : 0, 0);
    lv_obj_set_flex_flow(tool, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tool, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tool, h > 26 ? 8 : 6, 0);
    barPill(tool, N_("+ Add"), true, [](lv_event_t*) { openAdd(); }, h);
    barPill(tool, N_("Templates"), false, [](lv_event_t*) { openTemplates(); }, h);
    barPill(tool, N_("Settings"), false, [](lv_event_t*) { openSettings(); }, h);
  };
  bool inBar = false;
  if (land) {
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    tools(bar, 26);
    for (uint32_t i = 0; i < lv_obj_get_child_count(tool); i++) lv_obj_set_style_pad_hor(lv_obj_get_child(tool, i), 9, 0);
    lv_obj_move_to_index(pager, 1);
    lv_obj_move_to_index(doneB, -1);
    lv_obj_update_layout(bar);
    int used = 6 * 3;
    for (lv_obj_t* o : {cancelB, pager, tool, doneB}) used += lv_obj_get_width(o);
    inBar = used <= lv_obj_get_content_width(bar);
    if (!inBar) {  // too wide: the portrait way
      lv_obj_delete(tool);
      lv_obj_set_layout(bar, LV_LAYOUT_NONE);
      lv_obj_align(cancelB, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_align(doneB, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_align(pager, LV_ALIGN_CENTER, 0, 0);
    }
  }
  if (!inBar) {
    tools(root, 32);
    lv_obj_align(tool, LV_ALIGN_BOTTOM_MID, 0, -10);
  }
  keep(&hint_, box(root, 0x000000, 10));
  lv_obj_set_style_bg_opa(hint_, 150, 0);
  lv_obj_set_size(hint_, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_pad_hor(hint_, 8, 0);
  lv_obj_center(label(hint_, N_("Drag to move · tap to edit"), 12, false, 0xc9cdd6));
  lv_obj_align(hint_, inBar ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_MID, inBar ? 10 : 0, inBar ? -10 : -60);
  buildSheet();
}

}  // namespace

void open(void (*closed)()) {
  if (root) return;
  if (!slots::saverBuiltin()) {  // an app draws the saver (Settings › Default apps): its pages are not edited here
    os::toast(i18n::fmt(N_("{0} draws the screen saver. To edit pages, choose Clock and photos in Default apps, in Settings."),
                        slots::saverName()).c_str());
    if (closed) closed();
    return;
  }
  desksaver::doc();  // re-read if the web changed it
  desksaver::parse(desk::setting("s3.saver_pages", ""), d);  // the same as the saver's (no copy code: flash)
  land = os::landscape();
  page = desksaver::shownLand() == land ? desksaver::shownPage() : 0;
  if (page >= (int)pages().size() || (page < 0 && !ownNight())) page = 0;
  sel = -1;
  edited = false;
  pending = 0;
  isOpen_ = true;
  onClosed = closed;
  lcd::setBacklight(os::brightness());  // lit as the rest of the device (the saver may be dim or at night 3 %)
  buildAll();
  under(true);
  fetchTemplates();  // ready by the time "Mẫu" is tapped
  trace::add("saver editor open (%s %s %d)", land ? "land" : "port", page < 0 ? "night" : "page", page + 1);
}

void close(bool save) {
  if (!isOpen_) return;
  isOpen_ = false;
  kb::close();
  home::closeSheet();
  theme::refresh();
  if (dlg) lv_obj_delete_async(dlg), dlg = nullptr;
  if (save && edited) desksaver::save(d);
  under(false);
  if (root) {
    lv_obj_t* r = root;
    root = nullptr;
    lv_obj_delete_async(r);  // often called from one of its own buttons
  }
  boxes.clear();
  sel = -1;
  desksaver::redraw();
  trace::add("saver editor closed (%s)", save && edited ? "saved" : save ? "no change" : "cancelled");
  if (void (*f)() = onClosed) onClosed = nullptr, f();
}

bool isOpen() { return isOpen_; }

void tick() {
  if (!isOpen_) return;
  if (!root) {  // the screens were rebuilt (rotation): keep what was done
    close(true);
    return;
  }
  wid::tick();  // clocks on the page keep time
  if (os::idleMs() > 180000) close(true);
}

}  // namespace saveredit
