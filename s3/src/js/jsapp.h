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

#include <vector>

namespace jsapp {

void open(const char* id, const char* arg = nullptr);  // full screen; app.arg = arg (undefined when none)
uint32_t version();                   // bumps when the installed packages change (pkg::version)
void stackSize(size_t bytes, size_t under = 0);  // dev: QuickJS's own stack, C room under its limit

}  // namespace jsapp
