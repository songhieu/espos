#pragma GCC optimize("Os")  // cold code: built small (0.5.7 flash diet; the app slot is full)
#include "jsapp.h"

#include "i18n.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <lvgl.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>

extern "C" {
#include "quickjs.h"
}

#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "keyboard.h"
#include "notif.h"
#include "os.h"
#include "sound.h"
#include "trace.h"
#include "uikit.h"

#include "jsfs.h"
#include "jsnet.h"
#include "jsui.h"
#include "jsset.h"
#include "jsterm.h"
#include "jsvoice.h"
#include "pkg/pkg.h"

#ifndef SIM
#include "esp_expression_with_stack.h"
#endif

namespace jsapp {
namespace {

using namespace kit;

// "#rrggbb", or (level 2) a system colour that follows the light/dark theme: @bg @card @card2 @fg @dim @faint @line
// @chip @accent @ok @warn @bad.
uint32_t hexColor(const char* s, uint32_t def) {
  if (s && s[0] == '@') {
    const kit::Pal& p = kit::pal();
    static const char* const N[] = {"bg", "card", "card2", "fg", "dim", "faint", "line", "chip", "accent", "ok", "warn", "bad"};
    const uint32_t v[] = {p.bg, p.cell, p.cell2, p.fg, p.dim, p.faint, p.line, p.chip, p.dark ? 0x2f95ffu : 0x0a84ffu,
                          p.dark ? 0x34c759u : 0x1e9e52u, p.dark ? 0xff9f43u : 0xc2580cu, p.dark ? 0xff6b6bu : 0xe0383bu};
    for (int i = 0; i < 12; i++)
      if (!strcmp(s + 1, N[i])) return v[i];
    return def;
  }
  if (!s || s[0] != '#' || strlen(s) != 7) return def;
  return strtoul(s + 1, nullptr, 16);
}

// ---------------------------------------------------------------- the running app
JSRuntime* rt = nullptr;

void* jsMalloc(void*, size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
void jsFree(void*, void* p) { heap_caps_free(p); }
void* jsRealloc(void*, void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
void* jsCalloc(void*, size_t c, size_t n) { return heap_caps_calloc(c, n, MALLOC_CAP_SPIRAM); }
size_t jsUsable(const void*) { return 0; }

uint32_t deadline = 0;
int interrupt(JSRuntime*, void*) { return deadline && (int32_t)(millis() - deadline) > 0; }

// ---------------------------------------------------------------- QuickJS on a stack of its own
// The UI thread's stack is 8 KB: parsing an app as small as Xúc xắc (nested ui.column([...]) calls) overflowed it
// and the board restarted (2026-11-17): the UI's own calls had used most of it already. QuickJS runs on a 16 KB
// stack in internal RAM instead, made while a JS app is open and freed when it closes. QuickJS's own limit (11 KB)
// leaves 5 KB under it, so a deeper script throws "Maximum call stack size exceeded" (shown as the app's error)
// instead of crashing. Measured with Xúc xắc: 2.8 KB written, but a call reserves its whole frame first (a limit
// of 7 KB or less refused the prelude; JSSTACK on the dev console).
size_t BIG = 16 * 1024;
size_t UNDER = 5 * 1024;  // C under QuickJS's limit
#ifndef SIM
uint8_t* big = nullptr;
SemaphoreHandle_t bigLock = nullptr;
bool onBig = false, freeBig = false;
uint32_t bigUsed = 0;
const std::function<void()>* bigJob = nullptr;

void bigEntry() {
  onBig = true;
  if (rt) JS_SetMaxStackSize(rt, BIG - UNDER), JS_UpdateStackTop(rt);  // QuickJS's limit, from here
  (*bigJob)();
  onBig = false;
}

void dropBig() {
  if (onBig) {  // closing from inside a script: when it returns
    freeBig = true;
    return;
  }
  if (big) {
    uint32_t k = 0;  // how deep it went (the fill pattern still there under that)
    while (k < BIG && big[k] == 0xA5) k++;
    bigUsed = std::max(bigUsed, (uint32_t)(BIG - k));
    trace::add("js: stack used %u of %u bytes (at %p)", (unsigned)bigUsed, (unsigned)BIG, big);
  }
  heap_caps_free(big);
  big = nullptr, freeBig = false, bigUsed = 0;
}
#endif

// Its stack, made when an app opens: only while 12 KB of internal RAM stays free besides it (a sync's TLS
// handshake right after boot can leave little: the app then waits, see open()).
bool bigReady() {
#ifndef SIM
  if (!big && heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= BIG + 64 &&
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= BIG + 12 * 1024 &&
      (big = (uint8_t*)heap_caps_aligned_alloc(16, BIG, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)))
    memset(big, 0xA5, BIG);  // 16-byte aligned: the switch rounds the top up to 16
  return big != nullptr;
#else
  return true;
#endif
}

void onJsStack(const std::function<void()>& job) {
#ifndef SIM
  if (!onBig) {
    bigReady();
    if (!bigLock) bigLock = xSemaphoreCreateMutex();
    if (big && bigLock) {
      bigJob = &job;
      esp_execute_shared_stack_function(bigLock, big, BIG, bigEntry);
      bigJob = nullptr;
      if (rt) JS_UpdateStackTop(rt);
      if (freeBig) dropBig();
      return;
    }
    trace::add("js: no room for its stack (internal RAM %u KB)",
               (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10));
  }
#endif
  job();
}

struct Timer {
  int id;
  bool repeat;
  JSValue fn;
  lv_timer_t* t;
};

struct Run {
  String id, name;
  JSContext* ctx = nullptr;
  JSValue handlers = JS_UNDEFINED;  // functions of the current screen
  lv_obj_t *scr = nullptr, *body = nullptr, *title = nullptr;
  std::vector<Timer*> timers;
  int nextTimer = 1;
  bool sound = false, notify = false, awake = false;  // awake: perm "screen.awake"
  Preferences prefs;
  bool dirty = false;
  bool full = false;                    // app.full: no nav bar, the body is the app's (level 2)
  lv_obj_t* holder = nullptr;           // keyed nodes wait here while the screen is built again
  std::map<String, lv_obj_t*> keyed;    // node.key -> its object from the last render
  lv_obj_t* sheet = nullptr;            // ui.sheet's layer (over the body)
} run;

std::map<String, jsapp::NodeFn>& nodeTypes() {  // addNode: node types built in other files (jsui.cpp)
  static std::map<String, jsapp::NodeFn> m;
  return m;
}

void render();

// A throw (or a timeout) as the app's error: traced, and shown as a toast.
void report(JSContext* ctx, const char* where) {
  JSValue e = JS_GetException(ctx);
  const char* s = JS_ToCString(ctx, e);
  trace::add("app %s%s: %s", run.id.c_str(), where, s ? s : "?");
  os::toast((String(T_("App error: ")) + (s ? s : "?")).c_str());
  JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, e);
}

// Promise jobs (.then callbacks, code after an await), until none is left; on the JS stack, in the caller's budget
// (lane 1f: net.request answers through promises; level 1 never ran them).
void runJobs() {
  JSContext* jc = nullptr;
  for (int n = 0; rt && JS_IsJobPending(rt) && n < 10000; n++)
    if (JS_ExecutePendingJob(rt, &jc) < 0 && jc) report(jc, " (promise)");
}

// Calls fn() with a time budget; a throw or a timeout becomes a message, never a crash. *quiet: fn returned false.
bool call(JSValue fn, int argc = 0, JSValue* argv = nullptr, bool* quiet = nullptr) {
  if (!run.ctx || !JS_IsFunction(run.ctx, fn)) return false;
  deadline = millis() + 1500;
  JSValue r = JS_UNDEFINED;
  onJsStack([&] {
    r = JS_Call(run.ctx, fn, JS_UNDEFINED, argc, argv);
    if (!JS_IsException(r)) runJobs();
  });
  deadline = 0;
  if (JS_IsException(r)) {
    report(run.ctx, "");
    return false;
  }
  if (quiet) *quiet = JS_IsBool(r) && !JS_ToBool(run.ctx, r);
  JS_FreeValue(run.ctx, r);
  return true;
}

// A promise rejected with nobody to catch it: the app's error (else a failed net.request would pass in silence).
void onRejection(JSContext* ctx, JSValueConst, JSValueConst reason, bool handled, void*) {
  if (handled || ctx != run.ctx) return;
  const char* s = JS_ToCString(ctx, reason);
  trace::add("app %s: unhandled rejection: %s", run.id.c_str(), s ? s : "?");
  os::toast((String(T_("App error: ")) + (s ? s : "?")).c_str());
  JS_FreeCString(ctx, s);
}

// The keyboard opened, grew (its key row) or closed over a field of a full-screen app: the body gets shorter by its
// height, so a layout that fills the screen (a terminal with its prompt under it) stays whole above the keys.
bool kbMine() {  // the keyboard types into a field of the open app
  bool mine = false;
  for (lv_obj_t* q = kb::field(); q && run.body && !mine; q = lv_obj_get_parent(q)) mine = q == run.body;
  return mine;
}
void scheduleRender();
void keyboardRoom(int h) {
  if (!run.ctx || !run.body || !lv_obj_is_valid(run.body)) return;
  const int room = h && kbMine() ? h : 0;
  // app.kb (level 2): the keyboard's height over the app's own field, 0 when it is down; the app is drawn again
  JSValue g = JS_GetGlobalObject(run.ctx), a = JS_GetPropertyStr(run.ctx, g, "app");
  int32_t was = 0;
  JSValue v = JS_GetPropertyStr(run.ctx, a, "kb");
  JS_ToInt32(run.ctx, &was, v);
  JS_FreeValue(run.ctx, v);
  JS_SetPropertyStr(run.ctx, a, "kb", JS_NewInt32(run.ctx, room));
  JS_FreeValue(run.ctx, a);
  JS_FreeValue(run.ctx, g);
  if (run.full) {
    lv_obj_set_height(run.body, os::H() - os::SB_H - room);
    lv_obj_update_layout(run.body);
  }
  if (was != room) scheduleRender();
}
// app.closeKeyboard() (level 2)
JSValue nKbClose(JSContext*, JSValueConst, int, JSValueConst*) {
  if (kbMine()) kb::close();
  return JS_UNDEFINED;
}

// A finger down on a button with onDown/onUp (hold to talk): the screen is drawn again only after it lets go, or the
// button under the finger would be replaced and never hear its release (onUp).
bool holding = false;
bool pressedInBody() { return holding; }

void scheduleRender() {
  if (run.dirty) return;
  run.dirty = true;
  lv_async_call([](void*) {
    if (pressedInBody()) {  // again in a moment
      lv_timer_t* t = lv_timer_create([](lv_timer_t* t) {
        lv_timer_delete(t);
        run.dirty = false;
        scheduleRender();
      }, 40, nullptr);
      (void)t;
      return;
    }
    run.dirty = false;
    render();
  }, nullptr);
}

// ---- native functions for the prelude
JSValue nRefresh(JSContext*, JSValueConst, int, JSValueConst*) {
  scheduleRender();
  return JS_UNDEFINED;
}

void timerCb(lv_timer_t* t) {
  Timer* tm = (Timer*)lv_timer_get_user_data(t);
  JSValue fn = JS_DupValue(run.ctx, tm->fn);
  const bool repeat = tm->repeat;
  const int id = tm->id;
  bool quiet = false;  // level 2: a timer callback that returns false changed nothing on screen (no redraw)
  call(fn, 0, nullptr, &quiet);
  JS_FreeValue(run.ctx, fn);
  if (!repeat) {
    for (auto it = run.timers.begin(); it != run.timers.end(); ++it)
      if ((*it)->id == id) {
        lv_timer_delete((*it)->t);
        JS_FreeValue(run.ctx, (*it)->fn);
        delete *it;
        run.timers.erase(it);
        break;
      }
  }
  if (!quiet) scheduleRender();
}

JSValue nTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  int32_t ms = 1000, repeat = 0;
  if (argc < 3 || !JS_IsFunction(ctx, argv[1])) return JS_ThrowTypeError(ctx, "timer(ms, fn)");
  JS_ToInt32(ctx, &ms, argv[0]);
  JS_ToInt32(ctx, &repeat, argv[2]);
  if (run.timers.size() >= 8) return JS_ThrowRangeError(ctx, "too many timers");
  Timer* tm = new Timer{run.nextTimer++, repeat != 0, JS_DupValue(ctx, argv[1]), nullptr};
  tm->t = lv_timer_create(timerCb, max(50, (int)ms), tm);
  run.timers.push_back(tm);
  return JS_NewInt32(ctx, tm->id);
}

JSValue nCancel(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  int32_t id = 0;
  if (argc) JS_ToInt32(ctx, &id, argv[0]);
  for (auto it = run.timers.begin(); it != run.timers.end(); ++it)
    if ((*it)->id == id) {
      lv_timer_delete((*it)->t);
      JS_FreeValue(ctx, (*it)->fn);
      delete *it;
      run.timers.erase(it);
      break;
    }
  return JS_UNDEFINED;
}

JSValue nSound(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  if (!run.sound) return JS_ThrowTypeError(ctx, "app has no \"sound\" permission");
  int32_t k = 0;
  if (argc) JS_ToInt32(ctx, &k, argv[0]);
  if (k == 0) sound::chime();
  else if (k == 1) sound::alarmBeep();
  else sound::click();
  return JS_UNDEFINED;
}

String argStr(JSContext* ctx, int argc, JSValueConst* argv, int i) {
  if (i >= argc) return "";
  const char* s = JS_ToCString(ctx, argv[i]);
  String out = s ? s : "";
  JS_FreeCString(ctx, s);
  return out;
}

// notify(title, body, {arg, tag, sound, badge}) -> bool, notify.schedule(at, title, body, opts) -> bool,
// notify.cancel(tag), app.badge(n) (permission "notify"): __notify(op 0..3, ...) from the prelude (notif.h).
JSValue nNotify(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  if (!run.notify) return JS_ThrowTypeError(ctx, "app has no \"notify\" permission");
  int32_t op = 0;
  if (argc) JS_ToInt32(ctx, &op, argv[0]);
  const char* app = run.id.c_str();
  if (op == 2) {
    const String tag = argStr(ctx, argc, argv, 1);
    notif::cancel(app, tag.length() ? tag.c_str() : nullptr);
    return JS_UNDEFINED;
  }
  if (op == 3) {
    int32_t n = -1;
    if (argc > 1) JS_ToInt32(ctx, &n, argv[1]);
    notif::setBadge(app, n);
    return JS_UNDEFINED;
  }
  // The prelude spreads the options: title, body, arg, tag (strings), sound, badge (booleans).
  const int k = op == 1 ? 2 : 1;  // where the title is
  const String title = argStr(ctx, argc, argv, k), body = argStr(ctx, argc, argv, k + 1);
  const String arg = argStr(ctx, argc, argv, k + 2), tag = argStr(ctx, argc, argv, k + 3);
  notif::Opts o;
  o.arg = arg.length() ? arg.c_str() : nullptr;
  o.tag = tag.length() ? tag.c_str() : nullptr;
  o.sound = argc <= k + 4 || JS_ToBool(ctx, argv[k + 4]) > 0;
  o.badge = argc <= k + 5 || JS_ToBool(ctx, argv[k + 5]) > 0;
  if (op == 1) {
    double at = 0;
    if (argc > 1) JS_ToFloat64(ctx, &at, argv[1]);
    if (at < 1e9) at += desk::nowMs() ? (double)desk::nowMs() : (double)time(nullptr) * 1000;  // ms from now
    return JS_NewBool(ctx, notif::schedule(app, (time_t)(at / 1000), title.c_str(), body.c_str(), o));
  }
  return JS_NewBool(ctx, notif::post(app, title.c_str(), body.c_str(), o));
}

JSValue nToast(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  os::toast(argStr(ctx, argc, argv, 0).c_str());
  return JS_UNDEFINED;
}
// log(text): a line in the device's trace (serial console, the simulator's log) for the app's developer, 200 bytes.
JSValue nLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  trace::add("app %s: %.200s", run.id.c_str(), argStr(ctx, argc, argv, 0).c_str());
  return JS_UNDEFINED;
}

JSValue nGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const String k = argStr(ctx, argc, argv, 0);
  if (!k.length() || k.length() > 14 || !run.prefs.isKey(k.c_str())) return JS_UNDEFINED;
  const String v = run.prefs.getString(k.c_str(), "");
  return JS_NewString(ctx, v.c_str());
}

