#pragma once
// Pieces shared by the store (app_more.cpp) and the storage page (storage_ui.cpp).
#include <lvgl.h>

#include "pkg.h"

namespace pkg {
namespace ui {

struct Usage {
  uint64_t total = 0, freeB = 0, games = 0, apps = 0, other = 0;
  uint64_t pending = 0;  // removed while in use: free after a restart
  bool present = false;
};
Usage usage(Vol v);
// A stacked bar: games (orange), apps (blue), other (grey), then free.
lv_obj_t* bar(lv_obj_t* parent, const Usage& u, int h);
String permsText(const std::vector<String>& perms);  // "Play sounds · Send notifications" (translated)
const char* categoryName(const String& id);           // "game" -> "Games" (translated)
const char* volName(Vol v);                           // "Device", "Memory card" (translated)
// A small rounded action ("Install", "Open", "Remove"; literals are translated).
lv_obj_t* pill(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* data, int h = 28);
// Text wrapped only at spaces (LVGL also breaks after "," and ".": "7," / "4 MB"); glue() keeps a size together.
int wrapWords(lv_obj_t* l, const String& text, int w, int px, bool bold = false);
String glue(const String& s);  // spaces -> no-break spaces (wrapWords never breaks there)
// The app id that opens a package ("pet" for PawBoop), or nullptr.
const char* appOf(const String& pkgId);
void restartDevice();

}  // namespace ui
}  // namespace pkg
