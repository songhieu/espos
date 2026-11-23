#pragma GCC optimize("Os")  // the frame path is LVGL's drawing; the glue is built small (the app slot is nearly full)
// JavaScript screen saver apps (jssaver.h, docs/app-slots.md §4). A runtime of its own, made when the saver starts the
// app and freed when it stops: nothing of it stays between two savers. The script records shapes into a list (one per
// frame); a full-screen object draws the last whole list in its own draw event, so a frame that draws nothing keeps
// the one before on the screen.
#include "jssaver.h"

#include <ArduinoJson.h>

#include <chrono>
#include <functional>
#include <map>
#include <vector>

extern "C" {
#include "quickjs.h"
}

#include "appset.h"
#include "assistant.h"
#include "fonts.h"
#include "i18n.h"
#include "notif.h"
#include "os.h"
#include "pkg/pkg.h"
#include "trace.h"

#ifndef SIM
#include "esp_expression_with_stack.h"
#endif

namespace jssaver {
namespace {

constexpr size_t MEM = 512 * 1024;   // the script's heap (PSRAM)
constexpr uint32_t CALL_MS = 200;    // one call (onFrame, onStart...): a longer one is stopped
constexpr uint32_t START_MS = 1500;  // the script's first run
constexpr uint32_t SLOW_MS = 100;    // three frames slower than this in a row: stopped
constexpr int MAX_OPS = 600, MAX_TEXT = 4096, MAX_FILE = 128 * 1024;

// ---------------------------------------------------------------- the frame
enum : uint8_t { CLEAR, RECT, LINE, GRAD, TEXT };
struct Op {
  uint8_t k, opa, bold;
  int16_t x, y, w, h;  // LINE: from x, y to w, h
  int16_t r;           // RECT: radius; LINE: width; TEXT: size; GRAD: 1 = left to right
  uint32_t c, c2;      // GRAD: the second colour; TEXT: c2 = where its text starts in Frame::text
};
// Two fixed buffers in PSRAM, made when the app starts (internal RAM stays for WiFi and TLS).
struct Frame {
  Op* op = nullptr;     // MAX_OPS
  char* txt = nullptr;  // MAX_TEXT: the texts, each ended by a 0
  int n = 0, tn = 0;
  bool make() {
    if (!op) op = (Op*)heap_caps_malloc(MAX_OPS * sizeof(Op), MALLOC_CAP_SPIRAM);
    if (!txt) txt = (char*)heap_caps_malloc(MAX_TEXT, MALLOC_CAP_SPIRAM);
    n = tn = 0;
    return op && txt;
  }
  void drop() {
    heap_caps_free(op), heap_caps_free(txt);
    op = nullptr, txt = nullptr, n = tn = 0;
  }
  void clear() { n = tn = 0; }
  void push(const Op& o) { op[n++] = o; }
};
Frame next, shown;  // being recorded by onFrame; on the screen
int dropped = 0;    // shapes left out this run (over MAX_OPS or MAX_TEXT)

// ---------------------------------------------------------------- the runtime
void* jsMalloc(void*, size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
void jsFree(void*, void* p) { heap_caps_free(p); }
void* jsRealloc(void*, void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
void* jsCalloc(void*, size_t c, size_t n) { return heap_caps_calloc(c, n, MALLOC_CAP_SPIRAM); }
size_t jsUsable(const void*) { return 0; }

JSRuntime* rt = nullptr;
JSContext* ctx = nullptr;
JSValue appObj = JS_UNDEFINED, frameObj = JS_UNDEFINED;
String appId;
lv_obj_t* canvas = nullptr;
lv_timer_t* timer = nullptr;
int fps = 10, slowRun = 0;
uint32_t lastFrame = 0, frames = 0, busyMs = 0, startedAt = 0;
bool paused = false, failedNow = false;
String lastWhy;

// The budget's clock: the board's millis(); in the simulator the computer's own (its millis() stands still while a
// script runs, so a script stuck in a loop would never run out of time there).
#ifdef SIM
uint32_t cpuMs() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#else
uint32_t cpuMs() { return millis(); }
#endif
uint32_t deadline = 0;
int interrupt(JSRuntime*, void*) { return deadline && (int32_t)(cpuMs() - deadline) > 0; }

// QuickJS on a 16 KB stack of its own in internal RAM (the UI's is 8 KB; jsapp.cpp tells why), made while the saver
// app runs. Saver apps run when no app is open, so jsapp's stack is not there at the same time.
constexpr size_t BIG = 16 * 1024, UNDER = 5 * 1024;
#ifndef SIM
uint8_t* big = nullptr;
SemaphoreHandle_t bigLock = nullptr;
const std::function<void()>* bigJob = nullptr;
void bigEntry() {
  JS_SetMaxStackSize(rt, BIG - UNDER), JS_UpdateStackTop(rt);
  (*bigJob)();
}
#endif
bool stackReady() {
#ifndef SIM
  if (!big && heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= BIG + 64 &&
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= BIG + 12 * 1024)
    big = (uint8_t*)heap_caps_aligned_alloc(16, BIG, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (big && !bigLock) bigLock = xSemaphoreCreateMutex();
  return big && bigLock;
#else
  return true;
#endif
}
void onStack(const std::function<void()>& job) {
#ifndef SIM
  bigJob = &job;
  esp_execute_shared_stack_function(bigLock, big, BIG, bigEntry);
  bigJob = nullptr;
  JS_UpdateStackTop(rt);
#else
  job();
#endif
}

// The exception as one line ("" when none).
String exception() {
  JSValue e = JS_GetException(ctx);
  const char* s = JS_ToCString(ctx, e);
  String out = s ? s : "?";
  JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, e);
  return out;
}

// Runs job on the app's stack with a time budget, then its promise jobs. false (and *err) on a throw or a timeout.
bool guarded(uint32_t ms, const std::function<JSValue()>& job, String* err) {
  JSValue r = JS_UNDEFINED;
  deadline = cpuMs() + ms;
  onStack([&] {
    r = job();
    JSContext* jc = nullptr;
    for (int n = 0; !JS_IsException(r) && JS_IsJobPending(rt) && n < 1000; n++)
      if (JS_ExecutePendingJob(rt, &jc) < 0) {
        JS_FreeValue(ctx, r);
        r = JS_EXCEPTION;
      }
  });
  deadline = 0;
  if (JS_IsException(r)) {
    if (err) *err = exception();
    return false;
  }
  JS_FreeValue(ctx, r);
  return true;
}

// app[name](...args) when the app has it.
bool callApp(const char* name, int argc, JSValue* argv, String* err, uint32_t ms = CALL_MS) {
  JSValue fn = JS_GetPropertyStr(ctx, appObj, name);
  bool ok = true;
  if (JS_IsFunction(ctx, fn)) ok = guarded(ms, [&] { return JS_Call(ctx, fn, appObj, argc, argv); }, err);
  JS_FreeValue(ctx, fn);
  return ok;
}

// ---------------------------------------------------------------- stops in a row (docs/app-slots.md §4.1)
String strikeId;
uint32_t strikeAt[3] = {0, 0, 0};
bool blocked = false;
uint32_t blockedPkgs = 0;  // pkg::version() when blocked: an update or a reinstall lets it start again

void strike() {
  if (strikeId != appId) strikeId = appId, blocked = false, memset(strikeAt, 0, sizeof strikeAt);
  strikeAt[0] = strikeAt[1], strikeAt[1] = strikeAt[2], strikeAt[2] = millis() | 1;
  if (blocked || !strikeAt[0] || millis() - strikeAt[0] > 600000) return;
  blocked = true;
  blockedPkgs = pkg::version();
  const pkg::Package* p = pkg::find(appId.c_str());
  const String name = p ? p->name : appId;
  trace::add("saver app %s: stopped 3 times in 10 minutes, not started again", appId.c_str());
  notif::Opts o;
  o.arg = "defaults";
  notif::post("settings", i18n::fmt(N_("{0} stopped"), name).c_str(),
              T_("The screen saver app stopped 3 times, so the built-in saver shows instead. Choose it again in Settings, Default apps."),
              o);
}

void freeRuntime() {
  if (ctx) {
    JS_FreeValue(ctx, appObj), JS_FreeValue(ctx, frameObj);
    appObj = frameObj = JS_UNDEFINED;
    JS_FreeContext(ctx);
    ctx = nullptr;
  }
  if (rt) JS_FreeRuntime(rt), rt = nullptr;
  next.drop(), shown.drop();
#ifndef SIM
  heap_caps_free(big);
  big = nullptr;
#endif
}

void fail(const String& why) {
  lastWhy = why;
  failedNow = true;
  trace::add("saver app %s error: %s", appId.c_str(), why.c_str());
  strike();
  stop();
}

// ---------------------------------------------------------------- natives
double num(JSValueConst v, double def = 0) {
  double d;
  return JS_ToFloat64(ctx, &d, v) || d != d ? def : d;
}
int16_t px(JSValueConst v) { return (int16_t)constrain((long)num(v), -4000L, 4000L); }
uint8_t opa(JSValueConst v) { return (uint8_t)constrain((long)(num(v, 1) * 255 + 0.5), 0L, 255L); }

// "#rrggbb", "#rgb" or a number 0xrrggbb
uint32_t color(JSValueConst v, uint32_t def = 0xffffff) {
  if (JS_IsNumber(v)) return (uint32_t)num(v) & 0xffffff;
  const char* s = JS_ToCString(ctx, v);
  uint32_t c = def;
  if (s && s[0] == '#') {
    char* end = nullptr;
    const uint32_t n = strtoul(s + 1, &end, 16);
    const size_t len = end - (s + 1);
    if (!*end && len == 6) c = n;
    else if (!*end && len == 3) c = ((n >> 8) & 15) * 0x110000 + ((n >> 4) & 15) * 0x1100 + (n & 15) * 0x11;
  }
  JS_FreeCString(ctx, s);
  return c;
}

bool room() {
  if (next.n < MAX_OPS) return true;
  dropped++;
  return false;
}

JSValue nClear(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  // a clear forgets what came before it in this frame
  next.clear();
  next.push(Op{CLEAR, 255, 0, 0, 0, 0, 0, 0, argc ? color(argv[0], 0) : 0, 0});
  return JS_UNDEFINED;
}
// __rect(x, y, w, h, c, r, a)
JSValue nRect(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 7 || !room()) return JS_UNDEFINED;
  next.push(Op{RECT, opa(argv[6]), 0, px(argv[0]), px(argv[1]), px(argv[2]), px(argv[3]),
                        (int16_t)constrain((long)num(argv[5]), 0L, 32767L), color(argv[4]), 0});
  return JS_UNDEFINED;
}
// __line(x1, y1, x2, y2, c, w, a)
JSValue nLine(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 7 || !room()) return JS_UNDEFINED;
  next.push(Op{LINE, opa(argv[6]), 0, px(argv[0]), px(argv[1]), px(argv[2]), px(argv[3]),
                        (int16_t)constrain((long)num(argv[5], 2), 1L, 64L), color(argv[4]), 0});
  return JS_UNDEFINED;
}
// __grad(x, y, w, h, c1, c2, horizontal, a)
JSValue nGrad(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 8 || !room()) return JS_UNDEFINED;
  next.push(Op{GRAD, opa(argv[7]), 0, px(argv[0]), px(argv[1]), px(argv[2]), px(argv[3]),
                        (int16_t)(JS_ToBool(ctx, argv[6]) > 0), color(argv[4], 0), color(argv[5], 0)});
  return JS_UNDEFINED;
}

const lv_font_t* fontOf(int size, bool bold) { return fonts::get(constrain(size, 10, 96), bold); }

lv_point_t measure(const char* s, int size, bool bold) {
  lv_point_t p;
  lv_text_get_size(&p, s, fontOf(size, bold), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return p;
}

// __text(s, x, y, size, c, align (0 left, 1 centre, 2 right), bold, a): x is where that side of the text goes, y its top
JSValue nText(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 8 || !room()) return JS_UNDEFINED;
  size_t len = 0;
  const char* s = JS_ToCStringLen(ctx, &len, argv[0]);
  if (!s) return JS_EXCEPTION;
  if (len && next.tn + (int)len + 1 <= MAX_TEXT) {
    const int size = constrain((int)num(argv[3], 16), 10, 96);
    const bool bold = JS_ToBool(ctx, argv[6]) > 0;
    const int align = constrain((int)num(argv[5]), 0, 2);
    const lv_point_t sz = measure(s, size, bold);
    const int16_t x = px(argv[1]) - (align == 1 ? sz.x / 2 : align == 2 ? sz.x : 0);
    next.push(Op{TEXT, opa(argv[7]), (uint8_t)bold, x, px(argv[2]), (int16_t)sz.x, (int16_t)sz.y, (int16_t)size,
                 color(argv[4]), (uint32_t)next.tn});
    memcpy(next.txt + next.tn, s, len);
    next.txt[next.tn + len] = 0;
    next.tn += len + 1;
  } else if (len) {
    dropped++;
  }
  JS_FreeCString(ctx, s);
  return JS_UNDEFINED;
}
// __measure(s, size, bold) -> {w, h}
JSValue nMeasure(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  const char* s = argc ? JS_ToCString(ctx, argv[0]) : nullptr;
  const lv_point_t sz = measure(s ? s : "", argc > 1 ? (int)num(argv[1], 16) : 16, argc > 2 && JS_ToBool(ctx, argv[2]) > 0);
  JS_FreeCString(ctx, s);
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "w", JS_NewInt32(ctx, sz.x));
  JS_SetPropertyStr(ctx, o, "h", JS_NewInt32(ctx, sz.y));
  return o;
}

