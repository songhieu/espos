#pragma once

#include <GfxRenderer.h>

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "inkdesk/ReadingStats.h"

// Widget layouts for Home and the sleep screen, arranged on the InkDesk web by drag and drop
// and drawn here in 1-bit e-ink style. A layout is a grid (Home 4x7, sleep landscape 6x4,
// sleep portrait 4x7) of items: [kind, x, y, w, h, style, option, flags], with a frame style
// for the whole screen, stored as compact JSON in the settings home.w, sleep.wl and sleep.wp
// (docs/inkdesk-api.md, "Widgets").
namespace inkdesk::widgets {

enum class Kind : uint8_t {
  Clock, Date, Month, Events, Countdown, Weather, Reading, Stats, News, Notify, Battery, App,
  Gold, Stocks, Fx, Crypto, Books, Photo, Overview, Setup, Unknown,
};

// Frame style of a screen: rounded cards, ruled paper, bare, bold squares, cards with a shadow.
enum class Skin : uint8_t { Card, Line, Bare, Bold, Shadow };

struct Item {
  Kind kind = Kind::Unknown;
  uint8_t x = 0, y = 0, w = 1, h = 1;
  bool inverted = false;  // drawn white on black
  std::string style;
  std::string opt;
};

enum class Screen : uint8_t { Home, SleepLandscape, SleepPortrait };

struct Layout {
  Screen screen = Screen::Home;
  int cols = 4;
  int rows = 7;
  bool footer = true;  // sleep screens: battery and data time along the bottom
  Skin skin = Skin::Card;
  std::vector<Item> items;
};

// The layout set on the web for this screen, or the built-in one.
Layout load(Screen screen, bool linked = true);
Layout builtin(Screen screen, bool linked = true);
bool parse(const std::string& json, Screen screen, Layout& out);

// What the widgets show, read once per screen from the SD caches and stores.
struct Data {
  bool linked = false;
  bool timeValid = false;
  bool showTime = true;  // false on a sleep screen that is not ticking: clocks show the date
  struct tm now {};
  uint32_t showSeq = 0;  // counts the times the screen was opened (photo widgets "each time")
  std::string lunar, lunarYear;  // "6/10", "Bính Ngọ"

  struct Hour {
    std::string h, icon;
    int t = 0;
  };
  struct Day {
    std::string d, icon;
    int min = 0, max = 0;
  };
  bool hasWeather = false;
  std::string city, desc, icon;
  int temp = 0, tmin = 0, tmax = 0, rain = 0;
  std::vector<Hour> hours;
  std::vector<Day> days;

  struct Event {
    std::string time, title;
  };
  std::vector<Event> events;
  int eventCount = 0;
  bool eventsOld = false;  // the cache is from an earlier day

  struct Note {
    std::string title, time;
  };
  std::vector<Note> notes;

  bool hasBook = false;
  std::string bookPath, bookTitle, bookAuthor, coverPath;
  int bookPercent = -1;
  stats::Summary stats;

  // Books read lately, most recent first (the library widget).
  struct Book {
    std::string path, title, author, coverBmp;  // coverBmp: the book's cover, thumbnails are made from it
    std::string thumb;                          // the thumbnail at the height the shelf draws (ensureCovers)
    int percent = -1;
  };
  std::vector<Book> library;

  std::string newsPath, newsDay, newsTitle;
  std::vector<std::string> newsTop;
  int newsCount = 0;

  struct Quote {
    std::string code, name;
    double buy = 0, sell = 0, change = 0;  // million VND per tael
  };
  std::vector<Quote> gold;
  std::string goldAt, goldSrc;
  struct Ticker {
    std::string sym;
    double price = 0, change = 0, pct = 0;
  };
  std::vector<Ticker> stocks;
  std::string stocksAt;
  struct Rate {
    std::string code;
    double buy = 0, sell = 0;
  };
  std::vector<Rate> fx;
  std::string fxAt;
  struct Coin {
    std::string sym;
    double usd = 0, vnd = 0, pct = 0;  // pct: 24 h change
  };
  std::vector<Coin> crypto;
  std::string cryptoAt;

  int battery = 0;
  int64_t lastSyncMs = 0;

  int bookFocus = -1;  // the library widget's book marked as focused on Home
};

// Fills `d` (the reading card's cover thumbnail is looked up at `coverH`).
void loadData(Data& d, int coverH);

struct Box {
  int x, y, w, h;
};
// Pixel boxes of the layout's items inside the given area (the shadow style draws widgets smaller).
std::vector<Box> place(const Layout& l, int left, int top, int right, int bottom, int gap);
// The same boxes for the screen the layout belongs to, as Home and the sleep screens place them.
std::vector<Box> boxesFor(const Layout& l);

// Grid areas of the screens (also used by the web editor, keep them in step).
constexpr int HOME_SIDE = 12, HOME_TOP = 44, HOME_BOTTOM = 48, HOME_GAP = 8;  // HOME_BOTTOM: room for the button hints
constexpr int SLEEP_PAD = 12, SLEEP_GAP = 10, SLEEP_FOOTER_H = 40;
constexpr int SLEEP_TOP = 44;  // below the status bar (sleep screens with their bars on)
constexpr int SHADOW = 5;

void draw(GfxRenderer& r, const Item& it, const Box& b, const Data& d, bool focused, Skin skin = Skin::Card);

// Book covers the layout shows (current book, library shelf) at the heights it draws them;
// makes the missing thumbnails (slow the first time) and points `d` at them.
// Returns true when there was work to do.
bool ensureCovers(const Layout& l, const std::vector<Box>& boxes, Data& d, bool dryRun = false);

// Photo widgets: the picture files a layout needs on the SD card (one per picture and box size).
struct PhotoFile {
  int32_t id;
  std::string dim;     // "456x298"
  std::string params;  // "ca50"
  std::string path;
};
std::vector<PhotoFile> photoFiles(const Layout& l, const std::vector<Box>& boxes);
constexpr const char* PHOTO_DIR = "/.crosspoint/inkdesk/photos";
// "Screen opened" numbers for photo widgets that change each time (kept across deep sleep):
// next = the screen was opened again, otherwise the current number (a redraw of the same showing).
uint32_t showSeq(bool next);

enum class Action : uint8_t { None, Reader, Library, News, Stats, Sync, Transfer, Apps, Settings, Update, Setup };
// What OK on this widget opens (None: the widget cannot take focus).
Action action(const Item& it);
// Library widgets: how many books the widget shows in box `b`, and the path of the i-th (Home steps
// through them and opens the chosen one).
int bookCount(GfxRenderer& r, const Item& it, const Box& b, const Data& d);
std::string bookPath(const Item& it, const Data& d, int index);

}  // namespace inkdesk::widgets
