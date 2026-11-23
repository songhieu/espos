#pragma once
// The level-2 UI nodes of JavaScript apps (ui.grid, ui.image, ui.split, ui.nav, ui.input, ui.qr, ui.line, ui.sheet):
// jsui.cpp registers them with jsapp::addNode. See jsui.cpp.
#include <Arduino.h>
#include <lvgl.h>

namespace jsui {
void stop();  // the app closed: its decoded pictures and icons go
const lv_image_dsc_t* icon(const char* name);  // an OS glyph ("search") or the open app's .a8 file; nullptr if none
// A scrolling container built with a key: put back where that key was scrolled at the last render, and remembered.
void keepScroll(lv_obj_t* sc, const String& key);
}  // namespace jsui
