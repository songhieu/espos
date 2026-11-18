// Java (J2ME) games on the device: engine "j2me" of packages (src/pkg/pkg.cpp). The design: mockups/s3-j2me.html.
//   - The game's 240x320 screen on top (full screen: no status bar), the keypad under it: soft keys and the menu
//     (⋯) on the first row, the arrows with OK on the left, 0-9 * # on the right.
//   - The VM (src/j2me/jvm.h) runs on a task of its own; the game's frames come back through MidpHost::flush into a
//     back buffer, which the UI thread copies into the picture LVGL shows (only the changed rows).
//   - The class library is the runtime package dev.espos.j2me (classlib.jar): it is too big for the firmware slot.
//   - Record stores (saved games, scores) live next to the package: the card's or Máy's data folder.
#include <lvgl.h>

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "../apps.h"
#include "../desk.h"
#include "../home.h"
#include "../fonts.h"
#include "../os.h"
#include "../pkg/pkg.h"
#include "../sound.h"
#include "../trace.h"
#include "../uikit.h"
#include "jvm.h"
#include "midp.h"
#include "midp_draw.h"

#include <Arduino.h>  // heap_caps_* (the simulator's shim too)
extern "C" {
}

#ifdef SIM
#include <chrono>
#include <thread>
extern std::string simDir;
#else
#include <condition_variable>
#include <deque>

#include "freertos/idf_additions.h"
#endif

