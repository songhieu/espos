#pragma once

// Serial console commands (USB). One line each:
//   ESPOS:HELLO <epoch ms>   set the clock (also the start of USB provisioning)
//   SHOT                       send a screenshot: "SHOT <w> <h>\n" + w*h*2 bytes RGB565 + "\nENDSHOT"
//   TAP <x> <y>                tap at screen coordinates of the current orientation
//   DRAG <x0> <y0> <x1> <y1> <ms>
//   TOUCHLOG <0|1>             print raw touch transitions
namespace devtools {
void poll();
}
