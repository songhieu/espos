#pragma once
// net.request for JavaScript apps (app-platform-plan.md lane 1f, manifest-driven.md §1.4): HTTPS to the hosts an app
// declares (`net:<host>`, `net:*`, `net:setting:<key>`, `net:lan`), through the OS's one network queue (desk.cpp's
// worker on the board), with the app's device secrets added by the OS to the URLs their `send_to` names. The app never
// sees a secret, the device token or another app's data. `net:espos` + `auth: "espos"`: the OS adds the device's credential
// to the calling app's own path {espos}/v1/device/app/<its id>/* only (jsnet.cpp ownPath; tested by apps/sessions/test authtest). Docs: site guides/js/network.mdx.
#include <Arduino.h>

extern "C" {
#include "quickjs.h"
}

namespace pkg {
struct Package;
}

namespace jsnet {
// The app opened: its natives (__net, __netOnline) and what it may reach (perms, the manifest's secret fields).
void install(JSContext* ctx, const pkg::Package& p);
// The app is closing (before its context is freed): pending requests are dropped; one already on the network finishes
// there and its answer is thrown away. The OS's own requests (osPost) stay.
void stop();
// The OS's own request for an installed app (src/popnote.cpp: a pop-up's button while the app is closed): POST `json`
// to the app's own server path, {espos}/v1/device/app/<app>/<path>, with the device's credential (or the app's binding
// whose secret is set: the later app-scoped token); the app needs net:espos. `done` runs on the UI thread with the HTTP status (0: none), an error code (nullptr
// with a status) and the body. false: refused at once (no such app or no net:espos, a bad path, no network, no secret).
using OsDone = void (*)(void* ctx, int status, const char* err, const String& body);
bool osPost(const char* app, const char* path, const String& json, OsDone done, void* ctx);
}  // namespace jsnet
