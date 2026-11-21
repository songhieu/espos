#pragma once
// Biscuit's book (book.cpp): her level, needs and streak, the album of moments, the game's switches and the
// assistant demo. Opened from the paw badge of the game's header, or apps::open("pet", "book").
#include <lvgl.h>

namespace pet {
namespace book {
void open();
bool isOpen();
// The row of calls that wake the dog ("Gâu gâu", "Ki ki"...: several on, at least one), as in the book; the
// first-boot setup's step 5 uses it too (onboard.cpp).
lv_obj_t* callChips(lv_obj_t* parent);
}  // namespace book
}  // namespace pet
