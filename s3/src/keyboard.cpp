#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "keyboard.h"

#include <Arduino.h>

#include <string>
#include <vector>

#include "fonts.h"
#include "i18n.h"
#include "telex.h"
#include "touch.h"

namespace kb {
namespace {
static_assert(sizeof(lv_buttonmatrix_ctrl_t) == sizeof(uint32_t), "ctrl maps are stored as uint32_t");

constexpr uint32_t KEY = 2 | LV_BUTTONMATRIX_CTRL_POPOVER | LV_BUTTONMATRIX_CTRL_NO_REPEAT |
                         LV_BUTTONMATRIX_CTRL_CLICK_TRIG;
constexpr uint32_t GAP = 1 | LV_BUTTONMATRIX_CTRL_HIDDEN;
constexpr uint32_t FN = LV_BUTTONMATRIX_CTRL_CUSTOM_1 | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
constexpr uint32_t OK = 4 | LV_BUTTONMATRIX_CTRL_CUSTOM_2 | LV_BUTTONMATRIX_CTRL_NO_REPEAT |
                        LV_BUTTONMATRIX_CTRL_CLICK_TRIG;
constexpr const char* SHIFT = LV_SYMBOL_UP;
constexpr const char* BKSP = LV_SYMBOL_BACKSPACE;
const char* SPACE = "space";  // the space bar's label, translated by applyMap()
const char* DONE = "Done";   // label of the OK key, set by open() (translated)

const char* mapLower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                          " ", "a", "s", "d", "f", "g", "h", "j", "k", "l", " ", "\n",
                          SHIFT, "z", "x", "c", "v", "b", "n", "m", BKSP, "\n",
                          "123", "VI", SPACE, ".", DONE, ""};
const char* mapUpper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                          " ", "A", "S", "D", "F", "G", "H", "J", "K", "L", " ", "\n",
                          SHIFT, "Z", "X", "C", "V", "B", "N", "M", BKSP, "\n",
                          "123", "VI", SPACE, ".", DONE, ""};
// Password: no VI key, the OK key ("Connect") takes its room.
const char* mapLowerPw[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                            " ", "a", "s", "d", "f", "g", "h", "j", "k", "l", " ", "\n",
                            SHIFT, "z", "x", "c", "v", "b", "n", "m", BKSP, "\n",
                            "123", SPACE, ".", DONE, ""};
const char* mapUpperPw[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                            " ", "A", "S", "D", "F", "G", "H", "J", "K", "L", " ", "\n",
                            SHIFT, "Z", "X", "C", "V", "B", "N", "M", BKSP, "\n",
                            "123", SPACE, ".", DONE, ""};
const char* mapSym[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                        "-", "/", ":", ";", "(", ")", "@", "&", "_", "#", "\n",
                        "ABC", ".", ",", "?", "!", "'", "\"", "+", BKSP, "\n",
                        "=", "$", SPACE, "%", DONE, ""};

const uint32_t ctrlLetters[] = {
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    GAP, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, GAP,
    3 | FN, KEY, KEY, KEY, KEY, KEY, KEY, KEY, 3 | LV_BUTTONMATRIX_CTRL_CUSTOM_1,
    3 | FN | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, 2 | FN | LV_BUTTONMATRIX_CTRL_CLICK_TRIG,
    9 | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY, OK};
const uint32_t ctrlSym[] = {
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    3 | FN | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY, KEY, KEY, KEY, KEY, KEY, KEY, 3 | LV_BUTTONMATRIX_CTRL_CUSTOM_1,
    3 | LV_BUTTONMATRIX_CTRL_POPOVER | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY,
    9 | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY, OK};

const uint32_t ctrlLettersPw[] = {
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    GAP, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, GAP,
    3 | FN, KEY, KEY, KEY, KEY, KEY, KEY, KEY, 3 | LV_BUTTONMATRIX_CTRL_CUSTOM_1,
    3 | FN | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, 9 | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY,
    (OK & ~0xFu) | 6};
