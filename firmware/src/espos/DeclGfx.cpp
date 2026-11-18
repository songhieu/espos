#include "DeclGfx.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "espos/DashDraw.h"
#include "espos/Decl.h"
#include "espos/DeclFonts.h"
#include "espos/EspOSStore.h"
#include "espos/EspOSUi.h"
#include "espos/Pkgs.h"

namespace espos::decl {
namespace {

using namespace espos::draw;

struct FontDef {
  int id;
  Family family;
  bool bold;
  int px;
};
#define DEVICE_FONT(id, data, regular, family, bold, px) {id, Family::family, bold, px},
constexpr FontDef FONT_DEFS[] = {ESPOS_DECL_FONTS(DEVICE_FONT)};
#undef DEVICE_FONT

class GfxCanvas : public Canvas {
 public:
  explicit GfxCanvas(GfxRenderer& r) : r_(r) {
    fonts_.reserve(sizeof(FONT_DEFS) / sizeof(FONT_DEFS[0]));
    for (const FontDef& d : FONT_DEFS) {
      Font f;
      f.id = d.id;
      f.family = d.family;
      f.bold = d.bold;
      f.px = d.px;
      f.lineH = r.getLineHeight(d.id);
      f.inkTop = digitTop(d.id);
      f.inkH = digitHeight(d.id);
      fonts_.push_back(f);
    }
  }

  void fillRect(const int x, const int y, const int w, const int h, const int radius, const Col c) override {
    if (c == Col::None || w <= 0 || h <= 0) return;
    if (radius > 0) {
      r_.fillRoundedRect(x, y, w, h, radius,
                         c == Col::Ink     ? Color::Black
                         : c == Col::Paper ? Color::White
                                           : Color::DarkGray);
    } else if (c == Col::Gray) {
      r_.fillRectDither(x, y, w, h, Color::DarkGray);
    } else {
      r_.fillRect(x, y, w, h, c == Col::Ink);
    }
  }
  void strokeRect(const int x, const int y, const int w, const int h, const int radius, const int lw,
                  const Col c) override {
    if (c == Col::None || w <= 0 || h <= 0) return;
    if (radius > 0) {
      r_.drawRoundedRect(x, y, w, h, lw, radius, c != Col::Paper);
    } else {
      r_.drawRect(x, y, w, h, lw, c != Col::Paper);
    }
  }
  void line(const int x1, const int y1, const int x2, const int y2, const int lw, const Col c) override {
    if (c != Col::None) r_.drawLine(x1, y1, x2, y2, lw, c != Col::Paper);
  }
  void fillCircle(const int cx, const int cy, const int rad, const Col c) override {
    fillRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, c);
  }
  void strokeCircle(const int cx, const int cy, const int rad, const int lw, const Col c) override {
    strokeRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, lw, c);
  }
  void icon(const char* name, const int cx, const int cy, const int s, const Col c) override {
    // The icons draw black on white: a white icon is drawn on white and flipped.
    if (c == Col::Paper) r_.fillRect(cx - s / 2, cy - s / 2, s, s, false);
    if (!strcmp(name, "battery")) {
      const int lw = s >= 40 ? 3 : 2;
      r_.drawRoundedRect(cx - s * 40 / 100, cy - s * 20 / 100, s * 74 / 100, s * 40 / 100, lw, 3, true);
      r_.fillRect(cx + s * 34 / 100, cy - s * 8 / 100, s * 6 / 100 + 1, s * 16 / 100, true);
    } else {
      drawIcon(r_, name, cx, cy, s);
    }
    if (c == Col::Paper) r_.invertRect(cx - s / 2, cy - s / 2, s, s);
  }
  bool image(const std::string& path, const int x, const int y, const int w, const int h) override {
    HalFile f;
    if (!Storage.openFileForRead("PKG", path, f)) return false;
    Bitmap bmp(f);
    if (bmp.parseHeaders() != BmpReaderError::Ok || bmp.getWidth() <= 0 || bmp.getHeight() <= 0) return false;
    // drawBitmap shrinks to fit but never enlarges: centre what it will draw.
    const float sc = std::min({1.0f, static_cast<float>(w) / bmp.getWidth(), static_cast<float>(h) / bmp.getHeight()});
    const int dw = static_cast<int>(bmp.getWidth() * sc), dh = static_cast<int>(bmp.getHeight() * sc);
    return r_.drawBitmap(bmp, x + (w - dw) / 2, y + (h - dh) / 2, w, h);
  }
  const std::vector<Font>& fonts() const override { return fonts_; }
  int textWidth(const Font& f, const char* s) const override { return r_.getTextWidth(f.id, s, f.bold ? kBold : kReg); }
  void text(const Font& f, const int x, const int y, const char* s, const Col c) override {
    r_.drawText(f.id, x, y, s, c != Col::Paper, f.bold ? kBold : kReg);
  }

