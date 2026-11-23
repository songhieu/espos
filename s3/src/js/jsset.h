#pragma once
// settings.get / settings.open / app.onSettings for JavaScript apps (app-platform-plan.md lane 1e,
// manifest-driven.md §1.2): the app's own "app.<id>.*" values as typed JS values (a secret reads as {set}), its form
// opened over it, and a call to app.onSettings(changedKeys) when values change on the device or arrive from the web.
extern "C" {
#include "quickjs.h"
}

namespace pkg {
struct Package;
}

namespace jsset {
void install(JSContext* ctx, const pkg::Package& p);  // the app opened: its natives and the watcher
void stop();                                         // the app is closing
}  // namespace jsset