namespace j2me_app {

void open(const char* pkgId, const char* arg);

namespace {

constexpr const char* RUNTIME_ID = "dev.espos.j2me";
// The game's screen: 240x320 unless the game asked for another size (Native.screenHint, remembered in its data
// folder as "screen"): centred across the top; the keypad under it. At most 320 wide and 330 high (the keypad needs
// the rest).
constexpr int GAME_MAX_W = 320, GAME_MAX_H = 330;
constexpr uint32_t BG = 0x0d0f13, KEY = 0x2a2e37, KEY_ON = 0x3a3f4b, INK = 0xe6e8ee, ACC = 0xff7a00;

struct Game {
  std::string id, name;
  std::string tzMin = "420";  // minutes east of UTC (setting sys.tz_min), read on the UI thread
  // the jars (PSRAM), owned by the game until its VM is gone
  uint8_t* classlib = nullptr;
  uint32_t classlibLen = 0;
  uint8_t* jar = nullptr;
  uint32_t jarLen = 0;
  j2me::MidpHost host;
  // frames: the VM writes back565 (under mu) and marks the rows; the UI copies them into front565
  std::mutex mu;
  uint16_t* back565 = nullptr;
  uint16_t* front565 = nullptr;  // LVGL shows it (freed with the screen)
  int gw = 240, gh = 320;    // the game's screen
  int dirtyY1 = 320, dirtyY2 = 0;
  int hintW = 0, hintH = 0;  // the game asked for this size (under mu)
  std::string dataDir;       // the game's data folder (record stores in rms/, the screen size in "screen")
  size_t heapLimit = 3 * 1024 * 1024;
  std::string softL, softR;
  bool softChanged = false;
  std::atomic<bool> leaving{false};  // the screen is gone: the game gets 1.5 s to save (destroyApp), then stops
  std::atomic<bool> ended{false};    // the game ended (notifyDestroyed, an error): the UI closes the screen
  std::atomic<int> refs{2};          // the UI and the VM's task: the last one to let go frees the game
  std::string failure;               // why it could not run (under mu)
  bool muted = false;
  // UI (only the UI thread touches these)
  lv_obj_t *scr = nullptr, *img = nullptr, *softLeft = nullptr, *softRight = nullptr, *menu = nullptr;
  int scale = 256;  // landscape: a game too wide for 100 px of keys a side is drawn smaller (LVGL's 1/256 steps)
  lv_image_dsc_t dsc{};
  lv_timer_t* timer = nullptr;
};

Game* cur = nullptr;  // the game on screen (UI thread)
std::atomic<int> vmsRunning{0};  // VMs whose task has not finished (a game reopened waits for the old one)
std::string reopenId;            // a game closed to be opened again at the size it asked for

#ifndef SIM
// ---- the UI thread's helper for the VM tasks (their stacks are in PSRAM)
// File jobs: a task whose stack is in PSRAM must not touch flash (LittleFS, NVS: the cache is off meanwhile), so the
// record stores' file work runs here while the VM waits. Finished tasks: deleted from here (vTaskDeleteWithCaps
// must not be called by the task itself).
struct FileJob {
  const std::function<bool()>* fn;
  bool done = false, ok = false;
};
std::mutex jobMu;
std::condition_variable jobCv;
std::deque<FileJob*> jobs;                // waiting (under jobMu)
std::vector<TaskHandle_t> finished;       // tasks that ended and suspended themselves (under jobMu)
std::atomic<int> liveTasks{0};
lv_timer_t* helper = nullptr;

void helperTick(lv_timer_t*) {
  std::deque<FileJob*> todo;
  std::vector<TaskHandle_t> reap, keep;
  {
    std::lock_guard<std::mutex> l(jobMu);
    todo.swap(jobs);
    for (TaskHandle_t h : finished) (eTaskGetState(h) == eSuspended ? reap : keep).push_back(h);
    finished.swap(keep);
  }
  for (FileJob* j : todo) {
    const bool ok = (*j->fn)();
    {
      std::lock_guard<std::mutex> l(jobMu);
      j->ok = ok, j->done = true;
    }
    jobCv.notify_all();
  }
  for (TaskHandle_t h : reap) vTaskDeleteWithCaps(h), liveTasks--;
  std::lock_guard<std::mutex> l(jobMu);
  if (liveTasks == 0 && jobs.empty() && finished.empty() && helper) lv_timer_delete(helper), helper = nullptr;
}

bool fileJobOnUi(const std::function<bool()>& fn) {
  FileJob j{&fn};
  std::unique_lock<std::mutex> l(jobMu);
  jobs.push_back(&j);
  if (!jobCv.wait_for(l, std::chrono::seconds(3), [&] { return j.done; })) {
    auto it = std::find(jobs.begin(), jobs.end(), &j);
    if (it != jobs.end()) {  // never taken (the UI is stuck): given up
      jobs.erase(it);
      return false;
    }
    jobCv.wait(l, [&] { return j.done; });  // taken: it is running now, wait for it
  }
  return j.ok;
}
#endif

// ---- tones (Manager.playTone, ToneControl, Nokia Sound): one note at a time, the newest wins, on the system's
// 16 kHz stream (sound.h). The generator only reads these numbers on the sound task.
std::atomic<uint32_t> toneStep{0};  // phase step a sample (2^32 = one cycle)
std::atomic<int> toneLeft{0};       // samples still to play
std::atomic<int> toneAmp{0};
uint32_t tonePhase = 0;
int toneAt = 0;  // samples played of this note (the attack)

int tonePull(short* out, int n, void*) {
  int left = toneLeft.load();
  if (left <= 0) return 0;
  const int k = std::min(n, left);
  const uint32_t step = toneStep.load();
  const int amp = toneAmp.load();
  for (int i = 0; i < k; i++) {
    // a square wave with 2 ms in and out, so notes do not click
    const int ramp = std::min(std::min(toneAt + i, left - i), 32);
    out[i] = (short)(((tonePhase & 0x80000000u) ? amp : -amp) * ramp / 32);
    tonePhase += step;
  }
  toneAt += k;
  toneLeft -= k;
  return k;
}

void playTone(int note, int ms, int volume) {
  if (note < 0 || ms <= 0 || volume <= 0 || (cur && cur->muted)) {
    toneLeft = 0;
    return;
  }
  const double freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
  toneStep = (uint32_t)(freq * 4294967296.0 / 16000.0);
  toneAmp = 40 * std::min(volume, 100);  // at most ~4000 of 32767: as loud as the OS's own sounds
  tonePhase = 0;
  toneAt = 0;
  toneLeft = ms * 16;
  if (!sound::streaming()) sound::stream(tonePull, nullptr);
}

void release(Game* g) {
  if (--g->refs == 0) {
    heap_caps_free(g->back565);
    delete g;
  }
}

inline uint16_t to565(uint32_t c) { return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F)); }

void* psram(size_t n) { return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM); }

bool readAll(const char* id, const char* name, uint8_t*& out, uint32_t& len) {
  pkg::File f;
  if (!pkg::open(id, name, f) || !f.size()) return false;
  out = (uint8_t*)psram(f.size());
  if (!out) return false;
  if (!f.read(0, out, f.size())) {
    heap_caps_free(out);
    out = nullptr;
    return false;
  }
  len = f.size();
  return true;
}