JSValue nFps(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  if (argc) fps = constrain((int)num(argv[0], 10), 1, 20);
  if (timer) lv_timer_set_period(timer, 1000 / fps);
  return JS_NewInt32(ctx, fps);
}

// app.time(fmt): HH H hh h mm ss a dd d MM M yyyy yy, anything else as it is
JSValue nTime(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  const char* f = argc ? JS_ToCString(ctx, argv[0]) : nullptr;
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  String out;
  char b[8];
  for (const char* p = f ? f : "HH:mm"; *p;) {
    int n = 1;
    while (p[n] == p[0]) n++;
    const char c = p[0];
    int v = -1, w = n >= 2 ? 2 : 1;
    if (c == 'H') v = t.tm_hour;
    else if (c == 'h') v = t.tm_hour % 12 ? t.tm_hour % 12 : 12;
    else if (c == 'm') v = t.tm_min;
    else if (c == 's') v = t.tm_sec;
    else if (c == 'd' && n <= 2) v = t.tm_mday;
    else if (c == 'M' && n <= 2) v = t.tm_mon + 1;
    else if (c == 'y') v = n >= 4 ? t.tm_year + 1900 : (t.tm_year % 100), w = n >= 4 ? 4 : 2;
    if (v >= 0) {
      snprintf(b, sizeof b, "%0*d", w, v);
      out += b;
      p += n;
    } else if (c == 'a') {
      out += t.tm_hour < 12 ? "am" : "pm";
      p++;
    } else {
      out += c;
      p++;
    }
  }
  JS_FreeCString(ctx, f);
  return JS_NewString(ctx, out.c_str());
}