JSValue nSet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  const String k = argStr(ctx, argc, argv, 0), v = argStr(ctx, argc, argv, 1);
  if (!k.length() || k.length() > 14) return JS_ThrowRangeError(ctx, "store keys are 1-14 characters");
  if (v.length() > 3000) return JS_ThrowRangeError(ctx, "store values are at most 3000 characters");
  run.prefs.putString(k.c_str(), v);
  return JS_UNDEFINED;
}

const char* PRELUDE = R"JS(
var ui = {
  column: (c, o) => Object.assign({t: 'col', c: c || []}, o),
  row: (c, o) => Object.assign({t: 'row', c: c || []}, o),
  card: (c, o) => Object.assign({t: 'card', c: c || []}, o),
  title: (s, o) => Object.assign({t: 'title', s: String(s)}, o),
  text: (s, o) => Object.assign({t: 'text', s: String(s)}, o),
  big: (s, o) => Object.assign({t: 'big', s: String(s)}, o),
  button: (s, on, o) => Object.assign({t: 'button', s: String(s), on: on}, o),
  chips: (names, sel, on) => ({t: 'chips', names: names, sel: sel || 0, on: on}),
  list: (items, on) => ({t: 'list', items: items || [], on: on}),
  progress: (v, o) => Object.assign({t: 'progress', v: +v || 0}, o),
  spacer: (h) => ({t: 'spacer', h: h || 8}),
  grid: (items, on, o) => Object.assign({t: 'grid', items: items || [], on: on}, o),
  image: (src, o) => Object.assign({t: 'image', src: String(src || '')}, o),
  split: (a, b, o) => Object.assign({t: 'split', a: a, b: b}, o),
  nav: (tabs, sel, on, o) => Object.assign({t: 'nav', tabs: tabs || [], sel: sel || 0, on: on}, o),
  input: (v, o) => Object.assign({t: 'input', v: v === undefined || v === null ? '' : String(v)}, o),
  qr: (s, o) => Object.assign({t: 'qr', s: String(s)}, o),
  line: (l, r, o) => Object.assign({t: 'line', l: String(l), r: r === undefined || r === null ? '' : String(r)}, o),
  sheet: (c, o) => Object.assign({t: 'sheet', c: c || []}, o),
  rows: (items, on, o) => Object.assign({t: 'rows', items: items || [], on: on}, o),
  term: (o) => Object.assign({t: 'term'}, o),
};
var term = {apply: (k, s) => __termApply(String(k), String(s)), clear: (k) => __termDo(String(k), 0, 0),
  end: (k) => __termDo(String(k), 1, 0), pan: (k, d) => __termDo(String(k), 2, +d || 0), forget: (k) => __termDo(String(k), 3, 0),
  info: (k) => __termInfo(String(k)), tail: (k, n) => __termTail(String(k), n === undefined ? 20 : +n)};
