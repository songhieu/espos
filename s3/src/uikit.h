#pragma once
// Shared drawing helpers and colours for every screen of EspOS.
#include <Arduino.h>
#include <lvgl.h>

#include "i18n.h"

namespace kit {

// The keyboard and a few system parts keep these fixed dark colours.
namespace col {
constexpr uint32_t BG = 0x0e1116, CARD = 0x181d25, CARD2 = 0x1f2631, LINE = 0x2a3240, FG = 0xeef1f5, DIM = 0x9aa6b6,
                   ACC = 0xffb547, OK = 0x4cd28a, BAD = 0xff6b6b, BLUE = 0x6cb6ff, RED = 0xff3b30, IOS_BLUE = 0x0a84ff,
                   GREEN = 0x34c759, ORANGE = 0xff9500;
}
inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

// Colours of app screens: light by day, dark at night or with the dark theme (theme.cpp
// switches them before screens are built).
struct Pal {
  uint32_t bg, cell, cell2, fg, dim, faint, line, chip, chipFg, chipOn, chipOnFg, link, press;
  bool dark;
};
const Pal& pal();
void setDark(bool dark);

void setText(lv_obj_t* l, const char* text);  // only when it changed (no redraw otherwise)
lv_obj_t* label(lv_obj_t* parent, const char* text, int px, bool bold = false, uint32_t color = col::FG);
lv_obj_t* box(lv_obj_t* parent, uint32_t bg, int radius);
lv_obj_t* clear(lv_obj_t* parent);  // a transparent container, not scrollable, no padding
// Keep a global pointer to a widget safe: it turns null when the widget is deleted.
lv_obj_t* keep(lv_obj_t** ref, lv_obj_t* obj);

int lineH(int px, bool bold);
int lines(const char* s);
int textW(const char* s, int px, bool bold = false);

// Two text columns with level rows spread over `height` (see uikit.cpp).
void column(lv_obj_t* c, int y, int height, const char* left, const char* right, uint32_t rightColor, int px,
            bool leftBold = false, int leftW = -1, uint32_t leftColor = col::FG);
// Every helper here that takes text translates it when it is a literal (i18n.h, N_ marks).
// Text shown in full: largest size maxPx..minPx whose wrapped lines fit w x h.
int fitText(lv_obj_t* l, const char* text, int w, int h, int maxPx, int minPx, bool bold = false, int lineSpace = 2);
// Wraps `text` at width w with the font for px (lines broken once, never re-wrapped); returns lines.
int wrapText(lv_obj_t* l, const char* text, int w, int px, bool bold = false, int lineSpace = 2, int maxLines = 0);
// Upper case (i18n::upper: ASCII, Latin-1, Vietnamese, Cyrillic...).
String upperVi(const String& s);
// A spinner in the accent colour.
lv_obj_t* spinner(lv_obj_t* parent, int size, uint32_t track = col::LINE, uint32_t arc = col::ACC);

// Pictures.
lv_obj_t* img(lv_obj_t* parent, const void* src);
lv_obj_t* glyph(lv_obj_t* parent, const lv_image_dsc_t* a8, uint32_t color);  // A8 art tinted
void grad(lv_obj_t* o, uint32_t from, uint32_t to, lv_grad_dir_t dir = LV_GRAD_DIR_VER);

// App screen parts (colours from pal()).
lv_obj_t* cell(lv_obj_t* parent, int pad = 12);  // rounded card, full width, height of its content
lv_obj_t* stack(lv_obj_t* parent, bool row, int gap);  // transparent flex box, content-sized
// A settings-style row: name on the left, value and a chevron on the right. `cb` gets `data`.
lv_obj_t* listRow(lv_obj_t* parent, const char* name, const char* value, lv_event_cb_t cb, void* data = nullptr,
                  bool chevron = true);
// A list in landscape: its items flow two a line (and listRow/caption/hint know it, as in an os::TWO body). Returns
// the width one item takes: half the line in landscape, the whole line in portrait.
int twoCols(lv_obj_t* list);
lv_obj_t* caption(lv_obj_t* parent, const char* text);  // small grey heading above a group
lv_obj_t* hint(lv_obj_t* parent, const char* text);     // small grey paragraph
lv_obj_t* button(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* data = nullptr);
lv_obj_t* toggle(lv_obj_t* parent, bool on, lv_event_cb_t cb, void* data = nullptr);
// Pill tabs; tapping one calls cb(index) and moves the highlight. Scrolls sideways when wide.
lv_obj_t* chips(lv_obj_t* parent, const char* const* names, int count, int selected, void (*cb)(int));
// Rounded badge with a number (red) at the top right of `target`'s parent position.
lv_obj_t* badge(lv_obj_t* parent, int n);
// Runs fn every ms while owner exists (the timer dies with the object).
void every(lv_obj_t* owner, uint32_t ms, void (*fn)(lv_timer_t*), void* data = nullptr);
// Deletes `data` (a heap object of type T) with the object.
template <class T>
void own(lv_obj_t* obj, T* data) {
  lv_obj_add_event_cb(obj, [](lv_event_t* e) { delete (T*)lv_event_get_user_data(e); }, LV_EVENT_DELETE, data);
}

// A QR code of `text` on a white rounded square of `size` px (the code inside with a quiet zone; dark on white,
// as phones read best). The pairing screen's look, for the lock sheets (lock.h) and the dog's pop-up.
lv_obj_t* qr(lv_obj_t* parent, const char* text, int size);

// Date bits in the current language (i18n.h): WEEKDAY[0] "Sunday", WD_SHORT[1] "Mon".
using i18n::WEEKDAY;
using i18n::WD_SHORT;
String hhmm(int h, int m);
String ago(int64_t thenMs, int64_t nowMs);  // "12 min ago"

}  // namespace kit
