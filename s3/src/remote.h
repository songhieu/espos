#pragma once
#include <Arduino.h>

// Remote control in the home network (Settings › Điều khiển từ xa, docs/remote.md): the device serves a page on
// port 80 that shows its screen (JPEG frames from lcd::frame) and sends the mouse or a finger back as touches
// (touch::feedScreen), plus Home and Back. Every request carries the 6-digit PIN shown in Settings. Off until turned
// on; turning it on again makes a new PIN. The web EspOS opens the page from the device's own address and PIN,
// which the device sends with its sync (diag rc_url, rc_pin).
namespace remote {
void begin();  // from setup(): starts the server when it was left on
void poll();   // from loop(): Home/Back asked by the page, the panel mirror on while someone watches
bool enabled();
void setEnabled(bool on);
String pin();
String url();       // where the page is: "http://192.168.1.37" ("" while off or offline)
bool watching();    // a frame was asked for in the last 3 s
}  // namespace remote