var voice = {dictate: (o) => new Promise((res, rej) => __voice(o && o.lang ? String(o.lang) : '', o && o.max_ms ? +o.max_ms : 0, res, rej,
    o && o.expect ? String(o.expect) : '', o && o.about ? String(o.about) : '', !!(o && o.until_quiet))),
  stop: (cancel) => __voiceStop(!!cancel)};
var app = {title: '', render: () => ui.text(''), refresh: () => __refresh()};
var timer = {every: (ms, f) => __timer(ms, f, 1), after: (ms, f) => __timer(ms, f, 0), cancel: (id) => __cancel(id)};
var sound = {chime: () => __sound(0), beep: () => __sound(1), click: () => __sound(2)};
var __nopts = (o) => [o && o.arg !== undefined ? (typeof o.arg === 'string' ? o.arg : JSON.stringify(o.arg)) : '',
  o && o.tag !== undefined ? String(o.tag) : '', !(o && o.sound === false), !(o && o.badge === false)];
var notify = (t, b, o) => __notify(0, t, b || '', ...__nopts(o));
notify.schedule = (at, t, b, o) => __notify(1, at instanceof Date ? at.getTime() : +at, t, b || '', ...__nopts(o));
notify.cancel = (tag) => __notify(2, tag === undefined || tag === null ? '' : String(tag));
app.badge = (n) => __notify(3, n === undefined || n === null ? -1 : +n);
var toast = (t) => __toast(t);
var log = (t) => __log(String(t));
var store = {
  get: (k, d) => { const v = __get(k); if (v === undefined) return d; try { return JSON.parse(v); } catch (e) { return d; } },
  set: (k, v) => __set(k, JSON.stringify(v)),
};
var net = {
  request: (o) => new Promise((res, rej) => {
    o = o || {};
    const b = o.body, obj = b !== undefined && b !== null && typeof b === 'object';
    __net(o.method || 'GET', String(o.url || ''), obj ? JSON.stringify(b) : (b === undefined || b === null ? '' : String(b)),
      obj, o.headers || {}, o.keep || [], +o.timeout || 0, o.auth ? String(o.auth) : '', o.save ? String(o.save) : '', res, rej);
  }),
};
Object.defineProperty(net, 'online', {get: () => __netOnline()});
var fs = {read: (n) => __fsRead(String(n)), write: (n, t) => __fsWrite(String(n), String(t)), remove: (n) => __fsRemove(String(n)),
  list: (d) => __fsList(String(d || 'data')), free: (a) => __fsFree(String(a || 'data'))};
