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
void tick();  // closes the keyboard when its field went away (UI loop, outside drawing)
int height();  // keyboard height in the current orientation
}  // namespace kb
