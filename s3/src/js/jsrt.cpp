#pragma GCC optimize("Os")  // widgets/JS glue built once per change: size over speed (0.5.7 flash diet)
#include "jsrt.h"

extern "C" {
#include "quickjs.h"
}

namespace jsrt {
namespace {

// QuickJS's memory in PSRAM: scripts must never take internal RAM from WiFi and TLS.
void* jsMalloc(void*, size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
void jsFree(void*, void* p) { heap_caps_free(p); }
void* jsRealloc(void*, void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
void* jsCalloc(void*, size_t c, size_t n) { return heap_caps_calloc(c, n, MALLOC_CAP_SPIRAM); }
size_t jsUsable(const void*) { return 0; }

JSRuntime* rt = nullptr;

JSRuntime* runtime() {
  if (rt) return rt;
  static const JSMallocFunctions mf = {jsCalloc, jsMalloc, jsFree, jsRealloc, jsUsable};
  rt = JS_NewRuntime2(&mf, nullptr);
  JS_SetMemoryLimit(rt, 2 * 1024 * 1024);
  JS_SetMaxStackSize(rt, 48 * 1024);
  return rt;
}

}  // namespace

String eval(const char* code) {
  JSContext* ctx = JS_NewContext(runtime());
  JSValue v = JS_Eval(ctx, code, strlen(code), "<console>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(v)) {
    JS_FreeValue(ctx, v);
    v = JS_GetException(ctx);
  }
  const char* s = JS_ToCString(ctx, v);
  String out = s ? s : "(null)";
  JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, v);
  JS_FreeContext(ctx);
  return out;
}

}  // namespace jsrt