var crypto = {sha256: (t) => __sha256(String(t)), random: (n) => __random(n === undefined ? 16 : +n)};
var settings = {get: (k) => __setting(String(k)), open: () => __settingsOpen()};
)JS";

// ---- building the screen from the tree
JSValue prop(JSValueConst o, const char* k) { return JS_GetPropertyStr(run.ctx, o, k); }

String propStr(JSValueConst o, const char* k, const char* def = "") {
  JSValue v = prop(o, k);
  String out = def;
  if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
    const char* s = JS_ToCString(run.ctx, v);
    if (s) out = s;
    JS_FreeCString(run.ctx, s);
  }
  JS_FreeValue(run.ctx, v);
  return out;
}

int propInt(JSValueConst o, const char* k, int def) {
  JSValue v = prop(o, k);
  int32_t n = def;
  if (JS_IsNumber(v)) JS_ToInt32(run.ctx, &n, v);
  JS_FreeValue(run.ctx, v);
  return n;
}

double propNum(JSValueConst o, const char* k, double def) {
  JSValue v = prop(o, k);
  double n = def;
  if (JS_IsNumber(v)) JS_ToFloat64(run.ctx, &n, v);
  JS_FreeValue(run.ctx, v);
  return n;
}

bool propBool(JSValueConst o, const char* k) {
  JSValue v = prop(o, k);
  const bool b = JS_ToBool(run.ctx, v) > 0;
  JS_FreeValue(run.ctx, v);
  return b;
}

int64_t lengthOf(JSValueConst a) {
  int64_t n = 0;
  if (JS_GetLength(run.ctx, a, &n) < 0) return 0;
  return n;
}

// Keeps fn for this screen; its index goes into the LVGL object.
int keepHandler(JSValueConst fn) {
  if (!JS_IsFunction(run.ctx, fn)) return -1;
  const int n = (int)lengthOf(run.handlers);
  JS_SetPropertyUint32(run.ctx, run.handlers, n, JS_DupValue(run.ctx, fn));
  return n;
}

void fire(int idx, int arg) {
  if (!run.ctx || idx < 0) return;
  JSValue fn = JS_GetPropertyUint32(run.ctx, run.handlers, idx);
  JSValue a = JS_NewInt32(run.ctx, arg);
  call(fn, arg >= 0 ? 1 : 0, &a);
  JS_FreeValue(run.ctx, fn);
  scheduleRender();
}

void fireValue(int idx, JSValue v) {  // takes v
  if (!run.ctx || idx < 0) return JS_FreeValue(run.ctx, v);
  JSValue fn = JS_GetPropertyUint32(run.ctx, run.handlers, idx);
  call(fn, 1, &v);
  JS_FreeValue(run.ctx, fn);
  JS_FreeValue(run.ctx, v);
  scheduleRender();
}

void onButton(lv_event_t* e) {
  const intptr_t v = (intptr_t)lv_event_get_user_data(e);
  sound::click();
  fire((int)(v >> 8) - 1, (int)(v & 0xff) == 0xff ? -1 : (int)(v & 0xff));
}
// {quiet: true} (level 2): the button's handler draws nothing by itself (a terminal's keys: a tap is not a redraw;
// the handler calls app.refresh() when it does change the screen).
void onButtonQuiet(lv_event_t* e) {
  const intptr_t v = (intptr_t)lv_event_get_user_data(e);
  sound::click();
  if (!run.ctx) return;
  JSValue fn = JS_GetPropertyUint32(run.ctx, run.handlers, (int)(v >> 8) - 1);
  call(fn);
  JS_FreeValue(run.ctx, fn);
}

void build(lv_obj_t* parent, JSValueConst node, int width);

void children(lv_obj_t* parent, JSValueConst node, int width) {
  JSValue c = prop(node, "c");
  const int64_t n = JS_IsArray(c) ? lengthOf(c) : 0;
  for (int64_t i = 0; i < n && i < 60; i++) {
    JSValue ch = JS_GetPropertyUint32(run.ctx, c, i);
    build(parent, ch, width);
    JS_FreeValue(run.ctx, ch);
  }
  JS_FreeValue(run.ctx, c);
}

void buildOne(lv_obj_t* parent, JSValueConst node, int width, const String& t);

void build(lv_obj_t* parent, JSValueConst node, int width) {
  if (!JS_IsObject(node)) return;
  const String t = propStr(node, "t");
  const uint32_t before = lv_obj_get_child_count(parent);
  // {fill: true} (level 2): the node takes the room its parent has left. In a row, it is built for the width the
  // nodes before it left on the line.
  const bool fill = propBool(node, "fill"), inRow = lv_obj_get_style_flex_flow(parent, 0) != LV_FLEX_FLOW_COLUMN;
  if (fill && inRow && before) {
    lv_obj_update_layout(parent);
    int left = lv_obj_get_content_width(parent);
    for (uint32_t i = 0; i < before; i++) left -= lv_obj_get_width(lv_obj_get_child(parent, i)) + lv_obj_get_style_pad_column(parent, 0);
    if (left > 20 && left < width) width = left;
  }
  auto other = nodeTypes().find(t);
  if (other != nodeTypes().end()) other->second(parent, node, width);
  else buildOne(parent, node, width, t);
  if (fill && lv_obj_get_child_count(parent) > before) {
    lv_obj_t* o = lv_obj_get_child(parent, -1);
    lv_obj_set_flex_grow(o, 1);  // along the parent's flow; across it, the whole room when the parent has a set size
    if (!inRow) {
      lv_obj_set_height(o, 1), lv_obj_set_width(o, lv_pct(100));
      // A row given its height now: its own fill children (built before) take that height too.
      if (lv_obj_get_style_flex_flow(o, 0) != LV_FLEX_FLOW_COLUMN)
        for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
          if (lv_obj_get_style_flex_grow(lv_obj_get_child(o, i), 0)) lv_obj_set_height(lv_obj_get_child(o, i), lv_pct(100));
    } else {
      lv_obj_set_width(o, 1);
      if (lv_obj_get_style_height(parent, 0) != LV_SIZE_CONTENT) lv_obj_set_height(o, lv_pct(100));
    }
  }
}

