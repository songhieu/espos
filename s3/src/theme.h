#pragma once
// Looks of EspOS: Sắc màu (colour wallpaper, default), Sáng, Tối, Ảnh (a photo as the
// wallpaper). Wallpapers are computed on the device into PSRAM (no flash cost) and dithered
// so the RGB565 panel shows no bands. App screens are light by day; dark with the dark theme,
// or at night when "tự tối ban đêm" is on.
#include <Arduino.h>
#include <lvgl.h>

namespace theme {

enum class Kind { Color, Light, Dark, Photo };

struct Wall {
  const char* id;
  const char* name;
  uint32_t top, bottom;   // base gradient
  uint32_t blobA, blobB;  // glows at the top left and the bottom right
};
extern const Wall WALLS[];
extern const int WALL_COUNT;

// Colours of things drawn straight on the wallpaper (home, Today, Control Center).
struct Home {
  uint32_t fg, dim;        // text on the wallpaper
  uint32_t glass;          // widget "glass" cards
  lv_opa_t glassOpa;
  uint32_t dock;
  lv_opa_t dockOpa;
  bool light;              // light wallpaper (dark text)
};

void begin();
// Settings or the time of day changed the look: true when screens must be rebuilt.
bool refresh();
Kind kind();
const char* kindId();       // "color" | "light" | "dark" | "photo"
const Wall& wall();
const Home& home();
bool darkApps();            // app screens use the dark palette
// The wallpaper for a w x h screen (cached in PSRAM until the look or size changes).
const lv_image_dsc_t* wallpaper(int w, int h);
// A built-in wallpaper by id for a w x h screen (a page's own background, docs/widgets.md §4), painted once into
// PSRAM and shared: each call holds it, releaseWall() lets go, and it is freed when nothing holds it (so never while
// shown). An unknown id gives the look's wallpaper(), which is not held (releaseWall ignores it).
const lv_image_dsc_t* wallpaperOf(const char* id, int w, int h);
void releaseWall(const lv_image_dsc_t* d);
int heldWallKB();  // PSRAM held by wallpaperOf pictures
// A small preview of a wallpaper (for the picker), pw x ph; free it with freePreview.
lv_image_dsc_t* makePreview(const Wall& w, int pw, int ph);
void freePreview(lv_image_dsc_t* d);

}  // namespace theme
