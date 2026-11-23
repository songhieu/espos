#pragma GCC optimize("Os")  // widgets/JS glue built once per change: size over speed (0.5.7 flash diet)
// The widget core (docs/widgets.md): the registry, create() and the my-widgets store. The kinds are drawn by
// the family files in src/wid/ with the shared pieces of src/wid/kit.h; web/app/src/os/wid.ts mirrors this file.
#include "wid.h"

#include <ArduinoJson.h>

#include "apps.h"
#include "data.h"
#include "desk.h"
#ifndef NO_PET
#include "pet/pet.h"
#endif
#include "theme.h"
#include "wid/kit.h"

namespace wid {
namespace {

using namespace kit;



void onTap(lv_event_t* e) {
  const char* app = (const char*)lv_event_get_user_data(e);
  if (app && *app) apps::open(app);
}

}  // namespace

// The registry (docs/widgets.md §3): built on first use, after every family's lists exist.
const std::vector<Kind>& kinds() {
  // Names, gallery categories (home.cpp's CATS must hold the same English) and descriptions: marked, the editors
  // and the gallery translate them.
  static const std::vector<Kind> K = {
      {"clock", N_("Clock"), N_("Clock"), 7, "clock", N_("time and date"), 0x2b2f55, CLOCK_STYLES, Opt::None, CLOCK_FLAGS},
      {"date", N_("Daily calendar"), N_("Calendar"), 7, "lunar", N_("tear-off sheet, lunar date"), 0xd7261e, DATE_STYLES,
       Opt::None, {}},
      {"cal", N_("Calendar"), N_("Calendar"), 7, "cal", N_("week, month"), 0xffffff, CAL_STYLES, Opt::None, {}},
      {"lunar", N_("Lunar"), N_("Calendar"), 7, "lunar", N_("zodiac days, Lunar New Year"), 0xb3161c, LUNAR_STYLES,
       Opt::None, {}},
      {"count", N_("Countdown"), N_("Calendar"), 3, "lunar", N_("Lunar New Year, any date"), 0xd7261e, COUNT_STYLES,
       Opt::Date, {}},
      {"quote", N_("Sayings"), N_("Calendar"), 6, "", N_("a saying a day"), 0x7c2d12, QUOTE_STYLES, Opt::None, {}},
      {"weather", N_("Weather"), N_("Weather"), 7, "weather", N_("next hours, next days"), 0x2f7ff0, WEATHER_STYLES,
       Opt::None, {}},
      {"markets", N_("Markets"), N_("Markets"), 7, "markets", N_("gold, stocks"), 0x11161f, MARKETS_STYLES, Opt::Codes,
       {}},
      {"news", N_("Latest news"), N_("News"), 7, "news", N_("latest news, with photos"), 0xffffff, NEWS_STYLES,
       Opt::Section, {}},
      {"alarm", N_("Alarm"), N_("Clock"), 3, "clock", N_("next alarm"), 0x11161f, ALARM_STYLES, Opt::None, {}},
      {"world", N_("World clock"), N_("Clock"), 3, "clock", N_("cities"), 0x11161f, WORLD_STYLES, Opt::Cities, {}},
      {"remind", N_("Reminders"), N_("Calendar"), 7, "remind", N_("to-dos"), 0xffffff, REMIND_STYLES, Opt::None, {}},
      {"notes", N_("Notes"), N_("News"), 3, "notes", N_("latest note"), 0xffe066, NOTES_STYLES, Opt::Note, {}},
      {"photo", N_("Photos"), N_("Photos"), 7, "photos", N_("your photos"), 0x5856d6, PHOTO_STYLES, Opt::Photo, {}},
      {"my", N_("My widget"), N_("Custom"), 7, "", N_("made on the device"), 0xffe066, {}, Opt::Mine, {}},
#ifndef NO_PET
      {"pet", "PawBoop", N_("Fun"), 7, "pet", N_("Biscuit and her needs"), 0xfff3df, {}, Opt::None, {}},
#endif
      {"notify", N_("Notifications"), N_("Tools"), 6, "", N_("latest notifications"), 0xffffff, NOTIFY_STYLES, Opt::None,
       {}},
      {"system", N_("Device"), N_("Tools"), 3, "settings", N_("battery, WiFi, sync"), 0x11161f, SYSTEM_STYLES, Opt::None,
       {}},
      // The e-reader's widgets (docs/widgets-parity.md): its four markets, reading, the overview.
      {"gold", N_("Gold price"), N_("Markets"), 7, "markets", N_("buy, sell"), 0xb8860b, PRICE_STYLES, Opt::Codes, {}},
      {"stocks", N_("Stocks"), N_("Markets"), 7, "markets", N_("indexes, shares"), 0x0f766e, PRICE_STYLES, Opt::Codes,
       {}},
      {"fx", N_("Exchange rates"), N_("Markets"), 7, "markets", N_("Vietcombank currencies"), 0x1e3a8a, PRICE_STYLES,
       Opt::Codes, {}},
      {"crypto", N_("Crypto"), N_("Markets"), 7, "markets", "Bitcoin, Ethereum", 0xf7931a, PRICE_STYLES, Opt::Codes, {}},
      {"reading", N_("Now reading"), N_("Reading"), 7, "", N_("the book on your e-reader"), 0xc2410c, READING_STYLES,
       Opt::None, {}},
      {"stats", N_("Reading stats"), N_("Reading"), 7, "", N_("minutes read, streak"), 0x34c759, STATS_STYLES,
       Opt::None, {}},
      {"books", N_("Book library"), N_("Reading"), 7, "", N_("recently read books"), 0x7c2d12, BOOKS_STYLES, Opt::None,
       BOOKS_FLAGS},
      {"overview", N_("Overview"), N_("Clock"), 6, "cal", N_("time, date, weather, calendar"), 0xffffff, {}, Opt::None,
       {}},
      // EspOS Desktop (2026-11-21): the Claude usage of the person's Mac (dashboard `claude`).
      {"claude", "Claude", N_("Tools"), 7, "", N_("usage from your Mac"), 0xd97757, CLAUDE_STYLES, Opt::None, {}},
  };
  return K;
}

bool installed(const String& k) { return k != "pet" || apps::find("pet"); }

const Kind* kind(const String& id) {
  for (const Kind& k : kinds())
    if (id == k.id) return &k;
  return nullptr;
}

String styleOf(const Item& it) {
  const Kind* k = kind(it.kind);
  if (!k || k->styles.empty()) return "";
  for (const Style& s : k->styles)
    if (it.style == s.id) return it.style;
  return k->styles[0].id;
}

bool flag(const Item& it, const char* id) {
  for (const String& f : data::codesOf(it.opt, ""))
    if (f == id) return true;
  return false;
}

const std::vector<Style>& skins() {
  static const std::vector<Style> S_ = {
      {"", N_("Default")}, {"dark", N_("Dark")}, {"light", N_("Light")}, {"glass", N_("Glass")}, {"none", N_("No card")}};
  return S_;
}

const std::vector<uint32_t>& skinColours() {
  static const std::vector<uint32_t> C_ = {0xd7261e, 0xff9500, 0xffcc00, 0x34c759, 0x0f766e, 0x0a84ff,
                                           0x1e3a8a, 0x5856d6, 0xaf52de, 0xff2d55, 0x8e8e93, 0x3a3a3c};
  return C_;
}

void boxSize(Size s, int& w, int& h) {
  w = s == S ? 144 : 300;
  h = s == L ? 286 : 138;
}
int cols(Size s) { return s == S ? 2 : 4; }
int rows(Size s) { return s == L ? 4 : 2; }

lv_obj_t* create(lv_obj_t* parent, const Item& it, int w, int h, bool interactive) {
  const String& k = it.kind;
  const bool dk = theme::kind() == theme::Kind::Dark;
  INK = dk ? 0xeef1f5 : 0x16181d;
  GREY = dk ? 0x9aa3b2 : 0x6b7280;
  PAPER = dk ? 0x1c2029 : 0xffffff;
  uint32_t bg = PAPER;
  if (k == "markets" || k == "alarm" || k == "world" || k == "system" || marketDefault(k).length()) bg = 0x11161f;
  Card c = frame(parent, w, h, bg);
  // The item's card look: a painted card with its text colours, or "" (the kind's own).
  SKIN = skinOf(it.skin);
  if (!SKIN.own) {
    paintSkin(c, SKIN);
    INK = SKIN.fg, GREY = SKIN.dim, PAPER = SKIN.bg;
  }
  if (k == "clock") wClock(parent, it, c);
  else if (k == "weather") wWeather(parent, it, c);
  else if (k == "cal") wCal(parent, it, c);
  else if (k == "lunar") wLunar(parent, it, c);
  else if (k == "markets") wMarkets(parent, it, c);
  else if (k == "news") wNews(parent, it, c);
  else if (k == "alarm") wAlarm(parent, it, c);
  else if (k == "world") wWorld(parent, it, c);
  else if (k == "remind") wRemind(parent, it, c);
  else if (k == "notes") wNotes(parent, it, c);
  else if (k == "photo") wPhoto(parent, it, c);
  else if (k == "count") wCount(parent, it, c);
  else if (k == "date") wDate(parent, it, c);
  else if (k == "quote") wQuote(parent, it, c);
  else if (k == "notify") wNotify(parent, it, c);
  else if (k == "system") wSystem(parent, it, c);
  else if (k == "my") wMine(parent, it, c);
  else if (marketDefault(k).length()) wQuotes(parent, it, c);
  else if (k == "reading") wReading(parent, it, c);
  else if (k == "stats") wStats(parent, it, c);
  else if (k == "books") wBooks(parent, it, c);
  else if (k == "overview") wOverview(parent, it, c);
  else if (k == "claude") wClaude(parent, it, c);
#ifndef NO_PET
  else if (k == "pet") {
    if (apps::find("pet")) pet::widget(c.o, w, h, it.size);
    else lv_obj_add_flag(c.o, LV_OBJ_FLAG_HIDDEN);  // PawBoop removed: nothing of the dog (a saver page's slot stays empty)
  }
#endif
  else {
    cap(c.o, k.c_str(), GREY);
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(c.o); i++) lv_obj_remove_flag(lv_obj_get_child(c.o, i), LV_OBJ_FLAG_CLICKABLE);
  if (interactive) {
    const Kind* kd = kind(k);
    const char* app = it.open.length() ? nullptr : (kd ? kd->app : "");
    if (it.open.length()) {
      const apps::App* a = apps::find(it.open.c_str());
      app = a ? a->id : "";
    }
    if (app && *app) {
      lv_obj_add_flag(c.o, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(c.o, LV_OPA_80, LV_STATE_PRESSED);
      lv_obj_add_event_cb(c.o, onTap, LV_EVENT_SHORT_CLICKED, (void*)app);
    }
  }
  return c.o;
}

// ---------------------------------------------------------------- my widgets store
std::vector<Mine> mine() {
  std::vector<Mine> out;
  JsonDocument doc;
  if (!data::loadJson("s3.mywidgets", doc)) return out;
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    Mine m;
    m.id = o["id"] | "";
    m.type = o["t"] | "note";
    m.title = o["title"] | "";
    m.text = o["text"] | "";
    m.date = o["date"] | "";
    m.bg = strtoul(o["bg"] | "ffe066", nullptr, 16);
    m.fg = strtoul(o["fg"] | "3b2f00", nullptr, 16);
    m.px = o["px"] | 15;
    for (JsonArrayConst e : o["items"].as<JsonArrayConst>()) m.items.push_back({String(e[0] | ""), (e[1] | 0) != 0});
    out.push_back(m);
  }
  return out;
}

const Mine* previewMine = nullptr;
void setPreview(const Mine* m) { previewMine = m; }

const Mine* findMine(const String& id) {
  if (previewMine && previewMine->id == id) return previewMine;
  static std::vector<Mine> cache;
  static uint32_t seen = UINT32_MAX;
  if (seen != desk::settingsVersion()) {
    cache = mine();
    seen = desk::settingsVersion();
  }
  for (const Mine& m : cache)
    if (m.id == id) return &m;
  return nullptr;
}

namespace {
void writeMine(const std::vector<Mine>& all) {
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (const Mine& m : all) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = m.id;
    o["t"] = m.type;
    if (m.title.length()) o["title"] = m.title;
    if (m.text.length()) o["text"] = m.text;
    if (m.date.length()) o["date"] = m.date;
    char b[8];
    snprintf(b, sizeof b, "%06lx", (unsigned long)m.bg);
    o["bg"] = b;
    snprintf(b, sizeof b, "%06lx", (unsigned long)m.fg);
    o["fg"] = b;
    o["px"] = m.px;
    if (m.items.size()) {
      JsonArray it = o["items"].to<JsonArray>();
      for (const auto& e : m.items) {
        JsonArray x = it.add<JsonArray>();
        x.add(e.first);
        x.add(e.second ? 1 : 0);
      }
    }
  }
  data::saveJson("s3.mywidgets", doc);
}
}  // namespace

void saveMine(const Mine& m) {
  auto all = mine();
  bool found = false;
  for (Mine& x : all)
    if (x.id == m.id) x = m, found = true;
  if (!found) all.push_back(m);
  writeMine(all);
}

void deleteMine(const String& id) {
  auto all = mine();
  for (auto it = all.begin(); it != all.end(); ++it)
    if (it->id == id) {
      all.erase(it);
      break;
    }
  writeMine(all);
}

}  // namespace wid