void buildOne(lv_obj_t* parent, JSValueConst node, int width, const String& t) {
  const Pal& p = pal();
  const uint32_t color = hexColor(propStr(node, "color").c_str(), p.fg);
  if (t == "col" || t == "row" || t == "card") {
    lv_obj_t* o;
    int inner = width;
    if (t == "card") {
      o = cell(parent, 14);
      const String bg = propStr(node, "bg");
      if (bg.length()) lv_obj_set_style_bg_color(o, C(hexColor(bg.c_str(), p.cell)), 0);
      // {half: true}: half the line in landscape (two side by side in a ui.row), the whole line in portrait.
      if (propBool(node, "half") && os::landscape()) lv_obj_set_width(o, (width - 8) / 2), width = (width - 8) / 2;
      inner = width - 28;
      // level 2: {onTap} makes the whole card a button
      JSValue tap = prop(node, "onTap");
      const int th = keepHandler(tap);
      JS_FreeValue(run.ctx, tap);
      if (th >= 0) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(o, C(p.press), LV_STATE_PRESSED);
        lv_obj_add_event_cb(o, onButton, LV_EVENT_CLICKED, (void*)(intptr_t)(((th + 1) << 8) | 0xff));
      }
    } else {
      o = clear(parent);
      lv_obj_set_size(o, lv_pct(100), LV_SIZE_CONTENT);
      if (t == "col" && propBool(node, "scroll")) {  // level 2: a column that scrolls (give it a height: fill or h)
        lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(o, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_ACTIVE);
      }
      const int h = t == "col" ? propInt(node, "h", 0) : 0;  // level 2: a column of a set height (its fill children share it)
      if (h > 0) lv_obj_set_height(o, constrain(h, 20, 2000));
    }
    const bool row = t == "row";
    lv_obj_set_flex_flow(o, row ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(o, propInt(node, "gap", 8), 0);
    const int pad = t == "card" ? 0 : constrain(propInt(node, "pad", 0), 0, 24);  // level 2: room inside a column/row
    if (pad) lv_obj_set_style_pad_all(o, pad, 0), inner -= 2 * pad;
    const bool center = propBool(node, "center"), top = propBool(node, "top");  // level 2: {top} rows align their tops
    lv_obj_set_flex_align(o, center ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
                          row && !top ? LV_FLEX_ALIGN_CENTER : (center ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START),
                          center ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START);
    children(o, node, inner);
    // {scroll, key}: the column stays where it was scrolled when the screen is drawn again (jsui.h)
    if (t == "col" && propBool(node, "scroll") && propStr(node, "key").length()) jsui::keepScroll(o, propStr(node, "key"));
  } else if (t == "title" || t == "text") {
    const bool title = t == "title";
    const int px = constrain(propInt(node, "size", title ? 20 : 15), 12, 24);
    lv_obj_t* l = label(parent, "", px, title || propBool(node, "bold"),
                        hexColor(propStr(node, "color").c_str(), title ? p.fg : (propBool(node, "dim") ? p.dim : p.fg)));
    wrapText(l, propStr(node, "s").c_str(), width - 4, px, title || propBool(node, "bold"), 3);
    if (propBool(node, "center")) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  } else if (t == "big") {
    const int px = propInt(node, "size", 64);
    lv_obj_t* l = label(parent, propStr(node, "s").c_str(), px >= 60 ? 64 : px >= 50 ? 56 : 44, true, color);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  } else if (t == "button") {
    JSValue fn = prop(node, "on");
    const int h = keepHandler(fn);
    JS_FreeValue(run.ctx, fn);
    const uint32_t bg = hexColor(propStr(node, "bg").c_str(), col::IOS_BLUE);
    const uint32_t fg = hexColor(propStr(node, "color").c_str(), 0xffffff);
    lv_obj_t* b = button(parent, propStr(node, "s").c_str(), bg, fg, propBool(node, "quiet") ? onButtonQuiet : onButton,
                         (void*)(intptr_t)(((h + 1) << 8) | 0xff));
    if (propBool(node, "wide")) lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, constrain(propInt(node, "h", 44), 28, 120));  // level 2: h, w
    const int w = propInt(node, "w", 0);
    if (w > 0) lv_obj_set_width(b, std::min(w, width));
    // level 2: onDown / onUp (hold to talk)
    JSValue fd = prop(node, "onDown"), fu = prop(node, "onUp");
    const int hd = keepHandler(fd), hu = keepHandler(fu);
    JS_FreeValue(run.ctx, fd);
    JS_FreeValue(run.ctx, fu);
    if (hd >= 0 || hu >= 0) {
      lv_obj_add_event_cb(b, [](lv_event_t*) { holding = true; }, LV_EVENT_PRESSED, nullptr);
      lv_obj_add_event_cb(b, [](lv_event_t*) { holding = false; }, LV_EVENT_RELEASED, nullptr);
      lv_obj_add_event_cb(b, [](lv_event_t*) { holding = false; }, LV_EVENT_PRESS_LOST, nullptr);
      lv_obj_add_event_cb(b, [](lv_event_t*) { holding = false; }, LV_EVENT_DELETE, nullptr);
    }
    if (hd >= 0) lv_obj_add_event_cb(b, [](lv_event_t* e) { fire((int)(intptr_t)lv_event_get_user_data(e), -1); },
                                     LV_EVENT_PRESSED, (void*)(intptr_t)hd);
    if (hu >= 0) lv_obj_add_event_cb(b, [](lv_event_t* e) { fire((int)(intptr_t)lv_event_get_user_data(e), -1); },
                                     LV_EVENT_RELEASED, (void*)(intptr_t)hu);
    // level 2: an icon (an OS glyph or the app's .a8), alone or before the text
    if (const lv_image_dsc_t* d = jsui::icon(propStr(node, "icon").c_str())) {
      lv_obj_t* l = lv_obj_get_child(b, 0);
      lv_obj_t* g = glyph(b, d, fg);
      if (!propStr(node, "s").length()) {
        if (l) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(g);
      } else {
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW_REVERSE);  // the glyph before the text (the label stays child 0)
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 6, 0);
      }
    }
  } else if (t == "chips") {
    JSValue names = prop(node, "names"), fn = prop(node, "on");
    const int h = keepHandler(fn);
    static std::vector<String> keepNames;  // the chip labels live in the label objects; these only feed kit::chips
    keepNames.clear();
    const int64_t n = JS_IsArray(names) ? lengthOf(names) : 0;
    for (int64_t i = 0; i < n && i < 8; i++) {
      JSValue v = JS_GetPropertyUint32(run.ctx, names, i);
      const char* s = JS_ToCString(run.ctx, v);
      keepNames.push_back(s ? s : "");
      JS_FreeCString(run.ctx, s);
      JS_FreeValue(run.ctx, v);
    }
    std::vector<const char*> ptrs;
    for (const String& s : keepNames) ptrs.push_back(s.c_str());
    // Each bar keeps its own handler (on its chips' events): one static for all made every bar on a screen call
    // the last one's function (Xúc xắc's tabs re-rolled the dice instead of switching).
    lv_obj_t* bar = chips(parent, ptrs.data(), ptrs.size(), propInt(node, "sel", 0), nullptr);
    for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++)
      lv_obj_add_event_cb(lv_obj_get_child(bar, i), [](lv_event_t* e) {
        fire((int)(intptr_t)lv_event_get_user_data(e), (int)lv_obj_get_index((lv_obj_t*)lv_event_get_current_target(e)));
      }, LV_EVENT_CLICKED, (void*)(intptr_t)h);
    JS_FreeValue(run.ctx, names);
    JS_FreeValue(run.ctx, fn);
  } else if (t == "list") {
    JSValue items = prop(node, "items"), fn = prop(node, "on");
    const int h = keepHandler(fn);
    const int64_t n = JS_IsArray(items) ? lengthOf(items) : 0;
    for (int64_t i = 0; i < n && i < 40; i++) {
      JSValue it = JS_GetPropertyUint32(run.ctx, items, i);
      lv_obj_t* r = listRow(parent, propStr(it, "title").c_str(), propStr(it, "right").c_str(), h >= 0 ? onButton : nullptr,
                            (void*)(intptr_t)(((h + 1) << 8) | (int)(i & 0xff)), h >= 0);
      const String sub = propStr(it, "sub");
      if (sub.length()) {
        lv_obj_set_height(r, 58);
        lv_obj_align(lv_obj_get_child(r, 0), LV_ALIGN_TOP_LEFT, 0, 9);
        lv_obj_t* s = label(r, "", 12, false, p.faint);
        fitText(s, sub.c_str(), width - 90, 18, 12, 12);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, -9);
      }
      JS_FreeValue(run.ctx, it);
    }
    JS_FreeValue(run.ctx, items);
    JS_FreeValue(run.ctx, fn);
  } else if (t == "progress") {
    const double v = constrain(propNum(node, "v", 0), 0.0, 1.0);
    lv_obj_t* track = box(parent, p.line, 5);
    lv_obj_set_size(track, lv_pct(100), 10);
    lv_obj_t* fill = box(track, hexColor(propStr(node, "color").c_str(), col::GREEN), 5);
    lv_obj_set_size(fill, lv_pct((int)(v * 100)), 10);
  } else if (t == "spacer") {
    lv_obj_t* s = clear(parent);
    lv_obj_set_size(s, 4, constrain(propInt(node, "h", 8), 0, 200));
  }
}

