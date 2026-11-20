#pragma GCC optimize("Os")  // drawn once at boot: built small (the firmware must fit 3.75 MB)
#include "splash.h"

#include <Preferences.h>

#include "apps.h"
#include "brand.h"
#include "lcd.h"
#include "trace.h"
#include "uikit.h"

namespace splash {
namespace {

constexpr uint32_t ORANGE = 0xff7a00;  // the boot bar (espos.dev's accent)
constexpr int BAR_W = 120;

lv_obj_t *scr = nullptr, *fill = nullptr;
int shownPct = 0;
lv_obj_t *cov = nullptr, *covFill = nullptr;

lv_obj_t* rect(lv_obj_t* parent, int x, int y, int w, int h, int r, uint32_t color) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

// A thin bar: the track and its fill (plain boxes: no animation, nothing redrawn but the fill).
lv_obj_t* bar(lv_obj_t* parent, int x, int y, uint32_t track, uint32_t color, lv_obj_t** fillOut) {
  lv_obj_t* t = rect(parent, x, y, BAR_W, 4, 2, track);
  *fillOut = rect(t, 0, 0, 0, 4, 2, color);
  return t;
}

}  // namespace

void show() {
  Preferences p;
  p.begin("desk", true);
  const int rot = p.getInt("rot", 0) & 3, bl = p.getInt("bl", 70);
  p.end();
  static const lv_display_rotation_t ROT[] = {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180,
                                              LV_DISPLAY_ROTATION_270};
  if (rot) lcd::setRotation(ROT[rot]);
  const int W = lv_display_get_horizontal_resolution(nullptr), H = lv_display_get_vertical_resolution(nullptr);
  scr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, [](lv_event_t*) { scr = fill = nullptr; }, LV_EVENT_DELETE, nullptr);
  const bool land = W > H;
  // The EspOS lockup (src/brand.h: the chip mark over the wordmark, white on black; the same pictures at 2/3 on the
  // 2.8" board), centred a little above the middle; the bar under it.
  const int top = (H - (96 + 16 + 44)) / 2 - (land ? 10 : 24);
  lv_obj_align(brand::mark(scr, 96, 0xffffff), LV_ALIGN_TOP_MID, 0, top);
  lv_obj_align(brand::word(scr, 0xffffff), LV_ALIGN_TOP_MID, 0, top + 96 + 16);
  bar(scr, (W - BAR_W) / 2, land ? H - 52 : top + 96 + 16 + 44 + 56, 0x2a2d33, ORANGE, &fill);
  shownPct = 0;
  lv_screen_load(scr);
  lv_refr_now(nullptr);
  lcd::setBacklight(constrain(bl, 5, 100));  // only now: the panel's memory held whatever it had before
  trace::add("splash shown");
}

void step(int pct) {
  if (!scr || !fill || lv_screen_active() != scr) return;
  pct = constrain(pct, shownPct, 100);  // never backwards
  if (pct == shownPct) return;
  shownPct = pct;
  lv_obj_set_width(fill, BAR_W * pct / 100);
  lv_refr_now(nullptr);
}

void cover(const char* pkgId, const char* name) {
  coverDone();
  const kit::Pal& p = kit::pal();
  const int W = lv_display_get_horizontal_resolution(nullptr), H = lv_display_get_vertical_resolution(nullptr);
  cov = rect(lv_layer_top(), 0, 0, W, H, 0, p.bg);
  lv_obj_add_flag(cov, LV_OBJ_FLAG_CLICKABLE);  // a finger meanwhile lands here, not on Home under it
  lv_obj_add_event_cb(cov, [](lv_event_t*) { cov = covFill = nullptr; }, LV_EVENT_DELETE, nullptr);
  constexpr int S = 64;
  const int top = H / 2 - (W > H ? 66 : 80);
  lv_obj_t* ic = apps::packageIcon(cov, pkgId, S);
  lv_obj_set_pos(ic, (W - S) / 2, top);
  lv_obj_t* l = kit::label(cov, "", 17, true, p.fg);
  kit::fitText(l, name, W - 48, 44, 17, 13, true);  // the whole name: smaller or on two lines, never cut
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, top + S + 12);
  bar(cov, (W - BAR_W) / 2, top + S + 12 + 48, p.line, kit::col::IOS_BLUE, &covFill);
  lv_obj_set_width(covFill, BAR_W / 5);
  lv_refr_now(nullptr);
}

void coverStep(int pct) {
  if (!cov || !covFill) return;
  lv_obj_set_width(covFill, BAR_W * constrain(pct, 0, 100) / 100);
  lv_refr_now(nullptr);
}

void coverDone() {
  if (cov) lv_obj_delete(cov);  // the app's own screen is drawn at the next frame, the cover stays until then
  cov = covFill = nullptr;
}

}  // namespace splash
