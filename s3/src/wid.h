#pragma once
// Widgets of the home screen, the Today view and the desk mode, in three sizes like iOS:
// small 2x2 (144x138), medium 4x2 (300x138), large 4x4 (300x286) in portrait. Each kind reads
// the synced dashboard or the device's own data (alarms, notes, reminders, my widgets).
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

namespace wid {

enum Size { S = 0, M = 1, L = 2 };

struct Item {
  String kind;   // clock, weather, cal, markets, news, lunar, count, alarm, world, remind, notes, photo, my
  int x = 0, y = 0;
  Size size = S;
  String style;  // per kind ("analog", "sky"...)
  String opt;    // per kind (market codes, a note id, a my-widget id...)
  String open;   // app opened by a tap ("" = the kind's own app)
  String skin;   // card look: "" theme, dark, light, glass, c:rrggbb, none (docs/widgets.md §2)
  // A span of cells other than S 2x2, M 4x2, L 4x4 (docs/widgets.md §2b): cells across and down, 1..4 each; 0 = the
  // size's own span. With a span, `size` keeps the letter firmware before 0.6.2 reads (the largest of S/M/L inside).
  uint8_t cw = 0, ch = 0;
};

// One choice the editors list (a style, a flag, a skin): its id in the item and its English name (marked N_: the
// editors translate it).
struct Style {
  const char* id;
  const char* name;
  uint16_t spans = 0;  // the spans besides S, M, L this look draws (SPAN() bits); the kind draws their union
};
// The bit of a span in Style::spans: (ch - 1) * 4 + (cw - 1).
constexpr uint16_t SPAN(int cw, int ch) { return 1u << ((ch - 1) * 4 + (cw - 1)); }

// What a kind's option (Item::opt) holds, so the editors know which sheet to show (docs/widgets.md §2).
enum class Opt : uint8_t {
  None,
  Codes,    // markets: "gold:SJC,stocks:FPT,fx:USD,crypto:BTC" in order
  Note,     // a note id (s3.notes)
  Photo,    // an index into s3.photos
  Mine,     // a my-widget id (s3.mywidgets)
  Cities,   // world: city names joined by "," ("" = s3.cities)
  Section,  // news: a section slug ("" = all)
  Date,     // count: "YYYY-MM-DD|label"
  Todo,     // reminders
};

struct Kind {
  const char* id;
  const char* name;  // English, marked N_ (shown through translating helpers, or T_)
  const char* cat;   // gallery category (English; home.cpp's CATS compares it untranslated)
  uint8_t sizes;     // bit 0 small, 1 medium, 2 large
  const char* app;   // opened by a tap
  const char* desc;
  uint32_t tint;     // gallery tile colour
  std::vector<Style> styles;  // the looks it draws (the first is the default); empty: one look
  Opt opt;                    // what Item::opt holds
  std::vector<Style> flags;   // on/off options, their ids joined by "," in Item::opt (clock: 12, sec)
};
const std::vector<Kind>& kinds();
// The codes a gold, stocks, fx or crypto widget shows when its option is empty ("gold:SJC,gold:N24K"); "" for others.
String marketDefault(const String& kind);
const Kind* kind(const String& id);
// The kind can show now: an app's own widget only while its app is installed ("pet": PawBoop, app-slots.md §16).
// Home drops a widget whose app went; the gallery leaves it out.
bool installed(const String& kind);
// The item's style if its kind draws it, else the kind's default ("" for a kind with one look).
String styleOf(const Item& it);
// Whether a flag (Kind::flags) is on in the item's option ("12" in "12,sec").
bool flag(const Item& it, const char* id);
// The card looks every kind takes (Item::skin): "" the kind's own, dark, light, glass, none; plus "c:rrggbb"
// with a colour from skinColours().
const std::vector<Style>& skins();
const std::vector<uint32_t>& skinColours();

void boxSize(Size s, int& w, int& h);
int cols(Size s);  // grid cells: 2 or 4
int rows(Size s);  // 2 or 4

// ---- free sizes (docs/widgets.md §2b, mockups/widget-resize; web/app/src/os/model.ts and wid.ts the same)
// The item's span (its own, else its size's).
void spanOf(const Item& it, int& cw, int& ch);
// The card of a span: cw * 78 - 12 by ch * 74 - 10 (the same in portrait and landscape).
void boxSpan(int cw, int ch, int& w, int& h);
// The card of an item, by its span.
void boxOf(const Item& it, int& w, int& h);
// Whether a span is S, M or L (its letter in s), and the letter written for any span.
bool legacy(int cw, int ch, Size* s = nullptr);
Size letterOf(int cw, int ch);
// Sets the item's span (S, M, L become their letter, cw = ch = 0).
void setSpan(Item& it, int cw, int ch);
// Whether the kind draws a span (S, M, L by Kind::sizes, the others by its styles' spans); with a style, that look.
bool draws(const String& kind, int cw, int ch, const char* style = nullptr);
// The look drawn at the item's span: its style when that look draws the span, else the first that does.
String styleAt(const Item& it);

// Draws a widget into a new card of w x h (position it after). Taps open its app unless
// `interactive` is false (edit mode, gallery previews).
lv_obj_t* create(lv_obj_t* parent, const Item& it, int w, int h, bool interactive = true);
// Live parts (clock, countdowns): call every 500 ms; true at a new minute (the per-second parts, seconds hands and
// digits, update their own objects and do not count: Home's idle page pictures are not retaken every second).
bool tick();

// "My widgets" (s3.mywidgets): user-made notes, countdowns, quotes and lists.
struct Mine {
  String id, type;  // note | count | quote | list
  String title, text, date;
  uint32_t bg = 0xffe066, fg = 0x3b2f00;
  int px = 15;
  std::vector<std::pair<String, bool>> items;  // list entries (text, done)
};
std::vector<Mine> mine();
const Mine* findMine(const String& id);
void saveMine(const Mine& m);  // insert or replace by id
void deleteMine(const String& id);
void setPreview(const Mine* m);  // drawn instead of the saved one with the same id (builder)

}  // namespace wid