void renderNow();
void render() {
  onJsStack(renderNow);
}

void renderNow() {
  if (!run.ctx || !run.body) return;
  const uint32_t t0 = millis();
  JSValue global = JS_GetGlobalObject(run.ctx);
  JSValue app = JS_GetPropertyStr(run.ctx, global, "app");
  const String title = propStr(app, "title", run.name.c_str());
  if (run.title) setText(run.title, title.c_str());
  JSValue fn = JS_GetPropertyStr(run.ctx, app, "render");
  if (!run.full && propBool(app, "full")) {  // app.full (level 2): no nav bar; the body is the app's, edge to edge
    run.full = true;
    lv_obj_t* nav = (lv_obj_t*)lv_obj_get_user_data(run.scr);
    if (nav) lv_obj_add_flag(nav, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(run.body, 0, os::SB_H);
    lv_obj_set_size(run.body, os::W(), os::H() - os::SB_H);
    lv_obj_set_style_pad_all(run.body, 0, 0);
    lv_obj_set_style_pad_row(run.body, 0, 0);
    lv_obj_remove_flag(run.body, LV_OBJ_FLAG_SCROLLABLE);
  }
  JS_FreeValue(run.ctx, run.handlers);
  run.handlers = JS_NewArray(run.ctx);
  deadline = millis() + 1500;
  JSValue tree = JS_IsFunction(run.ctx, fn) ? JS_Call(run.ctx, fn, app, 0, nullptr) : JS_UNDEFINED;
  deadline = 0;
  const int32_t sy = lv_obj_get_scroll_y(run.body);
  if (!run.holder) {
    run.holder = clear(run.scr);
    lv_obj_add_flag(run.holder, LV_OBJ_FLAG_HIDDEN);
  }
  for (auto& kv : run.keyed) lv_obj_set_parent(kv.second, run.holder);  // keyed nodes survive the rebuild
  if (run.sheet) lv_obj_delete(run.sheet), run.sheet = nullptr;
  lv_obj_clean(run.body);
  if (JS_IsException(tree)) {
    JSValue e = JS_GetException(run.ctx);
    const char* s = JS_ToCString(run.ctx, e);
    hint(run.body, (String(T_("App error while drawing: ")) + (s ? s : "?")).c_str());
    trace::add("app %s render: %s", run.id.c_str(), s ? s : "?");
    JS_FreeCString(run.ctx, s);
    JS_FreeValue(run.ctx, e);
  } else {
    build(run.body, tree, run.full ? os::W() : os::W() - 24);
  }
  for (auto it = run.keyed.begin(); it != run.keyed.end();) {  // keyed nodes the new tree no longer has
    if (lv_obj_get_parent(it->second) == run.holder) {
      lv_obj_t* o = it->second;
      it = run.keyed.erase(it);
      lv_obj_delete(o);
    } else {
      ++it;
    }
  }
  lv_obj_update_layout(run.body);
  lv_obj_scroll_to_y(run.body, sy, LV_ANIM_OFF);
  if (millis() - t0 >= 60) trace::add("app %s: a slow redraw, %lu ms", run.id.c_str(), (unsigned long)(millis() - t0));
  JS_FreeValue(run.ctx, tree);
  JS_FreeValue(run.ctx, fn);
  JS_FreeValue(run.ctx, app);
  JS_FreeValue(run.ctx, global);
}

void stop() {
  jsnet::stop();  // before the context goes: it holds the pending requests' promise functions
  jsfs::stop();
  jsui::stop();
  jsset::stop();
  jsterm::stop();
  jsvoice::stop();
  for (Timer* t : run.timers) {
    lv_timer_delete(t->t);
    JS_FreeValue(run.ctx, t->fn);
    delete t;
  }
  run.timers.clear();
  if (run.ctx) {
    JS_FreeValue(run.ctx, run.handlers);
    run.handlers = JS_UNDEFINED;
    JS_FreeContext(run.ctx);
    run.ctx = nullptr;
  }
  if (rt) JS_RunGC(rt);
#ifndef SIM
  dropBig();
#endif
  run.prefs.end();
  run.scr = run.body = run.title = run.holder = run.sheet = nullptr;
  run.keyed.clear();
  run.full = false;
}

void addFn(JSContext* ctx, JSValue global, const char* name, JSCFunction* f, int n) {
  JS_SetPropertyStr(ctx, global, name, JS_NewCFunction(ctx, f, name, n));
}

String nsOf(const String& id) {
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < id.length(); i++) h = (h ^ (uint8_t)id[i]) * 16777619u;
  char b[12];
  snprintf(b, sizeof b, "a%08lx", (unsigned long)h);
  return b;
}

