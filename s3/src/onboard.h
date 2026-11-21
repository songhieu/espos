#pragma once
// The first-boot setup (docs/onboarding-plan.md; the screens: mockups/s3-onboarding.html). A unit just
// out of its box asks, one screen at a time: the language (and which way up), the WiFi, the dog (one dog a page, its
// voice), the person's name, the wake word with a test, the city, the activation (the claim code and its QR, or the
// 6-digit link code), three tips, then "All set". Every step but the language and the dog can wait ("Later":
// Settings shows "Finish setting up"); the step in progress is kept in NVS "onb", so a restart goes on there.
// It runs when a claim code waits, on a device that never had settings, after a factory reset, or when the web asks
// (s3.onboard "again"); a device in use that takes this firmware over the air never sees it.
// Also here: a store demo unit (ESPOS:DEMO, recorded lines only, no AI) and Settings › About › "Erase data and set up
// again" (the USB factory reset, held 3 s).
#include <Arduino.h>
#include <lvgl.h>

#include "claim.h"

namespace onboard {

#if !ESPOS_ONBOARD  // off by default until the owner approves it: build with -DESPOS_ONBOARD=1 to have it
inline void early() {}
inline void tick() {}
inline bool running() { return false; }
inline bool back() { return false; }
inline void settingsRow(lv_obj_t*) {}
inline void aboutRows(lv_obj_t*) {}
inline bool usb(const char*) { return false; }
inline int state() { return 0; }
#else
void early();     // desk::begin, before the settings load: a demo unit gets its settings as they were when its demo began
void tick();      // UI loop (os::tick): decides once, opens the step in front, the claim's welcome, the demo's card
bool running();   // the setup is in front (its screen, or a page it opened): the OS keeps only the back gesture
bool back();      // the back gesture: one step back when a setup screen is in front (false: not ours)
void settingsRow(lv_obj_t* body);  // Settings: "Finish setting up" with the steps left (nothing when none)
void aboutRows(lv_obj_t* body);    // Settings › About: "Erase data and set up again" (and "Turn off demo mode")
// USB (devtools.cpp) and the simulator's "onb" lines: ESPOS:ONBOARD reset | step N | done | state,
// ESPOS:DEMO 1 [PIN] | 0 | pin PIN. true when the line was one of these.
bool usb(const char* line);
int state();  // ESPOS:INFO "onb": the setup version done (0 never), 100 + the step while it runs, -1 a demo unit
#endif

}  // namespace onboard
