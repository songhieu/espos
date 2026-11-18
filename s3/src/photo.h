#pragma once
// Photo widget pictures: colour JPEGs the web renders at each widget's exact size
// (GET /v1/device/photos/:id/:dim/:p, p = fit + "j" + brightness), kept on flash in /ph.
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

namespace photo {

struct File {
  int id;
  String dim, p;
  String path() const;  // where it lives on flash
};

// Every picture the current layouts can show (portrait and landscape pages).
std::vector<File> wanted();
// Removes pictures no layout uses any more.
void prune(const std::vector<File>& keep);
bool have(const File& f);
// Saves a downloaded JPEG (UI loop: flash writes stay off the network task).
bool save(const File& f, const uint8_t* data, size_t len);
uint32_t version();  // bumps when a picture arrives

// Shows the picture a photo widget option picks for a w x h card; false when none is here.
bool show(lv_obj_t* parent, const String& option, int w, int h);

// Picture sets of the saver (desksaver.cpp): nothing is kept here, the caller frees both buffers (heap_caps_free).
// readCopy: the JPEG of picture id's copy at exactly w x h (UI loop: flash reads), null when it is not here yet.
// decodeJpg: that JPEG as w x h RGB565 in PSRAM; safe on another task (no flash, no trace, no LVGL).
uint8_t* readCopy(int id, int w, int h, size_t& len);
uint16_t* decodeJpg(const uint8_t* jpg, size_t len, int w, int h);

// Home is about to be rebuilt: its widget pictures may be evicted again.
void unpinAll();
// The n-th full-screen picture here for a w x h screen (n wraps); null when none.
// pin: a Home widget shows it (kept until the next Home rebuild, see unpinAll).
const lv_image_dsc_t* screen(uint32_t n, int w, int h, bool pin = false);
int screenCount(int w, int h);

}  // namespace photo