JSValue nSetting(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  const char* k = argc ? JS_ToCString(ctx, argv[0]) : nullptr;
  const String json = appset::jsValue(appId.c_str(), k ? k : "");
  JS_FreeCString(ctx, k);
  return JS_ParseJSON(ctx, json.c_str(), json.length(), "<settings>");
}

JSValue nLog(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
  const char* s = argc ? JS_ToCString(ctx, argv[0]) : nullptr;
  trace::add("saver app %s log: %.200s", appId.c_str(), s ? s : "");
  JS_FreeCString(ctx, s);
  return JS_UNDEFINED;
}

const char* PRELUDE = R"JS(
var __o = (o, k, d) => (o && o[k] !== undefined ? o[k] : d);
var __frame = {w: 0, h: 0, cls: 'medium', mono: false,
  clear: (c) => __clear(c === undefined ? '#000000' : c),
  rect: (x, y, w, h, c, o) => __rect(x, y, w, h, c, __o(o, 'r', 0), __o(o, 'a', 1)),
  circle: (x, y, r, c, o) => __rect(x - r, y - r, 2 * r, 2 * r, c, 32767, __o(o, 'a', 1)),
  line: (x1, y1, x2, y2, c, o) => __line(x1, y1, x2, y2, c, __o(o, 'w', 2), __o(o, 'a', 1)),
  gradient: (x, y, w, h, c1, c2, o) => __grad(x, y, w, h, c1, c2, __o(o, 'dir', 'v') === 'h', __o(o, 'a', 1)),
  text: (s, x, y, o) => __text(String(s), x, y, __o(o, 'size', 16), __o(o, 'color', '#ffffff'),
    {left: 0, center: 1, centre: 1, right: 2}[__o(o, 'align', 'left')] || 0, !!__o(o, 'bold', false), __o(o, 'a', 1)),
  measure: (s, o) => __measure(String(s), __o(o, 'size', 16), !!__o(o, 'bold', false))};