std::string dataDir(const pkg::Package* p) {
#ifdef SIM
  return simDir + (p && p->vol == pkg::SD ? "/sd/espos/data/" : "/fs/data/") + p->id.c_str() + "/";
#else
  return std::string(p && p->vol == pkg::SD ? "/sdcard/espos/data/" : "/littlefs/data/") + p->id.c_str() + "/";
#endif
}

// MANIFEST.MF attributes (continuation lines start with a space)
void parseManifest(const std::vector<uint8_t>& mf, std::map<std::string, std::string>& out) {
  std::string text(mf.begin(), mf.end()), last;
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(p, e - p);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == ' ' && !last.empty()) {
      out[last] += line.substr(1);
    } else {
      const size_t c = line.find(':');
      if (c != std::string::npos) {
        last = line.substr(0, c);
        size_t v = c + 1;
        while (v < line.size() && line[v] == ' ') v++;
        out[last] = line.substr(v);
      }
    }
    p = e + 1;
  }
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- the VM's task
void sleepMs(uint32_t ms) {
#ifdef SIM
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#else
  vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
#endif
}

void runGame(Game* g) {
  {
    j2me::Vm vm([g](const std::string& s) { trace::add("j2me %s: %s", g->id.c_str(), s.c_str()); });
    vm.heapLimit = g->heapLimit;
    vm.stackSlots = 4096;  // 32 KB a Java thread (+ its frames): old games do not recurse deep; PSRAM is short
    vm.allowExit = false;
    vm.setProperty("microedition.platform", "EspOS-S3");
    vm.setProperty("microedition.profiles", "MIDP-2.0");
    vm.setProperty("microedition.configuration", "CLDC-1.1");
    vm.setProperty("microedition.locale", "vi-VN");
    vm.setProperty("com.espos.tz.offset", g->tzMin);  // Calendar/Date in the device's time zone
    vm.addJar(g->classlib, g->classlibLen, false);
    vm.addJar(g->jar, g->jarLen, false);
    std::vector<uint8_t> mf;
    std::string midlet;
    if (vm.resource("META-INF/MANIFEST.MF", mf)) {
      parseManifest(mf, g->host.props);
      // MIDlet-1: name, icon, class
      const std::string m1 = g->host.props["MIDlet-1"];
      const size_t c1 = m1.find(','), c2 = c1 == std::string::npos ? c1 : m1.find(',', c1 + 1);
      if (c2 != std::string::npos) midlet = trim(m1.substr(c2 + 1));
    }
    for (char& c : midlet)
      if (c == '.') c = '/';
    j2me::registerMidp(vm, g->host);
    if (midlet.empty() || !vm.startMain("javax/microedition/midlet/Runner", {midlet})) {
      std::lock_guard<std::mutex> l(g->mu);
      g->failure = midlet.empty() ? std::string(T_("The game has no MIDlet-1 in MANIFEST.MF"))
                                  : std::string(i18n::fmt(N_("Cannot open class {0}"), midlet.c_str()).c_str());
    } else {
      trace::add("j2me: %s starts %s", g->id.c_str(), midlet.c_str());
      uint64_t stopAt = 0;
      while (!g->ended) {
        if (g->leaving && !stopAt) stopAt = vm.nowUs() + 1500000;  // its destroyApp may save first
        if (stopAt && vm.nowUs() > stopAt) break;
        if (!vm.step(8000)) break;
        // let the other tasks on this core run; sleep while every Java thread waits
        sleepMs(vm.idleUs == 0 ? 1 : std::min<uint32_t>(20, vm.idleUs == j2me::Vm::kForever ? 20 : vm.idleUs / 1000));
      }
      vm.stop();
      if (vm.uncaught && !g->leaving) {
        std::lock_guard<std::mutex> l(g->mu);
        if (g->failure.empty()) g->failure = T_("The game hit an error and stopped");
      }
      trace::add("j2me: %s ends (heap %u KB)", g->id.c_str(), (unsigned)(vm.heapBytes() / 1024));
    }
  }  // the VM is gone: the jars may go
  heap_caps_free(g->classlib);
  heap_caps_free(g->jar);
  g->classlib = g->jar = nullptr;
  g->ended = true;
  vmsRunning--;
  release(g);
}