const uint32_t ctrlSymPw[] = {
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    3 | FN | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY, KEY, KEY, KEY, KEY, KEY, KEY, 3 | LV_BUTTONMATRIX_CTRL_CUSTOM_1,
    3 | LV_BUTTONMATRIX_CTRL_POPOVER | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY,
    7 | LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG, KEY, (OK & ~0xFu) | 6};

constexpr int VI_KEY = 31;     // button index of the VI/EN key (map index 34 counts the "\n" rows)
constexpr int SHIFT_KEY = 21;  // index of the shift key

lv_obj_t *box, *btnm, *picker, *target;
lv_obj_t* acc = nullptr;  // the app's key row on top (kb::accessory)
int accH = 0;
lv_obj_t* lifted = nullptr;  // the scrolling container given room under its content
int32_t liftedPad = 0;

// The field being typed into stays above the keyboard: its scrolling container gets bottom
// room as tall as the keyboard and is scrolled to show the field.
lv_obj_t* scrollParent(lv_obj_t* t) {  // the field's nearest parent that scrolls up and down
  for (lv_obj_t* p = t ? lv_obj_get_parent(t) : nullptr; p; p = lv_obj_get_parent(p))
    if (lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE) && (lv_obj_get_scroll_dir(p) & LV_DIR_VER)) return p;
  return nullptr;
}

void liftTarget(int kbH) {
  lifted = scrollParent(target);
  if (!lifted) return;
  liftedPad = lv_obj_get_style_pad_bottom(lifted, 0);
  lv_obj_set_style_pad_bottom(lifted, kbH + 12, 0);
  lv_obj_update_layout(lifted);
  lv_area_t a, b;
  lv_obj_get_coords(target, &a);
  lv_obj_get_coords(lifted, &b);
  const int32_t visibleBottom = lv_display_get_vertical_resolution(nullptr) - kbH - 8;
  if (a.y2 > visibleBottom) lv_obj_scroll_by_bounded(lifted, 0, -(a.y2 - visibleBottom), LV_ANIM_ON);
}

void lower() {
  if (lifted && lv_obj_is_valid(lifted)) lv_obj_set_style_pad_bottom(lifted, liftedPad, 0);
  lifted = nullptr;
}
void (*doneCb)(lv_obj_t*) = nullptr;
Mode mode = Mode::Text;
bool vi = true, upper = false, caps = false, symbols = false, pickerOn = false;
uint32_t lastShift = 0;
std::vector<char32_t> pickChars;
int pickSel = 0, pickX0 = 0;
constexpr int PICK_W = 30;

bool landscape() { return lv_display_get_horizontal_resolution(nullptr) > lv_display_get_vertical_resolution(nullptr); }

bool telexOn() { return vi && mode != Mode::Password; }  // Vietnamese typing (on by default only in Vietnamese)

void applyMap() {
  const bool pw = mode == Mode::Password;
  mapLower[37] = mapUpper[37] = mapLowerPw[36] = mapUpperPw[36] = mapSym[36] = DONE;
  SPACE = T_("space");  // i18n: the space bar
  mapLower[35] = mapUpper[35] = mapLowerPw[34] = mapUpperPw[34] = mapSym[34] = SPACE;
  if (symbols) {
    lv_buttonmatrix_set_map(btnm, mapSym);
    lv_buttonmatrix_set_ctrl_map(btnm, (const lv_buttonmatrix_ctrl_t*)(pw ? ctrlSymPw : ctrlSym));
    return;
  }
  const char** m = pw ? (upper ? mapUpperPw : mapLowerPw) : (upper ? mapUpper : mapLower);
  if (!pw) m[34] = vi ? "VI" : "EN";  // the VI/EN label (index in the map with "\n")
  lv_buttonmatrix_set_map(btnm, m);
  lv_buttonmatrix_set_ctrl_map(btnm, (const lv_buttonmatrix_ctrl_t*)(pw ? ctrlLettersPw : ctrlLetters));
  if (upper) lv_buttonmatrix_set_button_ctrl(btnm, SHIFT_KEY, LV_BUTTONMATRIX_CTRL_CHECKED);
}

