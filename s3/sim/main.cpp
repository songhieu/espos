// EspOS simulator: runs the UI code with LVGL on a computer.
//   Script:  sim/build/espos-sim SCRIPT OUTDIR      (script time: as fast as the computer goes; shots as PPM)
//   Live:    sim/build/espos-live [--fast] [--zoom Z] [--out DIR] [SCRIPT]   (tools/sim-live.sh)
//            A window with the screen in real time, the mouse as the finger, the speaker, and the run game's
//            decoders at the board's speed in their own thread (--fast: as fast as the computer). Commands also
//            come from the terminal (stdin), the same as a script's; keys: sim/live.cpp.
// Commands (case does not matter, so the board's dev console words work too):
//   tap X Y | long X Y | drag X0 Y0 X1 Y1 MS | hold X0 Y0 X1 Y1 MS HOLD | down X Y | move X Y | up | wait MS
//   shot [NAME] | set KEY VALUE | rot N | app ID [ARG] | home | back | homeswipe | backswipe | swipe DIR
//   pet [ARG] | dismiss | run [ARG] | dev CMD | bench | saver | wake | mem [LABEL] | quit | help | notif ...
//   bgphotos (the page background photos fetched for each orientation)
#include <malloc/malloc.h>
#include <lvgl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "apps.h"
#include "desk.h"
#include "fonts.h"
#include "home.h"
#include "notif.h"
#include "os.h"
#include "saveredit.h"
#include "sound.h"
#include "touch.h"
#include "wid.h"
#include "trace.h"
#include "uikit.h"
#include "pet/pet.h"
#include "pet/game.h"
#include "pet/talk.h"
#include "led.h"
#include "wake/wake.h"
#include "run/run.h"
#include "tank/tank.h"
#ifdef SIM_LIVE
#include "live.h"
#endif

uint32_t simNow = 1000;
namespace touch {
extern lv_indev_t* dev;
extern uint32_t last;
extern Watcher watcher;
}  // namespace touch
namespace lcd {
lv_display_t* simDisp;
}
namespace trace {
extern bool quiet;
}
extern bool simBoard;
extern size_t simPsramKB, simInternalKB;
void simHeap(size_t& ps, size_t& in, size_t& peak);

namespace {

std::string out = ".";      // where shots go
std::atomic<bool> quitting{false};
bool dirty = true;           // the screen changed since the window last showed it

std::vector<uint16_t> fb(480 * 480);
int fbW = 320, fbH = 480;
uint8_t drawBuf[480 * 480 * 2];

lv_point_t ptr = {0, 0};
bool pressed = false;

void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  fbW = lv_display_get_horizontal_resolution(d);
  fbH = lv_display_get_vertical_resolution(d);
  const int w = lv_area_get_width(a);
  const uint16_t* src = (const uint16_t*)px;
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++)
      if (x >= 0 && y >= 0 && x < fbW && y < fbH) fb[y * fbW + x] = src[(y - a->y1) * w + (x - a->x1)];
  dirty = true;
  lv_display_flush_ready(d);
}

// As the board's touch.cpp: every sample goes past the system gestures first; a taken touch is forgotten by
// LVGL until the finger lifts.
bool taken = false;
lv_point_t downAt;
bool wasDown = false, slipped = false, moved = false;
uint32_t downMs = 0;
void readPtr(lv_indev_t* d, lv_indev_data_t* data) {
  // As touch.cpp: a finger that slid more than 20 px from where it landed is not a tap; a tap (no more than 12 px,
  // under 500 ms, not taken by a system gesture) gets the soft click.
  if (pressed && !wasDown) downAt = ptr, slipped = moved = false, downMs = simNow;
  else if (pressed) {
    const int dx = ptr.x - downAt.x, dy = ptr.y - downAt.y;
    if (abs(dx) > 12 || abs(dy) > 12) moved = true;
    if (dx * dx + dy * dy > 400) slipped = true;
  } else if (wasDown && !moved && simNow - downMs < 500 && !taken) {
    sound::click();
  }
  wasDown = pressed;
  data->point = ptr;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (!pressed) taken = false;
  else if (!taken && touch::watcher && touch::watcher(true, ptr.x, ptr.y)) {
    taken = true;
    lv_indev_reset(d, nullptr);
    lv_indev_wait_release(d);
  }
  if (!pressed && touch::watcher) touch::watcher(false, ptr.x, ptr.y);
}

