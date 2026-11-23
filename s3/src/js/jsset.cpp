#pragma GCC optimize("Os")  // cold code: built small
// settings for JavaScript apps (jsset.h). The prelude (jsapp.cpp) has:
//   var settings = {get: (k) => __setting(String(k)), open: () => __settingsOpen()};
// app.onSettings is the app's own function; the watcher calls it through jsapp::invoke (the app's stack and budget).
#include "jsset.h"

#include <Arduino.h>
#include <lvgl.h>

#include <map>

#include "appset.h"
#include "desk.h"
#include "jsapp.h"
#include "pkg/pkg.h"
#include "secret.h"

namespace jsset {
namespace {

String appId;
JSContext* jc = nullptr;
lv_timer_t* watch = nullptr;
uint32_t setVer = 0, secVer = 0;
std::map<String, String> seen;  // key -> its JS value as JSON text, as the app last heard it

JSValue nSetting(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const char* k = argc ? JS_ToCString(ctx, argv[0]) : nullptr;
  const String json = appset::jsValue(appId.c_str(), k ? k : "");
  JS_FreeCString(ctx, k);
  return JS_ParseJSON(ctx, json.c_str(), json.length(), "<settings>");
}

JSValue nOpen(JSContext*, JSValueConst, int, JSValueConst*) {
  const String id = appId;
  lv_async_call([](void* p) {  // after the script returns: the form slides in over the app
    String* s = (String*)p;
    if (*s == appId) appset::openForm(s->c_str());
    delete s;
  }, new String(id));
  return JS_UNDEFINED;
}

void snapshot(std::vector<String>* changed) {
  for (const String& k : appset::keys(appId.c_str())) {
    const String v = appset::jsValue(appId.c_str(), k.c_str());
    auto it = seen.find(k);
    if (it == seen.end()) seen[k] = v;
    else if (it->second != v) it->second = v, changed ? changed->push_back(k) : void();
  }
}

// Every 400 ms while the app runs: a change of settings (here or from a sync) or of its secrets -> app.onSettings.
void tick(lv_timer_t*) {
  if (!jc || (desk::settingsVersion() == setVer && secret::version() == secVer)) return;
  setVer = desk::settingsVersion(), secVer = secret::version();
  std::vector<String> changed;
  snapshot(&changed);
  if (changed.empty()) return;
  JSValue g = JS_GetGlobalObject(jc), app = JS_GetPropertyStr(jc, g, "app");
  JSValue fn = JS_GetPropertyStr(jc, app, "onSettings");
  if (JS_IsFunction(jc, fn)) {
    JSValue a = JS_NewArray(jc);
    for (size_t i = 0; i < changed.size(); i++) JS_SetPropertyUint32(jc, a, i, JS_NewString(jc, changed[i].c_str()));
    jsapp::invoke(fn, 1, &a);  // also redraws
    JS_FreeValue(jc, a);
  } else {
    jsapp::invoke(JS_UNDEFINED, 0, nullptr);  // no handler: the screen is drawn again with the new values
  }
  JS_FreeValue(jc, fn);
  JS_FreeValue(jc, app);
  JS_FreeValue(jc, g);
}

}  // namespace

void install(JSContext* ctx, const pkg::Package& p) {
  stop();
  appId = p.id;
  jc = ctx;
  JSValue g = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, g, "__setting", JS_NewCFunction(ctx, nSetting, "__setting", 1));
  JS_SetPropertyStr(ctx, g, "__settingsOpen", JS_NewCFunction(ctx, nOpen, "__settingsOpen", 0));
  JS_FreeValue(ctx, g);
  setVer = desk::settingsVersion(), secVer = secret::version();
  snapshot(nullptr);
  watch = lv_timer_create(tick, 400, nullptr);
}

void stop() {
  if (watch) lv_timer_delete(watch);
  watch = nullptr;
  jc = nullptr;
  seen.clear();
}

}  // namespace jsset
