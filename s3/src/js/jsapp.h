#pragma once
// Installed apps written in JavaScript, run by QuickJS: packages of kind "js" (pkg/pkg.h, docs/packages.md)
// whose main.js is the app; installed from the store into the internal flash or onto the memory card.
//
// The app's script describes its screen as a tree of plain objects, rebuilt after each event:
//   app.title = "Tập trung";
//   app.render = () => ui.column([ui.big(text), ui.row([ui.button("Bắt đầu", start)])]);
//   ui.column/row(children, opts) ui.card(children, opts) ui.title/text/big(text, opts)
//   ui.button(text, fn, opts) ui.chips(names, selected, fn) ui.list(items, fn) ui.progress(v, opts)
//   ui.spacer(h); app.refresh(); timer.every/after(ms, fn) -> id, timer.cancel(id);
//   sound.chime/beep/click() (permission "sound"); notify(title, body) (permission "notify");
//   toast(text); store.get(key, def) / store.set(key, value) (JSON values, per app).
// Scripts run with a memory limit (1.5 MB, PSRAM) and a time budget per call; a script that throws
// or runs too long is stopped with a message, the OS keeps running.
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

extern "C" {
#include "quickjs.h"
}

namespace jsapp {

void open(const char* id, const char* arg = nullptr);  // full screen; app.arg = arg (undefined when none)
String current();                      // the JS app open now ("" when none)
bool keepsAwake();                     // the open app has "screen.awake": no saver while it is open (os.cpp)
uint32_t version();                   // bumps when the installed packages change (pkg::version)
void stackSize(size_t bytes, size_t under = 0);  // dev: QuickJS's own stack, C room under its limit

// For the app's natives in other files (jsnet.cpp): calls fn on the app's stack with its time budget, runs the Promise
// jobs that queued, then redraws. A throw becomes the usual "App error" toast. false when no app is open.
bool invoke(JSValueConst fn, int argc, JSValueConst* argv);

// Node types built in other files (jsui.cpp: ui.grid, ui.image, ui.nav..., app-slots.md §15.4): fn builds one node of
// that type ({t: "<type>", ...} from the prelude) into parent, `width` pixels wide.
using NodeFn = void (*)(lv_obj_t* parent, JSValueConst node, int width);
void addNode(const char* type, NodeFn fn);
// Helpers for those builders, while the screen is being built (the open app's context).
namespace node {
JSContext* ctx();
String appId();  // the open app's package id
String str(JSValueConst o, const char* k, const char* def = "");
int num(JSValueConst o, const char* k, int def);
double real(JSValueConst o, const char* k, double def);
bool flag(JSValueConst o, const char* k);
uint32_t color(JSValueConst o, const char* k, uint32_t def);  // "#rrggbb" or def
int handler(JSValueConst o, const char* k);    // o[k] kept for this screen: its index, -1 when not a function
void fire(int h, int arg);                     // calls it (arg < 0: without an argument), then redraws
void fireText(int h, const char* text, bool redraw = true);  // calls it with a string (then redraws)
void build(lv_obj_t* parent, JSValueConst n, int width);     // a child node
void children(lv_obj_t* parent, JSValueConst n, int width);  // n.c
// Keyed nodes ({key}): the object kept from the last render for this type and key (re-parent it), or nullptr;
// keep() remembers a new one. They survive a re-render (a field keeps its text, cursor and keyboard).
lv_obj_t* kept(JSValueConst n);
void keep(JSValueConst n, lv_obj_t* o);
lv_obj_t* layer();  // a layer over the body (ui.sheet), removed at the next render
void redraw();      // the screen built again soon (outside a render: e.g. the keyboard opened on a field)
}  // namespace node

}  // namespace jsapp
