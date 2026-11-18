#pragma once
#include <lvgl.h>

#include "driver/i2c_master.h"

// The touch screen. The board's controller driver (hal::Input, src/boards/<name>/touch.cpp) reads every report (100 a
// second) in its own task into a queue that LVGL's pointer takes in order (no tap lost to a slow frame); LVGL maps the
// point through the display rotation. A finger that slides more than 20 px is not a click.
namespace touch {
bool begin(lv_display_t* disp);  // false when the controller does not answer
lv_indev_t* indev();
uint32_t lastTouchMs();          // millis() of the last press (for the screen saver)
void setPrivate(bool on);        // keep touch positions out of the event history (passwords)
i2c_master_bus_handle_t i2cBus();  // the board's own I2C bus (hal::i2c(), made on first use): touch and codec
// System gestures: called in the LVGL thread for every touch sample (none dropped), in screen coordinates,
// before LVGL sees it. Return true to take the touch: LVGL forgets the press (no click on what is under the
// finger) until the finger lifts.
using Watcher = bool (*)(bool down, int32_t x, int32_t y);
void setWatcher(Watcher w);
// Development: a press at (x0,y0) moving to (x1,y1) over durMs, in native panel coordinates.
void inject(int x0, int y0, int x1, int y1, uint32_t durMs);
// A controller's report, from its driver's task (hal::Input): x, y in the UI's portrait coordinates (in range), ms =
// millis() when it was read.
void report(int16_t x, int16_t y, bool down, uint32_t ms);
// One finger sample from elsewhere, as the controller's task queues them (native panel coordinates): QEMU's viewer
// sends the mouse this way (dev console TOUCH), through the same queue, gestures and click as a real finger.
void feed(int x, int y, bool down);
void feedScreen(int x, int y, bool down);  // the same, in screen coordinates of the current orientation
void toNative(int x, int y, int& nx, int& ny);  // screen coordinates of the current orientation -> the panel's
}  // namespace touch
