// The EspOS simulator's script driver (sim/main.cpp) plus the run game's commands, so it can be tested before
// the package installer opens it: links against the simulator's objects in place of sim/main.o.
//   tools/pawboop_run/sim.sh SCRIPT OUTDIR      (builds sim/build-run, then this driver)
// Script: the simulator's commands (tap X Y | drag X0 Y0 X1 Y1 MS | down X Y | move X Y | up | wait MS |
// shot NAME | app ID [ARG] | set KEY VALUE) and
//   run [ARG]        run::open("vn.pawboop.run", ARG)  (ARG: a map name, "bench")
//   dev CMD...       run::dev(CMD): auto, manual, god, warp M, end, smooth 0|1, stats
//   swipe DIR        a 60 px swipe in the middle of the screen (left, right, up, down) over 80 ms
//   bench            run::bench()
//   pet [ARG]        app_pet::open(ARG) (the pet game, its book: "book")
//   dismiss          pet::dismiss() (her morning greeting pops up over everything between 6 and 11)
//   shots N MS NAME  N shots MS apart: NAME_00 ...
#include <lvgl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "apps.h"
#include "desk.h"
#include "fonts.h"
#include "os.h"
#include "pet/pet.h"
#include "run/run.h"
#include "touch.h"
#include "trace.h"

uint32_t simNow = 1000;
namespace touch {
extern lv_indev_t* dev;
extern uint32_t last;
}  // namespace touch
namespace lcd {
lv_display_t* simDisp;
}
namespace trace {
extern bool quiet;
}

namespace {

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
  lv_display_flush_ready(d);
}

void readPtr(lv_indev_t*, lv_indev_data_t* data) {
  data->point = ptr;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void step(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 5) {
    simNow += 5;
    lv_timer_handler();
    os::tick();
    desk::poll();
  }
}

void savePpm(const std::string& path) {
  lv_refr_now(nullptr);
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

// The panel path of lcd.cpp (lcd::direct) for the run game: rows drawn band by band into the framebuffer the
// shots read, past LVGL. RUN_LVGL=1 leaves it out (the game then draws through LVGL as in the stock simulator).
namespace lcd {
bool direct(int y0, int y1, void (*fill)(uint16_t*, int, int, void*), void* ctx, bool) {
  if (getenv("RUN_LVGL") || fbW != 320) return false;
  static uint16_t band[320 * 80];
  for (int y = y0; y < y1; y += 80) {
    const int rows = std::min(80, y1 - y);
    fill(band, y, rows, ctx);
    memcpy(&fb[y * fbW], band, rows * 320 * 2);
  }
  return true;
}
}  // namespace lcd

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s SCRIPT OUTDIR\n", argv[0]);
    return 2;
  }
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
  fonts::begin();
  desk::begin();
  os::begin(lcd::simDisp);
  step(600);
  std::ifstream in(argv[1]);
  const std::string out = argv[2];
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty() || cmd[0] == '#') continue;
    if (cmd == "tap") {
      int x, y;
      ss >> x >> y;
      press(x, y);
      step(80);
      pressed = false;
      step(400);
    } else if (cmd == "drag") {
      int x0, y0, x1, y1, ms;
      ss >> x0 >> y0 >> x1 >> y1 >> ms;
      drag(x0, y0, x1, y1, ms, 500);
    } else if (cmd == "swipe") {
      std::string d;
      ss >> d;
      const int dx = d == "left" ? -60 : d == "right" ? 60 : 0, dy = d == "up" ? -60 : d == "down" ? 60 : 0;
      drag(160, 260, 160 + dx, 260 + dy, 80, 0);
    } else if (cmd == "down") {
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
      savePpm(out + "/" + name + ".ppm");
      fprintf(stderr, "shot %s\n", name.c_str());
    } else if (cmd == "shots") {
      int n, ms;
      std::string name;
      ss >> n >> ms >> name;
      for (int k = 0; k < n; k++) {
        char nm[16];
        snprintf(nm, sizeof nm, "_%02d", k);
        savePpm(out + "/" + name + nm + ".ppm");
        step(ms);
      }
    } else if (cmd == "set") {
      std::string k, v;
      ss >> k;
      std::getline(ss, v);
      if (!v.empty() && v[0] == ' ') v = v.substr(1);
      desk::setSetting(k.c_str(), v.c_str());
      step(2000);
    } else if (cmd == "app") {
      std::string id, arg;
      ss >> id >> arg;
      apps::open(id.c_str(), arg.empty() ? nullptr : arg.c_str());
      step(600);
    } else if (cmd == "run") {
      std::string arg;
      ss >> arg;
      run::open("vn.pawboop.run", arg.empty() ? nullptr : arg.c_str());
      step(600);
    } else if (cmd == "pet") {  // the pet game directly (app_pet::open), e.g. with its packs on the card
      std::string arg;
      ss >> arg;
      app_pet::open(arg.empty() ? nullptr : arg.c_str());
      step(600);
    } else if (cmd == "dismiss") {  // close Biscuit's pop-up (the morning greeting comes over everything)
      pet::dismiss();
      step(300);
    } else if (cmd == "dev") {
      std::string rest;
      std::getline(ss, rest);
      if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
      if (!run::dev(rest.c_str())) fprintf(stderr, "dev: unknown or not ready: %s\n", rest.c_str());
    } else if (cmd == "bench") {
      run::bench();
      step(100);
    } else if (cmd == "back") {
      os::back();
      step(600);
    } else if (cmd == "home") {
      os::goHome();
      step(600);
    }
  }
  return 0;
}
