#pragma once
// The boot screen (0.5.7): the EspOS mark, its name and a thin bar, drawn before the backlight comes on and moved
// along by setup()'s steps until Home replaces it. No words: right in every language before any language is loaded.
// Also the "opening" cover of a big app (an icon, its name, a bar) while its engine loads.
#include <lvgl.h>

namespace splash {
// The boot screen, the way up of last time (os.cpp keeps it in NVS), drawn now; the backlight then at the brightness
// of last time.
void show();
// The bar to pct (0..100), drawn now. Nothing once another screen is in front.
void step(int pct);

// A full-screen cover over whatever is shown (the top layer): an installed package's icon, its name, a bar. Drawn at
// once, before the caller does its slow work on this thread. coverStep() moves the bar (drawn now); coverDone() takes
// it away once the app's own screen has been drawn.
void cover(const char* pkgId, const char* name);
void coverStep(int pct);
void coverDone();
}  // namespace splash
