#pragma once
// StandBy: in landscape, the tile left of Home's first page (where Today is in portrait). Two big stacks of widgets,
// clocks on the left, information on the right; swipe each up or down. At night the screen saver shows a dim red clock.
#include <lvgl.h>

namespace standby {
void build(lv_obj_t* parent);  // into Home's tile (os::W() x os::H())
void reset();                  // before Home's screens are deleted
void tick();                   // the big clock, from home::tick
}  // namespace standby
