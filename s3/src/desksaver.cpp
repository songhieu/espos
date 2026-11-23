#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// The saver's pages (desksaver.h, docs/saver.md §2): the model, its JSON, and one renderer for every page.
#include "desksaver.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <vector>
#ifdef SIM
#include <fstream>
#include <iterator>
#include <string>
extern std::string simDir;
#else
#include <LittleFS.h>
#endif

#include "alarm.h"
#include "art.h"
#include "data.h"
#include "desk.h"
#include "slots.h"
#include "fonts.h"
#include "jpeg.h"
#include "os.h"
#include "photo.h"
#if __has_include("tank/tank.h")
#include "tank/tank.h"
#endif
#include "theme.h"
#include "trace.h"
#include "uikit.h"
#include "wid.h"

extern "C" uint32_t lv_text_get_next_line(const char* txt, const lv_font_t* font, int32_t letter_space,
                                          int32_t max_width, int32_t* used_width, lv_text_flag_t flag);

namespace desksaver {
namespace {

using namespace kit;

struct Spiram : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} spiram;

// ---------------------------------------------------------------- defaults and templates
// The defaults draw exactly today's desk saver (clock top 18, date right under it, the day's widgets).
const char* DEF =
    R"({"p":[{"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":15,"w":320,"h":100},)"
    R"({"t":"date","f":"wdml","x":0,"y":110,"w":320,"h":24,"c":"9aa3ad","px":15},{"t":"w","k":"weather","s":"S","x":8,"y":172},)"
    R"({"t":"w","k":"cal","s":"S","x":168,"y":172},{"t":"w","k":"remind","s":"M","x":10,"y":322}]}],)"
    R"("l":[{"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":89,"w":320,"h":100},)"
    R"({"t":"date","f":"wdml","x":0,"y":186,"w":320,"h":24,"c":"9aa3ad","px":15},{"t":"w","k":"weather","s":"S","x":328,"y":16},)"
    R"({"t":"w","k":"cal","s":"S","x":328,"y":162}]}]})";
// The built-in night pages: a dim red bedside clock and the next alarm.
const char* NIGHT_P =
    R"({"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":171,"w":320,"h":100,"c":"d92d20"},)"
    R"({"t":"alarm","x":0,"y":270,"w":320,"h":24,"c":"8a1a12","px":15}]})";
const char* NIGHT_L =
    R"({"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":111,"w":320,"h":100,"c":"d92d20"},)"
    R"({"t":"alarm","x":0,"y":208,"w":320,"h":24,"c":"8a1a12","px":15}]})";
// The web's templates "Tối giản" (s3.saver_mode "clock") and "Khung ảnh" ("photo").
const char* MINIMAL =
    R"({"p":[{"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":150,"w":320,"h":100,"c":"c9d0d8"},)"
    R"({"t":"date","f":"full","x":0,"y":256,"w":320,"h":24,"c":"9aa3ad"}]}],)"
    R"("l":[{"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":90,"w":480,"h":100,"c":"c9d0d8"},)"
    R"({"t":"date","f":"full","x":0,"y":196,"w":480,"h":24,"c":"9aa3ad"}]}]})";
const char* FRAME =
    R"({"p":[{"bg":{"t":"photo","id":0},"e":[{"t":"box","x":16,"y":16,"w":148,"h":60,"c":"000000","o":50,"r":12},)"
    R"({"t":"time","x":16,"y":16,"w":148,"h":60,"c":"ffffff"}]}],)"
    R"("l":[{"bg":{"t":"photo","id":0},"e":[{"t":"box","x":16,"y":16,"w":148,"h":60,"c":"000000","o":50,"r":12},)"
    R"({"t":"time","x":16,"y":16,"w":148,"h":60,"c":"ffffff"}]}]})";

// ---------------------------------------------------------------- JSON
uint32_t hexOf(const char* s, uint32_t def) {
  if (!s || strlen(s) != 6) return def;
  char* end;
  const uint32_t v = strtoul(s, &end, 16);
  return *end ? def : v;
}

String hexStr(uint32_t c) {
  char b[8];
  snprintf(b, sizeof b, "%06lx", (unsigned long)(c & 0xffffff));
  return b;
}

// One copy of the JSON access code (inlined per key it cost kilobytes of flash).
__attribute__((noinline)) int num(JsonObjectConst o, const char* k, int def, int lo, int hi) {
  JsonVariantConst v = o[k];
  return constrain(v.is<int>() ? v.as<int>() : def, lo, hi);
}
__attribute__((noinline)) const char* str(JsonObjectConst o, const char* k, const char* def) { return o[k] | def; }

void readEl(JsonObjectConst o, El& e) {
  e.t = str(o, "t", "");
  e.x = num(o, "x", 0, -480, 480);
  e.y = num(o, "y", 0, -480, 480);
  e.w = num(o, "w", 120, 1, 480);  // a line is a box 1 px thin (docs/saver.md §2)
  e.h = num(o, "h", 40, 1, 480);
  e.c = hexOf(str(o, "c", ""), 0xe8ecf2);
  e.o = num(o, "o", 100, 10, 100);
  e.a = *str(o, "a", "c");
  if (e.a != 'l' && e.a != 'r') e.a = 'c';
  e.b = num(o, "b", 0, 0, 1);
  e.px = num(o, "px", 0, 0, 96);
  e.f = str(o, "f", "");
  e.r = num(o, "r", 0, 0, 160);
  e.id = num(o, "id", 0, 0, 1 << 30);
  if (e.t == "w") {
    e.k = str(o, "k", "weather");
    e.st = str(o, "st", "");
    e.op = str(o, "op", "");
    e.sk = str(o, "sk", "");
    setWidgetSize(e, *str(o, "s", "S"));
  } else if (e.t == "photo") {
    for (JsonVariantConst v : o["ids"].as<JsonArrayConst>())
      if (v.as<int>() > 0 && e.ids.size() < 12) e.ids.push_back(v.as<int>());
    if (e.ids.size() < 2) e.ids.clear();  // one picture: "id" as before
    else if (!e.id) e.id = e.ids[0];
    e.all = num(o, "all", 0, 0, 1);
    e.iv = num(o, "iv", 60, 10, 900);
    e.ord = *str(o, "ord", "s") == 'r' ? 'r' : 's';
    e.tr = *str(o, "tr", "c") == 'f' ? 'f' : 'c';
    e.of = num(o, "of", 0, 0, 11);
  }
  if (e.text()) {
    if (e.t == "text") e.s = str(o, "s", "");
    const uint32_t bg = hexOf(str(o, "bg", ""), 0x1000000);
    e.band = bg != 0x1000000;
    e.bgc = e.band ? bg : 0;
    e.bo = num(o, "bo", 50, 10, 100);
    e.sh = num(o, "sh", 0, 0, 1);
  }
}

void readPage(JsonObjectConst o, Page& pg) {
  readBg(o["bg"], pg.bg);
  pg.e.clear();
  int shows = 0;
  for (JsonObjectConst eo : o["e"].as<JsonArrayConst>()) {
    if (pg.e.size() >= 24) break;
    El e;
    readEl(eo, e);
    if (e.slides() && ++shows > 3) e.ids.clear(), e.all = false;  // at most 3 slideshows a page (docs/saver.md §2)
    if (e.t.length()) pg.e.push_back(e);
  }
}

void readPages(JsonArrayConst a, std::vector<Page>& out) {
  out.clear();
  for (JsonObjectConst o : a) {
    if (out.size() >= 3) break;
    out.emplace_back();
    readPage(o, out.back());
  }
}

__attribute__((noinline)) void put(JsonObject o, const char* k, int v) { o[k] = v; }
__attribute__((noinline)) void puts_(JsonObject o, const char* k, const String& v) { o[k] = v; }

void writeEl(JsonObject o, const El& e) {
  puts_(o, "t", e.t);
  put(o, "x", e.x);
  put(o, "y", e.y);
  if (e.t == "w") {
    puts_(o, "k", e.k);
    puts_(o, "s", String(e.sz));
    if (e.st.length()) puts_(o, "st", e.st);
    if (e.op.length()) puts_(o, "op", e.op);
    if (e.sk.length()) puts_(o, "sk", e.sk);
    return;
  }
  put(o, "w", e.w);
  put(o, "h", e.h);
  if (e.c != 0xe8ecf2) puts_(o, "c", hexStr(e.c));
  if (e.o != 100) put(o, "o", e.o);
  if (e.a != 'c') puts_(o, "a", String(e.a));
  if (e.b) put(o, "b", 1);
  if (e.px) put(o, "px", e.px);
  if (e.f.length()) puts_(o, "f", e.f);
  if (e.t == "text") puts_(o, "s", e.s);
  if (e.r) put(o, "r", e.r);
  if (e.t == "photo") put(o, "id", e.id);
  if (e.slides()) {
    if (e.all) put(o, "all", 1);
    else {
      JsonArray a = o["ids"].to<JsonArray>();
      for (int id : e.ids) a.add(id);
    }
    if (e.iv != 60) put(o, "iv", e.iv);
    if (e.ord == 'r') puts_(o, "ord", "r");
    if (e.tr == 'f') puts_(o, "tr", "f");
    if (e.of) put(o, "of", e.of);
  }
  if (e.band) {
    puts_(o, "bg", hexStr(e.bgc));
    if (e.bo != 50) put(o, "bo", e.bo);
  }
  if (e.sh) put(o, "sh", 1);
}

void writePage(JsonObject o, const Page& pg) {
  writeBg(o["bg"].to<JsonObject>(), pg.bg);
  JsonArray a = o["e"].to<JsonArray>();
  for (const El& e : pg.e) writeEl(a.add<JsonObject>(), e);
}

// ---------------------------------------------------------------- text
// i18n: month names as the screen saver's month line spells them (vi "Tháng Một", "Tháng Hai"... "Tháng Mười Hai")
const char* const MONTH_EN[12] = {"January", "February", "March",     "April",   "May",      "June",
                                  "July",    "August",   "September", "October", "November", "December"};
const i18n::List MONTH_WORDS = {"@months_spelled", MONTH_EN, 12};

String textOf(const El& e, const tm& t) {
  const String& f = e.f;
  char b[160];
  const int d = t.tm_mday, m = t.tm_mon + 1;
  const char* wd = WEEKDAY[t.tm_wday];
  b[0] = 0;
  if (e.t == "time") {
    if (f == "12")
      snprintf(b, sizeof b, "%d:%02d %s", t.tm_hour % 12 ? t.tm_hour % 12 : 12, t.tm_min, t.tm_hour < 12 ? T_("AM") : T_("PM"));
    else snprintf(b, sizeof b, f == "24h1" ? "%d:%02d" : "%02d:%02d", t.tm_hour, t.tm_min);
  } else if (e.t == "date") {
    const int y = t.tm_year + 1900;
    if (f == "month") return upperVi(MONTH_WORDS[m - 1]);
    if (f == "dm") return i18n::date(N_("{M} {d}"), d, m, y);
    else if (f == "wd" || f == "wdu") return f == "wdu" ? upperVi(wd) : String(wd);  // wdu: MONDAY
    else if (f == "day") snprintf(b, sizeof b, "%d", d);
    else if (f == "full" || f == "fullu") {  // fullu: MONDAY, NOVEMBER 16 (docs/saver.md §2)
      const String s = i18n::date(N_("{W}, {MM} {d}"), d, m, y, t.tm_wday);
      return f == "fullu" ? upperVi(s) : s;
    }
    else if (f == "wdmy") return i18n::date(N_("{W}, {M} {d}, {y}"), d, m, y, t.tm_wday);
    else if (f == "wdml")
      return i18n::date(N_("{W}, {M} {d}"), d, m, y, t.tm_wday) + " · " +
             i18n::fmt(TC_("long", "{0} (lunar)"), data::lunarText(data::lunar(data::today())));
    else return i18n::date(N_("{W}, {M} {d}"), d, m, y, t.tm_wday);
  } else if (e.t == "lunar") {
    const data::Lunar l = data::lunar(data::today());
    if (f == "cc") return data::canChiYear(l.y);
    if (f == "full")  // i18n: {0} a lunar day and month, {1} the year's stems and branches (vi "{0} năm {1}")
      return i18n::fmt(N_("{0} of the {1} year"),
                       // i18n: a lunar day and month in words; {2} is " leap" or nothing (vi "Ngày {0} tháng {1}{2}")
                       i18n::fmt(TC_("saver", "Day {0}, month {1}{2}"), String(l.d), String(l.m), l.leap ? T_(" leap") : ""),
                       data::canChiYear(l.y));
    else return i18n::fmt(TC_("long", "{0} (lunar)"), data::lunarText(l));
  } else if (e.t == "text") {
    return e.s;
  } else if (e.t == "alarm") {
    String when, day;
    alarms::next(when, day);
    if (when.length()) return i18n::fmt(N_("Alarm {0}"), when);
  } else if (e.t == "temp") {
    JsonObjectConst w = data::weather();
    if (!w.isNull() && !w["temp"].isNull()) {
      const int deg = w["temp"] | 0;
      const char* desc = w["desc"] | "";
      if (f == "t") snprintf(b, sizeof b, "%d°", deg);
      else if (f == "d") return desc;
      else snprintf(b, sizeof b, "%d° %s", deg, desc);
    }
  } else if (e.t == "next") {
    const String today = data::isoDate(data::today());
    char now[8];
    snprintf(now, sizeof now, "%02d:%02d", t.tm_hour, t.tm_min);
    for (const data::Event& ev : data::agenda()) {
      if (ev.date.length() && ev.date != today) continue;
      if (ev.time.length() == 5 && strcmp(ev.time.c_str(), now) >= 0) {
        snprintf(b, sizeof b, T_("Next %s · %s"), ev.time.c_str(), ev.title.c_str());
        break;
      }
    }
  }
  size_t n = strlen(b);  // a long event title: never half a letter at the end
  if (n >= sizeof b - 1) {
    while (n && ((uint8_t)b[n - 1] & 0xC0) == 0x80) n--;
    if (n && ((uint8_t)b[n - 1] & 0xC0) == 0xC0) n--;
    b[n] = 0;
  }
  return b;
}

// The fonts of the device (fonts.h): display sizes (digits) for the time and the day number, text sizes otherwise.
const uint8_t TEXT_R[] = {20, 18, 16, 15, 14, 13, 12}, TEXT_B[] = {24, 22, 20, 18, 17, 16, 15, 14, 13, 12};
const uint8_t DISP_R[] = {96, 84, 64, 40}, DISP_B[] = {96, 64, 56, 44, 34, 28};  // bold 96 since 2026-11-16 (saver.md §2)

bool display(const El& e) { return e.t == "time" || (e.t == "date" && e.f == "day"); }

const lv_font_t* fontOf(const El& e, int px) { return display(e) ? fonts::get(px, e.b) : fonts::text(px, e.b); }

// Lines of `s` broken at w with font f (never re-wrapped later); returns the count.
int breakLines(const char* s, const lv_font_t* f, int w, String& out) {
  out = "";
  int n = 0;
  for (const char* p = s; *p;) {
    int32_t used = 0;
    const uint32_t len = lv_text_get_next_line(p, f, 0, w, &used, LV_TEXT_FLAG_NONE);
    if (!len) break;
    uint32_t keepLen = len;
    while (keepLen && (p[keepLen - 1] == ' ' || p[keepLen - 1] == '\n')) keepLen--;
    if (n++) out += '\n';
    out.concat(p, keepLen);
    p += len;
  }
  return n;
}

int floorHalf(int v) { return v >= 0 ? v / 2 : -((1 - v) / 2); }

// The label's text, font and place in its box (docs/saver.md §2, the web draws the same): px (nearest, a tie takes
// the smaller) or the largest, then smaller until it fits; at the smallest the end is cut by letters, never "…".
void fill(lv_obj_t* box, const El& e, const tm& t) {
  lv_obj_t* l = lv_obj_get_child(box, -1);  // the text (its shadow, when there is one, comes first)
  if (!l) return;
  const String s = textOf(e, t);
  // A band: drawn only when there is text; the text lays out in the box inset 10 px left and right (`text` also 6 px
  // top and bottom), every rule then as for that inner box (docs/saver.md §2).
  if (e.band) lv_obj_set_style_bg_opa(box, s.length() ? e.bo * 255 / 100 : 0, 0);
  const int in = e.band ? 10 : 0, iy = e.band && e.t == "text" ? 6 : 0, W = max(1, e.w - 2 * in), H = max(1, e.h - 2 * iy);
  const std::vector<uint8_t> list = sizes(e);
  size_t i = 0;
  if (e.px)
    for (size_t k = 1; k < list.size(); k++)
      if (abs(list[k] - e.px) <= abs(list[i] - e.px)) i = k;  // a tie takes the smaller (docs/saver.md §2)
  const bool wrap = e.t == "text";
  String shown = s;
  const lv_font_t* f = nullptr;
  int lines = 1;
  bool fits = false;
  for (; i < list.size() && !fits; i++) {
    f = fontOf(e, list[i]);
    if (wrap) fits = (lines = breakLines(s.c_str(), f, W, shown)) * f->line_height <= H;
    else fits = f->line_height <= H && lv_text_get_width(s.c_str(), s.length(), f, 0) <= W;
  }
  i--;  // the size used
  const int lh = f->line_height;
  if (!fits && wrap) {  // still too big at the smallest: the lines that fit, at least the first
    const int keepN = max(1, H / lh);
    int at = -1;
    for (int k = 0; k < keepN && (at = shown.indexOf('\n', at + 1)) >= 0;) k++;
    if (lines > keepN && at > 0) shown = shown.substring(0, at);
  } else if (!fits) {  // one line: letters off the end until it fits, never "…"
    while (shown.length() && lv_text_get_width(shown.c_str(), shown.length(), f, 0) > W) {
      int n = shown.length() - 1;
      while (n > 0 && ((uint8_t)shown[n] & 0xC0) == 0x80) n--;
      shown.remove(n);
    }
  }
  lv_obj_set_user_data(l, (void*)(intptr_t)list[i]);
  lv_obj_set_style_text_font(l, f, 0);
  setText(l, shown.c_str());
  // Wrapped text: each line aligned in w as an LVGL label does, (w - line)/2. One line: as wide as its text, centred
  // like LVGL aligns (w/2 - tw/2), vertically floor((h - lh)/2).
  const lv_text_align_t al = e.a == 'l' ? LV_TEXT_ALIGN_LEFT : e.a == 'r' ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER;
  const int tw = wrap ? W : lv_text_get_width(shown.c_str(), shown.length(), f, 0);
  const int lx = in + (wrap || e.a == 'l' ? 0 : e.a == 'r' ? W - tw : W / 2 - tw / 2), ly = iy + (wrap ? 0 : floorHalf(H - lh));
  // The text over its soft shadow (docs/saver.md §2): the same text in black at +1,+1 45 % and at +2,+2 25 %.
  for (int k = lv_obj_get_child_count(box) - 1, d = 0; k >= 0; k--, d++) {
    lv_obj_t* o = lv_obj_get_child(box, k);
    if (d) {
      lv_obj_set_style_text_font(o, f, 0);
      setText(o, shown.c_str());
    }
    lv_obj_set_width(o, wrap ? W : LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(o, al, 0);
    lv_obj_set_pos(o, lx + d, ly + d);
  }
}

// ---------------------------------------------------------------- the saver's state
Doc cur;
String curRaw;
bool loaded = false;
uint32_t seenSettings = 0;

lv_obj_t *root, *bgBox, *layer;
std::vector<lv_obj_t*> boxes;  // the elements of the page shown, in its order
bool shown = false, pageNight = false, pageLand = false, dirty = true;
int pageIdx = 0, lastMin = -1, shift = 0;
uint32_t pageAt = 0, builtAt = 0, slide = 0;

void reload() {
  if (loaded && seenSettings == desk::settingsVersion()) return;
  seenSettings = desk::settingsVersion();
  // The saver slot (slots.h): an app saver draws its own pages; "Clock and photos" (built in) the saver pages.
  const char* app = slots::saverDoc();
  const String raw = desk::setting("s3.saver_pages", "") + "|" + desk::setting("s3.saver_mode", "") + "|" + (app ? app : "");
  if (loaded && raw == curRaw) return;
  loaded = true;
  curRaw = raw;
  parse(app ? String(app) : desk::setting("s3.saver_pages", ""), cur);
  dirty = true;
}

const Page& pageOf(bool land, bool night, int idx) {
  if (night) return nightPage(cur, land);
  const std::vector<Page>& v = land ? cur.l : cur.p;
  return v[idx % v.size()];
}

void render() {
  if (!root) return;
  lv_obj_clean(root);
  boxes.clear();
  const Page& pg = pageOf(pageLand, pageNight, pageIdx);
  bgBox = drawBg(root, pg.bg, os::W(), os::H());
  keep(&layer, canvas(root, pageLand));
  for (const El& e : pg.e) boxes.push_back(drawEl(layer, e));
  builtAt = millis();
  dirty = false;
  lastMin = -1;  // text and place now
  trace::add("saver page %s %d%s, %u elements", pageLand ? "land" : "port", pageIdx + 1, pageNight ? " night" : "",
             (unsigned)pg.e.size());
}

}  // namespace

// ================================================================== backgrounds
bool Bg::operator==(const Bg& o) const { return key() == o.key(); }

String Bg::key() const {
  String k = t;
  if (t == "color" || t == "grad") k += hexStr(c);
  if (t == "grad") k += hexStr(c2);
  if (t == "wall") k += w;
  if (t == "photo") k += String(id);
  return k + "/" + dim;
}

void readBg(JsonObjectConst b, Bg& bg) {
  bg = Bg();
  bg.t = str(b, "t", "black");
  bg.c = hexOf(str(b, "c", ""), bg.c);
  bg.c2 = hexOf(str(b, "c2", ""), bg.c2);
  bg.id = num(b, "id", 0, 0, 1 << 30);
  bg.w = str(b, "w", "");
  bg.dim = num(b, "dim", 0, 0, 90);
}

void writeBg(JsonObject b, const Bg& bg) {
  puts_(b, "t", bg.t);
  if (bg.t == "color" || bg.t == "grad") puts_(b, "c", hexStr(bg.c));
  if (bg.t == "grad") puts_(b, "c2", hexStr(bg.c2));
  if (bg.t == "photo") put(b, "id", bg.id);
  if (bg.t == "wall" && bg.w.length()) puts_(b, "w", bg.w);
  if (bg.dim) put(b, "dim", bg.dim);
}

namespace {
// Photo backgrounds decoded from the copy photo.cpp keeps (/ph/<id>_<w>x<h>_cj50.jpg), shared and counted.
struct Pic {
  String key;
  lv_image_dsc_t dsc;
  int holds;
};
std::vector<Pic*> pics;

// The picture of the w x h copy (fw x fh), cover-scaled to w x h.
uint16_t* decodePhoto(int id, int fw, int fh, int w, int h) {
  const String path = "/ph/" + String(id) + "_" + fw + "x" + fh + "_cj50.jpg";
  const uint32_t t0 = millis();
#ifdef SIM
  std::ifstream in(simDir + "/fs" + path.c_str(), std::ios::binary);
  const std::string jpg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  uint16_t* px = jpg.size() ? jpeg::cover((const uint8_t*)jpg.data(), jpg.size(), w, h) : nullptr;
#else
  fs::File f = LittleFS.open(path, "r");
  const size_t len = f ? f.size() : 0;
  uint8_t* jpg = len ? (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : nullptr;
  uint16_t* px = jpg && f.read(jpg, len) == len ? jpeg::cover(jpg, len, w, h) : nullptr;
  if (f) f.close();
  heap_caps_free(jpg);
#endif
  trace::add("bg photo %d %dx%d %s in %lu ms", id, w, h, px ? "decoded" : "missing", millis() - t0);
  return px;
}
}  // namespace

const lv_image_dsc_t* holdPic(const Bg& bg, int w, int h) {
  if (bg.t == "wall") return bg.w.length() ? theme::wallpaperOf(bg.w.c_str(), w, h) : theme::wallpaper(w, h);
  if (bg.t != "photo" || !bg.id) return nullptr;
  const String key = String(bg.id) + "_" + w + "x" + h;
  for (Pic* p : pics)
    if (p->key == key) return p->holds++, &p->dsc;
  uint16_t* px = decodePhoto(bg.id, w > h ? 480 : 320, w > h ? 320 : 480, w, h);  // the copy of its orientation, covering
  if (!px) return nullptr;
  Pic* p = new Pic{key, {}, 1};
  p->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  p->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  p->dsc.header.w = w;
  p->dsc.header.h = h;
  p->dsc.header.stride = w * 2;
  p->dsc.data_size = w * h * 2;
  p->dsc.data = (const uint8_t*)px;
  pics.push_back(p);
  static int peak = 0;
  if (heldPicKB() > peak) trace::add("bg pictures peak %d KB", peak = heldPicKB());
  return &p->dsc;
}

void releasePic(const lv_image_dsc_t* pic) {
  if (!pic) return;
  for (size_t i = 0; i < pics.size(); i++) {
    if (&pics[i]->dsc != pic) continue;
    if (--pics[i]->holds > 0) return;
    lv_image_cache_drop(pic);
    heap_caps_free((void*)pic->data);
    delete pics[i];
    pics.erase(pics.begin() + i);
    return;
  }
  theme::releaseWall(pic);  // a wallpaperOf picture (or the look's, which is not counted)
}

namespace {
struct Owned {
  lv_image_dsc_t dsc{};
  ~Owned() {
    lv_image_cache_drop(&dsc);
    heap_caps_free((void*)dsc.data);
  }
};
}  // namespace

lv_obj_t* photoImg(lv_obj_t* parent, int id, int w, int h) {
  uint16_t* px = decodePhoto(id, 320, 480, w, h);
  if (!px) return nullptr;
  Owned* o = new Owned();
  o->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  o->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  o->dsc.header.w = w;
  o->dsc.header.h = h;
  o->dsc.header.stride = w * 2;
  o->dsc.data_size = w * h * 2;
  o->dsc.data = (const uint8_t*)px;
  lv_obj_t* i = lv_image_create(parent);
  lv_image_set_src(i, &o->dsc);
  own(i, o);
  return i;
}

lv_obj_t* thumb(lv_obj_t* parent, const Page& pg, int div) {
  const int w = os::W(), h = os::H(), tw = w / div, th = h / div;
  uint16_t* full = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  uint16_t* px = (uint16_t*)heap_caps_malloc(tw * th * 2, MALLOC_CAP_SPIRAM);
  if (!full || !px) {
    heap_caps_free(full);
    heap_caps_free(px);
    return nullptr;
  }
  // Drawn full size in a hidden holder (never on the panel), taken as a picture, then each div x div averaged.
  lv_obj_t* hold = kit::clear(lv_layer_top());
  lv_obj_add_flag(hold, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* c = kit::clear(hold);
  lv_obj_set_size(c, w, h);
  drawBg(c, pg.bg, w, h);
  lv_obj_t* cv = canvas(c, os::landscape());
  for (const El& e : pg.e) drawEl(cv, e);
  lv_obj_update_layout(c);
  lv_draw_buf_t buf;
  lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, w * 2, full, w * h * 2);
  const bool ok = lv_snapshot_take_to_draw_buf(c, LV_COLOR_FORMAT_RGB565, &buf) == LV_RESULT_OK;
  lv_obj_delete(hold);
  const int n = div * div;
  for (int y = 0; y < th; y++)
    for (int x = 0; x < tw; x++) {
      int r = 0, g = 0, b = 0;
      for (int j = 0; j < div; j++)
        for (int i = 0; i < div; i++) {
          const uint16_t v = full[(y * div + j) * w + x * div + i];
          r += v >> 11, g += (v >> 5) & 63, b += v & 31;
        }
      px[y * tw + x] = ok ? (uint16_t)((r / n) << 11 | (g / n) << 5 | b / n) : 0;
    }
  heap_caps_free(full);
  Owned* o = new Owned();
  o->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  o->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  o->dsc.header.w = tw;
  o->dsc.header.h = th;
  o->dsc.header.stride = tw * 2;
  o->dsc.data_size = tw * th * 2;
  o->dsc.data = (const uint8_t*)px;
  lv_obj_t* i = lv_image_create(parent);
  lv_image_set_src(i, &o->dsc);
  own(i, o);
  return i;
}

bool templates(const String& json, bool land, std::vector<Template>& out, std::vector<String>* groups) {
  out.clear();
  if (groups) groups->clear();
  JsonDocument j(&spiram);
  bool ok = json.length() && deserializeJson(j, json) == DeserializationError::Ok &&
            (j["templates"].is<JsonArrayConst>() || j["t"].is<JsonArrayConst>());
  if (!ok) {  // the built-in ones (the same JSON the saver falls back to)
    static const char* const B[][3] = {{N_("Desk"), N_("time, weather, calendar"), DEF},
                                       {N_("Minimal"), N_("only the time and date"), MINIMAL},
                                       {N_("Photo frame"), N_("your photos, the time in a corner"), FRAME},
                                       {N_("Red clock"), N_("dim red time, alarm"), nullptr}};
    // The names and descriptions in the language shown (quotes escaped for the JSON).
    auto q = [](const char* s) {
      String o = T_(s);
      o.replace("\\", "\\\\");
      o.replace("\"", "\\\"");
      return o;
    };
    String all = "{\"t\":[";
    for (int i = 0; i < 3; i++) all += String("{\"name\":\"") + q(B[i][0]) + "\",\"desc\":\"" + q(B[i][1]) + "\"," + (B[i][2] + 1) + ",";
    all += String("{\"name\":\"") + q(B[3][0]) + "\",\"desc\":\"" + q(B[3][1]) + "\",\"night\":1,\"p\":[" + NIGHT_P + "],\"l\":[" +
           NIGHT_L + "]}]}";
    deserializeJson(j, all.c_str());  // the reader the built-in pages use (one copy of the parser)
  }
  if (groups)
    for (JsonObjectConst g : j["groups"].as<JsonArrayConst>())
      groups->push_back(String(str(g, "id", "")) + "|" + str(g, "name", ""));
  for (JsonObjectConst o : (j["templates"].is<JsonArrayConst>() ? j["templates"] : j["t"]).as<JsonArrayConst>()) {
    JsonVariantConst v = o[land ? "l" : "p"];
    Template t;
    t.name = str(o, "name", "");
    t.desc = str(o, "desc", "");
    t.group = str(o, "group", "");
    t.night = num(o, "night", 0, 0, 1) || t.group == "night";
    if (v.is<JsonObjectConst>()) {  // one page, not in a list
      t.pages.emplace_back();
      readPage(v, t.pages.back());
    } else {
      readPages(v, t.pages);
    }
    if (t.night && t.pages.size() > 1) t.pages.resize(1);
    if (t.name.length() && !t.pages.empty()) out.push_back(t);
  }
  return ok;
}

int heldPicKB() {
  int kb = theme::heldWallKB();
  for (const Pic* p : pics) kb += p->dsc.data_size / 1024;
  return kb;
}


// ================================================================== model
bool El::text() const {
  return t == "time" || t == "date" || t == "lunar" || t == "text" || t == "next" || t == "alarm" || t == "temp";
}

void widgetBox(char size, int16_t& w, int16_t& h) {
  int ww, hh;
  wid::boxSize(size == 'L' ? wid::L : size == 'M' ? wid::M : wid::S, ww, hh);
  w = ww, h = hh;
}

void setWidgetSize(El& e, char size) {
  e.sz = size == 'M' || size == 'L' ? size : 'S';
  widgetBox(e.sz, e.w, e.h);
}

void parse(const String& json, Doc& d) {
  d = Doc();
  JsonDocument j(&spiram);
  const bool ok = json.length() && deserializeJson(j, json) == DeserializationError::Ok && j.is<JsonObject>();
  if (ok) {
    d.cycle = j["cycle"] | 0;
    d.light = constrain((int)(j["light"] | 30), 5, 100);
    d.night = j["night"] | "page";
    readPages(j["p"], d.p);
    readPages(j["l"], d.l);
    if ((d.hasNp = j["np"].is<JsonObjectConst>())) readPage(j["np"], d.np);
    if ((d.hasNl = j["nl"].is<JsonObjectConst>())) readPage(j["nl"], d.nl);
  }
  if (d.p.empty() || d.l.empty()) {  // the defaults, or the template of the old saver mode
    const String mode = desk::setting("s3.saver_mode", "desk");
    JsonDocument t(&spiram);
    const bool legacy = !json.length();  // only an empty setting follows the old mode
    deserializeJson(t, !legacy ? DEF : mode == "clock" ? MINIMAL : mode == "photo" ? FRAME : DEF);
    if (d.p.empty()) readPages(t["p"], d.p);
    if (d.l.empty()) readPages(t["l"], d.l);
  }
}

String dump(const Doc& d) {
  JsonDocument j(&spiram);
  j["v"] = 1;
  j["cycle"] = d.cycle;
  j["light"] = d.light;
  j["night"] = d.night;
  JsonArray p = j["p"].to<JsonArray>();
  for (const Page& pg : d.p) writePage(p.add<JsonObject>(), pg);
  JsonArray l = j["l"].to<JsonArray>();
  for (const Page& pg : d.l) writePage(l.add<JsonObject>(), pg);
  if (d.hasNp) writePage(j["np"].to<JsonObject>(), d.np);
  if (d.hasNl) writePage(j["nl"].to<JsonObject>(), d.nl);
  String out;
  serializeJson(j, out);
  return out;
}

const Doc& doc() {
  reload();
  return cur;
}

void save(const Doc& d) {
  const String json = dump(d);
  if (json.length() > 8000) {
    os::toast(N_("Too much on the screen saver: remove something to save"));
    return;
  }
  if (json == desk::setting("s3.saver_pages", "")) return;
  desk::setSetting("s3.saver_pages", json);
  trace::add("saver pages saved (%u bytes)", (unsigned)json.length());
  reload();
}

Page& nightPage(Doc& d, bool land) {
  Page& pg = land ? d.nl : d.np;
  bool& has = land ? d.hasNl : d.hasNp;
  if (!has && pg.e.empty()) {
    JsonDocument t(&spiram);
    deserializeJson(t, land ? NIGHT_L : NIGHT_P);
    readPage(t.as<JsonObjectConst>(), pg);
  }
  return pg;
}

// ================================================================== drawing
lv_obj_t* drawBg(lv_obj_t* parent, const Bg& bg, int w, int h) {
  lv_obj_t* c = box(parent, 0x000000, 0);
  lv_obj_set_size(c, w, h);
  if (bg.t == "color") {
    lv_obj_set_style_bg_color(c, C(bg.c), 0);
  } else if (bg.t == "grad") {
    grad(c, bg.c, bg.c2);
  } else if (bg.t == "photo" && !bg.id) {  // the s3.photos slideshow
    if (const lv_image_dsc_t* pic = photo::screen(slide, w, h)) img(c, pic);
#if __has_include("tank/tank.h")
  } else if (bg.t == "tank") {  // the user's aquarium, alive (src/tank); deep water when none is installed
    if (!tank::saverBg(c, w, h)) grad(c, 0x0b4f6c, 0x041e2e);
    else lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
#endif
  } else if (const lv_image_dsc_t* pic = holdPic(bg, w, h)) {
    lv_obj_t* i = img(c, pic);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);  // the picture covers it all
    lv_obj_add_event_cb(i, [](lv_event_t* e) { releasePic((const lv_image_dsc_t*)lv_event_get_user_data(e)); },
                        LV_EVENT_DELETE, (void*)pic);
  }
  if (bg.dim) {
    lv_obj_t* d = box(c, 0x000000, 0);
    lv_obj_set_size(d, w, h);
    lv_obj_set_style_bg_opa(d, bg.dim * 255 / 100, 0);
  }
  return c;
}


// ================================================================== photo slideshows: a picture, then the next
// docs/saver.md §2: a photo element with "all" or 2+ "ids" shows one picture of its set at a time, changing every
// "iv" seconds, in order or shuffled, cut or faded. Memory (the rule for the board): an element holds at most
// the picture shown and the next one, both in PSRAM; the next is decoded ahead on a task of its own (core 0), the one
// before is freed right after the change. The fade blends the two pictures already held (image opacity, no layer).
namespace {

// The set's index shown at tick k = floor(epoch seconds / iv): in order (k + of) mod n; shuffled, a new order each
// round of n ticks (seeded by the round, so elements with the same set and iv agree), never the same twice in a row.
int pickRaw(int n, char ord, uint32_t k, int of) {
  const uint32_t j = (k + of) % n;
  if (ord != 'r') return j;
  uint8_t perm[12];
  for (int i = 0; i < n; i++) perm[i] = i;
  uint32_t x = (k / n) * 2654435761u + 1;  // the round's seed (xorshift32)
  for (int i = n - 1; i > 0; i--) {
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    std::swap(perm[i], perm[x % (i + 1)]);
  }
  return perm[j];
}
int pickOf(int n, char ord, uint32_t k, int of) {
  const int p = pickRaw(n, ord, k, of);
  return n > 1 && k && p == pickRaw(n, ord, k - 1, of) ? (p + 1) % n : p;
}

// One picture being decoded. The UI hands it to the task and polls `done`; whoever lets go last frees it (an element
// deleted while its picture is still being decoded leaves it to the task).
struct Job {
  uint8_t* jpg;
  size_t len;
  uint16_t* px;
  int16_t w, h;
  int8_t pick;      // the set's index it is for
  bool crop;        // a full-screen copy, cropped to the box (no copy at the box's size yet)
  volatile bool done, orphan;
};
void freeJob(Job* j) {
  heap_caps_free(j->jpg);
  heap_caps_free(j->px);
  heap_caps_free(j);
}
uint16_t* decodeJob(const Job* j) {
  return j->crop ? jpeg::cover(j->jpg, j->len, j->w, j->h) : photo::decodeJpg(j->jpg, j->len, j->w, j->h);
}
#ifndef SIM
portMUX_TYPE jobMux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t jobs;
void decodeTask(void*) {
  for (Job* j;;) {
    if (xQueueReceive(jobs, &j, portMAX_DELAY) != pdTRUE) continue;
    uint16_t* px = j->orphan ? nullptr : decodeJob(j);
    heap_caps_free(j->jpg);
    j->jpg = nullptr;
    taskENTER_CRITICAL(&jobMux);
    j->px = px;
    j->done = true;
    const bool orphan = j->orphan;
    taskEXIT_CRITICAL(&jobMux);
    if (orphan) freeJob(j);
  }
}
#endif

// Lets go of a job: freed now when done, else by the task when it is.
void dropJob(Job*& j) {
  if (!j) return;
#ifndef SIM
  taskENTER_CRITICAL(&jobMux);
  const bool done = j->done;
  j->orphan = true;
  taskEXIT_CRITICAL(&jobMux);
  if (done) freeJob(j);
#else
  freeJob(j);
#endif
  j = nullptr;
}

struct Show {
  int ids[12];
  uint8_t n, of;
  char ord, tr;
  uint16_t iv;
  int16_t w, h;
  lv_obj_t *box, *img, *old;  // the element; the picture shown; the one fading out
  uint16_t *px, *oldPx;
  lv_image_dsc_t dsc[2];
  uint8_t cur;   // dsc[cur] is shown
  int8_t shown;  // the set's index shown (-1: none yet)
  Job* job;      // the next picture
};

uint32_t tickNow(const Show* s) { return (uint32_t)time(nullptr) / s->iv; }

// A job for the set's index `pick`: its JPEG read here (flash, UI loop), decoded on the task, or here at once (`now`:
// the first picture of an element). The copy at the box's size, else the full-screen one cropped, else the next
// picture of the set (docs/saver.md §2).
Job* startJob(Show* s, int pick, bool now = false) {
  for (int k = 0; k < 2 * s->n; k++) {
    const int i = (pick + k / 2) % s->n;
    size_t len;
    const bool crop = k & 1;
    uint8_t* jpg = crop ? photo::readCopy(s->ids[i], os::landscape() ? 480 : 320, os::landscape() ? 320 : 480, len)
                        : photo::readCopy(s->ids[i], s->w, s->h, len);
    if (!jpg) continue;
    Job* j = (Job*)heap_caps_calloc(1, sizeof(Job), MALLOC_CAP_SPIRAM);
    if (!j) return heap_caps_free(jpg), nullptr;
    j->jpg = jpg, j->len = len, j->w = s->w, j->h = s->h, j->pick = pick, j->crop = crop;
#ifdef SIM
    now = true;  // the simulator decodes at once
#else
    if (!now && !jobs) {
      jobs = xQueueCreateWithCaps(8, sizeof(Job*), MALLOC_CAP_SPIRAM);
      // Core 0 beside WiFi (the UI runs on core 1), low priority; the stack in PSRAM (no flash use there).
      xTaskCreatePinnedToCoreWithCaps(decodeTask, "slides", 4096, nullptr, 1, nullptr, 0, MALLOC_CAP_SPIRAM);
    }
    if (!now && (!jobs || xQueueSend(jobs, &j, 0) != pdTRUE)) {
      heap_caps_free(jpg), heap_caps_free(j);
      return nullptr;
    }
#endif
    if (now) {
      j->px = decodeJob(j);
      heap_caps_free(jpg);
      j->jpg = nullptr;
      j->done = true;
    }
    return j;
  }
  return nullptr;
}

void freeOld(Show* s) {
  if (s->old) lv_obj_delete(s->old), s->old = nullptr;
  if (s->oldPx) {
    lv_image_cache_drop(&s->dsc[!s->cur]);
    heap_caps_free(s->oldPx);
    s->oldPx = nullptr;
  }
}

// The decoded picture of the job becomes the one shown (cut, or faded in over the old one).
void showJob(Show* s) {
  Job* j = s->job;
  uint16_t* px = j->px;
  j->px = nullptr;
  s->shown = j->pick;
  dropJob(s->job);
  if (!px) return;
  freeOld(s);
  const uint8_t nx = s->img ? !s->cur : s->cur;
  lv_image_dsc_t& d = s->dsc[nx];
  d = {};
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_RGB565;
  d.header.w = s->w;
  d.header.h = s->h;
  d.header.stride = s->w * 2;
  d.data_size = s->w * s->h * 2;
  d.data = (const uint8_t*)px;
  s->old = s->img, s->oldPx = s->px;
  s->img = img(s->box, &d), s->px = px, s->cur = nx;
  if (!s->old || s->tr != 'f') return freeOld(s);
  lv_anim_t a;  // the new picture over the old one, from clear to whole (about 400 ms); then the old one goes
  lv_anim_init(&a);
  lv_anim_set_var(&a, s->img);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_duration(&a, 400);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_image_opa((lv_obj_t*)o, v, 0); });
  lv_anim_set_user_data(&a, s);
  lv_anim_set_completed_cb(&a, [](lv_anim_t* a) { freeOld((Show*)lv_anim_get_user_data(a)); });
  lv_obj_set_style_image_opa(s->img, 0, 0);
  lv_anim_start(&a);
}