#ifdef SIM_LIVE
// ------------------------------------------------------------------ live: real time, the window, the mouse
bool scripted = false;  // a command is moving the finger: the mouse waits
std::mutex pendMtx;
std::deque<std::string> pending;  // commands from the terminal and from keys
uint32_t liveT0 = 0, shownAt = 0, frames = 0, framesAt = 0;
void push(const std::string& c) {
  std::lock_guard<std::mutex> g(pendMtx);
  pending.push_back(c);
}
bool pop(std::string& c) {
  std::lock_guard<std::mutex> g(pendMtx);
  if (pending.empty()) return false;
  c = pending.front();
  pending.pop_front();
  return true;
}
void press(int x, int y);
void title() {
  size_t ps, in, peak;
  simHeap(ps, in, peak);
  char t[200];
  snprintf(t, sizeof t, "EspOS · ES3C35P 320×480 · %s · %u hình/giây · PSRAM còn %zu KB",
           simBoard ? "tốc độ mạch" : "tốc độ máy tính", frames, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
  live::setTitle(t);
}
// One pass of the board's loop(): the finger, LVGL, the OS; the window at most 60 times a second.
void tick() {
  live::Finger f;
  std::vector<std::string> keys;
  if (!live::events(f, keys)) quitting = true;
  for (auto& k : keys) push(k);
  if (f.changed && !scripted) {
    if (f.down && !pressed) press(f.x, f.y);
    ptr = {f.x, f.y}, pressed = f.down, touch::last = simNow;
  }
  simNow = 1000 + (live::ms() - liveT0);
  lv_timer_handler();
  os::tick();
  desk::poll();
  wake::tick();
  led::tick();
  const uint32_t now = live::ms();
  if ((dirty || live::ledChanged()) && now - shownAt >= 16) {
    live::present(fb.data(), fbW, fbH);
    dirty = false, shownAt = now, frames++;
  }
  if (now - framesAt >= 1000) {
    title();
    framesAt = now, frames = 0;
  }
}
#endif

void step(uint32_t ms) {
#ifdef SIM_LIVE
  if (live::on) {
    const uint32_t end = live::ms() + ms;
    while (!quitting && live::ms() < end) tick(), live::sleepMs(1);
    return;
  }
#endif
  for (uint32_t t = 0; t < ms; t += 5) {
    simNow += 5;
    lv_timer_handler();
    os::tick();
    desk::poll();
    wake::tick();
    led::tick();
  }
}

void savePpm(const std::string& path) {
  // Make sure everything is drawn.
  lv_refr_now(nullptr);
#ifdef SIM_LIVE
  if (live::on) {
    const std::string png = path.substr(0, path.size() - 4) + ".png";
    fprintf(stderr, live::savePng(png, fb.data(), fbW, fbH) ? "[sim] shot %s\n" : "[sim] could not write %s\n", png.c_str());
    return;
  }
#endif
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", fbW, fbH);
  for (int i = 0; i < fbW * fbH; i++) {
    const uint16_t v = fb[i];
    const uint8_t rgb[3] = {(uint8_t)((v >> 11) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

void press(int x, int y) {
  if (getenv("SIM_HIT")) {
    lv_point_t p = {x, y};
    lv_obj_t* top = lv_indev_search_obj(lv_layer_top(), &p);
    lv_obj_t* o = top ? top : lv_indev_search_obj(lv_screen_active(), &p);
    fprintf(stderr, "press %d,%d -> %s %p (layer %s) parent %p\n", x, y, o ? (lv_obj_check_type(o, &lv_tileview_tile_class) ? "tile" : lv_obj_check_type(o, &lv_tileview_class) ? "tileview" : lv_obj_check_type(o, &lv_image_class) ? "image" : lv_obj_check_type(o, &lv_label_class) ? "label" : "obj") : "none", (void*)o,
            top ? "top" : "screen", o ? (void*)lv_obj_get_parent(o) : nullptr);
    if (o) fprintf(stderr, "  events %u user %p size %dx%d at %d,%d\n", (unsigned)lv_obj_get_event_count(o), lv_obj_get_user_data(o),
                   (int)lv_obj_get_width(o), (int)lv_obj_get_height(o), (int)lv_obj_get_x(o), (int)lv_obj_get_y(o));
  }
  ptr = {x, y};
  pressed = true;
  touch::last = simNow;
}

void drag(int x0, int y0, int x1, int y1, int ms, int after) {
  press(x0, y0);
  step(20);
  for (int t = 0; t <= ms; t += 10) {
    ptr = {(int32_t)(x0 + (x1 - x0) * t / ms), (int32_t)(y0 + (y1 - y0) * t / ms)};
    touch::last = simNow;
    step(10);
  }
  pressed = false;
  step(after);
}

}  // namespace

#ifdef SIM_LIVE
namespace lcd {
// As the board: the run game sends its pictures straight to the panel below the status bar (80-row bands).
bool direct(int y0, int y1, void (*fill)(uint16_t*, int, int, void*), void* ctx, bool) {
  if (fbW != 320) return false;
  static uint16_t band[320 * 80];
  for (int y = y0; y < y1; y += 80) {
    const int rows = std::min(80, y1 - y);
    fill(band, y, rows, ctx);
    memcpy(&fb[y * fbW], band, rows * 320 * 2);
  }
  dirty = true;
  return true;
}
}  // namespace lcd
#endif

namespace touch {
extern lv_indev_t* dev;
extern uint32_t last;
}  // namespace touch

void runLine(const std::string& line) {
  std::istringstream ss(line);
  std::string cmd;
  ss >> cmd;
  if (cmd.empty() || cmd[0] == '#') return;
  for (char& ch : cmd) ch = (char)tolower(ch);
#ifdef SIM_LIVE
  scripted = true;
#endif
  {
    if (cmd == "tap") {
      int x, y;
      ss >> x >> y;
      press(x, y);
      step(80);
      pressed = false;
      step(400);
    } else if (cmd == "long") {
      int x, y;
      ss >> x >> y;
      press(x, y);
      step(900);
      pressed = false;
      step(400);
    } else if (cmd == "drag") {
      int x0, y0, x1, y1, ms;
      ss >> x0 >> y0 >> x1 >> y1 >> ms;
      press(x0, y0);
      step(20);
      for (int t = 0; t <= ms; t += 10) {
        ptr = {(int32_t)(x0 + (x1 - x0) * t / ms), (int32_t)(y0 + (y1 - y0) * t / ms)};
        touch::last = simNow;
        step(10);
      }
      pressed = false;
      step(500);
    } else if (cmd == "moments") {  // every kind of the dog's "firsts" in its album (the book's text checks)
      const int args[8] = {0, 12, 0, 0, 0, 200, 0, 0};
      for (int k = 0; k < 8; k++)
        if (!pet::game::hasMoment(k, args[k])) pet::game::addMoment(k, args[k], "happy", 0);
    } else if (cmd == "fittext") {  // fittext W H MAXPX MINPX BOLD LINESPACE TEXT: kit::fitText's verdict for TEXT in
      // a W x H box in the current language's fonts (text checks without the screen that shows it)
      int w, h, maxPx, minPx, bold, ls;
      ss >> w >> h >> maxPx >> minPx >> bold >> ls;
      std::string text;
      std::getline(ss, text);
      text.erase(0, text.find_first_not_of(' '));
      lv_obj_t* l = lv_label_create(lv_layer_top());
      const int px = kit::fitText(l, text.c_str(), w, h, maxPx, minPx, bold, ls);
      const char* t = lv_label_get_text(l);
      int lines = 1;
      for (const char* c = t; *c; c++) lines += *c == '\n';
      const lv_font_t* f = lv_obj_get_style_text_font(l, 0);
      const int need = lines * lv_font_get_line_height(f) + (lines - 1) * ls;
      std::string flat = t;
      for (char& ch : flat)
        if (ch == '\n') ch = '|';
      fprintf(stderr, "FITTEXT %s %d px, %d lines, %d of %d px, spacing %d: \"%s\"\n", need <= h + 2 ? "ok" : "NO", px, lines, need,
              h, (int)lv_obj_get_style_text_letter_space(l, 0), flat.c_str());
      lv_obj_delete(l);
    } else if (cmd == "flick") {  // flick X0 Y0 X1 Y1 MS: a drag timed by the wall clock (a slow frame does not slow
      // the finger down, as on the board), then the finger lifts at once
      int x0, y0, x1, y1, ms;
      ss >> x0 >> y0 >> x1 >> y1 >> ms;
      press(x0, y0);
      step(20);
#ifdef SIM_LIVE
      if (live::on) {
        const uint32_t t0 = live::ms();
        for (uint32_t t = 0; t < (uint32_t)ms; t = live::ms() - t0) {
          ptr = {(int32_t)(x0 + (x1 - x0) * (int)t / ms), (int32_t)(y0 + (y1 - y0) * (int)t / ms)};
          touch::last = simNow;
          step(2);
        }
      }
#endif
      ptr = {x1, y1};
      touch::last = simNow;
      step(5);
      pressed = false;
      step(500);
    } else if (cmd == "hold") {  // drag and keep the finger down at the end for MS
      int x0, y0, x1, y1, ms, hold;
      ss >> x0 >> y0 >> x1 >> y1 >> ms >> hold;
      press(x0, y0);
      step(20);
      for (int t = 0; t <= ms; t += 10) {
        ptr = {(int32_t)(x0 + (x1 - x0) * t / ms), (int32_t)(y0 + (y1 - y0) * t / ms)};
        touch::last = simNow;
        step(10);
      }
      for (int t = 0; t < hold; t += 10) touch::last = simNow, step(10);
      pressed = false;
      step(500);
    } else if (cmd == "down") {  // finger down / move / up, for shots in the middle of a gesture
      int x, y;
      ss >> x >> y;
      press(x, y);
      step(30);
    } else if (cmd == "move") {
      int x, y;
      ss >> x >> y;
      const lv_point_t from = ptr;
      for (int k = 1; k <= 10; k++) {
        ptr = {(int32_t)(from.x + (x - from.x) * k / 10), (int32_t)(from.y + (y - from.y) * k / 10)};
        touch::last = simNow;
        step(15);
      }
    } else if (cmd == "up") {
      pressed = false;
      step(400);
    } else if (cmd == "wait") {
      int ms;
      ss >> ms;
      step(ms);
    } else if (cmd == "shot") {
      std::string name;
      ss >> name;
      if (name.empty()) {  // shot: named by the time
        char t[32];
        const time_t now = time(nullptr);
        strftime(t, sizeof t, "shot-%Y%m%d-%H%M%S", localtime(&now));
        name = t;
      }
      mkdir(out.c_str(), 0755);
      step(pressed ? 20 : 300);
      savePpm(out + "/" + name + ".ppm");
      fprintf(stderr, "shot %s\n", name.c_str());
    } else if (cmd == "clock") {  // clock YYYY-MM-DD HH:MM[:SS]: the wall clock jumps there (stubs.cpp time())
      std::string d, t;
      ss >> d >> t;
      struct tm tmv = {};
      if (sscanf((d + " " + t).c_str(), "%d-%d-%d %d:%d:%d", &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday, &tmv.tm_hour,
                 &tmv.tm_min, &tmv.tm_sec) >= 5) {
        extern long long simClockBase;
        tmv.tm_year -= 1900, tmv.tm_mon -= 1, tmv.tm_isdst = -1;
        time(nullptr);  // (reads SIM_CLOCK first)
        simClockBase = (long long)mktime(&tmv) - simNow / 1000;
      }
      step(100);
    } else if (cmd == "set") {
      std::string k, v;
      ss >> k;
      std::getline(ss, v);
      if (!v.empty() && v[0] == ' ') v = v.substr(1);
      desk::setSetting(k.c_str(), v.c_str());
      step(2000);
    } else if (cmd == "app") {  // open an app directly: app ID [ARG]
      std::string id, arg;
      ss >> id >> arg;
      apps::open(id.c_str(), arg.empty() ? nullptr : arg.c_str());
      step(600);
    } else if (cmd == "notif") {  // notif post APP TITLE|BODY|ARG|TAG, notif sched SECONDS APP TITLE|BODY|ARG|TAG,
      // notif center | close | settings | clear APP | seen APP | cancel APP TAG | badge APP N | show APP
      std::string sub, app, rest;
      ss >> sub;
      if (sub == "post" || sub == "sched") {
        long secs = 0;
        if (sub == "sched") ss >> secs;
        ss >> app;
        std::getline(ss, rest);
        if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
        std::string f[4];
        for (int i = 0, k = 0; k < 4; k++) {
          const size_t j = rest.find('|', i);
          f[k] = rest.substr(i, j == std::string::npos ? std::string::npos : j - i);
          if (j == std::string::npos) break;
          i = j + 1;
        }
        notif::Opts o;
        o.arg = f[2].empty() ? nullptr : f[2].c_str();
        o.tag = f[3].empty() ? nullptr : f[3].c_str();
        if (sub == "post") notif::post(app.c_str(), f[0].c_str(), f[1].c_str(), o);
        else notif::schedule(app.c_str(), time(nullptr) + secs, f[0].c_str(), f[1].c_str(), o);
      } else if (sub == "center") {
        notif::openCenter();
      } else if (sub == "close") {
        notif::closeCenter();
      } else if (sub == "settings") {
        notif::openSettings();
      } else if (sub == "clear" || sub == "seen") {
        ss >> app;
        sub == "clear" ? notif::clear(app.c_str()) : notif::seen(app.c_str());
      } else if (sub == "cancel") {
        std::string tag;
        ss >> app >> tag;
        notif::cancel(app.c_str(), tag.empty() ? nullptr : tag.c_str());
      } else if (sub == "show") {
        ss >> app;
        fprintf(stderr, "badge %s = %d, enabled %d, %d in the Center\n", app.c_str(), notif::badge(app.c_str()),
                notif::enabled(app.c_str()), notif::count());
      } else if (sub == "badge") {
        int n = 0;
        ss >> app >> n;
        notif::setBadge(app.c_str(), n);
        fprintf(stderr, "badge %s = %d\n", app.c_str(), notif::badge(app.c_str()));
      }
      step(600);
    } else if (cmd == "rot") {
      int r;
      ss >> r;
      os::applyRotation(r);
      step(600);
    } else if (cmd == "bgphotos") {  // the page background photos photo::wanted() fetches (home::bgPhotos)
      std::vector<int> p, l;
      home::bgPhotos(p, l);
      std::string sp, sl;
      for (int id : p) sp += (sp.empty() ? "" : ",") + std::to_string(id);
      for (int id : l) sl += (sl.empty() ? "" : ",") + std::to_string(id);
      fprintf(stderr, "bgphotos 320x480 [%s] 480x320 [%s]\n", sp.c_str(), sl.c_str());
    } else if (cmd == "saver") {  // the screen saver now
      os::showSaver();
      step(600);
    } else if (cmd == "typetop") {  // typetop TEXT: into the focused text field of the top layer (saver editor)
      std::string t;
      std::getline(ss, t);
      if (!t.empty() && t[0] == ' ') t = t.substr(1);
      struct F {
        static lv_obj_t* find(lv_obj_t* o) {
          if (lv_obj_check_type(o, &lv_textarea_class) && lv_obj_has_state(o, LV_STATE_FOCUSED)) return o;
          for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
            if (lv_obj_t* f = find(lv_obj_get_child(o, i))) return f;
          return nullptr;
        }
      };
      if (lv_obj_t* ta = F::find(lv_layer_top())) lv_textarea_add_text(ta, t.c_str());
      else fprintf(stderr, "typetop: no focused text field\n");
      step(300);
    } else if (cmd == "widgets") {  // widgets RRGGBB KIND|SIZE|STYLE|OPT|SKIN|X|Y ...: cards on a plain screen ("+" = space)
      std::string bg, spec;
      ss >> bg;
      static lv_obj_t* scr = nullptr;
      lv_obj_t* old = scr;
      scr = lv_obj_create(nullptr);
      lv_obj_remove_style_all(scr);
      lv_obj_set_style_bg_color(scr, lv_color_hex(strtoul(bg.c_str(), nullptr, 16)), 0);
      lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
      while (ss >> spec) {
        for (char& ch : spec)
          if (ch == '+') ch = ' ';
        std::string f[7];
        size_t i = 0, k = 0;
        for (; k < 7; k++) {
          const size_t j = spec.find('|', i);
          f[k] = spec.substr(i, j == std::string::npos ? std::string::npos : j - i);
          if (j == std::string::npos) break;
          i = j + 1;
        }
        wid::Item it;
        it.kind = f[0].c_str();
        it.size = f[1] == "L" ? wid::L : f[1] == "M" ? wid::M : wid::S;
        it.style = f[2].c_str();
        it.opt = f[3].c_str();
        it.skin = f[4].c_str();
        int w, h;
        wid::boxSize(it.size, w, h);
        lv_obj_t* o = wid::create(scr, it, w, h, false);
        lv_obj_set_pos(o, atoi(f[5].c_str()), atoi(f[6].c_str()));
      }
      lv_screen_load(scr);
      if (old && lv_obj_is_valid(old)) lv_obj_delete(old);  // a rebuild (theme, rotation) may have deleted it
      step(600);
    } else if (cmd == "kinds") {  // kinds: the widget registry on stderr, one line a kind: KINDS id sizes style,style
      for (const wid::Kind& k : wid::kinds()) {
        std::string st;
        for (const wid::Style& s : k.styles) st += std::string(st.empty() ? "" : ",") + s.id;
        fprintf(stderr, "KINDS %s %d %s\n", k.id, k.sizes, st.empty() ? "-" : st.c_str());
      }
    } else if (cmd == "saveredit") {  // the saver's editor (as a long press on the saver)
      os::showSaver();
      saveredit::open();
      step(600);
    } else if (cmd == "wake") {  // leave the saver (as a short touch)
      os::wake();
      step(400);
    } else if (cmd == "overflow") {  // overflow NAME: text cut by its box, out of its parent, or over other text
      std::string name;
      ss >> name;
      step(50);
      int found = 0;
      const int32_t sw = lv_display_get_horizontal_resolution(nullptr), sh = lv_display_get_vertical_resolution(nullptr);
      struct Txt {
        lv_obj_t* o;
        int32_t x1, x2, y1, y2, h = 0;
      };
      // Where the text itself is drawn (its longest line, by the label's alignment).
      auto textBox = [](lv_obj_t* c, int32_t& tw) {
        lv_area_t a;
        lv_obj_get_content_coords(c, &a);
        const lv_font_t* f = lv_obj_get_style_text_font(c, 0);
        tw = 0;
        int lines = 0;
        for (const char* t = lv_label_get_text(c); *t;) {
          const char* e = strchr(t, '\n');
          const size_t len = e ? (size_t)(e - t) : strlen(t);
          tw = std::max(tw, lv_text_get_width(t, len, f, lv_obj_get_style_text_letter_space(c, 0)));
          lines++;
          t += len + (e ? 1 : 0);
        }
        int32_t wrapH = 0;
        if (lv_label_get_long_mode(c) == LV_LABEL_LONG_WRAP) {  // LVGL wraps it itself (CJK has no spaces to break at)
          lv_point_t sz;
          lv_text_get_size(&sz, lv_label_get_text(c), f, lv_obj_get_style_text_letter_space(c, 0),
                           lv_obj_get_style_text_line_space(c, 0), a.x2 - a.x1 + 1, LV_TEXT_FLAG_NONE);
          tw = sz.x, wrapH = sz.y;
        }
        const lv_text_align_t al = lv_obj_get_style_text_align(c, 0);
        int32_t x1 = a.x1;
        if (al == LV_TEXT_ALIGN_RIGHT) x1 = a.x2 + 1 - tw;
        else if (al == LV_TEXT_ALIGN_CENTER) x1 = (a.x1 + a.x2 + 1 - tw) / 2;
        Txt t{c, x1, x1 + tw - 1, a.y1, a.y2};
        t.h = wrapH ? wrapH : lines * lv_font_get_line_height(f) + (lines - 1) * lv_obj_get_style_text_line_space(c, 0);
        return t;
      };
      auto flat = [](lv_obj_t* l) {
        std::string t = lv_label_get_text(l);
        for (char& ch : t)
          if (ch == '\n') ch = '|';
        return t;
      };
      std::function<void(lv_obj_t*, bool)> walk = [&](lv_obj_t* o, bool inScrollX) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
        // A sideways scrolling parent (Home's pages, chips) may hold text past its box.
        const bool sx = lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_dir(o) == LV_DIR_HOR &&
                        lv_obj_get_scroll_right(o) + lv_obj_get_scroll_left(o) > 0;
        lv_area_t pa;
        lv_obj_get_coords(o, &pa);
        if (sx && getenv("OVERFLOW_ROWS") && pa.y2 >= 0 && pa.y1 < sh) {  // a row that only fits by scrolling sideways
          lv_obj_t* first = lv_obj_get_child_count(o) ? lv_obj_get_child(o, 0) : nullptr;
          lv_obj_t* fl = first && lv_obj_get_child_count(first) ? lv_obj_get_child(first, 0) : nullptr;
          fprintf(stderr, "OVERFLOW %s row-scrolls \"%s\" by %d px at y %d\n", name.c_str(),
                  fl && lv_obj_check_type(fl, &lv_label_class) ? lv_label_get_text(fl) : "?",
                  (int)(lv_obj_get_scroll_right(o) + lv_obj_get_scroll_left(o)), (int)pa.y1);
          found++;
        }
        std::vector<Txt> texts;
        for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
          lv_obj_t* c = lv_obj_get_child(o, i);
          if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
          if (lv_obj_check_type(c, &lv_label_class) && *lv_label_get_text(c)) {
            int32_t tw;
            const Txt t = textBox(c, tw);
            const bool visible = t.x2 >= 0 && t.x1 < sw && t.y2 >= 0 && t.y1 < sh;
            if (getenv("OVERFLOW_ALL"))
              fprintf(stderr, "LABEL %s \"%s\" text %d..%d box w %d h %d (text h %d) parent %d..%d sx %d\n", name.c_str(),
                      flat(c).c_str(), (int)t.x1, (int)t.x2, (int)lv_obj_get_content_width(c),
                      (int)lv_obj_get_content_height(c), (int)t.h, (int)pa.x1, (int)pa.x2, (int)(inScrollX || sx));
            if (visible) {
              const char* why = nullptr;
              if (tw > lv_obj_get_content_width(c) + 2 && lv_label_get_long_mode(c) != LV_LABEL_LONG_WRAP) why = "cut";
              else if (t.h > lv_obj_get_content_height(c) + 3) why = "cut-lines";
              else if (!sx && !lv_obj_has_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE) && (t.x2 > pa.x2 + 3 || t.x1 < pa.x1 - 3))
                why = "out";
              else if (!inScrollX && (t.x2 > sw + 2 || t.x1 < -2)) why = "offscreen";
              else if (!lv_obj_has_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE) && !lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) &&
                       t.y1 + t.h > pa.y2 + 3)
                why = "out-below";
              if (why) {
                fprintf(stderr, "OVERFLOW %s %s \"%s\" text %d box %d x %d..%d parent %d..%d\n", name.c_str(), why,
                        flat(c).c_str(), (int)tw, (int)lv_obj_get_content_width(c), (int)t.x1, (int)t.x2,
                        (int)pa.x1, (int)pa.x2);
                found++;
              }
              texts.push_back(t);
            }
          }
          walk(c, inScrollX || sx);
        }
        for (size_t i = 0; i < texts.size(); i++)
          for (size_t j = i + 1; j < texts.size(); j++) {
            const Txt &a = texts[i], &b = texts[j];
            const int ox = std::min(a.x2, b.x2) - std::max(a.x1, b.x1), oy = std::min(a.y2, b.y2) - std::max(a.y1, b.y1);
            if (ox > 2 && oy > 4) {
              fprintf(stderr, "OVERFLOW %s overlap \"%s\" / \"%s\"\n", name.c_str(), flat(a.o).c_str(),
                      flat(b.o).c_str());
              found++;
            }
          }
      };
      walk(lv_screen_active(), false);
      walk(lv_layer_top(), false);
      fprintf(stderr, "overflow %s: %d\n", name.c_str(), found);
    } else if (cmd == "mem") {  // mem LABEL: heap in use (the simulator's stand-in for PSRAM) on stderr
      std::string l;
      ss >> l;
      malloc_statistics_t st;
      malloc_zone_statistics(nullptr, &st);
      size_t ps, in, peak;
      simHeap(ps, in, peak);
      fprintf(stderr, "MEM %s %zu KB | heap_caps: PSRAM %zu KB in use (peak %zu), internal %zu KB", l.c_str(),
              st.size_in_use / 1024, ps / 1024, peak / 1024, in / 1024);
      if (simBoard)
        fprintf(stderr, " | as the board: PSRAM free %zu KB, internal free %zu KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
      fputc('\n', stderr);
    } else if (cmd == "dumpset") {  // dumpset KEY [NAME]: the setting's whole value into OUTDIR/NAME.txt (KEY.txt)
      std::string k, name;
      ss >> k >> name;
      FILE* f = fopen((out + "/" + (name.empty() ? k : name) + ".txt").c_str(), "w");
      if (f) fputs(desk::setting(k.c_str(), "").c_str(), f), fclose(f);
    } else if (cmd == "type") {  // type TEXT: into the focused text field (as if typed on the keyboard)
      std::string t;
      std::getline(ss, t);
      if (!t.empty() && t[0] == ' ') t = t.substr(1);
      struct F {
        static lv_obj_t* find(lv_obj_t* o) {
          if (lv_obj_check_type(o, &lv_textarea_class) && lv_obj_has_state(o, LV_STATE_FOCUSED)) return o;
          for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
            if (lv_obj_t* f = find(lv_obj_get_child(o, i))) return f;
          return nullptr;
        }
      };
      lv_obj_t* ta = F::find(lv_screen_active());
      if (ta) lv_textarea_add_text(ta, t.c_str());
      else fprintf(stderr, "type: no focused text field\n");
      step(300);
    } else if (cmd == "swipe") {  // swipe left|right|up|down: 60 px in the middle of the screen over 80 ms
      std::string d;
      ss >> d;
      const int dx = d == "left" ? -60 : d == "right" ? 60 : 0, dy = d == "up" ? -60 : d == "down" ? 60 : 0;
      drag(160, 260, 160 + dx, 260 + dy, 80, 0);
    } else if (cmd == "homeswipe") {  // the system gestures as a finger makes them (os.cpp watchTouch)
      drag(fbW / 2, fbH - 4, fbW / 2, fbH - 110, 120, 300);
    } else if (cmd == "backswipe") {
      drag(4, fbH / 2, 140, fbH / 2, 150, 300);
    } else if (cmd == "home") {
      os::goHome();
      step(600);
    } else if (cmd == "back") {
      os::back();
      step(600);
    } else if (cmd == "pet") {  // the pet game (app_pet::open), e.g. "pet book"
      std::string arg;
      ss >> arg;
      app_pet::open(arg.empty() ? nullptr : arg.c_str());
      step(600);
    } else if (cmd == "dismiss") {  // close Biscuit's pop-up
      pet::dismiss();
      step(300);
    } else if (cmd == "run") {  // the run game directly: run [ARG] (a map, "bench", "pet")
      std::string arg;
      ss >> arg;
      run::open("vn.pawboop.run", arg.empty() ? nullptr : arg.c_str());
      step(600);
    } else if (cmd == "tank") {  // tank CMD: the aquarium's developer commands (src/tank/tank.h dev)
      std::string rest;
      std::getline(ss, rest);
      if (!tank::dev(rest.c_str())) fprintf(stderr, "tank: ? %s\n", rest.c_str());
      step(100);
    } else if (cmd == "dev") {  // run::dev: auto | manual | god | warp M | end | smooth 0|1 | stats
      std::string rest;
      std::getline(ss, rest);
      if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
      if (!run::dev(rest.c_str())) fprintf(stderr, "dev: unknown or not ready: %s\n", rest.c_str());
    } else if (cmd == "bench") {
      run::bench();
      step(100);
#ifdef SIM_LIVE
    } else if (cmd == "window") {  // window PATH.png: the whole window (frame and screen) at its next picture
      std::string p;
      ss >> p;
      live::shootWindow(p);
      dirty = true;
      step(100);
#endif
    } else if (cmd == "quit" || cmd == "exit") {
      quitting = true;
    } else if (cmd == "help") {
      fprintf(stderr, "commands: tap X Y | long X Y | drag X0 Y0 X1 Y1 MS | swipe left|right|up|down | homeswipe | "
                      "backswipe | home | back | app ID [ARG] | pet | run [ARG] | dev auto|god|manual|stats | shot [NAME] | "
                      "mem | set KEY VALUE | rot N | saver | wake | wait MS | quit\n"
                      "keys: arrows = swipe, H = home, B = back, S = shot, M = memory, Q = quit; mouse = finger\n");
    } else if (wake::console(line.c_str()) || pet::talk::console(line.c_str())) {  // WAKE, WAKEREC n, ASK text, TALK
    } else {
      fprintf(stderr, "?? %s (help: the commands)\n", cmd.c_str());
    }
  }
#ifdef SIM_LIVE
  scripted = false;
#endif
}

