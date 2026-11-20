#pragma once
// The apps of EspOS: built-in ones (C++, below) and, later, installed ones. Each app has an
// icon, a name, an entry point taking an optional argument ("alarm", "wifi", an article id...)
// and an optional badge count (red dot on its icon).
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

namespace apps {

struct App {
  const char* id;
  const char* name;
  const lv_image_dsc_t* icon52;  // 60x60 with its shadow (home, dock)
  const lv_image_dsc_t* icon40;
  const lv_image_dsc_t* icon28;
  void (*open)(const char* arg);
  int (*badge)();
  // Installed packages without pictures in the firmware (JavaScript apps, games): an icon drawn from these, or
  // the package's icon.jpg.
  bool js = false;
  uint32_t bg1 = 0, bg2 = 0;
  const char* glyph = nullptr;
  // An installed package (pkg/pkg.h): its id; `off` while it is on a memory card that is out (greyed icon).
  const char* pkg = nullptr;
  bool off = false;
};

const std::vector<App>& all();
// Changes when what Home shows of the apps changes (an install, a removal, a name, a picture, greyed or not); not
// when only a package's place or a card scan that found the same apps changed (0.5.7: no second Home build at boot).
uint32_t version();
const App* find(const char* id);
void open(const char* id, const char* arg = nullptr);
// The app in front and how it was opened (open() keeps it; os.cpp clears it at Home), so a turn of the screen can
// open it again; a page inside an app may say where it is (Settings > Display: "settings", "display").
bool route(String& id, String& arg);
void setRoute(const char* id, const char* arg);
void clearRoute();
int badgeOf(const char* id);
// The icon of an app at size 52 (with shadow, 60x60 box), 40 or 28; the calendar and lunar
// icons draw today's date on top. Unknown ids get a grey placeholder.
lv_obj_t* icon(lv_obj_t* parent, const char* id, int size);
void refreshIcons();  // the date changed
// The icon of an installed package by its package id, drawn at `size` px (no shadow box): its icon.jpg rounded,
// else its gradient and glyph. Used by the store and the storage page.
lv_obj_t* packageIcon(lv_obj_t* parent, const char* pkgId, int size);
// A gradient squircle with a glyph (catalog entries not installed yet).
lv_obj_t* drawnIcon(lv_obj_t* parent, uint32_t bg1, uint32_t bg2, const char* glyph, int size);
// A square JPEG as an icon of `size` px with rounded corners (alpha), decoded once per key and size and kept.
const lv_image_dsc_t* jpegIcon(const String& key, const uint8_t* jpg, size_t len, int size);

}  // namespace apps

// Entry points of the built-in apps (app_*.cpp).
namespace app_news { void open(const char* arg); int badge(); void seenAll(); }
namespace app_weather { void open(const char* arg); }
namespace app_calendar { void open(const char* arg); }
namespace app_photos { void open(const char* arg); }
namespace app_clock { void open(const char* arg); }
namespace app_notes { void open(const char* arg); }
namespace app_markets { void open(const char* arg); }
namespace app_settings { void open(const char* arg); }
namespace app_store {
void open(const char* arg);
// A catalog package's picture (its icon_url, fetched once and kept) at `size` px; nullptr while it is coming.
const lv_image_dsc_t* catalogIcon(const char* id, int size);
}
namespace app_lunar { void open(const char* arg); }
namespace app_remind { void open(const char* arg); int badge(); }
namespace app_calc { void open(const char* arg); }
namespace app_pet { void open(const char* arg); }
