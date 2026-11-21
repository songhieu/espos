#include "view.h"
#include "voice.h"

#include <Arduino.h>

#include "dog.h"
#include "os.h"
#include "packs.h"
#include "talk.h"

namespace pet {

View* View::create(lv_obj_t* parent, int w, int h, int shift, uint32_t bg, int ox, int oy) {
  View* v = new View();
  v->w_ = w, v->h_ = h, v->shift_ = shift, v->ox_ = ox, v->oy_ = oy;
  v->bg_ = lv_color_to_u16(lv_color_hex(bg));
  v->buf_ = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  if (v->buf_)
    for (int i = 0; i < w * h; i++) v->buf_[i] = v->bg_;
  v->dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
  v->dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
  v->dsc_.header.w = w;
  v->dsc_.header.h = h;
  v->dsc_.header.stride = w * 2;
  v->dsc_.data_size = w * h * 2;
  v->dsc_.data = (const uint8_t*)v->buf_;
  v->img_ = lv_image_create(parent);
  if (v->buf_) lv_image_set_src(v->img_, &v->dsc_);
  lv_obj_set_size(v->img_, w, h);
  lv_obj_add_event_cb(v->img_, onDelete, LV_EVENT_DELETE, v);
  return v;
}

void View::onDelete(lv_event_t* e) {
  View* v = (View*)lv_event_get_user_data(e);
  if (v->timer_) lv_timer_delete(v->timer_);
  lv_image_cache_drop(&v->dsc_);
  heap_caps_free(v->buf_);
  heap_caps_free(v->prev_);
  delete v;
}

const char* View::clip() const { return clip_ ? ((const pbs2_clip_t*)clip_)->name : ""; }

void View::drawPicture(bool fade) {
  const pbs2_clip_t* c = (const pbs2_clip_t*)clip_;
  if (!buf_ || !c) return;
  const pbs2_frame_t* f = pk::frame(c, i_);
  for (int i = 0; i < w_ * h_; i++) buf_[i] = bg_;
  Canvas cv = {buf_, w_, h_, w_, 0, h_};
  dog::shadow(cv, f, -ox_, -oy_, shift_);
  const Pic* p = dog::get(f, shift_);
  if (p) dog::draw(cv, *p, -ox_, -oy_);
  shown_ = p != nullptr;
  if (fade && fade_ > 0 && prev_) {
    const int a = 255 * fade_ / 3;
    for (int i = 0; i < w_ * h_; i++) buf_[i] = dog::blend(buf_[i], prev_[i], a);
  }
  lv_obj_invalidate(img_);
}

void View::still(const char* name, int frame) {
  const pbs2_clip_t* c = pk::clip(name);
  if (!c) return;
  if (timer_) lv_timer_delete(timer_), timer_ = nullptr;
  clip_ = c, i_ = frame % c->count, fade_ = 0;
  drawPicture(false);
}

void View::play(const char* name, int loops) {
  const pbs2_clip_t* c = pk::clip(name);
  if (!c || !buf_) return;
  if (clip_) {  // keep what shows for the blend (the page's snapshot)
    if (!prev_) prev_ = (uint16_t*)heap_caps_malloc(w_ * h_ * 2, MALLOC_CAP_SPIRAM);
    if (prev_) memcpy(prev_, buf_, w_ * h_ * 2), fade_ = 2;
  }
  clip_ = c, i_ = 0, loops_ = loops, loopsLeft_ = loops;
  if (!timer_) {
    timer_ = lv_timer_create(onTimer, 20, this);
    last_ = millis();
    acc_ = 0.1f;  // the first picture at the next run
  }
  dog::want(pk::frame(c, 0), shift_);
}

void View::onTimer(lv_timer_t* t) {
  View* v = (View*)lv_timer_get_user_data(t);
  const uint32_t now = millis();
  const float dt = min(0.25f, (now - v->last_) / 1000.0f);
  v->last_ = now;
  lv_obj_t* s = lv_obj_get_screen(v->img_);
  if (os::saverOn() || (s != lv_screen_active() && s != lv_layer_top())) return;  // not seen: no work
  // 10 pictures a second; 5 while the assistant's TLS connection is being made: each picture is ~55 ms of decoding
  // on core 0, the handshake's core (the board, 2026-11-20: 4.8 s for the handshake beside the dog at 10, 2.4 s alone).
  // In a turn (owner, 2026-11-21: "giảm animation, lag quá"): the mic, the upload and the answer come first (each
  // picture is ~55 ms of decoding on the network's core).
  float every = talk::warming() ? 0.2f : 0.1f;
  if (talk::busy()) {  // 2026-11-21 ("agent vẫn lag quá"): still while it listens and waits, 4 pictures a second as it speaks
    if (!voice::busy()) {
      // ...but never an empty pop-up: a call straight into the question (no "Dạ?") came before the first picture was
      // decoded, and the dog stayed blank until it spoke (owner: "mất hình khi gọi agent", 2026-11-21)
      if (!v->shown_) v->drawPicture(false);
      return;
    }
    every = 0.25f;
  }
  v->acc_ += dt;
  if (v->acc_ < every) return;
  v->acc_ = v->acc_ > 3 * every ? 0 : v->acc_ - every;
  v->step();
}

void View::step() {
  const pbs2_clip_t* c = (const pbs2_clip_t*)clip_;
  drawPicture(true);
  if (fade_ > 0) fade_--;
  if (++i_ < c->count) {
    dog::want(pk::frame(c, i_), shift_);
    return;
  }
  if (c->loop && (loops_ == 0 || --loopsLeft_ > 0)) {
    i_ = 0;
    dog::want(pk::frame(c, 0), shift_);
    return;
  }
  i_ = c->count - 1;  // hold the last picture
  if (timer_) lv_timer_delete(timer_), timer_ = nullptr;
  if (endCb_) endCb_(this, endUd_);  // may play() the next clip
}

}  // namespace pet