void insertText(const char* s) { lv_textarea_add_text(target, s); }

// Telex: rewrite the word right before the cursor.
void typeLetter(char c) {
  const std::u32string all = telex::fromUtf8(lv_textarea_get_text(target));
  size_t end = lv_textarea_get_cursor_pos(target);
  if (end > all.size()) end = all.size();
  size_t start = end;
  while (start > 0 && telex::isLetter(all[start - 1])) start--;
  const std::u32string word = all.substr(start, end - start);
  const std::u32string next = telex::type(word, (char32_t)c);
  for (size_t i = 0; i < word.size(); i++) lv_textarea_delete_char(target);
  lv_textarea_add_text(target, telex::toUtf8(next).c_str());
}

void afterLetter() {
  if (upper && !caps) {
    upper = false;
    applyMap();
  }
}

void handleKey(const char* t) {
  if (!target) return;
  if (!strcmp(t, BKSP)) return lv_textarea_delete_char(target);
  if (!strcmp(t, DONE)) {
    if (doneCb) doneCb(target);
    return;
  }
  if (!strcmp(t, SHIFT)) {
    const uint32_t now = millis();
    if (upper && !caps && now - lastShift < 400) caps = true;  // double tap: caps lock
    else if (caps || upper) upper = caps = false;
    else upper = true;
    lastShift = now;
    return applyMap();
  }
  if (!strcmp(t, "123")) return symbols = true, applyMap();
  if (!strcmp(t, "ABC")) return symbols = false, applyMap();
  if (!strcmp(t, "VI") || !strcmp(t, "EN")) {
    vi = !vi;
    return applyMap();
  }
  if (!strcmp(t, SPACE)) return insertText(" ");
  const char c = t[0];
  if (t[1] == 0 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
    if (telexOn()) typeLetter(c);
    else insertText(t);
    return afterLetter();
  }
  insertText(t);
}

void closePicker() {
  if (picker) lv_obj_delete(picker);
  picker = nullptr;
  pickerOn = false;
}

void markPick() {
  uint32_t n = lv_obj_get_child_count(picker);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t* cell = lv_obj_get_child(picker, i);
    const bool on = (int)i == pickSel;
    lv_obj_set_style_bg_color(cell, on ? lv_color_hex(0xffb547) : lv_color_hex(0x3a4452), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(cell, 0), on ? lv_color_hex(0x1b1408) : lv_color_hex(0xeef1f5), 0);
  }
}