// Every second: the picture of this tick when it is ready, and the next one decoded ahead.
void slideTick(lv_timer_t* t) {
  Show* s = (Show*)lv_timer_get_user_data(t);
  if (s->old) return;  // a fade is running: two pictures are held already
  const uint32_t k = tickNow(s);
  const int want = pickOf(s->n, s->ord, k, s->of);
  if (want != s->shown) {
    if (s->job && s->job->pick != want) dropJob(s->job);  // the clock jumped: another one is wanted
    if (!s->job) s->job = startJob(s, want);
    if (s->job && s->job->done) showJob(s);
  } else if (!s->job) {
    const int next = pickOf(s->n, s->ord, k + 1, s->of);
    if (next != s->shown) s->job = startJob(s, next);  // ahead
  }
}

// The picture of now, at once (decoded here), and the timer that changes it; false when none is here.
bool slides(lv_obj_t* box, const El& e) {
  std::vector<int> set;  // "all": the device's own pictures, in their order; else "ids"
  if (!e.all) set = e.ids;
  else
    for (const String& id : data::codesOf(desk::setting("s3.photos", ""), ""))
      if (id.toInt() > 0) set.push_back(id.toInt());
  if (set.empty()) return false;
  Show* s = (Show*)heap_caps_calloc(1, sizeof(Show), MALLOC_CAP_SPIRAM);
  if (!s) return false;
  s->n = min<size_t>(set.size(), 12);
  for (int i = 0; i < s->n; i++) s->ids[i] = set[i];
  s->ord = e.ord, s->tr = e.tr, s->iv = max<int>(e.iv, 10), s->of = e.of % s->n, s->w = e.w, s->h = e.h;
  s->box = box, s->shown = -1;
  s->job = startJob(s, pickOf(s->n, s->ord, tickNow(s), s->of), true);  // the first one now (as a single photo is)
  if (s->job) showJob(s);
  lv_obj_add_event_cb(box, [](lv_event_t* ev) {
    Show* s = (Show*)lv_event_get_user_data(ev);
    dropJob(s->job);
    if (s->px) lv_image_cache_drop(&s->dsc[s->cur]), heap_caps_free(s->px);
    if (s->oldPx) lv_image_cache_drop(&s->dsc[!s->cur]), heap_caps_free(s->oldPx);
    heap_caps_free(s);
  }, LV_EVENT_DELETE, s);
  if (s->n > 1) every(box, 1000, slideTick, s);
  return s->img;
}

}  // namespace

