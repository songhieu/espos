#pragma once
// The widget families' shared pieces (docs/widgets.md §1): the card, the label helpers, the colours of the
// "white" cards and the live parts. Every family file (src/wid/*.cpp) draws one kind into a Card with these;
// web/app/src/os/wid/kit.ts has the same names line by line.
#include <Arduino.h>
#include <lvgl.h>

#include <ctime>

#include "../uikit.h"
#include "../wid.h"

namespace wid {

struct Card {
  lv_obj_t* o;
  int w, h;  // content box
};

constexpr uint32_t WHITE = 0xffffff;
// Ink and paper of the "white" cards (calendar, news, reminders): dark cards with the dark theme (set by create()).
extern uint32_t INK, GREY, PAPER;

// A rounded card of w x h with padding; its content box is w - 2 padH by h - 2 padV.
Card frame(lv_obj_t* parent, int w, int h, uint32_t bg, int padH = 12, int padV = 10);
// The theme's see-through card (the clocks): glass colour, opacity and a thin white border.
void glass(lv_obj_t* c);
// The small upper-case heading at the top of a card, in w px (0: the rest of the row from x): too wide, its letters
// close up, then it wraps (never cut).
lv_obj_t* cap(lv_obj_t* p, const char* text, uint32_t color, int x = 0, int y = 0, int w = 0);
// Whether cap() keeps the heading on one line in w px (its letters closed up as far as cap() goes).
bool capFits(const char* text, int w);
// A label that shows the text in full inside w x h (shrinking, never cut).
// (Each helper takes a C string; the String forms below only forward, so literals build no String at the call.)
lv_obj_t* fit(lv_obj_t* p, const char* text, int x, int y, int w, int h, int maxPx, int minPx, bool bold, uint32_t color,
              int ls = 2);
lv_obj_t* at(lv_obj_t* p, const char* text, int x, int y, int px, bool bold, uint32_t color);
// Aligned to the top right of the content box (xo moves it left when negative).
lv_obj_t* right(lv_obj_t* p, const char* text, int y, int px, bool bold, uint32_t color, int xo = 0);
inline lv_obj_t* fit(lv_obj_t* p, const String& text, int x, int y, int w, int h, int maxPx, int minPx, bool bold,
                     uint32_t color, int ls = 2) {
  return fit(p, text.c_str(), x, y, w, h, maxPx, minPx, bold, color, ls);
}
inline lv_obj_t* at(lv_obj_t* p, const String& text, int x, int y, int px, bool bold, uint32_t color) {
  return at(p, text.c_str(), x, y, px, bold, color);
}
inline lv_obj_t* right(lv_obj_t* p, const String& text, int y, int px, bool bold, uint32_t color, int xo = 0) {
  return right(p, text.c_str(), y, px, bold, color, xo);
}
// The height a label's text takes (its lines and line space), to place what follows it.
int heightOf(lv_obj_t* l);
// fit(), only when the text fits w x h whole at minPx or more; nullptr (nothing drawn) otherwise. For layouts that
// pick what fits (another story, no lead, a smaller picture) instead of letting text run out of its box.
lv_obj_t* fitIn(lv_obj_t* p, const char* text, int x, int y, int w, int h, int maxPx, int minPx, bool bold,
                uint32_t color, int ls = 2);
// The height fit() would give the text in w x h, or -1 when it does not fit whole (nothing stays drawn).
int measure(lv_obj_t* p, const char* text, int w, int h, int maxPx, int minPx, bool bold, int ls = 2);
inline lv_obj_t* fitIn(lv_obj_t* p, const String& text, int x, int y, int w, int h, int maxPx, int minPx, bool bold,
                       uint32_t color, int ls = 2) {
  return fitIn(p, text.c_str(), x, y, w, h, maxPx, minPx, bold, color, ls);
}
inline int measure(lv_obj_t* p, const String& text, int w, int h, int maxPx, int minPx, bool bold, int ls = 2) {
  return measure(p, text.c_str(), w, h, maxPx, minPx, bold, ls);
}
// One line of a poster: its text, the size it would like, bold, colour.
struct PLine {
  const char* s;
  int px;
  bool bold;
  uint32_t color;
};
// The biggest font size up to px whose line fits w (letters only in the text sizes, up to 24); 0 when none does.
int posterPx(const char* s, int px, bool bold, int w);
// Centred lines in (x, y, w, h), each as big as its px and the width allow (numbers in the big digit fonts); while the
// block is taller than h, the biggest line steps down. A line that fits at no size is left out (never cut).
void poster(lv_obj_t* p, const PLine* lines, int n, int x, int y, int w, int h, int gap = 2);
// "Waiting for the next sync" or "Link EspOS to get data" (marked: the helpers translate it).
const char* noData();
// A grey message filling the card from y down.
void empty(Card c, const char* text, uint32_t color, int y);

// The card look of the widget being drawn (Item::skin, docs/widgets.md §2), set by create() before the kind
// draws. own: "" (the kind paints its own background and colours); otherwise the card is already painted and
// the kind draws its text in fg / dim (INK / GREY / PAPER follow the skin too).
struct Skin {
  bool own = true;
  bool none = false;   // no card at all (text straight on the background)
  bool glass = false;  // the theme's see-through card
  uint32_t bg = 0, fg = 0, dim = 0;
};
extern Skin SKIN;
Skin skinOf(const String& id);
void paintSkin(Card c, const Skin& s);
// Text colours for a kind whose own look is light text on its own background: those, or the skin's.
inline uint32_t onOwn(uint32_t own) { return SKIN.own ? own : SKIN.fg; }
inline uint32_t onOwnDim(uint32_t own) { return SKIN.own ? own : SKIN.dim; }

// The styles and flags of each kind (first style = default), defined by the kind's family file and listed by
// the registry in wid.cpp (the editors on the web and on the device show these names).
extern const std::vector<Style> CLOCK_STYLES, CLOCK_FLAGS, WORLD_STYLES, ALARM_STYLES;            // clock.cpp
extern const std::vector<Style> DATE_STYLES, CAL_STYLES, LUNAR_STYLES, COUNT_STYLES, QUOTE_STYLES;  // cal.cpp
extern const std::vector<Style> WEATHER_STYLES;                                                     // weather.cpp
extern const std::vector<Style> MARKETS_STYLES, PRICE_STYLES;                                       // market.cpp
extern const std::vector<Style> NEWS_STYLES;                                                        // news.cpp
extern const std::vector<Style> REMIND_STYLES, NOTES_STYLES, PHOTO_STYLES, NOTIFY_STYLES, SYSTEM_STYLES;  // misc.cpp
extern const std::vector<Style> READING_STYLES, STATS_STYLES, BOOKS_STYLES, BOOKS_FLAGS;            // read.cpp

// Live parts: fn(obj, now, newDay) runs at every new minute (every second with everySecond) while obj lives, with
// SKIN, INK, GREY and PAPER as they were when live() was called (the widget's own, not the last one drawn).
using LiveFn = void (*)(lv_obj_t* o, const tm& now, bool newDay);
void live(lv_obj_t* o, LiveFn fn, bool everySecond = false);
String hmNow();                 // "09:21"
String dateLine(bool withLunar);  // "T2 16/11" (+ " · 8/10 âm lịch")

// The families: each draws its kind into the card c (parent is the page, for widgets that need it).
void wClock(lv_obj_t* parent, const Item& it, Card c);    // src/wid/clock.cpp
void wWorld(lv_obj_t* parent, const Item& it, Card c);
void wAlarm(lv_obj_t* parent, const Item& it, Card c);
void wCal(lv_obj_t* parent, const Item& it, Card c);      // src/wid/cal.cpp
void wLunar(lv_obj_t* parent, const Item& it, Card c);
void wCount(lv_obj_t* parent, const Item& it, Card c);
void wDate(lv_obj_t* parent, const Item& it, Card c);
void wQuote(lv_obj_t* parent, const Item& it, Card c);
void wWeather(lv_obj_t* parent, const Item& it, Card c);  // src/wid/weather.cpp
void wMarkets(lv_obj_t* parent, const Item& it, Card c);  // src/wid/market.cpp
void wQuotes(lv_obj_t* parent, const Item& it, Card c);   // gold, stocks, fx, crypto
void wNews(lv_obj_t* parent, const Item& it, Card c);     // src/wid/news.cpp
void wRemind(lv_obj_t* parent, const Item& it, Card c);   // src/wid/misc.cpp
void wNotes(lv_obj_t* parent, const Item& it, Card c);
void wPhoto(lv_obj_t* parent, const Item& it, Card c);
void wMine(lv_obj_t* parent, const Item& it, Card c);
void wNotify(lv_obj_t* parent, const Item& it, Card c);
void wSystem(lv_obj_t* parent, const Item& it, Card c);
void wReading(lv_obj_t* parent, const Item& it, Card c);  // src/wid/read.cpp
void wStats(lv_obj_t* parent, const Item& it, Card c);
void wBooks(lv_obj_t* parent, const Item& it, Card c);
void wOverview(lv_obj_t* parent, const Item& it, Card c);

}  // namespace wid
