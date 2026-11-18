#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "standby.h"

#include <ctime>

#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "os.h"
#include "theme.h"
#include "trace.h"
#include "uikit.h"
#include "wid.h"

namespace standby {
namespace {

using namespace kit;

lv_obj_t *root, *leftStack, *rightStack, *bigH, *bigM, *bigCap;
int leftIdx = 0, rightIdx = 0;
// Two cards with room for their dots on the 480 px screen (228 wide put the left dots at x -4).
constexpr int CARD_W = 216, CARD_H = 284, TOP = 28;

void bigClock(lv_obj_t* tile) {
  lv_obj_t* c = box(tile, 0xff7a45, 24);
  grad(c, 0xff8a3d, 0xd9468f);
  lv_obj_set_size(c, CARD_W, CARD_H);
  lv_obj_set_style_pad_all(c, 16, 0);
  const data::Day d = data::today();
  keep(&bigCap, label(c, upperVi(i18n::date(N_("{W}, {M} {d}"), d.d, d.m, d.y, d.wday)).c_str(), 14, true, 0xffe2c2));
  lv_obj_set_style_text_letter_space(bigCap, 1, 0);
  keep(&bigH, label(c, "", 96, false, 0xffffff));
  lv_obj_set_pos(bigH, -4, 22);
  keep(&bigM, label(c, "", 96, false, 0xffffff));
  lv_obj_set_pos(bigM, -4, 112);
  const data::Lunar l = data::lunar(d);
  lv_obj_t* f = label(c, "", 13, false, 0xffe2c2);
  // i18n: a lunar date and the year's stems and branches, no comma (vi "Âm lịch {0} {1}")
  fitText(f, i18n::fmt(N_("Lunar {0} {1}"), data::lunarText(l), data::canChiYear(l.y)).c_str(), CARD_W - 32, 36, 13, 12);
  lv_obj_align(f, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

lv_obj_t* stackView(lv_obj_t* parent, int x, int n, int* idx, bool dotsRight) {
  lv_obj_t* tv = lv_tileview_create(parent);
  lv_obj_set_size(tv, CARD_W, CARD_H);
  lv_obj_set_pos(tv, x, TOP);
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < n; i++) {
    lv_obj_t* t = lv_tileview_add_tile(tv, 0, i, (lv_dir_t)((i > 0 ? LV_DIR_TOP : 0) | (i < n - 1 ? LV_DIR_BOTTOM : 0)));
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);  // sideways swipes go on to the page pager
  }
  // Dots beside the stack.
  lv_obj_t* dots = clear(parent);
  lv_obj_set_size(dots, 6, n * 12);
  lv_obj_set_pos(dots, dotsRight ? x + CARD_W + 4 : x - 10, TOP + (CARD_H - n * 12) / 2);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(dots, 6, 0);
  for (int i = 0; i < n; i++) {
    lv_obj_t* d = box(dots, 0xffffff, 3);
    lv_obj_set_size(d, 6, i == *idx ? 14 : 6);
    lv_obj_set_style_bg_opa(d, i == *idx ? LV_OPA_COVER : 100, 0);
  }
  lv_obj_set_user_data(tv, dots);
  lv_obj_add_event_cb(tv, [](lv_event_t* e) {
    lv_obj_t* tv = (lv_obj_t*)lv_event_get_current_target(e);
    int* idx = (int*)lv_event_get_user_data(e);
    *idx = lv_obj_get_index(lv_tileview_get_tile_active(tv));
    lv_obj_t* dots = (lv_obj_t*)lv_obj_get_user_data(tv);
    for (uint32_t i = 0; i < lv_obj_get_child_count(dots); i++) {
      lv_obj_t* d = lv_obj_get_child(dots, i);
      lv_obj_set_height(d, (int)i == *idx ? 14 : 6);
      lv_obj_set_style_bg_opa(d, (int)i == *idx ? LV_OPA_COVER : 100, 0);
    }
  }, LV_EVENT_VALUE_CHANGED, idx);
  return tv;
}

lv_obj_t* tileAt(lv_obj_t* tv, int i) { return lv_obj_get_child(tv, i); }

void widgetIn(lv_obj_t* tile, const char* kind, wid::Size s, const char* style = "") {
  wid::Item it;
  it.kind = kind;
  it.size = s;
  it.style = style;
  wid::create(tile, it, CARD_W, CARD_H, true);
}

void buildStandby(lv_obj_t* page) {
  const int x0 = (os::W() - 2 * CARD_W - 12) / 2;
  leftStack = stackView(page, x0, 3, &leftIdx, false);
  bigClock(tileAt(leftStack, 0));
  widgetIn(tileAt(leftStack, 1), "clock", wid::L, "analog");
  widgetIn(tileAt(leftStack, 2), "lunar", wid::S);
  rightStack = stackView(page, x0 + CARD_W + 12, 4, &rightIdx, true);
  widgetIn(tileAt(rightStack, 0), "weather", wid::L);
  widgetIn(tileAt(rightStack, 1), "cal", wid::L);
  widgetIn(tileAt(rightStack, 2), "markets", wid::L);
  widgetIn(tileAt(rightStack, 3), "news", wid::L);
  lv_tileview_set_tile_by_index(leftStack, 0, leftIdx, LV_ANIM_OFF);
  lv_tileview_set_tile_by_index(rightStack, 0, rightIdx, LV_ANIM_OFF);
}

}  // namespace

void build(lv_obj_t* parent) {
  root = parent;
  bigH = bigM = bigCap = nullptr;
  buildStandby(parent);
}

void reset() { root = leftStack = rightStack = bigH = bigM = bigCap = nullptr; }

void tick() {
  if (!bigH) return;
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  char h[4], m[4];
  snprintf(h, sizeof h, "%02d", l.tm_hour);
  snprintf(m, sizeof m, "%02d", l.tm_min);
  setText(bigH, h);
  setText(bigM, m);
}

}  // namespace standby