void openPicker(const char* t) {
  pickChars = telex::variants((char32_t)(unsigned char)t[0]);
  if (pickChars.empty()) return;
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  const int n = pickChars.size(), w = n * PICK_W + 8, sw = lv_display_get_horizontal_resolution(nullptr);
  picker = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(picker);
  lv_obj_set_size(picker, w, 48);
  pickX0 = constrain(p.x - PICK_W / 2 - 4, 2, sw - w - 2);
  lv_obj_set_pos(picker, pickX0, lv_obj_get_y(box) + p.y - lv_obj_get_y(box) - 74);
  lv_obj_set_style_bg_color(picker, lv_color_hex(0x3a4452), 0);
  lv_obj_set_style_bg_opa(picker, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(picker, 10, 0);
  lv_obj_set_style_shadow_width(picker, 18, 0);
  lv_obj_set_style_shadow_opa(picker, LV_OPA_60, 0);
  lv_obj_set_style_pad_all(picker, 4, 0);
  lv_obj_set_flex_flow(picker, LV_FLEX_FLOW_ROW);
  for (char32_t ch : pickChars) {
    lv_obj_t* cell = lv_obj_create(picker);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, PICK_W, 40);
    lv_obj_set_style_radius(cell, 6, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_t* l = lv_label_create(cell);
    lv_label_set_text(l, telex::toUtf8(std::u32string(1, ch)).c_str());
    lv_obj_set_style_text_font(l, fonts::get(20), 0);
    lv_obj_center(l);
  }
  pickSel = 0;
  pickerOn = true;
  markPick();
}

void onEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  // After close() the matrix is deleted on the next cycle and still gets events (DELETE...).
  if (!btnm || !target || lv_event_get_current_target(e) != btnm || code == LV_EVENT_DELETE) return;
  if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED)
    return;
  // The field may have been deleted under the keyboard (a list redrawn): close instead of typing.
  if (!lv_obj_is_valid(target)) {
    target = nullptr;
    lv_async_call([](void*) { close(); }, nullptr);
    return;
  }
  const uint32_t id = lv_buttonmatrix_get_selected_button(btnm);
  if (code == LV_EVENT_VALUE_CHANGED) {
    if (pickerOn || id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    handleKey(lv_buttonmatrix_get_button_text(btnm, id));
  } else if (code == LV_EVENT_LONG_PRESSED && !symbols && id != LV_BUTTONMATRIX_BUTTON_NONE) {
    const char* t = lv_buttonmatrix_get_button_text(btnm, id);
    if (t && t[0] && !t[1] && isalpha((unsigned char)t[0])) openPicker(t);
  } else if (code == LV_EVENT_PRESSING && pickerOn) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    const int i = (p.x - pickX0 - 4) / PICK_W;
    const int sel = constrain(i, 0, (int)pickChars.size() - 1);
    if (sel != pickSel) pickSel = sel, markPick();
  } else if (code == LV_EVENT_RELEASED && pickerOn) {
    const char32_t ch = pickChars[pickSel];
    closePicker();
    insertText(telex::toUtf8(std::u32string(1, ch)).c_str());
    afterLetter();
  }
}

// Colours per key kind: function keys darker, "Done" in the accent colour.
void onDraw(lv_event_t* e) {
  lv_obj_t* btnm = (lv_obj_t*)lv_event_get_current_target(e);  // the global may be gone already
  lv_draw_task_t* t = lv_event_get_draw_task(e);
  auto* base = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(t);
  if (base->part != LV_PART_ITEMS) return;
  const uint32_t id = base->id1;
  const bool pressed = lv_buttonmatrix_get_selected_button(btnm) == id && lv_obj_has_state(btnm, LV_STATE_PRESSED);
  const bool checked = lv_buttonmatrix_has_button_ctrl(btnm, id, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_draw_fill_dsc_t* fill = lv_draw_task_get_fill_dsc(t);
  lv_draw_label_dsc_t* label = lv_draw_task_get_label_dsc(t);
  if (lv_buttonmatrix_has_button_ctrl(btnm, id, LV_BUTTONMATRIX_CTRL_CUSTOM_2)) {
    if (fill) fill->color = pressed ? lv_color_hex(0xffd08a) : lv_color_hex(0xffb547);
    if (label) label->color = lv_color_hex(0x1b1408);
  } else if (lv_buttonmatrix_has_button_ctrl(btnm, id, LV_BUTTONMATRIX_CTRL_CUSTOM_1)) {
    if (fill) fill->color = pressed ? lv_color_hex(0x3a4452) : checked ? lv_color_hex(0x4a5a70) : lv_color_hex(0x1d222a);
    if (label && id == VI_KEY && telexOn() && !symbols) label->color = lv_color_hex(0xffb547);
  }
}

}  // namespace

void (*roomCb)(int) = nullptr;
void room(int h) {
  if (roomCb) roomCb(h);
}
int keysH() { return landscape() ? 164 : 204; }
int height() { return keysH() + accH; }