 private:
  GfxRenderer& r_;
  std::vector<Font> fonts_;
};

// Binding values from what the widgets read (widgets::loadData). The time and date follow the widgets'
// own drawing in Widgets.cpp: timeText() and twelveHour() (SETTINGS.clockFormat, "%02d:%02d" with 12 for
// noon and midnight), dateColumn() ("%d/%d" day/month), and ui::weekdayCaps() for the weekday names; those
// helpers are private to Widgets.cpp, so the formats are repeated here: change both together.
class Binder : public Data {
 public:
  Binder(const widgets::Data& d, const bool live) : d_(d), live_(live) {}

  bool value(const char* n, std::string& out) const override {
    char buf[64];
    const struct tm& t = d_.now;
    const auto is = [n](const char* k) { return strcmp(n, k) == 0; };
    if (is("time.live")) return out = live_ && d_.timeValid ? "1" : "", true;
    if (!strncmp(n, "time.", 5) || !strncmp(n, "date.", 5)) {
      if (!d_.timeValid) return false;
      const bool h12 = SETTINGS.clockFormat == 1;
      const int hour = h12 ? (t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12) : t.tm_hour;
      if (is("time.hm"))
        snprintf(buf, sizeof(buf), h12 ? "%d:%02d" : "%02d:%02d", hour, t.tm_min);
      else if (is("time.h"))
        snprintf(buf, sizeof(buf), "%02d", hour);
      else if (is("time.m"))
        snprintf(buf, sizeof(buf), "%02d", t.tm_min);
      else if (is("time.ampm"))
        snprintf(buf, sizeof(buf), "%s", !h12 ? "" : t.tm_hour < 12 ? "SA" : "CH");
      else if (is("date.d"))
        snprintf(buf, sizeof(buf), "%d", t.tm_mday);
      else if (is("date.dd"))
        snprintf(buf, sizeof(buf), "%02d", t.tm_mday);
      else if (is("date.m"))
        snprintf(buf, sizeof(buf), "%d", t.tm_mon + 1);
      else if (is("date.mm"))
        snprintf(buf, sizeof(buf), "%02d", t.tm_mon + 1);
      else if (is("date.y"))
        snprintf(buf, sizeof(buf), "%d", t.tm_year + 1900);
      else if (is("date.yday"))
        snprintf(buf, sizeof(buf), "%d", t.tm_yday);
      else if (is("date.dm"))
        snprintf(buf, sizeof(buf), "%d/%d", t.tm_mday, t.tm_mon + 1);
      else if (is("date.wd"))
        return out = title(ui::weekdayCaps(t.tm_wday)), true;
      else if (is("date.wds"))
        return out = weekdayShort(t.tm_wday), true;
      else if (is("date.month")) {
        snprintf(buf, sizeof(buf), tr(STR_EO_MONTH_FMT), t.tm_mon + 1);
        return out = title(buf), true;
      } else if (is("date.full")) {
        snprintf(buf, sizeof(buf), ", %02d/%02d/%d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
        return out = title(ui::weekdayCaps(t.tm_wday)) + buf, true;
      } else {
        return false;
      }
      return out = buf, true;
    }
    if (!strncmp(n, "lunar.", 6)) {
      if (d_.lunar.empty()) return false;
      if (is("lunar.dm")) return out = d_.lunar, true;
      if (is("lunar.year")) return out = d_.lunarYear, true;
      const size_t slash = d_.lunar.find('/');
      if (is("lunar.d")) return out = d_.lunar.substr(0, slash), true;
      if (is("lunar.m") && slash != std::string::npos) return out = d_.lunar.substr(slash + 1), true;
      return false;
    }
    if (is("battery.pct")) return out = std::to_string(d_.battery), true;
    if (is("sync.hm")) {
      if (d_.lastSyncMs <= 0) return false;
      ESPOS.formatClock(d_.lastSyncMs, buf, sizeof(buf));
      return out = buf, true;
    }
    if (!strncmp(n, "weather.", 8)) {
      if (is("weather.ok")) return out = d_.hasWeather ? "1" : "", true;
      if (!d_.hasWeather) return false;
      if (is("weather.temp")) return out = std::to_string(d_.temp), true;
      if (is("weather.min")) return out = std::to_string(d_.tmin), true;
      if (is("weather.max")) return out = std::to_string(d_.tmax), true;
      if (is("weather.rain")) return out = std::to_string(d_.rain), true;
      if (is("weather.desc")) return out = d_.desc, true;
      if (is("weather.icon")) return out = d_.icon, true;
      if (is("weather.city")) return out = d_.city, true;
      return false;
    }
    if (!strncmp(n, "cal.", 4)) {
      if (d_.eventsOld) return false;
      if (is("cal.count")) return out = std::to_string(d_.eventCount), true;
      if (d_.events.empty()) return false;
      if (is("cal.next.time")) return out = d_.events[0].time, true;
      if (is("cal.next.title")) return out = d_.events[0].title, true;
      return false;
    }
    if (!strncmp(n, "read.", 5)) {
      if (is("read.ok")) return out = d_.hasBook ? "1" : "", true;
      if (is("read.today_min") && d_.stats.dated) return out = std::to_string(d_.stats.todaySec / 60), true;
      if (is("read.goal_min")) return out = std::to_string(d_.stats.goalMin), true;
      if (is("read.streak")) return out = std::to_string(d_.stats.streak), true;
      if (!d_.hasBook) return false;
      if (is("read.title")) return out = d_.bookTitle, true;
      if (is("read.author")) return out = d_.bookAuthor, true;
      if (is("read.pct") && d_.bookPercent >= 0) return out = std::to_string(d_.bookPercent), true;
      return false;
    }
    if (is("news.count")) return out = std::to_string(d_.newsCount), true;
    if (is("news.day")) return out = d_.newsDay, !d_.newsDay.empty();
    return false;
  }

  int count(const char* list) const override {
    if (!strcmp(list, "weather.hours")) return d_.hasWeather ? static_cast<int>(d_.hours.size()) : 0;
    if (!strcmp(list, "weather.days")) return d_.hasWeather ? static_cast<int>(d_.days.size()) : 0;
    if (!strcmp(list, "cal.events")) return d_.eventsOld ? 0 : static_cast<int>(d_.events.size());
    if (!strcmp(list, "news.top")) return static_cast<int>(d_.newsTop.size());
    return 0;
  }

  bool field(const char* list, const int i, const char* f, std::string& out) const override {
    if (i < 0 || i >= count(list)) return false;
    const auto is = [f](const char* k) { return strcmp(f, k) == 0; };
    if (!strcmp(list, "weather.hours")) {
      const auto& h = d_.hours[i];
      if (is("h")) return out = h.h, true;
      if (is("t")) return out = std::to_string(h.t), true;
      if (is("icon")) return out = h.icon, true;
    } else if (!strcmp(list, "weather.days")) {
      const auto& dd = d_.days[i];
      if (is("d")) return out = dd.d, true;
      if (is("min")) return out = std::to_string(dd.min), true;
      if (is("max")) return out = std::to_string(dd.max), true;
      if (is("icon")) return out = dd.icon, true;
    } else if (!strcmp(list, "cal.events")) {
      if (is("time")) return out = d_.events[i].time, true;
      if (is("title")) return out = d_.events[i].title, true;
    } else if (!strcmp(list, "news.top")) {
      if (is("title")) return out = d_.newsTop[i], true;
    }
    return false;
  }

 private:
  static std::string weekdayShort(const int wday) {
    // "CN,T2,T3,T4,T5,T6,T7"
    const std::string all = tr(STR_EO_WDAYS_SHORT);
    size_t start = 0;
    for (int k = 0; k < wday; k++) {
      start = all.find(',', start);
      if (start == std::string::npos) return {};
      start++;
    }
    const size_t end = all.find(',', start);
    return all.substr(start, end == std::string::npos ? std::string::npos : end - start);
  }

  const widgets::Data& d_;
  bool live_;
};

// Reads and parses a package's layout file. The document is the caller's to free.
bool loadLayout(const pkgs::Info& info, JsonDocument& doc) {
  std::string raw;
  const std::string path = pkgs::dirOf(info.id) + "/" + info.entry;
  if (!Storage.readFileToString("PKG", path, pkgs::LAYOUT_CAP, raw)) {
    LOG_ERR("PKG", "%s: cannot read %s", info.id.c_str(), info.entry.c_str());
    return false;
  }
  if (const DeserializationError e = deserializeJson(doc, raw)) {
    LOG_ERR("PKG", "%s: %s is not JSON (%s)", info.id.c_str(), info.entry.c_str(), e.c_str());
    return false;
  }
  return true;
}

}  // namespace