std::deque<String> pool;  // app strings the registry hands out (stable pointers)

}  // namespace

uint32_t version() { return pkg::version(); }
String current() { return run.ctx ? run.id : String(); }
bool keepsAwake() { return run.ctx && run.awake; }

void addNode(const char* type, NodeFn fn) { nodeTypes()[type] = fn; }

namespace node {
JSContext* ctx() { return run.ctx; }
String appId() { return run.id; }
String str(JSValueConst o, const char* k, const char* def) { return propStr(o, k, def); }
int num(JSValueConst o, const char* k, int def) { return propInt(o, k, def); }
double real(JSValueConst o, const char* k, double def) { return propNum(o, k, def); }
bool flag(JSValueConst o, const char* k) { return propBool(o, k); }
uint32_t color(JSValueConst o, const char* k, uint32_t def) { return hexColor(propStr(o, k).c_str(), def); }
int handler(JSValueConst o, const char* k) {
  JSValue fn = prop(o, k);
  const int h = keepHandler(fn);
  JS_FreeValue(run.ctx, fn);
  return h;
}
void fire(int h, int arg) { ::jsapp::fire(h, arg); }
void fireText(int h, const char* text, bool redraw) {
  if (!run.ctx || h < 0) return;
  if (redraw) return fireValue(h, JS_NewString(run.ctx, text ? text : ""));
  JSValue fn = JS_GetPropertyUint32(run.ctx, run.handlers, h), v = JS_NewString(run.ctx, text ? text : "");
  call(fn, 1, &v);
  JS_FreeValue(run.ctx, v);
  JS_FreeValue(run.ctx, fn);
}
void build(lv_obj_t* parent, JSValueConst n, int width) { ::jsapp::build(parent, n, width); }
void redraw() { if (run.ctx) scheduleRender(); }
void children(lv_obj_t* parent, JSValueConst n, int width) { ::jsapp::children(parent, n, width); }
lv_obj_t* kept(JSValueConst n) {
  const String k = propStr(n, "key");
  auto it = k.length() ? run.keyed.find(propStr(n, "t") + ":" + k) : run.keyed.end();
  return it != run.keyed.end() && lv_obj_get_parent(it->second) == run.holder ? it->second : nullptr;
}
void keep(JSValueConst n, lv_obj_t* o) {
  const String k = propStr(n, "key");
  if (!k.length()) return;
  const String full = propStr(n, "t") + ":" + k;
  run.keyed[full] = o;
  lv_obj_add_event_cb(o, [](lv_event_t* e) {
    lv_obj_t* me = (lv_obj_t*)lv_event_get_current_target(e);
    for (auto it = run.keyed.begin(); it != run.keyed.end(); ++it)
      if (it->second == me) return (void)run.keyed.erase(it);
  }, LV_EVENT_DELETE, nullptr);
}
lv_obj_t* layer() {  // ui.sheet: a layer over the body, gone at the next render
  if (!run.scr) return nullptr;
  if (!run.sheet) {
    run.sheet = clear(run.scr);
    lv_obj_set_pos(run.sheet, 0, os::SB_H);
    lv_obj_set_size(run.sheet, os::W(), os::H() - os::SB_H);
  }
  return run.sheet;
}
}  // namespace node

// app.selfDraw = true (level 2): answers (net.request, voice) do not redraw by themselves; the app calls app.refresh()
// when one changed the screen. A terminal that polls twice a second would otherwise be rebuilt twice a second.
bool selfDraw() {
  JSValue g = JS_GetGlobalObject(run.ctx), a = JS_GetPropertyStr(run.ctx, g, "app"), v = JS_GetPropertyStr(run.ctx, a, "selfDraw");
  const bool on = JS_ToBool(run.ctx, v) > 0;
  JS_FreeValue(run.ctx, v), JS_FreeValue(run.ctx, a), JS_FreeValue(run.ctx, g);
  return on;
}
bool invoke(JSValueConst fn, int argc, JSValueConst* argv) {
  if (!run.ctx) return false;
  const bool ok = call(fn, argc, (JSValue*)argv);
  if (run.ctx && !selfDraw()) scheduleRender();
  return ok;
}

void stackSize(size_t n, size_t under) {  // dev
#ifndef SIM
  if (!onBig && !run.ctx) dropBig();
#endif
  BIG = n;
  if (under) UNDER = under;
}

