#pragma once
#include <lvgl.h>

#include "i18n.h"

// On-screen keyboard: QWERTY with key pop-ups, Vietnamese Telex (VI/EN key), long-press accent
// picker, numbers/symbols page, backspace auto-repeat. One keyboard at a time.
namespace kb {
// Text: Telex on, capital first letter. Plain: Telex off (VI key turns it on), no capitals,
// for names like a WiFi network. Password: no Telex, no capitals, wide OK key.
enum class Mode { Text, Plain, Password };
// okLabel: the OK key's text (a literal is translated, i18n.h: mark it N_).
void open(lv_obj_t* textarea, Mode mode, void (*onDone)(lv_obj_t* textarea), const char* okLabel = N_("Done"));
void setMode(Mode mode, const char* okLabel);  // change the layout in place (safe inside onDone)
void close();
bool isOpen();
lv_obj_t* field();  // the field being typed into, nullptr when closed
void tick();  // closes the keyboard when its field went away (UI loop, outside drawing)
int height();  // keyboard height in the current orientation (with its key row, when it has one)
// A row of the app's own keys on top of the keyboard (ui.input's "keys": Esc, arrows... for a terminal), h px tall:
// the container to build into while `textarea` has the keyboard, else nullptr. The keyboard grows by h and the field
// is lifted above both. It goes with the keyboard.
lv_obj_t* accessory(lv_obj_t* textarea, int h);
// Told the keyboard's height as it opens or grows (h) and closes (0), before the field is lifted: a full-screen JS app
// makes its body that much shorter, so a layout that fills the screen sits above the keys (jsapp.cpp).
void onRoom(void (*cb)(int h));
}  // namespace kb
