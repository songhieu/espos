#pragma once
// Pictures from the network (news thumbnails and article photos): fetched through desk::fetch,
// decoded to the exact box, kept in a PSRAM cache by their storage path (the signature in the
// query string changes, the picture does not).
#include <Arduino.h>
#include <lvgl.h>

namespace media {

// A w x h box (placeholder colour) that shows the JPEG at `url` once it has arrived.
lv_obj_t* image(lv_obj_t* parent, const String& url, int w, int h, int radius, uint32_t placeholder);
// Frees cached pictures nothing shows.
void trim();

}  // namespace media