void open(const char* id, const char* arg) {
  // The package's main.js (from the internal flash or the memory card).
  const pkg::Package* p = pkg::find(id);
  pkg::File f;
  uint8_t* text = p && pkg::open(id, "main.js", f) && f.size() < 512 * 1024 ? pkg::loadAll(f) : nullptr;
  if (!text) {
    os::toast(p && !p->available ? N_("Needs the memory card") : N_("This app is not on the device. Install it again from the Store."));
    return;
  }
  String main;
  main.concat((const char*)text, f.size());
  heap_caps_free(text);
  if (run.ctx) stop();
  if (!bigReady()) {  // no room for QuickJS's stack now (it would overflow the UI's): not opened
    trace::add("js: %s not opened, internal RAM %u KB", id, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10));
    os::toast(N_("The device is busy. Open it again in a moment."));
    return;
  }
  if (!rt) {
    static const JSMallocFunctions mf = {jsCalloc, jsMalloc, jsFree, jsRealloc, jsUsable};
    rt = JS_NewRuntime2(&mf, nullptr);
    JS_SetMemoryLimit(rt, 1536 * 1024);
#ifdef SIM
    // The computer's stack frames are several times the board's and onJsStack runs on the host's own stack here:
    // no QuickJS limit (the board's 11 KB is checked on the board or in QEMU).
    JS_SetMaxStackSize(rt, 0);
#else
    JS_SetMaxStackSize(rt, BIG - UNDER);
#endif
    JS_SetInterruptHandler(rt, interrupt, nullptr);
    JS_SetHostPromiseRejectionTracker(rt, onRejection, nullptr);
  }
  run.id = id;
  run.name = p->name;
  run.sound = run.notify = run.awake = false;
  for (const String& perm : p->perms) {
    if (perm == "sound") run.sound = true;
    if (perm == "notify") run.notify = true;
    if (perm == "screen.awake") run.awake = true;
  }
  run.prefs.begin(nsOf(run.id).c_str(), false);
  run.ctx = JS_NewContext(rt);
  run.handlers = JS_NewArray(run.ctx);
  JSValue g = JS_GetGlobalObject(run.ctx);
  addFn(run.ctx, g, "__refresh", nRefresh, 0);
  addFn(run.ctx, g, "__timer", nTimer, 3);
  addFn(run.ctx, g, "__cancel", nCancel, 1);
  addFn(run.ctx, g, "__sound", nSound, 1);
  addFn(run.ctx, g, "__notify", nNotify, 2);
  addFn(run.ctx, g, "__toast", nToast, 1);
  addFn(run.ctx, g, "__log", nLog, 1);
  addFn(run.ctx, g, "__get", nGet, 1);
  addFn(run.ctx, g, "__set", nSet, 2);
  JS_FreeValue(run.ctx, g);
  jsnet::install(run.ctx, *p);
  jsfs::install(run.ctx, *p);
  jsset::install(run.ctx, *p);  // settings.get/open, app.onSettings (lane 1e)
  jsterm::install(run.ctx, *p);  // ui.term + term.* (jsterm.h)
  jsvoice::install(run.ctx, *p);  // voice.dictate / voice.stop (permission "dictate", jsvoice.h)
  lv_obj_t* body;
  run.scr = os::screen(run.name.c_str(), &body, os::ROOT);
  run.body = body;
  kb::onRoom(keyboardRoom);
  run.title = os::navTitle(run.scr);
  lv_obj_add_event_cb(run.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == run.scr) stop();
  }, LV_EVENT_DELETE, nullptr);
  // app.onBack (level 2): the back gesture goes to the app first; a true answer means it went back inside itself.
  os::setBackHook(run.scr, [] {
    if (!run.ctx) return false;
    if (kbMine()) return kb::close(), true;  // back puts the keyboard away first
    JSValue g = JS_GetGlobalObject(run.ctx), a = JS_GetPropertyStr(run.ctx, g, "app");
    JSValue fn = JS_GetPropertyStr(run.ctx, a, "onBack");
    bool handled = false;
    if (JS_IsFunction(run.ctx, fn)) {
      deadline = millis() + 1500;
      JSValue r = JS_UNDEFINED;
      onJsStack([&] {
        r = JS_Call(run.ctx, fn, a, 0, nullptr);
        if (!JS_IsException(r)) runJobs();
      });
      deadline = 0;
      if (JS_IsException(r)) report(run.ctx, " (onBack)");
      else handled = JS_ToBool(run.ctx, r) > 0;
      JS_FreeValue(run.ctx, r);
      if (handled) scheduleRender();
    }
    JS_FreeValue(run.ctx, fn);
    JS_FreeValue(run.ctx, a);
    JS_FreeValue(run.ctx, g);
    return handled;
  });
  // The prelude, then the app's own script (with its time budget).
  const uint32_t t0 = millis();
  for (int k = 0; k < 2; k++) {
    const char* src = k ? main.c_str() : PRELUDE;
    if (k) {  // app.lang: the language the device shows ("en", "vi"... docs/i18n.md), for the app's own texts
      JSValue g = JS_GetGlobalObject(run.ctx), a = JS_GetPropertyStr(run.ctx, g, "app");
      JS_SetPropertyStr(run.ctx, a, "lang", JS_NewString(run.ctx, i18n::lang()));
      JS_SetPropertyStr(run.ctx, a, "dark", JS_NewBool(run.ctx, kit::pal().dark));  // level 2: the theme now
      // app.screen: the room the app draws in (the OS opens the app again after the screen turns).
      JSValue sc = JS_NewObject(run.ctx);
      JS_SetPropertyStr(run.ctx, sc, "w", JS_NewInt32(run.ctx, os::W()));
      JS_SetPropertyStr(run.ctx, sc, "h", JS_NewInt32(run.ctx, os::H()));
      JS_SetPropertyStr(run.ctx, sc, "landscape", JS_NewBool(run.ctx, os::landscape()));
      JS_SetPropertyStr(run.ctx, a, "screen", sc);
      JS_SetPropertyStr(run.ctx, a, "kb", JS_NewInt32(run.ctx, 0));
      JS_SetPropertyStr(run.ctx, a, "closeKeyboard", JS_NewCFunction(run.ctx, nKbClose, "closeKeyboard", 0));
      JS_FreeValue(run.ctx, a);
      JS_FreeValue(run.ctx, g);
    }
    deadline = millis() + 3000;
    JSValue r = JS_UNDEFINED;
    onJsStack([&] {
      r = JS_Eval(run.ctx, src, strlen(src), k ? id : "<prelude>", JS_EVAL_TYPE_GLOBAL);
      if (k && !JS_IsException(r)) runJobs();
    });
    deadline = 0;
    if (JS_IsException(r)) {
      JSValue e = JS_GetException(run.ctx);
      const char* s = JS_ToCString(run.ctx, e);
      trace::add("app %s start (%s): %s", id, k ? "main.js" : "prelude", s ? s : "?");
      JSValue stk = JS_GetPropertyStr(run.ctx, e, "stack");
      const char* st = JS_ToCString(run.ctx, stk);
      if (st) trace::add("  %.120s", st);
      JS_FreeCString(run.ctx, st);
      JS_FreeValue(run.ctx, stk);
      hint(run.body, (String(T_("The app cannot run: ")) + (s ? s : "?")).c_str());
      JS_FreeCString(run.ctx, s);
      JS_FreeValue(run.ctx, e);
      JS_FreeValue(run.ctx, r);
      os::open(run.scr);
      return;
    }
    JS_FreeValue(run.ctx, r);
    if (!k) {
      // app.arg: what the app was opened with (a notification's arg), undefined when nothing.
      JSValue g2 = JS_GetGlobalObject(run.ctx);
      JSValue a2 = JS_GetPropertyStr(run.ctx, g2, "app");
      JS_SetPropertyStr(run.ctx, a2, "arg", arg && *arg ? JS_NewString(run.ctx, arg) : JS_UNDEFINED);
      JS_FreeValue(run.ctx, a2);
      JS_FreeValue(run.ctx, g2);
    }
  }
  const uint32_t t1 = millis();
  render();
  trace::add("app %s started: main.js %u KB ran in %lu ms, first screen %lu ms", id, (unsigned)(main.length() >> 10),
             (unsigned long)(t1 - t0), (unsigned long)(millis() - t1));
  os::open(run.scr);
}

}  // namespace jsapp
