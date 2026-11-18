// The touch screen, the OS side: the board's touch controller driver (hal::Input, src/boards/<name>/touch.cpp) reads
// every report in its own task, so no report is lost, and queues it here (report()):
//
// The controller reports 100 times a second while a finger is down. Reading it from LVGL's read callback, as
// before, took only the latest report once per frame: a heavy frame (70 ms) left ~14 samples a second, and a
// quick tap whose press and release both fell inside one frame was lost. Now a task on core 0 reads every
// report into a queue; LVGL's read callback takes them in order (a press and a release are never merged; moves in
// between are merged to the newest, so scrolling sees the same per-frame steps as before), and the system gestures
// (os.cpp: edge back, swipe up home, pull-down search) watch every sample through setWatcher().
#include "touch.h"

#include <Arduino.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "hal/board.h"
#include "sound.h"
#include "src/display/lv_display_private.h"
#include "trace.h"

namespace touch {
namespace {

lv_indev_t* dev;
lv_display_t* display;
volatile uint32_t lastPress = 0;
bool hideCoords = false;
Watcher watcher = nullptr;

// ---- the queue between the touch task and LVGL
struct Sample {
  int16_t x, y;
  bool down;
  uint32_t ms;
};
constexpr int QN = 64;
Sample q[QN];
int qHead = 0, qTail = 0;  // under qLock
portMUX_TYPE qLock = portMUX_INITIALIZER_UNLOCKED;
uint32_t dropped = 0;

void push(const Sample& s) {
  portENTER_CRITICAL(&qLock);
  const int last = (qHead + QN - 1) % QN;
  if (qHead != qTail && s.down && q[last].down) {
    q[last] = s;  // a move after a move: keep the newest (LVGL merges them anyway)
  } else if ((qHead + 1) % QN == qTail) {
    dropped++;  // full (LVGL stalled for long): keep the transitions already queued
  } else {
    q[qHead] = s;
    qHead = (qHead + 1) % QN;
  }
  portEXIT_CRITICAL(&qLock);
}

int popAll(Sample* out, int max) {
  int n = 0;
  portENTER_CRITICAL(&qLock);
  while (qTail != qHead && n < max) {
    out[n++] = q[qTail];
    qTail = (qTail + 1) % QN;
  }
  portEXIT_CRITICAL(&qLock);
  return n;
}

// ---- LVGL side
struct Inject {
  bool active = false;
  int x0, y0, x1, y1;
  uint32_t start, dur;
} inj;

Sample pend[QN];  // what LVGL still has to get, oldest first
int pendN = 0, pendAt = 0;
bool lvDown = false;  // what LVGL got last
int16_t lvX = 0, lvY = 0;
uint32_t downMs = 0, lastMs = 0;
int16_t downX = 0, downY = 0;
bool moved = false, taken = false;
// As on a phone, a finger that slid away is not a tap: past SLOP px from where it landed, LVGL's click and long
// press are dropped (LVGL itself clicks on release whenever nothing scrolled, however far the finger went).
// RELEASED still arrives, so drags (sliders, Home edit, games) work as before.
constexpr int SLOP = 20;
bool slipped = false;

void track(const Sample& s) {
  if (s.down && !lvDown) {
    downX = s.x, downY = s.y, downMs = s.ms, moved = slipped = false;
  } else if (s.down) {
    const int dx = s.x - downX, dy = s.y - downY;
    if (abs(dx) > 12 || abs(dy) > 12) moved = true;
    if (dx * dx + dy * dy > SLOP * SLOP) slipped = true;
  } else if (lvDown && !moved && s.ms - downMs < 500 && !taken) {
    sound::click();  // a tap gets a soft click; swipes stay silent
  }
  lvDown = s.down, lvX = s.x, lvY = s.y;
}

// Native panel point -> screen point (as LVGL does it in indev_pointer_proc).
void toScreen(int32_t& x, int32_t& y) {
  const lv_display_rotation_t r = lv_display_get_rotation(display);
  const int32_t hor = display->hor_res, ver = display->ver_res;  // as LVGL: the panel's own size
  if (r == LV_DISPLAY_ROTATION_180 || r == LV_DISPLAY_ROTATION_270) x = hor - x - 1, y = ver - y - 1;
  if (r == LV_DISPLAY_ROTATION_90 || r == LV_DISPLAY_ROTATION_270) {
    const int32_t t = y;
    y = x;
    x = ver - t - 1;
  }
}

// Every sample goes past the gesture watcher; a taken touch makes LVGL forget the press until the finger lifts.
void watch(const Sample& s) {
  if (!watcher || taken) {
    if (!s.down) taken = false;
    return;
  }
  int32_t x = s.x, y = s.y;
  toScreen(x, y);
  if (watcher(s.down, x, y) && s.down) {
    taken = true;
    lv_indev_reset(dev, nullptr);
    lv_indev_wait_release(dev);
  }
}

// What LVGL gets (gap = ms between the samples' own times, so stalls of the LVGL thread do not show as gaps).
void logSample(const Sample& s) {
  const bool edge = s.down != lvDown;
  const uint32_t gap = s.ms - lastMs;
  lastMs = s.ms;
  if (hideCoords)  // a password is being typed: where the finger lands would give it away
    trace::add("T +%lu%s", gap, edge ? (s.down ? " DOWN" : " UP") : "");
  else
    trace::add("T +%lu %d,%d%s", gap, s.x, s.y, edge ? (s.down ? " DOWN" : " UP") : "");
}

void read(lv_indev_t*, lv_indev_data_t* data) {
  const uint32_t now = millis();
  if (inj.active) {
    const uint32_t t = now - inj.start;
    if (t > inj.dur + 30) inj.active = false;
    const float k = inj.dur ? min(1.0f, (float)t / inj.dur) : 1.0f;
    const Sample s = {(int16_t)(inj.x0 + (inj.x1 - inj.x0) * k), (int16_t)(inj.y0 + (inj.y1 - inj.y0) * k),
                      inj.active && t <= inj.dur, now};
    if (s.down) lastPress = now;
    watch(s);
    track(s);
  } else {
    if (pendAt >= pendN) {
      // Take what the task queued: watch every sample, merge moves for LVGL.
      Sample in[QN];
      const int n = popAll(in, QN);
      pendN = pendAt = 0;
      for (int i = 0; i < n; i++) {
        watch(in[i]);
        // A move after a move replaces it; the press itself (a down after an up) and the release stay.
        const bool lastIsMove = pendN && pend[pendN - 1].down && (pendN > 1 ? pend[pendN - 2].down : lvDown);
        if (in[i].down && lastIsMove) pend[pendN - 1] = in[i];
        else pend[pendN++] = in[i];
      }
      if (dropped) trace::add("touch: %lu samples dropped (LVGL stalled)", (unsigned long)dropped), dropped = 0;
    }
    if (pendAt < pendN) {
      const Sample& s = pend[pendAt++];
      logSample(s);
      track(s);
    }
  }
  data->state = lvDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = lvX;  // native panel coordinates; LVGL applies the display rotation
  data->point.y = lvY;
  data->continue_reading = pendAt < pendN;
}

}  // namespace

bool begin(lv_display_t* disp) {
  display = disp;
  if (const hal::Input* in = hal::bsp().input) {
    if (!in->begin()) return false;  // the board's controller (src/boards/<name>/touch.cpp), on i2cBus()
  } else {
    // No controller (QEMU: no I2C devices there): touches come from the dev console (TOUCH, TAP, DRAG).
    Serial.println("touch: no controller");
  }

  dev = lv_indev_create();
  lv_indev_set_type(dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(dev, read);
  lv_indev_set_display(dev, disp);
  lv_timer_set_period(lv_indev_get_read_timer(dev), 10);
  lv_indev_set_long_press_time(dev, 500);  // as a phone: a slow press is not yet a long press
  for (lv_event_code_t c : {LV_EVENT_CLICKED, LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED, LV_EVENT_LONG_PRESSED_REPEAT})
    lv_indev_add_event_cb(dev, [](lv_event_t*) {
      if (slipped) lv_indev_stop_processing(dev);
    }, c, nullptr);
  return true;
}

lv_indev_t* indev() { return dev; }
uint32_t lastTouchMs() { return lastPress; }
void setPrivate(bool on) { hideCoords = on; }
void setWatcher(Watcher w) { watcher = w; }
i2c_master_bus_handle_t i2cBus() { return hal::i2c(); }

void report(int16_t x, int16_t y, bool down, uint32_t ms) {
  if (down) lastPress = ms;
  push({x, y, down, ms});
}

void feed(int x, int y, bool down) {
  report((int16_t)constrain(x, 0, LCD_W - 1), (int16_t)constrain(y, 0, LCD_H - 1), down, millis());
}

void toNative(int x, int y, int& nx, int& ny) {
  nx = x, ny = y;  // the inverse of toScreen()
  switch (lv_display_get_rotation(display)) {
    case LV_DISPLAY_ROTATION_90: nx = y, ny = LCD_H - 1 - x; break;
    case LV_DISPLAY_ROTATION_180: nx = LCD_W - 1 - x, ny = LCD_H - 1 - y; break;
    case LV_DISPLAY_ROTATION_270: nx = LCD_W - 1 - y, ny = x; break;
    default: break;
  }
}

void feedScreen(int x, int y, bool down) {
  int nx, ny;
  toNative(x, y, nx, ny);
  feed(nx, ny, down);
}

void inject(int x0, int y0, int x1, int y1, uint32_t durMs) {
  trace::add("inject %d,%d -> %d,%d in %lu ms", x0, y0, x1, y1, durMs);
  inj = {true, x0, y0, x1, y1, millis(), durMs};
}

}  // namespace touch
