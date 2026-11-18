#pragma once
// A living picture of Biscuit inside any LVGL parent: the pack's clips at 10 pictures a second on a plain
// background, with her blob shadow, the soft edge and the page's blend between clips, at full or half size.
// The summon overlay, the home widget and her book use it; the coming dog assistant will use it the same way
// (play a clip, speak a line with pet::voice, show the words in a bubble next to it).
#include <lvgl.h>
#include <stdint.h>

namespace pet {

class View {
 public:
  // w x h px on `bg`. The stage (320x292 px, scaled by 1/2^shift) is placed so that its point (ox, oy), in
  // scaled px, is at the view's top-left corner. Deleted with its LVGL object.
  static View* create(lv_obj_t* parent, int w, int h, int shift, uint32_t bg, int ox, int oy);
  lv_obj_t* obj() const { return img_; }

  // Plays a clip from its first picture (blending from the picture shown). A looping clip loops `loops`
  // times (0 = until something else plays); then, or when a clip that does not loop ends, onEnd runs.
  void play(const char* clip, int loops = 0);
  void still(const char* clip, int frame);  // one picture, no clock
  void onEnd(void (*cb)(View*, void*), void* ud) { endCb_ = cb, endUd_ = ud; }
  const char* clip() const;
  bool playing() const { return timer_ != nullptr; }

 private:
  View() = default;
  void step();
  void drawPicture(bool fade);
  static void onTimer(lv_timer_t* t);
  static void onDelete(lv_event_t* e);

  lv_obj_t* img_ = nullptr;
  lv_image_dsc_t dsc_ = {};
  uint16_t *buf_ = nullptr, *prev_ = nullptr;
  int w_ = 0, h_ = 0, shift_ = 0, ox_ = 0, oy_ = 0;
  uint16_t bg_ = 0xFFFF;
  const void* clip_ = nullptr;  // pbs2_clip_t
  int i_ = 0, loops_ = 0, loopsLeft_ = 0, fade_ = 0;
  lv_timer_t* timer_ = nullptr;
  uint32_t last_ = 0;
  float acc_ = 0;
  void (*endCb_)(View*, void*) = nullptr;
  void* endUd_ = nullptr;
};

}  // namespace pet