var app = {lang: 'en', dark: true, fps: (n) => __fps(n), time: (f) => __time(f === undefined ? 'HH:mm' : String(f))};
var settings = {get: (k) => __setting(String(k))};
var log = (t) => __log(String(t));
)JS";

// ---------------------------------------------------------------- drawing
void onDraw(lv_event_t* e) {
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_coords((lv_obj_t*)lv_event_get_current_target(e), &c);
  const int ox = c.x1, oy = c.y1;
  for (int i = 0; i < shown.n; i++) {
    const Op& o = shown.op[i];
    if (o.k == LINE) {
      lv_draw_line_dsc_t d;
      lv_draw_line_dsc_init(&d);
      d.p1.x = ox + o.x, d.p1.y = oy + o.y, d.p2.x = ox + o.w, d.p2.y = oy + o.h;
      d.color = lv_color_hex(o.c), d.opa = o.opa, d.width = o.r, d.round_start = d.round_end = 1;
      lv_draw_line(layer, &d);
      continue;
    }
    lv_area_t a = o.k == CLEAR ? c : lv_area_t{ox + o.x, oy + o.y, ox + o.x + o.w - 1, oy + o.y + o.h - 1};
    if (a.x2 < a.x1 || a.y2 < a.y1) continue;
    if (o.k == TEXT) {
      lv_draw_label_dsc_t d;
      lv_draw_label_dsc_init(&d);
      d.font = fontOf(o.r, o.bold), d.color = lv_color_hex(o.c), d.opa = o.opa;
      d.text = shown.txt + o.c2;  // kept until the next whole frame: drawn within this refresh
      d.flag = LV_TEXT_FLAG_EXPAND;       // its own width: never wrapped (only its \n break lines)
      a.x2 += 1;
      lv_draw_label(layer, &d, &a);
      continue;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(o.c), d.bg_opa = o.opa;
    if (o.k == RECT) d.radius = o.r;
    if (o.k == GRAD) {
      d.bg_grad.dir = o.r ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER;
      d.bg_grad.stops_count = 2;
      d.bg_grad.stops[0] = {lv_color_hex(o.c), LV_OPA_COVER, 0};
      d.bg_grad.stops[1] = {lv_color_hex(o.c2), LV_OPA_COVER, 255};
    }
    lv_draw_rect(layer, &d, &a);
  }
}

// ---------------------------------------------------------------- frames
void tick(lv_timer_t*) {
  if (!ctx) return;
  String err;
  const bool p = assistant::open();  // the assistant's pop-up over the saver: no frames under it
  if (p != paused) {
    paused = p;
    if (!callApp(p ? "onPause" : "onResume", 0, nullptr, &err)) return fail(err);
  }
  if (paused) return;
  const uint32_t t0 = cpuMs(), now = millis();
  JSValue argv[2] = {JS_DupValue(ctx, frameObj), JS_NewInt32(ctx, frames ? now - lastFrame : 0)};
  lastFrame = now;
  next.clear();
  const bool ok = callApp("onFrame", 2, argv, &err);
  JS_FreeValue(ctx, argv[0]);
  if (!ok) return fail(err.indexOf("interrupted") >= 0 ? String("a frame took over 200 ms") : err);
  const uint32_t ms = cpuMs() - t0;
  frames++, busyMs += ms;
  slowRun = ms > SLOW_MS ? slowRun + 1 : 0;
  if (slowRun >= 3) return fail("three frames over 100 ms in a row");
  if (next.n) {  // a frame that drew nothing keeps the last one
    std::swap(next, shown);
    if (canvas) lv_obj_invalidate(canvas);
  }
}

void setInt(JSValue o, const char* k, int v) { JS_SetPropertyStr(ctx, o, k, JS_NewInt32(ctx, v)); }

}  // namespace