lv_obj_t* canvas(lv_obj_t* parent, bool land) {
  lv_obj_t* c = kit::clear(parent);
  lv_obj_set_size(c, land ? PAGE_H : PAGE_W, land ? PAGE_W : PAGE_H);
  lv_obj_set_pos(c, os::colX(), os::colY());  // (the screen is turned as the page)
  return c;
}

lv_obj_t* drawEl(lv_obj_t* parent, const El& e) {
  lv_obj_t* c = kit::clear(parent);
  lv_obj_set_pos(c, e.x, e.y);
  lv_obj_set_size(c, e.w, e.h);
  if (e.o < 100) lv_obj_set_style_opa(c, e.o * 255 / 100, 0);  // "o": the whole element (widgets and photos too)
  if (e.t == "box") {
    lv_obj_set_style_bg_color(c, C(e.c), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, e.r, 0);
  } else if (e.t == "photo") {
    lv_obj_set_style_radius(c, e.r, 0);
    lv_obj_set_style_clip_corner(c, e.r > 0, 0);
    lv_obj_set_style_bg_color(c, C(0x2a3240), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
#ifdef SIM
    const lv_image_dsc_t* pic = e.slides() || !e.id ? nullptr : photo::screen(e.id - 1, e.w, e.h);  // photos/N.jpg
    const bool got = e.slides() ? slides(c, e) : pic && img(c, pic);
#else
    // The full option ("fhcj50|id": first picture, colour JPEG, brightness 50 = the copy wanted() fetches). A bare
    // "f|id" made photo::pOf() read two digits of the id as the brightness ("cj04"), so the saver never found the
    // copy it had downloaded and showed the placeholder (owner, 2026-11-18: "màn chờ không hiện được ảnh").
    const bool got = e.slides() ? slides(c, e) : e.id && photo::show(c, "fhcj50|" + String(e.id), e.w, e.h);
#endif
    if (!got) lv_obj_center(glyph(c, &gl_photo_22, 0x6b7380));
  } else if (e.t == "w") {
    wid::Item it;
    it.kind = e.k;
    it.size = e.sz == 'L' ? wid::L : e.sz == 'M' ? wid::M : wid::S;
    it.style = e.st;
    it.opt = e.op;
    it.skin = e.sk;
    wid::create(c, it, e.w, e.h, false);
  } else if (e.text()) {
    // Text over pictures (docs/saver.md §2): a band behind it (its opacity set by fill), a soft shadow under it.
    if (e.band) {
      lv_obj_set_style_bg_color(c, C(e.bgc), 0);
      lv_obj_set_style_radius(c, min<int>(e.r, min(e.w, e.h) / 2), 0);
    }
    if (e.sh)
      for (int k = 0; k < 2; k++) lv_obj_set_style_text_opa(label(c, "", 15, e.b, 0x000000), k ? 115 : 64, 0);
    label(c, "", 15, e.b, e.c);
    const time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    fill(c, e, t);
  }
  return c;
}

std::vector<uint8_t> sizes(const El& e) {
  std::vector<uint8_t> v;
  if (display(e)) {
    if (e.b) v.assign(DISP_B, DISP_B + sizeof DISP_B);
    else v.assign(DISP_R, DISP_R + sizeof DISP_R);
  }
  if (e.b) v.insert(v.end(), TEXT_B, TEXT_B + sizeof TEXT_B);
  else v.insert(v.end(), TEXT_R, TEXT_R + sizeof TEXT_R);
  return v;
}

int sizeShown(lv_obj_t* elBox) {
  lv_obj_t* l = elBox ? lv_obj_get_child(elBox, -1) : nullptr;
  return l && lv_obj_check_type(l, &lv_label_class) ? (int)(intptr_t)lv_obj_get_user_data(l) : 0;
}

// ================================================================== the saver
void build(lv_obj_t* parent) {
  keep(&root, kit::clear(parent));
  lv_obj_set_size(root, os::W(), os::H());
  lv_obj_add_flag(root, (lv_obj_flag_t)(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN));
  lv_obj_set_pos(root, 0, 0);
  shown = false;
  dirty = true;
}

void show(bool on) {
  if (!root) return;
  shown = on;
  if (on) {
    lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
    dirty = true;  // draw everything fresh at the next update
    pageIdx = 0;
    pageAt = millis();
  } else {
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(root);  // widgets and pictures are not needed until the next time
    boxes.clear();
  }
}

void update(const tm& t, bool night) {
  if (!root || !shown) return;
  reload();
  const bool land = os::landscape();
  const bool n = night && cur.night != "day";
  const std::vector<Page>& v = land ? cur.l : cur.p;
  if (!n && cur.cycle > 0 && v.size() > 1 && millis() - pageAt >= (uint32_t)cur.cycle * 1000) {
    pageAt = millis();
    pageIdx = (pageIdx + 1) % v.size();
    dirty = true;
  }
  if (pageIdx >= (int)v.size()) pageIdx = 0, dirty = true;
  // The page: at the start, when night starts or ends, on a new page or setting, and every 10 minutes (widgets).
  if (dirty || n != pageNight || land != pageLand || millis() - builtAt > 600000) {
    pageNight = n;
    pageLand = land;
    render();
  }
  wid::tick();  // clocks on the page: minutes, seconds hands
  const int m = t.tm_hour * 60 + t.tm_min;
  if (m == lastMin) return;
  lastMin = m;
  // Text once a minute, the slideshow picture too; the whole page moves a few pixels against burn-in.
  const Page& pg = pageOf(pageLand, pageNight, pageIdx);
  for (size_t i = 0; i < boxes.size() && i < pg.e.size(); i++)
    if (pg.e[i].text() && pg.e[i].t != "text") fill(boxes[i], pg.e[i], t);
  if (pg.bg.t == "photo" && !pg.bg.id && bgBox && lv_obj_get_child_count(bgBox)) {
    lv_obj_t* im = lv_obj_get_child(bgBox, 0);
    if (const lv_image_dsc_t* pic = lv_obj_check_type(im, &lv_image_class) ? photo::screen(++slide, os::W(), os::H()) : nullptr)
      lv_image_set_src(im, pic);
  }
  static const int8_t DX[] = {0, 2, 3, 2, 0, -2, -3, -2}, DY[] = {0, -2, 0, 2, 3, 2, 0, -2};
  const int k = ++shift % 8;
  if (layer) lv_obj_set_pos(layer, os::colX() + DX[k], os::colY() + DY[k]);
}

int backlight(bool night) {
  reload();
  if (night && cur.night == "page") return 3;
  if (night && cur.night == "off") return 0;
  return cur.light;
}

bool shownLand() { return pageLand; }
int shownPage() { return pageNight ? -1 : pageIdx; }

void redraw() {
  dirty = true;
  reload();
}

}  // namespace desksaver
