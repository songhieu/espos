#pragma once
// The home screen (portrait): pages of app icons and widgets on a 4x5 grid, a dock of four
// apps, the Today view left of page 1, the App Library after the last page. Long-press starts
// the edit mode: items wiggle, drag to move (across pages), "−" removes, "+ Widget" opens the
// widget gallery, a tap on a widget opens its settings. Saved as setting s3.home:
//   {"v":2,"dock":["clock",...],"pages":[[["a","news",x,y],["w",kind,x,y,"s|m|l",style,opt,open]],...]}
#include <lvgl.h>

#include <vector>

#include "desksaver.h"

namespace home {

// A page's own background (docs/widgets.md §4); not own = the look's wallpaper.
struct PageBg {
  bool own = false;
  desksaver::Bg bg;
};

// The edit mode's sheets, shared with the saver editor (saveredit.cpp): one look, one copy of the code.
lv_obj_t* openSheet(int h, const char* title);  // light, from the bottom, over everything; a tap outside closes it
void closeSheet();
// The widget gallery (categories, tiles, sizes, Thêm). free: a first tab "Cơ bản" with the saver's own elements
// (kind "el:time", "el:date" ...). add(kind, size, opt) after Thêm, the sheet closed.
using AddFn = void (*)(const char* kind, int size, const char* opt);
void gallery(const char* title, bool free, AddFn add);
// The page background sheet (built-in wallpapers, photos, colours, gradients, dim). set(b) after each pick (the
// sheet then shows it); slideshow: a "Lần lượt" photo tile (the saver's photo id 0).
void bgSheet(const char* title, const PageBg& cur, void (*set)(const PageBg&), bool slideshow);

void build(lv_obj_t* scr);
void reset();            // before the screens are rebuilt
void refresh();          // new data: redraw the pages in place
void tick();             // live parts, every 500 ms
bool editing();
// Search: a downward swipe that starts at (x, y) opens it (a Home page, not Today, the Library, the dock).
bool searchAt(int32_t x, int32_t y);
void openSearch();
bool settingsChanged();  // s3.home changed on the web: true when the pages must be redrawn
void toFirstPage();
void bench();  // dev console
// A big game opens (PawBoop, its run, a Java game): Home's page pictures (one a page, 250-300 KB of PSRAM each) are
// let go; Home takes them again when it is idle in front.
void releasePictures();
// The photo ids of the page backgrounds (docs/widgets.md §4) each orientation's pages show, read from the settings (not
// the pages on screen, so any task may ask): portrait from s3.home; landscape from s3.home_l when it has pages, else
// the portrait ones (SEED gives each landscape page its portrait page's background). photo::wanted() fetches their
// copies at 320x480 and 480x320.
void bgPhotos(std::vector<int>& port, std::vector<int>& landscape);
// Adds a widget to the current page (or a new one) and leaves the edit mode on.
void addWidget(const char* kind, int size, const char* opt);

}  // namespace home