// ================================================================== the saver app
bool runs(const pkg::Package* p) {
  if (!p || p->addonOf.length() || p->kind != "js") return false;
  static std::map<String, bool> seen;  // the manifest read once per package list
  static uint32_t ver = 0xffffffffu;
  if (ver != pkg::version()) seen.clear(), ver = pkg::version();
  auto it = seen.find(p->id);
  if (it != seen.end()) return it->second;
  JsonDocument d;
  bool yes = false;
  if (!deserializeJson(d, pkg::manifestJson(p->id.c_str())))
    for (JsonVariantConst x : d["provides"].as<JsonArrayConst>()) yes |= !strcmp(x | "", "saver");
  seen[p->id] = yes;
  return yes;
}

bool start(lv_obj_t* parent, const char* id) {
  stop();
  appId = id;
  failedNow = false;
  lastWhy = "";
  const pkg::Package* p = pkg::find(id);
  auto no = [&](const char* why) {
    lastWhy = why;
    trace::add("saver app %s not started: %s", id, why);
    freeRuntime();
    return false;
  };
  if (!runs(p) || !p->available) return no("not on the device");
  if (blocked && strikeId == appId && blockedPkgs == pkg::version()) return no("stopped 3 times in 10 minutes");
  // Its saver file: manifest entry.saver, else saver.js, else main.js.
  String file = "saver.js";
  {
    JsonDocument d;
    if (!deserializeJson(d, pkg::manifestJson(id)) && d["entry"]["saver"].is<const char*>()) file = d["entry"]["saver"].as<const char*>();
  }
  pkg::File f;
  if (!pkg::open(id, file.c_str(), f) && !pkg::open(id, (file = "main.js").c_str(), f)) return no("no saver file");
  if (f.size() > MAX_FILE) return no("the saver file is over 128 KB");
  uint8_t* raw = pkg::loadAll(f);
  if (!raw) return no("the saver file cannot be read");
  String src;
  src.concat((const char*)raw, f.size());
  heap_caps_free(raw);
  if (!stackReady()) return no("no room for its stack in internal RAM");
  if (!next.make() || !shown.make()) return no("no PSRAM for its frames");

  static const JSMallocFunctions mf = {jsCalloc, jsMalloc, jsFree, jsRealloc, jsUsable};
  rt = JS_NewRuntime2(&mf, nullptr);
  if (!rt) return no("no memory for a runtime");
  JS_SetMemoryLimit(rt, MEM);
#ifdef SIM
  JS_SetMaxStackSize(rt, 0);  // the computer's frames are bigger: the board's limit is checked on the board or QEMU
#else
  JS_SetMaxStackSize(rt, BIG - UNDER);
#endif
  JS_SetInterruptHandler(rt, interrupt, nullptr);
  ctx = JS_NewContext(rt);
  if (!ctx) return no("no memory for a context");
  JSValue g = JS_GetGlobalObject(ctx);
  static const struct { const char* n; JSCFunction* f; int a; } FNS[] = {
      {"__clear", nClear, 1}, {"__rect", nRect, 7},   {"__line", nLine, 7},       {"__grad", nGrad, 8},
      {"__text", nText, 8},   {"__measure", nMeasure, 3}, {"__fps", nFps, 1}, {"__time", nTime, 1},
      {"__setting", nSetting, 1}, {"__log", nLog, 1}};
  for (const auto& x : FNS) JS_SetPropertyStr(ctx, g, x.n, JS_NewCFunction(ctx, x.f, x.n, x.a));
  String err;
  const uint32_t t0 = cpuMs();
  bool ok = guarded(START_MS, [&] { return JS_Eval(ctx, PRELUDE, strlen(PRELUDE), "<saver prelude>", JS_EVAL_TYPE_GLOBAL); }, &err);
  if (ok) {
    appObj = JS_GetPropertyStr(ctx, g, "app");
    frameObj = JS_GetPropertyStr(ctx, g, "__frame");
    JS_SetPropertyStr(ctx, appObj, "lang", JS_NewString(ctx, i18n::lang()));
    setInt(frameObj, "w", os::W()), setInt(frameObj, "h", os::H());
    ok = guarded(START_MS, [&] { return JS_Eval(ctx, src.c_str(), src.length(), file.c_str(), JS_EVAL_TYPE_GLOBAL); }, &err);
  }
  JS_FreeValue(ctx, g);
  if (ok) {
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "as", JS_NewString(ctx, "saver"));
    setInt(info, "w", os::W()), setInt(info, "h", os::H()), setInt(info, "scale", 1);
    JS_SetPropertyStr(ctx, info, "cls", JS_NewString(ctx, "medium"));
    JS_SetPropertyStr(ctx, info, "mono", JS_FALSE);
    JS_SetPropertyStr(ctx, info, "landscape", JS_NewBool(ctx, os::landscape()));
    ok = callApp("onStart", 1, &info, &err, START_MS);
    JS_FreeValue(ctx, info);
  }
  if (!ok) {
    lastWhy = err;
    failedNow = true;
    trace::add("saver app %s start (%s): %s", id, file.c_str(), err.c_str());
    strike();
    freeRuntime();
    return false;
  }
  canvas = lv_obj_create(parent);
  lv_obj_remove_style_all(canvas);
  lv_obj_set_size(canvas, os::W(), os::H());
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_set_style_bg_color(canvas, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, 0);
  lv_obj_remove_flag(canvas, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_event_cb(canvas, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(canvas, [](lv_event_t*) {
    if (!canvas) return;  // stop() deletes it itself
    canvas = nullptr;
    stop();  // the saver's objects went (it closed, the screen turned)
  }, LV_EVENT_DELETE, nullptr);
  frames = busyMs = slowRun = dropped = 0;
  paused = false;
  startedAt = millis();
  timer = lv_timer_create(tick, 1000 / fps, nullptr);
  lv_timer_ready(timer);  // the first frame now
  trace::add("saver app %s started: %s %u KB ran in %lu ms", id, file.c_str(), (unsigned)(src.length() >> 10),
             (unsigned long)(cpuMs() - t0));
  return true;
}

void stop() {
  if (timer) lv_timer_delete(timer), timer = nullptr;
  if (ctx) {
    String err;
    if (!failedNow && !callApp("onStop", 0, nullptr, &err)) trace::add("saver app %s error: onStop: %s", appId.c_str(), err.c_str());
    JSMemoryUsage m;
    JS_ComputeMemoryUsage(rt, &m);
    trace::add("saver app %s ended: %lu frames in %lu s, %lu ms each, %u KB of script memory%s", appId.c_str(),
               (unsigned long)frames, (unsigned long)((millis() - startedAt) / 1000),
               (unsigned long)(frames ? busyMs / frames : 0), (unsigned)(m.memory_used_size >> 10),
               dropped ? " (shapes left out)" : "");
  }
  if (canvas) {  // (before the frames go: nothing draws from them after this)
    lv_obj_t* c = canvas;
    canvas = nullptr;
    lv_obj_delete(c);
  }
  freeRuntime();
  fps = 10;
}

bool running() { return ctx != nullptr; }
bool failed() { return failedNow; }
String why() { return lastWhy; }

void unblock() {
  blocked = false;
  strikeId = "";
}

}  // namespace jssaver