int main(int argc, char** argv) {
#ifdef SIM_LIVE
  std::string script;
  float zoom = 0;
  simBoard = true;
  out = "sim/shots";
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--fast") simBoard = false;
    else if (a == "--zoom" && i + 1 < argc) zoom = atof(argv[++i]);
    else if (a == "--out" && i + 1 < argc) out = argv[++i];
    else script = a;
  }
  // The board's memory once the OS runs (sim/stubs.cpp): PSRAM for heap_caps calibrated on the run game's start
  // from the memory card (the board: 1642 KB free, 2026-11-16; the simulator holds 3738 KB then), internal RAM as
  // at Home. SIM_PSRAM_KB=... to try a board with less.
  simPsramKB = getenv("SIM_PSRAM_KB") ? atoi(getenv("SIM_PSRAM_KB")) : 5380;
  simInternalKB = 180;
#else
  if (argc < 3) {
    fprintf(stderr, "usage: %s SCRIPT OUTDIR\n", argv[0]);
    return 2;
  }
#endif
  setenv("TZ", "ICT-7", 1);
  tzset();
  if (getenv("SIM_QUIET")) trace::quiet = true;
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return simNow; });
  lcd::simDisp = lv_display_create(320, 480);
  lv_display_set_flush_cb(lcd::simDisp, flush);
  lv_display_set_buffers(lcd::simDisp, drawBuf, nullptr, sizeof drawBuf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  touch::dev = lv_indev_create();
  lv_indev_set_type(touch::dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch::dev, readPtr);
  for (lv_event_code_t c : {LV_EVENT_CLICKED, LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED, LV_EVENT_LONG_PRESSED_REPEAT})
    lv_indev_add_event_cb(touch::dev, [](lv_event_t*) {
      if (slipped) lv_indev_stop_processing(touch::dev);
    }, c, nullptr);
  fonts::begin();
#ifdef SIM_LIVE
  if (!live::begin(320, 480, zoom)) {
    fprintf(stderr, "[sim] no window\n");
    return 1;
  }
  liveT0 = live::ms() - (simNow - 1000);
  fprintf(stderr, "[sim] EspOS live, %s. Mouse = finger; arrows = swipe, H = home, B = back, S = shot (%s), "
                  "M = memory, Q = quit. Commands here too (help).\n",
          simBoard ? "the board's speed (--fast: the computer's)" : "the computer's speed", out.c_str());
#endif
  desk::begin();
  os::begin(lcd::simDisp);
  led::begin();   // the RGB LED: a dot in the window's frame
  wake::begin();  // the mic's listener (SIM_MIC=file feeds it) and the dog assistant's turns
  pet::talk::begin();
  step(600);
#ifdef SIM_LIVE
  if (!script.empty()) {  // its commands first, in real time
    std::ifstream in(script);
    for (std::string line; std::getline(in, line);) push(line);
  }
  std::thread([] {  // the terminal: one command a line
    for (std::string line; std::getline(std::cin, line);) push(line);
  }).detach();
  while (!quitting) {
    std::string c;
    if (pop(c)) runLine(c);
    else tick(), live::sleepMs(2);
  }
  live::end();
  _exit(0);  // the terminal thread may still wait on stdin
#else
  out = argv[2];
  std::ifstream in(argv[1]);
  for (std::string line; std::getline(in, line);) runLine(line);
  return 0;
#endif
}