#ifndef SIM
void vmTask(void* arg) {
  runGame((Game*)arg);
  {
    std::lock_guard<std::mutex> l(jobMu);
    finished.push_back(xTaskGetCurrentTaskHandle());  // the UI thread deletes it (helperTick)
  }
  vTaskSuspend(nullptr);
}
#endif

// ---------------------------------------------------------------- UI
void onTimer(lv_timer_t* t) {
  Game* g = (Game*)lv_timer_get_user_data(t);
  int y1, y2;
  std::string l, r, fail;
  bool soft = false;
  int hw = 0, hh = 0;
  {
    std::lock_guard<std::mutex> lk(g->mu);
    y1 = g->dirtyY1, y2 = g->dirtyY2;
    if (y1 < y2) memcpy(g->front565 + y1 * g->gw, g->back565 + y1 * g->gw, (size_t)(y2 - y1) * g->gw * 2);
    g->dirtyY1 = g->gh, g->dirtyY2 = 0;
    hw = g->hintW, hh = g->hintH;
    g->hintW = g->hintH = 0;
    if (g->softChanged) l = g->softL, r = g->softR, soft = true, g->softChanged = false;
    fail = g->failure;
  }
  if (y1 < y2) {
    lv_area_t a;
    lv_obj_get_coords(g->img, &a);
    a.y2 = a.y1 + y2 - 1;
    a.y1 += y1;
    lv_obj_invalidate_area(g->img, &a);
  }
  if (soft) {
    // always there: full-screen games draw their own labels and wait for the soft keys (-6, -7)
    lv_label_set_text(lv_obj_get_child(g->softLeft, 0), l.empty() ? T_("Left key") : l.c_str());
    lv_label_set_text(lv_obj_get_child(g->softRight, 0), r.empty() ? T_("Right key") : r.c_str());
  }
  // The game's Canvas says it was made for another screen (a port that ignores the real size): remembered for this
  // game, and the game opens again at that size once its VM is gone.
  if (hw && (hw != g->gw || hh != g->gh) && hw <= GAME_MAX_W && hh <= GAME_MAX_H) {
    mkdir(g->dataDir.c_str(), 0755);
    if (FILE* f = fopen((g->dataDir + "screen").c_str(), "w")) {
      fprintf(f, "%dx%d\n", hw, hh);
      fclose(f);
      trace::add("j2me: %s wants %dx%d, reopening", g->id.c_str(), hw, hh);
      // i18n: {0} is the game's screen size, e.g. 176×208
      os::toast(i18n::fmt(N_("The game uses a {0} screen; opening it again to fit"),
                          String(hw) + "\u00d7" + String(hh)).c_str());
      reopenId = g->id;
      lv_timer_create([](lv_timer_t* t) {
        if (vmsRunning > 0) return;  // the old game's VM still holds its memory
        lv_timer_delete(t);
        if (!reopenId.empty()) open(reopenId.c_str(), nullptr), reopenId.clear();
      }, 100, nullptr);
      lv_timer_delete(g->timer);
      g->timer = nullptr;
      os::back();
      return;
    }
  }
  if (g->ended) {
    if (!fail.empty()) os::toast(fail.c_str());
    lv_timer_delete(g->timer);
    g->timer = nullptr;
    os::back();  // closes the screen (onDelete cleans up)
  }
}

void onKey(lv_event_t* e) {
  if (!cur) return;
  const int code = (int)(intptr_t)lv_event_get_user_data(e);
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_PRESSED) cur->host.key(code, true);
  else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) cur->host.key(code, false);
}

void onTouch(lv_event_t* e) {
  if (!cur) return;
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t a;
  lv_obj_get_coords(cur->img, &a);
  const int sc = cur->scale;  // drawn smaller in landscape: back to the game's own pixels
  const int x = std::max(0, std::min(cur->gw - 1, (int)((p.x - a.x1) * 256 / sc))),
            y = std::max(0, std::min(cur->gh - 1, (int)((p.y - a.y1) * 256 / sc)));
  const lv_event_code_t c = lv_event_get_code(e);
  cur->host.post(c == LV_EVENT_PRESSED ? 4 : c == LV_EVENT_PRESSING ? 6 : 5, x, y);
}