lv_obj_t* accessory(lv_obj_t* textarea, int h) {
  if (!box || !btnm || textarea != target) return nullptr;
  if (!acc) {
    acc = lv_obj_create(box);
    lv_obj_remove_style_all(acc);
    lv_obj_remove_flag(acc, LV_OBJ_FLAG_SCROLLABLE);
  }
  if (accH != h) {
    accH = h;
    lv_obj_set_height(box, keysH() + h);
    lv_obj_set_size(btnm, lv_pct(100), keysH() - 8);  // the box's padding: 4 around
    lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(acc, lv_pct(100), h);
    lv_obj_align(acc, LV_ALIGN_TOP_MID, 0, 0);
    room(height());
    lower();
    liftTarget(height());
  }
  return acc;
}

void open(lv_obj_t* textarea, Mode m, void (*onDone)(lv_obj_t*), const char* okLabel) {
  close();
  target = textarea;
  mode = m;
  doneCb = onDone;
  DONE = i18n::trl(okLabel);
  touch::setPrivate(m == Mode::Password);
  vi = m == Mode::Text && !strcmp(i18n::lang(), "vi");
  symbols = false;
  caps = false;
  upper = m == Mode::Text && !lv_textarea_get_text(textarea)[0];  // capital first letter in names
  lv_textarea_set_password_mode(textarea, m == Mode::Password && lv_textarea_get_password_mode(textarea));

  const int h = height();
  box = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, lv_pct(100), h);
  lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x0b0d11), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(box, 4, 0);

  btnm = lv_buttonmatrix_create(box);
  lv_obj_set_size(btnm, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btnm, 0, 0);
  lv_obj_set_style_pad_all(btnm, 0, 0);
  lv_obj_set_style_pad_row(btnm, 5, 0);
  lv_obj_set_style_pad_column(btnm, 3, 0);
  lv_obj_set_style_bg_color(btnm, lv_color_hex(0x2b323d), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(btnm, lv_color_hex(0x46505f), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btnm, lv_color_hex(0xeef1f5), LV_PART_ITEMS);
  lv_obj_set_style_text_font(btnm, fonts::get(landscape() ? 16 : 18), LV_PART_ITEMS);
  lv_obj_set_style_radius(btnm, 7, LV_PART_ITEMS);
  lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(btnm, 0, LV_PART_ITEMS);
  lv_obj_add_flag(btnm, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
  lv_obj_add_event_cb(btnm, onDraw, LV_EVENT_DRAW_TASK_ADDED, nullptr);
  lv_obj_add_event_cb(btnm, onEvent, LV_EVENT_ALL, nullptr);
  applyMap();
  room(h);
  liftTarget(h);
}

void onRoom(void (*cb)(int)) { roomCb = cb; }

void setMode(Mode m, const char* okLabel) {
  if (!btnm) return;
  mode = m;
  DONE = i18n::trl(okLabel);
  touch::setPrivate(m == Mode::Password);
  vi = m == Mode::Text && !strcmp(i18n::lang(), "vi");
  symbols = upper = caps = false;
  applyMap();
}

void close() {
  touch::setPrivate(false);
  lower();
  if (box) room(0);
  closePicker();
  // Deleted on the next cycle: close() is often called from the keyboard's own key event.
  if (box) lv_obj_delete_async(box);
  box = btnm = acc = nullptr;
  accH = 0;
}

bool isOpen() { return box != nullptr; }
lv_obj_t* field() { return box ? target : nullptr; }

void tick() {
  if (box && (!target || !lv_obj_is_valid(target))) {
    target = nullptr;
    close();
    return;
  }
  // The field moved into a rebuilt screen (a JS app's keyed field after a render): lift its new scrolling parent.
  if (box && lifted != scrollParent(target)) {
    if (lifted && lv_obj_is_valid(lifted)) lower();
    liftTarget(height());
  }
}

}  // namespace kb
