#pragma once
// The screen saver: pages the user builds (setting s3.saver_pages, docs/saver.md §2) drawn by one renderer.
// A page is a background (black, a colour, two colours, the wallpaper, a photo, plus a dim) and up to 24 elements
// (time, date, lunar date, the user's text, the next event, the next alarm, the weather, boxes, photos, Home
// widgets) placed anywhere. Each orientation has 1..3 pages (cycled), plus a night page for the quiet hours.
// Without the setting: the old s3.saver_mode picks a template ("desk" = the defaults, like today's desk saver).
// The device editor (saveredit.h) edits the same model. Mockup: mockups/s3-saver-custom.html.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <time.h>

#include <vector>

namespace desksaver {

// ---------------------------------------------------------------- the model (keys as in docs/saver.md §2)
struct El {
  String t;                  // time date lunar text next alarm temp box photo w
  String f, s;               // format; the text of "text"
  String k, st, op, sk;      // widget kind, style, option, card skin (docs/widgets.md §2)
  int16_t x = 0, y = 0, w = 0, h = 0;
  uint32_t c = 0xe8ecf2;     // text colour or box fill
  uint8_t o = 100, px = 0, r = 0;
  char a = 'c', sz = 'S';    // align l c r; widget size S M L
  bool b = false;            // bold
  int id = 0;                // photo
  // Photo slideshows (docs/saver.md §2): the device's own pictures (all) or a set (ids, 2..12; id = the first),
  // iv seconds a picture, in order or shuffled (ord s|r), cut or faded (tr c|f), starting `of` pictures on.
  std::vector<int> ids;
  bool all = false;
  uint16_t iv = 60;
  char ord = 's', tr = 'c';
  uint8_t of = 0;
  // Text over pictures (docs/saver.md §2): a band behind it (band: colour bgc, opacity bo %, radius r), a soft shadow.
  bool band = false, sh = false;
  uint32_t bgc = 0;
  uint8_t bo = 50;
  bool text() const;         // one of the text types
  bool slides() const { return t == "photo" && (all || ids.size() > 1); }
  bool free() const { return t != "w"; }  // resizable by hand (widgets keep their size)
};
struct Bg {
  String t = "black";        // black color grad wall photo
  uint32_t c = 0x1c2029, c2 = 0x5a2a6c;
  int id = 0;                // photo: 0 = the s3.photos slideshow
  String w;                  // wall: a built-in wallpaper id ("" = the look's wallpaper)
  uint8_t dim = 0;
  bool operator==(const Bg& o) const;
  String key() const;        // equal backgrounds, equal keys
};
struct Page {
  Bg bg;
  std::vector<El> e;
};
struct Doc {
  int cycle = 0, light = 30;
  String night = "page";     // page off day
  std::vector<Page> p, l;    // portrait, landscape (1..3 each)
  Page np, nl;
  bool hasNp = false, hasNl = false;  // false: the built-in night page
};

void parse(const String& json, Doc& d);  // empty or bad JSON: the defaults (or the s3.saver_mode template)
String dump(const Doc& d);
const Doc& doc();                        // the saved one, re-read when the setting changes
void save(const Doc& d);                 // desk::setSetting("s3.saver_pages") when it changed
Page& nightPage(Doc& d, bool land);      // makes the built-in one editable
void widgetBox(char size, int16_t& w, int16_t& h);
void setWidgetSize(El& e, char size);

// A page's bg object (docs/saver.md §2) as Home's s3.home "bg" entries use it too (docs/widgets.md §4).
void readBg(JsonObjectConst o, Bg& bg);
void writeBg(JsonObject o, const Bg& bg);

// ---------------------------------------------------------------- drawing
// A w x h container (at 0,0) with the background. Its picture (a built-in wall, a photo by id) is held while the
// container exists: equal backgrounds share one painted picture, freed when nothing shows it.
lv_obj_t* drawBg(lv_obj_t* parent, const Bg& bg, int w, int h);
// The full-size picture of a background, held until releasePic (null: none needed or none here).
const lv_image_dsc_t* holdPic(const Bg& bg, int w, int h);
void releasePic(const lv_image_dsc_t* pic);
int heldPicKB();  // PSRAM held by background pictures (walls and photos)
// A small picture of photo id (from its portrait copy), w x h, owned by the image; null when it is not here yet.
lv_obj_t* photoImg(lv_obj_t* parent, int id, int w, int h);
// A container at the element's box with the element in it (text, box, photo or widget).
lv_obj_t* drawEl(lv_obj_t* parent, const El& e);
// Text sizes: the sizes an element may use, largest first, and the one its label uses now.
std::vector<uint8_t> sizes(const El& e);
int sizeShown(lv_obj_t* elBox);

// A picture of the page at 1/div of the screen, drawn now by this renderer (a snapshot, scaled down); null when
// out of memory. The image owns its pixels.
lv_obj_t* thumb(lv_obj_t* parent, const Page& pg, int div);

// ---------------------------------------------------------------- templates (docs/saver.md §5)
// The shared file (web os/saver-templates.json, published as apps/saver-templates.json):
// {"v":1,"groups":[{"id","name"}..],"templates":[{"id","name","desc","group","p":PAGE|[PAGE..],"l":PAGE|[PAGE..]}..]}
// ("t" for "templates" and "night":1 for group "night" are read too).
struct Template {
  String name, desc, group;
  bool night = false;       // a night page (group "night"; the first page is used)
  std::vector<Page> pages;  // this orientation's
};
// The templates that have a page for this orientation, and the groups' "id|name". false (and the built-in ones:
// Bàn làm việc, Tối giản, Khung ảnh, Đồng hồ đỏ, no groups) when json is not the file.
bool templates(const String& json, bool land, std::vector<Template>& out, std::vector<String>* groups = nullptr);

// ---------------------------------------------------------------- the saver
void build(lv_obj_t* parent);           // fills parent (the saver, W x H); hidden until show()
void show(bool on);
void update(const tm& t, bool night);   // every half second while shown (redraws only what changed)
int backlight(bool night);              // percent the saver wants now (0: off)
// What is on screen: the orientation and the page (-1: the night page).
bool shownLand();
int shownPage();
void redraw();                          // after the editor saved or left

}  // namespace desksaver
