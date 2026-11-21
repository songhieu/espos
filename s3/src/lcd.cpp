// lcd.h on every board: the board's panel driver (hal::Display: src/boards/<name>/display.cpp), the one found at boot
// (hal::detect). The simulator has its own (sim/stubs.cpp, sim/main.cpp).
#include "lcd.h"

#include "hal/board.h"

namespace lcd {
namespace {
inline const hal::Display& drv() { return *hal::bsp().display; }
bool flip_ = false;
}  // namespace

bool flipped() { return flip_; }
void setFlipped(bool on) { flip_ = on; }

lv_display_t* begin() { return drv().begin(); }
void setRotation(lv_display_rotation_t r) { drv().setRotation(r); }
void setBacklight(int percent) { drv().setBacklight(percent); }
void stats(uint32_t& frames, uint32_t& avgFlushUs) { drv().stats(frames, avgFlushUs); }
void logInvalid(bool on) { drv().logInvalid(on); }
bool direct(int y0, int y1, void (*fill)(uint16_t*, int, int, void*), void* ctx, bool tear) {
  return drv().direct(y0, y1, fill, ctx, tear);
}
void mirror(bool on) { drv().mirror(on); }
const uint16_t* frame(int& w, int& h) { return drv().frame(w, h); }
bool native(NativeFill fill, void* ctx, bool tear) { return drv().native && drv().native(fill, ctx, tear); }

}  // namespace lcd
