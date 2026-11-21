#include "uikit.h"

#include "art.h"
#include "fonts.h"
#include "i18n.h"
#include "trace.h"

extern "C" uint32_t lv_text_get_next_line(const char* txt, const lv_font_t* font, int32_t letter_space,
                                          int32_t max_width, int32_t* used_width, lv_text_flag_t flag);

namespace kit {

namespace {
const Pal LIGHT = {0xf4f4f7, 0xffffff, 0xeceef3, 0x16181d, 0x6b7280, 0x8a8f99, 0xe4e6ec, 0xe4e6ec,
                   0x3a3f4a, 0x16181d, 0xffffff, 0x1360d6, 0xe6e8ee, false};
const Pal DARK = {0x0d1018, 0x171c26, 0x1f2531, 0xeef1f5, 0x9aa3b2, 0x6b7380, 0x252c38, 0x1f2430,
                  0xc9d0d8, 0xffffff, 0x0d1018, 0x5aa9ff, 0x232a36, true};
const Pal* current = &LIGHT;
}  // namespace

const Pal& pal() { return *current; }
void setDark(bool dark) { current = dark ? &DARK : &LIGHT; }

// Text handed to these helpers is translated when it lies in the firmware image (i18n.h: N_ marks).
void setText(lv_obj_t* l, const char* text) {
  text = i18n::trl(text);
  if (l && strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, int px, bool bold, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);  // no per-frame line breaking; use \n
  lv_label_set_text(l, i18n::trl(text));
  lv_obj_set_style_text_font(l, fonts::get(px, bold), 0);
  lv_obj_set_style_text_color(l, C(color), 0);
  return l;
}

lv_obj_t* keep(lv_obj_t** ref, lv_obj_t* obj) {
  *ref = obj;
  lv_obj_add_event_cb(obj, [](lv_event_t* e) {
    lv_obj_t** r = (lv_obj_t**)lv_event_get_user_data(e);
    if (*r == lv_event_get_current_target(e)) *r = nullptr;
  }, LV_EVENT_DELETE, ref);
  return obj;
}

lv_obj_t* box(lv_obj_t* parent, uint32_t bg, int radius) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_bg_color(o, C(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

lv_obj_t* clear(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

int lineH(int px, bool bold) { return fonts::get(px, bold)->line_height; }

int lines(const char* s) {
  int n = 1;
  for (; *s; s++) n += *s == '\n';
  return n;
}

int textW(const char* s, int px, bool bold) {
  s = i18n::trl(s);
  return lv_text_get_width(s, strlen(s), fonts::get(px, bold), 0);
}

// One label per column (labels are the costly part of a frame); both use the same row pitch so
// rows stay level. Vietnamese fonts carry a tall line box for stacked marks, so the pitch may
// go below it.
void column(lv_obj_t* c, int y, int height, const char* left, const char* right, uint32_t rightColor, int px,
            bool leftBold, int leftW, uint32_t leftColor) {
  left = i18n::trl(left), right = i18n::trl(right);
  const int n = lines(left), la = lineH(px, leftBold), lb = lineH(px, true);
  const int lh = max(la, lb);
  const int pitch = n > 1 ? min(lh + 6, (height - lh) / (n - 1)) : lh;
  lv_obj_t* a = label(c, left, px, leftBold, leftBold ? rightColor : leftColor);
  lv_obj_set_style_text_line_space(a, pitch - la, 0);
  lv_obj_set_pos(a, 0, y);
  lv_obj_t* b = label(c, right, px, !leftBold, leftBold ? leftColor : rightColor);
  lv_obj_set_style_text_line_space(b, pitch - (leftBold ? lineH(px, false) : lb), 0);
  if (leftW >= 0) {
    lv_obj_set_pos(b, leftW, y);
  } else {
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, 0, y);
  }
}

namespace {
// Breaks `text` into lines of at most w px with font f; returns the line count.
int breakLines(const char* text, const lv_font_t* f, int w, String& out, int letterSpace = 0) {
  out = "";
  int n = 0;
  for (const char* p = text; *p;) {
    int32_t used = 0;
    uint32_t len = lv_text_get_next_line(p, f, letterSpace, w, &used, LV_TEXT_FLAG_NONE);
    if (len == 0) break;
    uint32_t keepLen = len;
    while (keepLen && (p[keepLen - 1] == ' ' || p[keepLen - 1] == '\n')) keepLen--;
    if (n++) out += '\n';
    out.concat(p, keepLen);
    p += len;
  }
  return n;
}
}  // namespace

// Never cut, never "…". Lines are broken here once, so the label never re-wraps while a page
// slides. Returns the size used; logs when even minPx does not fit.
int fitText(lv_obj_t* l, const char* text, int w, int h, int maxPx, int minPx, bool bold, int lineSpace) {
  text = i18n::trl(text);
  static const int SIZES[] = {24, 22, 20, 18, 16, 15, 14, 13, 12};  // text fonts (bigger ones are digits only)
  String out;
  int px = minPx, letter = 0;
  bool fits = false;
  for (int s : SIZES) {
    if (s > maxPx || s < minPx) continue;
    if (!bold && (s == 22 || s == 24)) continue;  // regular stops at 20
    px = s;
    const lv_font_t* f = fonts::text(s, bold);
    const int n = breakLines(text, f, w, out);
    // 2 px of slack: the line box holds room for stacked Vietnamese marks most text never uses.
    if ((fits = n * f->line_height + (n - 1) * lineSpace <= h + 2)) break;
  }
  if (!fits) {
    // The smallest size is still too long (German, Japanese app names): letters 1 px closer first.
    const lv_font_t* f = fonts::text(px, bold);
    String o2;
    const int n = breakLines(text, f, w, o2, -1);
    if (n * f->line_height + (n - 1) * lineSpace <= h + 2) out = o2, letter = -1;
    else trace::add("fitText: \"%s\" needs %d lines at %d px, box %dx%d", text, n, px, w, h);
  }
  if (letter || lv_obj_get_style_text_letter_space(l, 0) == -1) lv_obj_set_style_text_letter_space(l, letter, 0);
  lv_obj_set_style_text_font(l, fonts::text(px, bold), 0);
  lv_obj_set_style_text_line_space(l, lineSpace, 0);
  setText(l, out.c_str());
  return px;
}

int wrapText(lv_obj_t* l, const char* text, int w, int px, bool bold, int lineSpace, int maxLines) {
  text = i18n::trl(text);
  String out;
  const lv_font_t* f = fonts::text(px, bold);
  int n = breakLines(text, f, w, out);
  if (maxLines > 0 && n > maxLines) {
    // Too long for the box: a smaller size first (never cut the text).
    for (int s = px - 1; s >= 12 && n > maxLines; s--) {
      f = fonts::text(s, bold);
      n = breakLines(text, f, w, out);
    }
  }
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_line_space(l, lineSpace, 0);
  setText(l, out.c_str());
  return n;
}

String upperVi(const String& in) { return i18n::upper(in); }

lv_obj_t* spinner(lv_obj_t* parent, int size, uint32_t track, uint32_t arc) {
  lv_obj_t* sp = lv_spinner_create(parent);
  lv_obj_set_size(sp, size, size);
  lv_obj_set_style_arc_width(sp, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(sp, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(sp, C(track), LV_PART_MAIN);
  lv_obj_set_style_arc_color(sp, C(arc), LV_PART_INDICATOR);
  return sp;
}

lv_obj_t* img(lv_obj_t* parent, const void* src) {
  lv_obj_t* i = lv_image_create(parent);
  lv_image_set_src(i, src);
  return i;
}

lv_obj_t* glyph(lv_obj_t* parent, const lv_image_dsc_t* a8, uint32_t color) {
  lv_obj_t* i = img(parent, a8);
  lv_obj_set_style_image_recolor(i, C(color), 0);
  lv_obj_set_style_image_recolor_opa(i, LV_OPA_COVER, 0);
  return i;
}

void grad(lv_obj_t* o, uint32_t from, uint32_t to, lv_grad_dir_t dir) {
  lv_obj_set_style_bg_color(o, C(from), 0);
  lv_obj_set_style_bg_grad_color(o, C(to), 0);
  lv_obj_set_style_bg_grad_dir(o, dir, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

lv_obj_t* cell(lv_obj_t* parent, int pad) {
  lv_obj_t* c = box(parent, pal().cell, 14);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, pad, 0);
  return c;
}

lv_obj_t* stack(lv_obj_t* parent, bool row, int gap) {
  lv_obj_t* s = clear(parent);
  lv_obj_set_size(s, row ? LV_SIZE_CONTENT : lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s, row ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(s, gap, 0);
  if (row) lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return s;
}

lv_obj_t* listRow(lv_obj_t* parent, const char* name, const char* value, lv_event_cb_t cb, void* data, bool chevron) {
  const Pal& p = pal();
  lv_obj_t* r = box(parent, p.cell, 12);
  // A list of os::TWO in landscape (USER_1 on its body): half the line, two rows side by side.
  const bool half = lv_obj_has_flag(parent, LV_OBJ_FLAG_USER_1);
  if (half) lv_obj_update_layout(parent);
  lv_obj_set_size(r, half ? (lv_obj_get_content_width(parent) - 8) / 2 : lv_pct(100), 46);
  lv_obj_set_style_pad_hor(r, 14, 0);
  lv_obj_t* a = label(r, name, 15, false, p.fg);
  lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* b = label(r, value, 14, false, p.faint);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, chevron && cb ? -14 : 0, 0);
  // A long name and value (a longer language) share the row: the value shrinks first, then the name wraps.
  lv_obj_update_layout(parent);
  const int rw = (half ? (lv_obj_get_content_width(parent) - 8) / 2 : lv_obj_get_content_width(parent)) - 28 -
                 (chevron && cb ? 14 : 0);
  const char* vt = i18n::trl(value);
  const int nw = textW(name, 15), vw = *vt ? textW(vt, 14) : 0;
  if (rw > 60 && nw + vw + 12 > rw) {
    const int vroom = max(rw / 3, rw - nw - 12);
    // One line if it can (14..12 px, then letters 1 px closer, as fitText does); else two lines, which fit the 46 px
    // row (it was a one-line box the text ran out of).
    int n = 0;
    for (const char* q = vt; *q; q++) n += (*q & 0xC0) != 0x80;
    if (vw > vroom) fitText(b, vt, vroom, textW(vt, 12) - (n - 1) > vroom ? 38 : 20, 14, 12);
    const int nroom = rw - min(vw, vroom) - 12;
    if (nw > nroom) fitText(a, name, nroom, 40, 15, 12);
  }
  if (cb) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, C(p.press), LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, data);
    if (chevron) {
      lv_obj_t* ch = glyph(r, &gl_chev_16, p.faint);
      lv_obj_align(ch, LV_ALIGN_RIGHT_MID, 4, 0);
    }
  }
  return r;
}

int twoCols(lv_obj_t* list) {
  lv_obj_update_layout(list);
  const int w = lv_obj_get_content_width(list);
  if (lv_display_get_horizontal_resolution(nullptr) <= lv_display_get_vertical_resolution(nullptr)) return w;
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(list, 8, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_USER_1);
  return (w - 8) / 2;
}

lv_obj_t* caption(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = label(parent, text, 13, true, pal().faint);
  lv_obj_set_style_pad_left(l, 4, 0);
  lv_obj_set_style_pad_top(l, 6, 0);
  if (lv_obj_has_flag(parent, LV_OBJ_FLAG_USER_1)) lv_obj_set_width(l, lv_pct(100));  // a whole line of a TWO list
  return l;
}

lv_obj_t* hint(lv_obj_t* parent, const char* text) {
  lv_obj_t* h = label(parent, "", 13, false, pal().faint);
  lv_obj_update_layout(parent);
  const int w = lv_obj_get_content_width(parent);
  wrapText(h, text, (w > 40 ? w : 296) - 8, 13, false, 3);
  lv_obj_set_style_pad_hor(h, 4, 0);
  if (lv_obj_has_flag(parent, LV_OBJ_FLAG_USER_1)) lv_obj_set_width(h, lv_pct(100));  // a whole line of a TWO list
  return h;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* data) {
  lv_obj_t* b = box(parent, bg, 12);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 40);
  lv_obj_set_style_pad_hor(b, 16, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_t* l = label(b, text, 15, true, fg);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
  // Narrower than its label (a share of a row, a long translation): the label shrinks, then wraps.
  lv_obj_add_event_cb(b, [](lv_event_t* e) {
    lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
    lv_obj_t* l = lv_obj_get_child(b, 0);
    const int w = lv_obj_get_content_width(b);
    const String t = lv_label_get_text(l);
    if (w > 20 && t.indexOf('\n') < 0 && textW(t.c_str(), 15, true) > w) {
      lv_obj_set_style_pad_hor(b, 8, 0);
      fitText(l, t.c_str(), w + 16, 38, 15, 12, true, 0);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_center(l);
    }
  }, LV_EVENT_SIZE_CHANGED, nullptr);
  return b;
}

lv_obj_t* toggle(lv_obj_t* parent, bool on, lv_event_cb_t cb, void* data) {
  lv_obj_t* sw = lv_switch_create(parent);
  lv_obj_set_size(sw, 46, 28);
  lv_obj_set_style_bg_color(sw, C(pal().dark ? 0x3a4250 : 0xd9dbe1), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, C(col::GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, C(0xffffff), LV_PART_KNOB);
  lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, data);
  return sw;
}

namespace {
void paintChips(lv_obj_t* bar, int sel) {
  const Pal& p = pal();
  for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++) {
    lv_obj_t* c = lv_obj_get_child(bar, i);
    const bool on = (int)i == sel;
    lv_obj_set_style_bg_color(c, C(on ? p.chipOn : p.chip), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(c, 0), C(on ? p.chipOnFg : p.chipFg), 0);
  }
}
}  // namespace

lv_obj_t* chips(lv_obj_t* parent, const char* const* names, int count, int selected, void (*cb)(int)) {
  lv_obj_t* bar = clear(parent);
  lv_obj_set_size(bar, lv_pct(100), 30);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(bar, 6, 0);
  lv_obj_add_flag(bar, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(bar, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_user_data(bar, (void*)cb);
  for (int i = 0; i < count; i++) {
    lv_obj_t* c = box(bar, pal().chip, 14);
    lv_obj_set_size(c, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = label(c, names[i], 13, true, pal().chipFg);
    lv_obj_center(l);
    lv_obj_add_event_cb(c, [](lv_event_t* e) {
      lv_obj_t* c = (lv_obj_t*)lv_event_get_current_target(e);
      lv_obj_t* bar = lv_obj_get_parent(c);
      const int idx = lv_obj_get_index(c);
      paintChips(bar, idx);
      auto f = (void (*)(int))lv_obj_get_user_data(bar);
      if (f) f(idx);
    }, LV_EVENT_CLICKED, nullptr);
  }
  paintChips(bar, selected);
  // Tighter chips when they would not all fit (the row still scrolls if even that is too wide).
  lv_obj_update_layout(bar);
  const int room = lv_obj_get_content_width(bar);
  for (int pad : {9, 7}) {
    int total = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++) total += lv_obj_get_width(lv_obj_get_child(bar, i)) + 6;
    if (total - 6 <= room) break;
    for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++) lv_obj_set_style_pad_hor(lv_obj_get_child(bar, i), pad, 0);
    if (pad == 7) lv_obj_set_style_pad_column(bar, 4, 0);
    lv_obj_update_layout(bar);
  }
  return bar;
}

lv_obj_t* badge(lv_obj_t* parent, int n) {
  char b[8];
  snprintf(b, sizeof b, "%d", n > 99 ? 99 : n);
  lv_obj_t* o = box(parent, col::RED, 10);
  lv_obj_set_size(o, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_min_width(o, 20, 0);
  lv_obj_set_style_pad_hor(o, 5, 0);
  lv_obj_t* l = label(o, b, 12, true, 0xffffff);
  lv_obj_center(l);
  return o;
}

void every(lv_obj_t* owner, uint32_t ms, void (*fn)(lv_timer_t*), void* data) {
  lv_timer_t* t = lv_timer_create(fn, ms, data);
  lv_obj_add_event_cb(owner, [](lv_event_t* e) { lv_timer_delete((lv_timer_t*)lv_event_get_user_data(e)); },
                      LV_EVENT_DELETE, t);
}

String hhmm(int h, int m) {
  char b[8];
  snprintf(b, sizeof b, "%02d:%02d", h, m);
  return b;
}

String ago(int64_t thenMs, int64_t nowMs) {
  if (thenMs <= 0 || nowMs <= 0) return "";
  const int64_t m = (nowMs - thenMs) / 60000;
  if (m < 1) return T_("just now");
  if (m < 60) return i18n::plural((int)m, NP_("{n} min ago|{n} min ago"));
  if (m < 48 * 60) return i18n::plural((int)(m / 60), NP_("{n} hour ago|{n} hours ago"));
  return i18n::plural((int)(m / 1440), NP_("{n} day ago|{n} days ago"));
}

lv_obj_t* qr(lv_obj_t* parent, const char* text, int size) {
  lv_obj_t* b = box(parent, 0xffffff, size / 10);
  lv_obj_set_size(b, size, size);
  lv_obj_t* q = lv_qrcode_create(b);
  lv_qrcode_set_size(q, size - 2 * max(6, size / 12));
  lv_qrcode_set_dark_color(q, lv_color_black());
  lv_qrcode_set_light_color(q, lv_color_white());
  lv_qrcode_update(q, text, strlen(text));
  lv_obj_center(q);
  return b;
}

}  // namespace kit