bool drawFace(GfxRenderer& r, const pkgs::FaceRef& face, const bool live) {
  const std::string& id = face.id;
  pkgs::Info info;
  if (!pkgs::get(id, info) || info.type != "face") return false;
  const uint32_t heap0 = ESP.getFreeHeap(), block0 = ESP.getMaxAllocHeap();
  widgets::Data data;
  widgets::loadData(data, 0);
  data.showTime = live;
  JsonDocument doc;
  if (!loadLayout(info, doc)) return false;
  const uint32_t heapDoc = ESP.getFreeHeap();
  const auto all = sizes(doc.as<JsonVariantConst>());
  if (all.empty()) return false;
  int index = 0;
  for (size_t i = 0; i < all.size(); i++) {
    if (all[i].w == face.w && all[i].h == face.h) index = static_cast<int>(i);
  }
  const bool portrait = all[index].h > all[index].w;
  r.setOrientation(portrait ? GfxRenderer::Orientation::Portrait : GfxRenderer::Orientation::LandscapeCounterClockwise);
  r.clearScreen();
  GfxCanvas canvas(r);
  Binder binder(data, live);
  Stats st;
  const bool ok = render(doc.as<JsonVariantConst>(), index, canvas, binder, info.perms, pkgs::dirOf(id), 0, 0,
                         r.getScreenWidth(), r.getScreenHeight(), &st);
  LOG_INF("PKG", "Face %s %dx%d: %d drawn, %d skipped; heap %u -> %u with the layout (%u B), block %u -> %u",
          id.c_str(), st.w, st.h, st.drawn, st.skipped, static_cast<unsigned>(heap0), static_cast<unsigned>(heapDoc),
          static_cast<unsigned>(heap0 > heapDoc ? heap0 - heapDoc : 0), static_cast<unsigned>(block0),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (st.imagesSkipped > 0)
    LOG_INF("PKG", "Face %s: %d pictures over the limit of 8 skipped", id.c_str(), st.imagesSkipped);
  return ok;
}

void drawWidget(GfxRenderer& r, const std::string& id, const widgets::Box& b, const widgets::Data& d) {
  pkgs::Info info;
  JsonDocument doc;
  if (!pkgs::get(id, info) || info.type != "widget" || !loadLayout(info, doc)) {
    // Installed later by the sync, or removed on the device.
    drawFitCentered(r, BODY_FONT, b.x + b.w / 2, b.y + (b.h - r.getLineHeight(BODY_FONT)) / 2, tr(STR_EO_PKG_MISSING),
                    b.w - 24);
    return;
  }
  GfxCanvas canvas(r);
  Binder binder(d, d.showTime);
  Stats st;
  // Inside the OS frame.
  constexpr int INSET = 6;
  if (!render(doc.as<JsonVariantConst>(), -1, canvas, binder, info.perms, pkgs::dirOf(id), b.x + INSET, b.y + INSET,
              b.w - 2 * INSET, b.h - 2 * INSET, &st)) {
    LOG_ERR("PKG", "Widget %s: not a level-%d layout", id.c_str(), LEVEL);
    drawFitCentered(r, BODY_FONT, b.x + b.w / 2, b.y + (b.h - r.getLineHeight(BODY_FONT)) / 2, tr(STR_EO_PKG_BROKEN),
                    b.w - 24);
  }
  if (st.imagesSkipped > 0)
    LOG_INF("PKG", "Widget %s: %d pictures over the limit of 8 skipped", id.c_str(), st.imagesSkipped);
}

}  // namespace espos::decl