lv_obj_t* keyBox(lv_obj_t* parent, int x, int y, int w, int h, int radius, uint32_t bg, const char* text, int code) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, radius, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg == ACC ? 0xff9a40 : KEY_ON), LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(b, 2, LV_STATE_PRESSED);
  lv_obj_set_style_outline_color(b, lv_color_hex(ACC), LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  if (text) {
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, i18n::trl(text));       // N_ marked labels
    lv_obj_set_style_text_font(l, fonts::get(14), 0);  // with the language font's letters
    lv_obj_set_style_text_color(l, lv_color_hex(INK), 0);
    lv_obj_center(l);
  }
  if (code)
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
      lv_obj_add_event_cb(b, onKey, c, (void*)(intptr_t)code);
  return b;
}

void closeMenu() {
  if (!cur || !cur->menu) return;
  lv_obj_delete(cur->menu);
  cur->menu = nullptr;
  cur->host.post(8);  // shown again
}

void openMenu(lv_event_t*) {
  if (!cur || cur->menu) return;
  cur->host.post(7);  // paused while the menu is up
  lv_obj_t* m = lv_obj_create(cur->scr);
  lv_obj_remove_style_all(m);
  lv_obj_set_size(m, os::W(), os::H());
  lv_obj_set_style_bg_color(m, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(m, 115, 0);
  lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(m, [](lv_event_t* e) {
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) closeMenu();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sheet = lv_obj_create(m);
  lv_obj_remove_style_all(sheet);
  lv_obj_set_size(sheet, os::W(), LV_SIZE_CONTENT);
  lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(sheet, lv_color_white(), 0);
  lv_obj_set_style_radius(sheet, 18, 0);
  lv_obj_set_style_pad_all(sheet, 14, 0);
  lv_obj_set_style_pad_bottom(sheet, 30, 0);
  lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(sheet, 4, 0);
  lv_obj_t* title = lv_label_create(sheet);
  lv_label_set_text(title, cur->name.c_str());
  lv_obj_set_style_text_font(title, fonts::get(14, true), 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x16181d), 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(title, lv_pct(100));
  auto row = [&](const char* text, uint32_t color, lv_event_cb_t cb) {
    lv_obj_t* r = lv_obj_create(sheet);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(r, lv_color_hex(0xeef0f3), LV_STATE_PRESSED);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = lv_label_create(r);
    lv_label_set_text(l, i18n::trl(text));
    lv_obj_set_style_text_font(l, fonts::get(14), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, nullptr);
  };
  row(N_("Resume"), 0x16181d, [](lv_event_t*) { closeMenu(); });
  row(cur->muted ? N_("Sound: off") : N_("Sound: on"), 0x16181d, [](lv_event_t*) {
    if (!cur) return;
    cur->muted = !cur->muted;
    closeMenu();
  });
  row(N_("Quit game"), 0xe5484d, [](lv_event_t*) {
    if (!cur) return;
    closeMenu();
    os::back();  // onDelete asks the game to leave (destroyApp), then the VM stops
  });
  cur->menu = m;
}

void onDelete(lv_event_t* e) {
  Game* g = (Game*)lv_event_get_user_data(e);
  if (g->timer) lv_timer_delete(g->timer), g->timer = nullptr;
  if (cur == g) cur = nullptr;
  heap_caps_free(g->front565);  // its picture went with the screen
  g->front565 = nullptr;
  toneLeft = 0;     // no note after the game
  g->host.post(9);  // the user leaves: the game's destroyApp runs (it may save), then the VM stops
  g->leaving = true;
  release(g);
}

// The arrows' pictures: A8 triangles, made once (tinted by kit::glyph)
constexpr int AR = 14;
uint8_t arrowBits[4][AR * AR];
lv_image_dsc_t arrowDsc[4];
const lv_image_dsc_t* arrow(int dir) {  // 0 up, 1 down, 2 left, 3 right
  static bool made = false;
  if (!made) {
    made = true;
    for (int d = 0; d < 4; d++) {
      for (int y = 0; y < AR; y++)
        for (int x = 0; x < AR; x++) {
          // up: apex at the top centre; the others are this one turned
          int u = x, v = y;
          if (d == 1) v = AR - 1 - y;
          if (d == 2) u = y, v = x;
          if (d == 3) u = y, v = AR - 1 - x;
          const float cx = (AR - 1) / 2.0f, half = (v + 1) * 0.55f;
          const float dist = half - (u > cx ? u - cx : cx - u);
          const int a = v < 2 || v > AR - 3 ? 0 : dist >= 1 ? 255 : dist <= 0 ? 0 : (int)(dist * 255);
          arrowBits[d][y * AR + x] = (uint8_t)a;
        }
      lv_image_dsc_t& ds = arrowDsc[d];
      ds = lv_image_dsc_t{};
      ds.header.magic = LV_IMAGE_HEADER_MAGIC;
      ds.header.cf = LV_COLOR_FORMAT_A8;
      ds.header.w = AR;
      ds.header.h = AR;
      ds.header.stride = AR;
      ds.data_size = AR * AR;
      ds.data = arrowBits[d];
    }
  }
  return &arrowDsc[dir];
}

// Landscape: the game in the middle, the left soft key, the arrows and OK on the left, the menu, the right soft key
// and the number keys on the right (each side as wide as the game leaves, at least 100).
void buildSideKeys(Game* g, int side) {
  lv_obj_t* s = g->scr;
  const int x1 = os::W() - side, kw0 = side - 16;
  g->softLeft = keyBox(s, 8, 10, kw0, 28, 14, KEY, N_("Left key"), -6);
  g->softRight = keyBox(s, x1 + 8, 10, kw0, 28, 14, KEY, N_("Right key"), -7);
  for (lv_obj_t* k : {g->softLeft, g->softRight}) {  // a long command label: smaller, then wrapped, never cut
    lv_obj_t* l = lv_obj_get_child(k, 0);
    lv_obj_set_width(l, kw0 - 6);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_t* m = keyBox(s, x1 + (side - 44) / 2, 46, 44, 26, 13, KEY, "\u2022\u2022\u2022", 0);
  lv_obj_add_event_cb(m, openMenu, LV_EVENT_CLICKED, nullptr);
  const int c = std::min(40, (side - 10) / 3), dx = (side - 3 * c - 2) / 2, dy = (os::H() - 3 * c - 2) / 2 + 16;
  struct {
    int x, y, dir, code;
  } arrows[4] = {{dx + c + 1, dy, 0, -1}, {dx, dy + c + 1, 2, -3}, {dx + 2 * c + 2, dy + c + 1, 3, -4},
                 {dx + c + 1, dy + 2 * c + 2, 1, -2}};
  for (auto& a : arrows) {
    lv_obj_t* b = keyBox(s, a.x, a.y, c, c, 8, KEY, nullptr, a.code);
    lv_obj_center(kit::glyph(b, arrow(a.dir), 0xc9ced8));
  }
  keyBox(s, dx + c + 1, dy + c + 1, c, c, c / 2, ACC, nullptr, -5);
  static const char* LBL[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
  static const int CODE[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
  const int gap = 4, kw = (side - 16 - 2 * gap) / 3, ky = 84, kh = std::min(38, (os::H() - ky - 10 - 3 * gap) / 4);
  for (int i = 0; i < 12; i++)
    keyBox(s, x1 + 8 + (i % 3) * (kw + gap), ky + (i / 3) * (kh + gap), kw, kh, 7, KEY, LBL[i], CODE[i]);
}

void buildKeypad(Game* g) {
  lv_obj_t* s = g->scr;
  const int y0 = g->gh + 6;
  g->softLeft = keyBox(s, 10, y0, 96, 26, 13, KEY, N_("Left key"), -6);
  g->softRight = keyBox(s, os::W() - 106, y0, 96, 26, 13, KEY, N_("Right key"), -7);
  for (lv_obj_t* k : {g->softLeft, g->softRight}) {  // a long command label: smaller, then wrapped, never cut
    lv_obj_t* l = lv_obj_get_child(k, 0);
    lv_obj_set_width(l, 90);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_t* m = keyBox(s, (os::W() - 44) / 2, y0, 44, 26, 13, KEY, "\u2022\u2022\u2022", 0);
  lv_obj_add_event_cb(m, openMenu, LV_EVENT_CLICKED, nullptr);
  // the arrows and OK (codes -1 up, -2 down, -3 left, -4 right, -5 OK)
  const int dx = 16, dy = y0 + 32, c = 38;
  struct {
    int x, y, dir, code;
  } arrows[4] = {{dx + c + 1, dy, 0, -1}, {dx, dy + c + 1, 2, -3}, {dx + 2 * c + 2, dy + c + 1, 3, -4},
                 {dx + c + 1, dy + 2 * c + 2, 1, -2}};
  for (auto& a : arrows) {
    lv_obj_t* b = keyBox(s, a.x, a.y, c, c, 8, KEY, nullptr, a.code);
    lv_obj_center(kit::glyph(b, arrow(a.dir), 0xc9ced8));
  }
  keyBox(s, dx + c + 1, dy + c + 1, c, c, c / 2, ACC, nullptr, -5);
  // 1-9 * 0 #
  static const char* LBL[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
  static const int CODE[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
  const int kw = 44, kh = 27, gap = 4, kx = os::W() - 14 - 3 * kw - 2 * gap;
  for (int i = 0; i < 12; i++)
    keyBox(s, kx + (i % 3) * (kw + gap), dy + (i / 3) * (kh + gap), kw, kh, 7, KEY, LBL[i], CODE[i]);
}

}  // namespace

void open(const char* pkgId, const char* arg) {
  (void)arg;
  if (cur && lv_obj_is_valid(cur->scr) && lv_screen_active() == cur->scr) return;  // already in front
  const pkg::Package* p = pkg::find(pkgId);
  if (!p) return;
#ifndef SIM
  desk::releaseConnection();  // the kept HTTPS connection holds internal RAM; a sync reconnects when it needs to
#endif
  home::releasePictures();  // PSRAM for the game
  if (!j2me::draw::prepareFonts()) trace::add("j2me: no fonts");
  Game* g = new Game();
  g->id = pkgId;
  g->name = p->name.c_str();
  g->tzMin = desk::setting("sys.tz_min", "420").c_str();
  if (!readAll(RUNTIME_ID, "classlib.jar", g->classlib, g->classlibLen)) {
    delete g;
    os::toast(N_("Install the Java game runtime first"));
    apps::open("store", RUNTIME_ID);
    return;
  }
  if (!readAll(pkgId, "game.jar", g->jar, g->jarLen)) {
    heap_caps_free(g->classlib);
    delete g;
    os::toast(N_("Cannot read this game"));
    return;
  }
  // the size this game uses (it asked for one before: remembered), and where its data goes
  g->dataDir = dataDir(p);
  if (FILE* f = fopen((g->dataDir + "screen").c_str(), "r")) {
    int w = 0, h = 0;
    if (fscanf(f, "%dx%d", &w, &h) == 2 && w >= 96 && h >= 96 && w <= GAME_MAX_W && h <= GAME_MAX_H) g->gw = w, g->gh = h;
    fclose(f);
  }
  g->back565 = (uint16_t*)psram(g->gw * g->gh * 2);
  g->front565 = (uint16_t*)psram(g->gw * g->gh * 2);
  if (!g->back565 || !g->front565) {
    heap_caps_free(g->classlib), heap_caps_free(g->jar), heap_caps_free(g->back565), heap_caps_free(g->front565);
    delete g;
    os::toast(N_("Not enough memory to open the game"));
    return;
  }
  for (int i = 0; i < g->gw * g->gh; i++) g->back565[i] = g->front565[i] = 0;
  g->dirtyY1 = g->gh;
  // The Java heap: what PSRAM can spare now, leaving room for the classes, the threads' stacks and the OS
  {
    const size_t freePs = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), keep = 1400 * 1024;
    g->heapLimit = freePs > keep + 1024 * 1024 ? std::min<size_t>(3 * 1024 * 1024, freePs - keep) : 1024 * 1024;
  }
  // the host: frames into the back buffer, soft keys, the end
  g->host.screenW = g->gw, g->host.screenH = g->gh;
  g->host.rmsDir = g->dataDir + "rms/";
  g->host.flush = [g](const uint32_t* px, int x, int y, int w, int h) {
    std::lock_guard<std::mutex> l(g->mu);
    for (int r = y; r < y + h; r++) {
      const uint32_t* s = px + r * g->gw + x;
      uint16_t* d = g->back565 + r * g->gw + x;
      for (int i = 0; i < w; i++) d[i] = to565(s[i]);
    }
    g->dirtyY1 = std::min(g->dirtyY1, y), g->dirtyY2 = std::max(g->dirtyY2, y + h);
  };
  g->host.softLabels = [g](const std::string& l, const std::string& r) {
    std::lock_guard<std::mutex> lk(g->mu);
    g->softL = l, g->softR = r, g->softChanged = true;
  };
  g->host.exit = [g]() { g->ended = true; };
  g->host.tone = [](int note, int ms, int volume) { playTone(note, ms, volume); };
  g->host.screenHint = [g](int w, int h) {
    std::lock_guard<std::mutex> l(g->mu);
    g->hintW = w, g->hintH = h;
  };
#ifndef SIM
  g->host.fileJob = fileJobOnUi;
#endif
  // the screen
  lv_obj_t* body;
  g->scr = os::screen(g->name.c_str(), &body, os::BARE);
  lv_obj_set_style_bg_color(g->scr, lv_color_hex(BG), 0);
  g->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  g->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  g->dsc.header.w = g->gw;
  g->dsc.header.h = g->gh;
  g->dsc.header.stride = g->gw * 2;
  g->dsc.data_size = g->gw * g->gh * 2;
  g->dsc.data = (const uint8_t*)g->front565;
  g->img = lv_image_create(g->scr);
  lv_image_set_src(g->img, &g->dsc);
  lv_obj_add_flag(g->img, LV_OBJ_FLAG_CLICKABLE);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED})
    lv_obj_add_event_cb(g->img, onTouch, c, nullptr);
  if (os::landscape()) {
    // Scaled down only when the keys would get less than 100 px a side, or the game is taller than the screen.
    const int room = os::W() - 2 * 100;
    g->scale = std::min(256, std::min(room * 256 / g->gw, os::H() * 256 / g->gh));
    const int sw = g->gw * g->scale / 256, sh = g->gh * g->scale / 256;
    if (g->scale < 256) {
      lv_image_set_pivot(g->img, 0, 0);
      lv_image_set_scale(g->img, g->scale);
    }
    lv_obj_set_pos(g->img, (os::W() - sw) / 2, (os::H() - sh) / 2);
    buildSideKeys(g, (os::W() - sw) / 2);
  } else {
    lv_obj_set_pos(g->img, (os::W() - g->gw) / 2, 0);
    buildKeypad(g);
  }
  lv_obj_add_event_cb(g->scr, onDelete, LV_EVENT_DELETE, g);
  lv_obj_add_event_cb(g->scr, [](lv_event_t* e) {
    Game* gg = (Game*)lv_event_get_user_data(e);
    gg->host.post(lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOADED ? 7 : 8);
  }, LV_EVENT_SCREEN_UNLOADED, g);
  lv_obj_add_event_cb(g->scr, [](lv_event_t* e) {
    ((Game*)lv_event_get_user_data(e))->host.post(8);
  }, LV_EVENT_SCREEN_LOADED, g);
  os::fullScreenFor(g->scr, true);
  g->timer = lv_timer_create(onTimer, 15, g);
  cur = g;
  os::open(g->scr);
  trace::add("j2me: open %s (jar %u KB, runtime %u KB)", pkgId, (unsigned)(g->jarLen / 1024), (unsigned)(g->classlibLen / 1024));
  vmsRunning++;
#ifdef SIM
  std::thread(runGame, g).detach();
#else
  // its own task on core 0 (LVGL is on core 1); the stack in internal RAM
  // 16 KB in PSRAM (internal RAM is short and split after a download: a 16 KB internal block was often missing).
  // Java-to-Java calls take no C stack; each nested native -> Java level ~0.7 KB (docs/j2me.md).
  if (!helper) helper = lv_timer_create(helperTick, 10, nullptr);
  liveTasks++;
  if (xTaskCreatePinnedToCoreWithCaps(vmTask, "j2me", 16 * 1024, g, 3, nullptr, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
    liveTasks--;
    vmsRunning--;
    std::lock_guard<std::mutex> l(g->mu);
    g->failure = T_("Not enough memory to run the game");
    g->ended = true;
    heap_caps_free(g->classlib), heap_caps_free(g->jar);
    g->classlib = g->jar = nullptr;
    --g->refs;  // no task will release it
  }
#endif
}

}  // namespace j2me_app
