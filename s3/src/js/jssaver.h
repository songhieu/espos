#pragma once
// Screen saver apps written in JavaScript (docs/app-slots.md §4, lane 4a): a package of kind "js" whose manifest says
// "provides": ["saver"] can be picked in Settings › Default apps › Screen saver. While the saver shows, the OS runs its
// saver file (manifest "entry": {"saver": "saver.js"}, else saver.js, else main.js) in a QuickJS runtime of its own and
// calls app.onFrame(g, dt) up to app.fps(n) times a second; g draws into one full-screen frame:
//   g.clear(c) g.rect(x, y, w, h, c, {r, a}) g.circle(x, y, r, c, {a}) g.line(x1, y1, x2, y2, c, {w, a})
//   g.gradient(x, y, w, h, c1, c2, {dir: "v"|"h"}) g.text(s, x, y, {size, color, align, bold, a}) g.measure(s, {size, bold})
//   g.w g.h g.cls g.mono; app.onStart(info) app.onStop(); app.time(fmt); settings.get(k); log(text)
// Colours are "#rrggbb" (or "#rgb"); a is the opacity 0..1.
//
// The budget: 512 KB of PSRAM for the script, 200 ms for one call (a longer one is stopped), 1 to 20 frames a second
// (10 when it does not ask), 600 shapes and 4 KB of text a frame (more are left out). A saver that throws, runs out of
// time or memory, or takes over 100 ms for three frames in a row is stopped: the built-in saver shows for the rest of
// that time. Three stops in ten minutes and it is not started again until the saver slot or the app changes (or the
// device restarts), with one notification. A touch leaves the saver as always (the app gets no touches).
// UI loop only.
#include <Arduino.h>
#include <lvgl.h>

namespace pkg {
struct Package;
}

namespace jssaver {

bool runs(const pkg::Package* p);  // a JavaScript package that provides "saver" (never an add-on)

// Starts the app's saver full screen in parent (W x H at 0, 0). false: it cannot now (not installed, no saver file, no
// room for its stack, a throw at start, or stopped too often): the caller shows the built-in saver.
bool start(lv_obj_t* parent, const char* id);
void stop();             // onStop, then everything freed (also when parent's objects are deleted)
bool running();
// The app stopped itself since start() (a throw, its time or memory budget): the built-in saver should show.
bool failed();
String why();            // what stopped it last ("" when nothing)
void unblock();          // chosen again in Settings › Default apps: it may start again after 3 stops

}  // namespace jssaver
