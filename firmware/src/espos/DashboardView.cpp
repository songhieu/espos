#include "DashboardView.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EspOSStore.h"
#include "ReadingStats.h"
#include "RecentBooksStore.h"
#include "espos/DashDraw.h"
#include "espos/DeclGfx.h"
#include "espos/EspOSFonts.h"
#include "espos/EspOSUi.h"
#include "espos/Pkgs.h"
#include "espos/Wallpapers.h"
#include "espos/Widgets.h"
#include "fontIds.h"
#include "util/BookProgress.h"
#include "util/QrUtils.h"

namespace espos::dashboard {
namespace {

namespace iu = espos::ui;

constexpr int PAD = 22;
constexpr int FOOTER_H = 40;
constexpr size_t DASH_FILE_CAP = 8 * 1024;

// System seconds (gettimeofday keeps counting through deep sleep) of the last download attempt.
RTC_DATA_ATTR int64_t s_lastTrySec = 0;

struct Now {
  bool valid = false;
  struct tm tm{};
  int minuteOfDay() const { return tm.tm_hour * 60 + tm.tm_min; }
};

struct Book {
  bool has = false;
  std::string title;
  int percent = -1;
  std::string today;       // "HÔM NAY 18/30 PHÚT" when there is reading to show
  std::string todayShort;  // "ĐỌC 18/30 PHÚT", for narrow panels
  std::string streak;      // "CHUỖI 6"
};

using namespace espos::draw;

bool inWindow(const int minute, const int from, const int to) {
  if (from == to) return false;
  if (from < to) return minute >= from && minute < to;
  return minute >= from || minute < to;
}

int64_t systemSec() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec;
}

int64_t fetchIntervalSec(const bool usbPowered) {
  int intervalMin = ESPOS.getInt("dash.interval_min", 30);
  if (intervalMin < 5) intervalMin = 5;
  if (intervalMin > 240) intervalMin = 240;
  if (usbPowered && ESPOS.get("dash.charging_min", "1") == "1") return 60;
  return static_cast<int64_t>(intervalMin) * 60;
}

// Failed downloads are retried at most this often.
int64_t retryGapSec(const bool usbPowered) {
  const int64_t interval = fetchIntervalSec(usbPowered);
  return interval < 300 ? interval : 300;
}

// ---- data ------------------------------------------------------------------

Now localNow() {
  Now n;
  n.valid = espos::ui::localTime(n.tm);
  return n;
}

Book loadBook() {
  Book b;
  const auto st = espos::stats::summary();
  if (st.dated && (st.recent || st.todaySec > 0)) {
    char buf[64];
    snprintf(buf, sizeof(buf), tr(STR_EO_DASH_TODAY_FMT), static_cast<int>(st.todaySec / 60), st.goalMin);
    b.today = buf;
    snprintf(buf, sizeof(buf), tr(STR_EO_DASH_TODAY_SHORT_FMT), static_cast<int>(st.todaySec / 60), st.goalMin);
    b.todayShort = buf;
    if (st.streak > 0) {
      snprintf(buf, sizeof(buf), tr(STR_EO_DASH_STREAK_FMT), st.streak);
      b.streak = buf;
    }
  }
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) return b;
  const auto& rb = books.front();
  b.has = true;
  b.title = rb.title.empty() ? FsHelpers::getFileNameWithoutExtension(rb.path) : rb.title;
  b.percent = loadBookProgress(rb.path);
  return b;
}

// Server date "14.11.2026" -> day, month; false when it does not parse.
bool serverDate(JsonDocument& d, int& day, int& month, int& year) {
  const char* s = d["date"] | "";
  return sscanf(s, "%d.%d.%d", &day, &month, &year) == 3;
}

// "THỨ BẢY · 14.11.2026" from the device clock, else from the server copy.
void dateCaps(const Now& now, JsonDocument& d, char* out, const size_t n) {
  if (now.valid) {
    snprintf(out, n, "%s · %02d.%02d.%04d", espos::ui::weekdayCaps(now.tm.tm_wday), now.tm.tm_mday, now.tm.tm_mon + 1,
             now.tm.tm_year + 1900);
  } else {
    snprintf(out, n, "%s · %s", d["weekday"] | "", d["date"] | "");
  }
}

int nextEventIndex(JsonArray events, const Now& now) {
  if (!now.valid || events.isNull()) return -1;
  int i = 0;
  for (JsonObject e : events) {
    const int m = minutesOfDay(e["time"] | "", -1);
    if (m >= 0 && m >= now.minuteOfDay()) return i;
    i++;
  }
  return -1;
}

// Lunar "6/10" (or "6/10N" in a leap month) -> day, month, leap.
bool lunarParts(const char* lunar, int& day, int& month, bool& leap) {
  if (!lunar || sscanf(lunar, "%d/%d", &day, &month) != 2) return false;
  leap = strchr(lunar, 'N') != nullptr;
  return true;
}

// ---- shared blocks ---------------------------------------------------------

// Caps title on the left, a small count on the right. Returns the height used.
int sectionHeader(const GfxRenderer& r, const int x, const int w, const int y, const char* title, const char* right) {
  r.drawText(CAPS_FONT, x, y, title, true, kBold);
  if (right && *right) drawRight(r, CAPS_FONT, x + w, y, right, kBold);
  return r.getLineHeight(CAPS_FONT) + 4;
}

// Event rows; the next upcoming event is inverted. Returns the new y.
int drawEventRows(const GfxRenderer& r, const int x, const int w, int y, const int yMax, const int rowH,
                  const int timeW, JsonArray events, const int nextIdx, const int maxRows) {
  int i = 0;
  int shown = 0;
  bool prevInv = false;
  const int lineH = r.getLineHeight(LIST_FONT);
  for (JsonObject e : events) {
    if (shown == maxRows || y + rowH > yMax) break;
    const bool inv = i == nextIdx;
    if (shown > 0 && !inv && !prevInv) r.drawLine(x, y, x + w, y, true);
    if (inv) r.fillRoundedRect(x - 8, y + 1, w + 16, rowH - 2, 6, Color::Black);
    const int ty = y + (rowH - lineH) / 2;
    r.drawText(LIST_FONT, x, ty, e["time"] | "", !inv, kBold);
    int titleW = w - timeW;
    if (inv) {
      const char* soon = tr(STR_EO_NEXT_CAPS);
      const int tw = r.getTextWidth(CAPS_FONT, soon, kBold) + 12;
      const int th = 24;
      const int tx = x + w - tw;
      const int tyy = y + (rowH - th) / 2;
      r.drawRoundedRect(tx, tyy, tw, th, 2, 4, false);
      r.drawText(CAPS_FONT, tx + 6, tyy + (th - r.getLineHeight(CAPS_FONT)) / 2 + 1, soon, false, kBold);
      titleW -= tw + 10;
    }
    drawFit(r, LIST_FONT, x + timeW, ty, e["title"] | "", titleW, kReg, !inv);
    y += rowH;
    shown++;
    i++;
    prevInv = inv;
  }
  return y;
}

// Weather icon, temperature and three lines of text; the hours strip below.
// Returns the height used.
int hoursStrip(const GfxRenderer& r, const int x, const int w, const int y, JsonObject w0, const bool icons) {
  JsonArray hours = w0["hours"];
  if (hours.isNull() || hours.size() == 0) return 0;
  const int cells = hours.size() > 4 ? 4 : static_cast<int>(hours.size());
  const int cellH = 40;
  r.fillRect(x, y, w, 2, true);
  r.fillRect(x, y + cellH, w, 2, true);
  const int cellW = w / cells;
  for (int i = 0; i < cells; i++) {
    JsonObject h = hours[i];
    const int cx = x + i * cellW;
    if (i > 0) r.fillRect(cx, y, 2, cellH, true);
    char label[24];
    snprintf(label, sizeof(label), "%s %d°", h["h"] | "", static_cast<int>(h["t"] | 0));
    const int iconW = icons ? 22 : 0;
    const int gap = icons ? 6 : 0;
    const int avail = cellW - iconW - gap - 10;
    const int f = espos::ui::fitFont(r, CAPS_FONT, label, avail, kBold);
    const int textW = std::min(r.getTextWidth(f, label, kBold), avail);
    const int startX = cx + (cellW - textW - iconW - gap) / 2;
    if (icons) drawIcon(r, h["icon"] | "cloud", startX + iconW / 2, y + cellH / 2 + 1, iconW);
    drawFit(r, CAPS_FONT, startX + iconW + gap, y + (cellH - r.getLineHeight(CAPS_FONT)) / 2 + 2, label, avail, kBold);
  }
  return cellH + 2;
}

// The latest notification. Returns false when there is none.
bool pickBottom(JsonDocument& d, const char*& tag, const char*& big, const char*& small, bool& isNote) {
  JsonArray notes = d["notes"];
  if (!notes.isNull() && notes.size() > 0) {
    tag = tr(STR_ID_NOTES_CAPS);
    big = notes[0]["title"] | "";
    small = notes[0]["time"] | "";
    isNote = true;
    return *big != 0;
  }
  return false;
}

// The widget sleep screens' status bar, as Home's: the time and date on the left, the battery on the right.
// A screen that does not tick shows the date only.
void drawStatusBar(const GfxRenderer& r, const bool clock) {
  constexpr int H = 40;
  const int x = iu::SIDE + 4;
  const int battLeft = iu::battery(r, r.getScreenWidth() - iu::SIDE - 4, H / 2 + 2);
  struct tm now{};
  if (!iu::localTime(now)) return;
  char date[48];
  snprintf(date, sizeof(date), "%s · %02d/%02d", iu::weekdayCaps(now.tm_wday), now.tm_mday, now.tm_mon + 1);
  const int cf = NOTOSANS_14_FONT_ID;
  const int cy = (H - r.getLineHeight(cf)) / 2 + 2;
  if (!clock) {
    iu::fitText(r, cf, x, cy, date, battLeft - 14 - x, true, EpdFontFamily::BOLD);
    return;
  }
  char t[8];
  const int hour = SETTINGS.clockFormat == 1 ? (now.tm_hour % 12 == 0 ? 12 : now.tm_hour % 12) : now.tm_hour;
  snprintf(t, sizeof(t), "%02d:%02d", hour, now.tm_min);
  r.drawText(cf, x, cy, t, true, EpdFontFamily::BOLD);
  const int baseline = cy + r.getFontAscenderSize(cf);
  const int dx = x + r.getTextWidth(cf, t, EpdFontFamily::BOLD) + 12;
  iu::fitText(r, iu::fontKicker(), dx, baseline - r.getFontAscenderSize(iu::fontKicker()), date, battLeft - 14 - dx,
              true, EpdFontFamily::BOLD);
}

// The bottom line, ruled like Home's button hints: the device name, when the data came and when it comes
// next (or "offline" in a black pill); the battery too where there is no status bar.
void drawFooter(const GfxRenderer& r, const WakePlan& plan, const bool showName, const bool battery) {
  const int w = r.getScreenWidth();
  const int y = r.getScreenHeight() - FOOTER_H;
  r.fillRect(0, y, w, 2, true);
  const int lineH = r.getLineHeight(CAPS_FONT);
  const int textY = y + (FOOTER_H - lineH) / 2 + 1;
  const int left = iu::SIDE + 4;
  int x = left;
  if (battery) {
    const int pct = powerManager.getBatteryPercentage();
    const int bw = 30;
    const int bh = 15;
    const int by = y + (FOOTER_H - bh) / 2;
    r.drawRect(x, by, bw, bh, 2, true);
    r.fillRect(x + bw, by + 4, 3, bh - 8, true);
    const int fill = (bw - 6) * (pct > 100 ? 100 : pct) / 100;
    if (fill > 0) r.fillRect(x + 3, by + 3, fill, bh - 6, true);
    char pctText[8];
    snprintf(pctText, sizeof(pctText), "%d%%", pct);
    x += bw + 10;
    r.drawText(CAPS_FONT, x, textY, pctText, true, kBold);
    x += r.getTextWidth(CAPS_FONT, pctText, kBold) + 16;
  }
  char dataAt[8];
  ESPOS.formatClock(ESPOS.dashFetchedMs, dataAt, sizeof(dataAt));
  char text[96];
  if (plan.saving) {
    snprintf(text, sizeof(text), tr(STR_EO_SAVING_FMT), dataAt);
  } else if (ESPOS.dashOffline) {
    snprintf(text, sizeof(text), tr(STR_ID_OFFLINE_FMT), dataAt);
  } else if (plan.nextFetchMs > 0) {
    char next[8];
    ESPOS.formatClock(plan.nextFetchMs, next, sizeof(next));
    snprintf(text, sizeof(text), tr(STR_ID_DATA_AT_FMT), dataAt, next);
  } else {
    snprintf(text, sizeof(text), tr(STR_ID_DATA_AT_ONLY_FMT), dataAt);
  }
  const int tw = r.getTextWidth(CAPS_FONT, text, kBold);
  int rightX = w - left;
  if (ESPOS.dashOffline) {
    // Offline: a black pill so it stands out.
    const int pw = tw + 20;
    const int ph = FOOTER_H - 14;
    r.fillRoundedRect(rightX - pw, y + 8, pw, ph, 6, Color::Black);
    r.drawText(CAPS_FONT, rightX - pw + 10, y + 8 + (ph - lineH) / 2 + 1, text, false, kBold);
    rightX -= pw;
  } else {
    r.drawText(CAPS_FONT, rightX - tw, textY, text, true, kBold);
    rightX -= tw;
  }
  if (showName && !ESPOS.deviceName.empty()) {
    drawFit(r, CAPS_FONT, x, textY, ESPOS.deviceName.c_str(), rightX - 20 - x, kBold, true);
  }
}

// ---- layout A: desk --------------------------------------------------------

void drawDesk(const GfxRenderer& r, JsonDocument& d, const WakePlan& plan, const Now& now, const Book& book) {
  const int top = 18;
  const int bottom = r.getScreenHeight() - FOOTER_H - 12;
  char line[128];

  // Left column: date, clock, lunar, weather, book.
  const int lx = PAD;
  const int lw = 340;
  int y = top;
  dateCaps(now, d, line, sizeof(line));
  drawFit(r, CAPS_FONT, lx, y, line, lw, kBold);
  y += r.getLineHeight(CAPS_FONT) + 6;

  char hero[16] = "";
  if (plan.clock && now.valid) {
    snprintf(hero, sizeof(hero), "%02d:%02d", now.tm.tm_hour, now.tm.tm_min);
  } else if (now.valid) {
    snprintf(hero, sizeof(hero), "%d/%d", now.tm.tm_mday, now.tm.tm_mon + 1);
  } else {
    int dd, mm, yy;
    if (serverDate(d, dd, mm, yy)) snprintf(hero, sizeof(hero), "%d/%d", dd, mm);
  }
  if (*hero) {
    drawDigits(r, ESPOS_60_FONT_ID, lx - 4, y, hero);
    y += digitHeight(ESPOS_60_FONT_ID) + 16;
  }

  const char* lunar = d["lunar"] | "";
  if (*lunar) {
    const int rowH = r.getLineHeight(SMALL_FONT);
    const int tagW = invTag(r, lx, y + (rowH - 26) / 2, tr(STR_ID_LUNAR_CAPS));
    snprintf(line, sizeof(line), "%s · %s", lunar, d["lunar_year"] | "");
    drawFit(r, SMALL_FONT, lx + tagW + 10, y, line, lw - tagW - 10, kBold);
    y += rowH + 6;
  }
  r.fillRect(lx, y, lw, 3, true);
  y += 3 + 8;

  JsonObject w = d["weather"];
  if (!w.isNull()) {
    const int rowH = 84;
    drawIcon(r, w["icon"] | "cloud", lx + 29, y + rowH / 2, 58);
    char temp[16];
    snprintf(temp, sizeof(temp), "%d°", static_cast<int>(w["temp"] | 0));
    const int tx = lx + 66;
    drawDigits(r, ESPOS_30_FONT_ID, tx, y + (rowH - digitHeight(ESPOS_30_FONT_ID)) / 2, temp);
    const int metaW = lw - (tx - lx) - r.getTextWidth(ESPOS_30_FONT_ID, temp, kBold) - 12;
    const int right = lx + lw;
    if (metaW > 40) {
      drawFitRight(r, BODY_FONT, right, y + 2, w["city"] | "", metaW, kBold);
      drawFitRight(r, CAPS_FONT, right, y + 38, w["desc"] | "", metaW);
      snprintf(line, sizeof(line), "%d–%d° · %d%%", static_cast<int>(w["min"] | 0), static_cast<int>(w["max"] | 0),
               static_cast<int>(w["rain"] | 0));
      drawFitRight(r, CAPS_FONT, right, y + 60, line, metaW);
    }
    y += rowH + 6;
    const int hs = hoursStrip(r, lx, lw, y, w, false);
    if (hs) y += hs + 10;
  }

  if (book.has) {
    // Today's minutes sit in the right column here (the left one is full).
    const int bh = r.getLineHeight(CAPS_FONT) + r.getLineHeight(SMALL_FONT) + 16;
    const int by = bottom - bh;
    if (by >= y) {
      r.drawText(CAPS_FONT, lx, by, tr(STR_EO_READING), true, kBold);
      const int ty = by + r.getLineHeight(CAPS_FONT);
      drawFit(r, SMALL_FONT, lx, ty, book.title.c_str(), lw, kBold);
      const int barY = ty + r.getLineHeight(SMALL_FONT) + 2;
      if (book.percent >= 0) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", book.percent);
        const int pw = r.getTextWidth(CAPS_FONT, pct, kBold);
        bar(r, lx, barY, lw - pw - 10, 12, book.percent, true);
        r.drawText(CAPS_FONT, lx + lw - pw, barY + 6 - r.getLineHeight(CAPS_FONT) / 2, pct, true, kBold);
      }
    }
  }

  // Divider.
  const int divX = lx + lw + 22;
  r.fillRect(divX, top, 3, bottom - top, true);

  // Right column: today's events, then the latest notification.
  const int rx = divX + 3 + 21;
  const int rw = r.getScreenWidth() - PAD - rx;
  y = top;

  const char* tag = nullptr;
  const char* big = nullptr;
  const char* small = nullptr;
  bool isNote = false;
  int listMax = bottom;
  if (pickBottom(d, tag, big, small, isNote)) {
    const int boxH = 50;
    const int boxY = bottom - boxH;
    r.drawRoundedRect(rx, boxY, rw, boxH, 2, 8, true);
    int x = rx + 12;
    x += invTag(r, x, boxY + (boxH - 26) / 2, tag) + 12;
    const int avail = rx + rw - 12 - x;
    if (isNote) {
      const int timeW = *small ? r.getTextWidth(CAPS_FONT, small, kBold) + 10 : 0;
      drawFit(r, SMALL_FONT, x, boxY + (boxH - r.getLineHeight(SMALL_FONT)) / 2, big, avail - timeW, kBold);
      if (timeW) drawRight(r, CAPS_FONT, rx + rw - 12, boxY + (boxH - r.getLineHeight(CAPS_FONT)) / 2, small, kBold);
    } else {
      const int bw = drawFit(r, BODY_FONT, x, boxY + (boxH - r.getLineHeight(BODY_FONT)) / 2 + 1, big, avail, kBold);
      drawFit(r, SMALL_FONT, x + bw + 12, boxY + (boxH - r.getLineHeight(SMALL_FONT)) / 2 + 2, small, avail - bw - 12);
    }
    listMax = boxY - 10;
  }
  if (!book.today.empty()) {
    const int ry = listMax - r.getLineHeight(CAPS_FONT);
    r.fillRect(rx, ry - 8, rw, 2, true);
    if (!book.streak.empty()) drawRight(r, CAPS_FONT, rx + rw, ry, book.streak.c_str(), kBold);
    const int sw = book.streak.empty() ? 0 : r.getTextWidth(CAPS_FONT, book.streak.c_str(), kBold) + 12;
    drawFit(r, CAPS_FONT, rx, ry, book.today.c_str(), rw - sw, kBold);
    listMax = ry - 16;
  }

  JsonArray events = d["events"];
  const int nEvents = events.isNull() ? 0 : static_cast<int>(events.size());
  snprintf(line, sizeof(line), tr(STR_EO_EVENTS_N_FMT), nEvents);
  y += sectionHeader(r, rx, rw, y, tr(STR_ID_EVENTS_CAPS), nEvents ? line : "");
  if (nEvents == 0) {
    espos::ui::wrap(r, LIST_FONT, rx, y + 4, rw, tr(STR_EO_NO_EVENTS), 2);
  } else {
    drawEventRows(r, rx, rw, y, listMax, 40, 72, events, nextEventIndex(events, now), 8);
  }
}

// ---- layout B: calendar page -----------------------------------------------

void drawCalendar(const GfxRenderer& r, JsonDocument& d, const WakePlan& plan, const Now& now, const Book& book) {
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  const int panelW = 236;
  const int panelBottom = H - FOOTER_H;
  char line[128];

  // Black tear-off calendar sheet.
  r.fillRect(0, 0, panelW, panelBottom, true);
  const int cx = panelW / 2;
  int day = 0, month = 0, year = 0, wday = -1;
  if (now.valid) {
    day = now.tm.tm_mday;
    month = now.tm.tm_mon + 1;
    year = now.tm.tm_year + 1900;
    wday = now.tm.tm_wday;
  } else {
    serverDate(d, day, month, year);
  }
  int y = 20;
  if (month > 0) {
    snprintf(line, sizeof(line), tr(STR_EO_MONTH_FMT), month);
    drawCentered(r, CAPS_BIG_FONT, cx, y, line, kBold, false);
  }
  y += r.getLineHeight(CAPS_BIG_FONT) + 8;
  if (day > 0) {
    snprintf(line, sizeof(line), "%d", day);
    drawDigits(r, ESPOS_60_FONT_ID, cx - r.getTextWidth(ESPOS_60_FONT_ID, line, kBold) / 2, y, line, false);
  }
  y += digitHeight(ESPOS_60_FONT_ID) + 14;
  const char* wd = wday >= 0 ? espos::ui::weekdayCaps(wday) : (d["weekday"] | "");
  drawCentered(r, CAPS_BIG_FONT, cx, y, wd, kBold, false);
  y += r.getLineHeight(CAPS_BIG_FONT) + 14;

  int ld, lm;
  bool leap = false;
  const char* lunar = d["lunar"] | "";
  if (lunarParts(lunar, ld, lm, leap)) {
    const int bx = 18;
    const int bw = panelW - 36;
    const int bh = 6 + r.getLineHeight(CAPS_FONT) + 40 + r.getLineHeight(SMALL_FONT) + 4;
    r.drawRoundedRect(bx, y, bw, bh, 2, 8, false);
    int yy = y + 6;
    drawCentered(r, CAPS_FONT, cx, yy, tr(STR_ID_LUNAR_CAPS), kBold, false);
    yy += r.getLineHeight(CAPS_FONT) - 4;
    snprintf(line, sizeof(line), tr(STR_EO_LUNAR_DAY_FMT), ld, lm);
    if (leap) strncat(line, tr(STR_EO_LUNAR_LEAP), sizeof(line) - strlen(line) - 1);
    drawFitCentered(r, NOTOSANS_16_FONT_ID, cx, yy, line, bw - 12, kBold, false);
    yy += 40;
    snprintf(line, sizeof(line), tr(STR_EO_LUNAR_YEAR_FMT), d["lunar_year"] | "");
    drawFitCentered(r, SMALL_FONT, cx, yy, line, bw - 12, kReg, false);
    y += bh;
  }

  if (book.has) {
    const int baseH = r.getLineHeight(CAPS_FONT) + r.getLineHeight(SMALL_FONT) + 16;
    int todayH = book.today.empty() ? 0 : r.getLineHeight(CAPS_FONT) + 2;
    if (panelBottom - 14 - baseH - todayH < y + 10) todayH = 0;
    const int bh = baseH + todayH;
    const int by = panelBottom - 14 - bh;
    if (by >= y + 10) {
      const int bx = 18;
      const int bw = panelW - 36;
      r.drawText(CAPS_FONT, bx, by, tr(STR_EO_READING), false, kBold);
      const int ty = by + r.getLineHeight(CAPS_FONT);
      drawFit(r, SMALL_FONT, bx, ty, book.title.c_str(), bw, kBold, false);
      const int barY = ty + r.getLineHeight(SMALL_FONT) + 2;
      if (book.percent >= 0) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", book.percent);
        const int pw = r.getTextWidth(CAPS_FONT, pct, kBold);
        bar(r, bx, barY, bw - pw - 10, 12, book.percent, false);
        r.drawText(CAPS_FONT, bx + bw - pw, barY + 6 - r.getLineHeight(CAPS_FONT) / 2, pct, false, kBold);
      }
      if (todayH) {
        // Narrow panel: today's minutes only (the streak is on the stats screen).
        drawFit(r, CAPS_FONT, bx, barY + 16, book.todayShort.c_str(), bw, kBold, false);
      }
    }
  }

  // Main area.
  const int mx = panelW + 22;
  const int mw = W - PAD - mx;
  const int top = 14;
  JsonObject w = d["weather"];
  y = top + 8;
  if (plan.clock && now.valid) {
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", now.tm.tm_hour, now.tm.tm_min);
    drawDigits(r, ESPOS_60_FONT_ID, mx - 4, y, clock);
    const int clockW = r.getTextWidth(ESPOS_60_FONT_ID, clock, kBold);
    if (!w.isNull()) {
      const int right = mx + mw;
      const int wb = mw - clockW - 24;
      char temp[16];
      snprintf(temp, sizeof(temp), "%d°", static_cast<int>(w["temp"] | 0));
      const int tw = r.getTextWidth(ESPOS_30_FONT_ID, temp, kBold);
      drawDigits(r, ESPOS_30_FONT_ID, right - tw, y + 4, temp);
      drawIcon(r, w["icon"] | "cloud", right - tw - 34, y + 4 + digitHeight(ESPOS_30_FONT_ID) / 2, 50);
      snprintf(line, sizeof(line), "%s · %s", w["city"] | "", w["desc"] | "");
      drawFitRight(r, CAPS_FONT, right, y + 58, line, wb, kBold);
      snprintf(line, sizeof(line), "%d–%d° · %d%%", static_cast<int>(w["min"] | 0), static_cast<int>(w["max"] | 0),
               static_cast<int>(w["rain"] | 0));
      drawFitRight(r, CAPS_FONT, right, y + 82, line, wb);
    }
    y += digitHeight(ESPOS_60_FONT_ID) + 18;
  } else if (!w.isNull()) {
    // No ticking clock: the weather takes the top row.
    drawIcon(r, w["icon"] | "cloud", mx + 40, y + digitHeight(ESPOS_60_FONT_ID) / 2, 76);
    char temp[16];
    snprintf(temp, sizeof(temp), "%d°", static_cast<int>(w["temp"] | 0));
    drawDigits(r, ESPOS_60_FONT_ID, mx + 92, y, temp);
    const int tx = mx + 92 + r.getTextWidth(ESPOS_60_FONT_ID, temp, kBold) + 18;
    const int tw = mx + mw - tx;
    if (tw > 60) {
      drawFit(r, BODY_FONT, tx, y + 4, w["city"] | "", tw, kBold);
      drawFit(r, SMALL_FONT, tx, y + 44, w["desc"] | "", tw);
      snprintf(line, sizeof(line), "%d–%d° · %d%%", static_cast<int>(w["min"] | 0), static_cast<int>(w["max"] | 0),
               static_cast<int>(w["rain"] | 0));
      drawFit(r, CAPS_FONT, tx, y + 76, line, tw);
    }
    y += digitHeight(ESPOS_60_FONT_ID) + 18;
  }
  if (!w.isNull()) {
    const int hs = hoursStrip(r, mx, mw, y, w, true);
    if (hs) y += hs + 10;
  }

  // Bottom line: the latest notification.
  const int bottom = panelBottom - 10;
  int colBottom = bottom;
  const char* tag = nullptr;
  const char* big = nullptr;
  const char* small = nullptr;
  bool isNote = false;
  if (pickBottom(d, tag, big, small, isNote)) {
    const int rowH = 34;
    const int ly = bottom - rowH;
    r.fillRect(mx, ly - 6, mw, 2, true);
    int x = mx;
    r.drawText(CAPS_FONT, x, ly + (rowH - r.getLineHeight(CAPS_FONT)) / 2 + 1, tag, true, kBold);
    x += r.getTextWidth(CAPS_FONT, tag, kBold) + 14;
    const int avail = mx + mw - x;
    if (isNote) {
      const int timeW = *small ? r.getTextWidth(CAPS_FONT, small, kBold) + 10 : 0;
      drawFit(r, SMALL_FONT, x, ly + (rowH - r.getLineHeight(SMALL_FONT)) / 2, big, avail - timeW, kBold);
      if (timeW) drawRight(r, CAPS_FONT, mx + mw, ly + (rowH - r.getLineHeight(CAPS_FONT)) / 2, small, kBold);
    } else {
      const int bw = drawFit(r, SMALL_FONT, x, ly + (rowH - r.getLineHeight(SMALL_FONT)) / 2, big, avail, kBold);
      drawFit(r, SMALL_FONT, x + bw + 12, ly + (rowH - r.getLineHeight(SMALL_FONT)) / 2, small, avail - bw - 12);
    }
    colBottom = ly - 14;
  }

  // Today's events across the main area.
  JsonArray events = d["events"];
  const int nEvents = events.isNull() ? 0 : static_cast<int>(events.size());
  snprintf(line, sizeof(line), tr(STR_EO_EVENTS_N_FMT), nEvents);
  y += sectionHeader(r, mx, mw, y, tr(STR_ID_EVENTS_CAPS), nEvents ? line : "");
  if (nEvents == 0) {
    espos::ui::wrap(r, LIST_FONT, mx, y + 2, mw, tr(STR_EO_NO_EVENTS), 2);
  } else {
    drawEventRows(r, mx, mw, y, colBottom, 34, 72, events, nextEventIndex(events, now), 6);
  }
}

// ---- portrait ----------------------------------------------------------------

struct Column {
  int x;
  int w;
  int y;
  int yMax;
};

bool sectionFits(const Column& c, const int need) { return c.y + need <= c.yMax; }

void drawSectionTitle(const GfxRenderer& r, Column& c, const char* title) {
  r.drawText(CAPS_FONT, c.x, c.y, title, true, kBold);
  c.y += r.getLineHeight(CAPS_FONT) + 4;
}

void drawPortraitHeader(const GfxRenderer& r, Column& c, JsonDocument& d, const WakePlan& plan, const Now& now) {
  const int capsH = r.getLineHeight(CAPS_FONT);
  const char* lunar = d["lunar"] | "";
  const bool hasLunar = *lunar;
  const char* lunarYear = d["lunar_year"] | "";
  // The box grows to fit the year ("Bính Ngọ") instead of cutting it.
  int boxW = 112;
  if (hasLunar) {
    const int need =
        std::max(r.getTextWidth(CAPS_FONT, lunarYear) + 24, r.getTextWidth(NOTOSANS_16_FONT_ID, lunar, kBold) + 24);
    if (need > boxW) boxW = need;
  }
  const int boxX = c.x + c.w - boxW;
  const int leftW = hasLunar ? boxX - c.x - 14 : c.w;
  char line[96];
  dateCaps(now, d, line, sizeof(line));
  drawFit(r, CAPS_FONT, c.x, c.y, line, leftW, kBold);
  int bottom;
  if (plan.clock && now.valid) {
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", now.tm.tm_hour, now.tm.tm_min);
    drawDigits(r, ESPOS_44_FONT_ID, c.x - 2, c.y + capsH + 6, clock);
    bottom = c.y + capsH + 6 + digitHeight(ESPOS_44_FONT_ID) + 4;
  } else {
    int dd = 0, mm = 0, yy = 0;
    if (now.valid) {
      dd = now.tm.tm_mday;
      mm = now.tm.tm_mon + 1;
    } else {
      serverDate(d, dd, mm, yy);
    }
    char date[16];
    snprintf(date, sizeof(date), "%d/%d", dd, mm);
    drawDigits(r, ESPOS_44_FONT_ID, c.x - 2, c.y + capsH + 6, date);
    bottom = c.y + capsH + 6 + digitHeight(ESPOS_44_FONT_ID) + 4;
  }

  if (hasLunar) {
    const int lunarH = 42;
    const int boxH = 4 + capsH + lunarH + capsH + 6;
    r.drawRoundedRect(boxX, c.y - 2, boxW, boxH, 2, 10, true);
    int yy = c.y + 2;
    drawCentered(r, CAPS_FONT, boxX + boxW / 2, yy, tr(STR_ID_LUNAR_CAPS), kBold);
    yy += capsH - 4;
    drawCentered(r, NOTOSANS_16_FONT_ID, boxX + boxW / 2, yy, lunar, kBold);
    yy += lunarH;
    drawCentered(r, CAPS_FONT, boxX + boxW / 2, yy, lunarYear);
    if (c.y + boxH > bottom) bottom = c.y + boxH;
  }
  c.y = bottom + 10;
  r.fillRect(c.x, c.y, c.w, 3, true);
  c.y += 14;
}

void drawPortraitWeather(const GfxRenderer& r, Column& c, JsonDocument& d) {
  JsonObject w = d["weather"];
  if (w.isNull()) return;
  const int iconS = 70;
  const int cityH = r.getLineHeight(BODY_FONT);
  const int smallH = r.getLineHeight(CAPS_FONT);
  const int rowH = std::max(digitHeight(ESPOS_44_FONT_ID) + 12, cityH + smallH * 2 + 4);
  drawIcon(r, w["icon"] | "cloud", c.x + iconS / 2, c.y + rowH / 2, iconS);
  char temp[16];
  snprintf(temp, sizeof(temp), "%d°", static_cast<int>(w["temp"] | 0));
  const int tempX = c.x + iconS + 10;
  drawDigits(r, ESPOS_44_FONT_ID, tempX, c.y + (rowH - digitHeight(ESPOS_44_FONT_ID)) / 2, temp);
  const int tempW = r.getTextWidth(ESPOS_44_FONT_ID, temp, kBold);
  const int right = c.x + c.w;
  const int textW = right - (tempX + tempW + 14);
  if (textW > 40) {
    int y = c.y + (rowH - (cityH + smallH * 2)) / 2;
    drawFitRight(r, BODY_FONT, right, y, w["city"] | "", textW, kBold);
    y += cityH;
    drawFitRight(r, CAPS_FONT, right, y, w["desc"] | "", textW);
    y += smallH;
    char range[48];
    snprintf(range, sizeof(range), "%d–%d° · %d%%", static_cast<int>(w["min"] | 0), static_cast<int>(w["max"] | 0),
             static_cast<int>(w["rain"] | 0));
    drawFitRight(r, CAPS_FONT, right, y, range, textW);
  }
  c.y += rowH + 8;
  const int hs = hoursStrip(r, c.x, c.w, c.y, w, true);
  if (hs) c.y += hs + 12;
}

void drawPortraitEvents(const GfxRenderer& r, Column& c, JsonDocument& d, const Now& now) {
  JsonArray events = d["events"];
  if (events.isNull() || events.size() == 0) return;
  const int rowH = 38;
  if (!sectionFits(c, r.getLineHeight(CAPS_FONT) + rowH)) return;
  drawSectionTitle(r, c, tr(STR_ID_EVENTS_CAPS));
  c.y = drawEventRows(r, c.x, c.w, c.y, c.yMax, rowH, 72, events, nextEventIndex(events, now), 7) + 8;
}

void drawPortraitReading(const GfxRenderer& r, Column& c, const Book& book) {
  if (book.today.empty()) return;
  const int lineH = r.getLineHeight(CAPS_FONT);
  if (!sectionFits(c, lineH + 12)) return;
  r.fillRect(c.x, c.y, c.w, 2, true);
  c.y += 8;
  if (!book.streak.empty()) drawRight(r, CAPS_FONT, c.x + c.w, c.y, book.streak.c_str(), kBold);
  const int sw = book.streak.empty() ? 0 : r.getTextWidth(CAPS_FONT, book.streak.c_str(), kBold) + 12;
  drawFit(r, CAPS_FONT, c.x, c.y, book.today.c_str(), c.w - sw, kBold);
  c.y += lineH + 10;
}

void drawPortraitBottom(const GfxRenderer& r, Column& c, JsonDocument& d) {
  const char* tag = nullptr;
  const char* big = nullptr;
  const char* small = nullptr;
  bool isNote = false;
  if (!pickBottom(d, tag, big, small, isNote)) return;
  const int boxH = r.getLineHeight(CAPS_FONT) + r.getLineHeight(SMALL_FONT) + 18;
  if (!sectionFits(c, boxH)) return;
  const int boxY = c.yMax - boxH;
  r.drawRoundedRect(c.x, boxY, c.w, boxH, 2, 8, true);
  r.drawText(CAPS_FONT, c.x + 12, boxY + 6, tag, true, kBold);
  const int ty = boxY + 6 + r.getLineHeight(CAPS_FONT);
  const int bw = drawFit(r, SMALL_FONT, c.x + 12, ty, big, c.w - 24, kBold);
  if (*small) drawFit(r, SMALL_FONT, c.x + 12 + bw + 12, ty, small, c.w - 36 - bw);
}

}  // namespace

bool hasCache() { return Storage.exists(DASH_FILE); }

Mode effectiveMode() {
  const std::string m = ESPOS.get("sleep.mode", "dashboard");
  if (m == "images") return wallpapers::any() ? Mode::Images : Mode::Dashboard;
  if (m == "cover" || m == "auto") {
    const std::string& open = APP_STATE.openEpubPath;
    if (!open.empty()) {
      const int pct = loadBookProgress(open);
      if (pct < 98) return Mode::Cover;
    }
    return Mode::Dashboard;
  }
  return Mode::Dashboard;
}

bool sleepScreenEnabled() {
  if (!ESPOS.isLinked() || ESPOS.get("dash.enabled", "1") == "0") return false;
  switch (effectiveMode()) {
    case Mode::Images:
      return true;
    case Mode::Cover:
      return false;
    default:
      return hasCache();
  }
}

bool ownsSleepScreen() { return ESPOS.get("dash.enabled", "1") != "0"; }

bool draw(GfxRenderer& renderer, const WakePlan& plan) {
  if (plan.clock) espos::ui::settleMinute();
  if (effectiveMode() == Mode::Images) {
    return wallpapers::draw(renderer, plan.clock && ESPOS.get("sleep.clock", "1") != "0");
  }
  const std::string chosen = ESPOS.get("dash.layout", "desk");
  {
    // A face from a package (pkg:<id>[@WxH]) owns the whole screen; a missing one falls back to "desk".
    pkgs::FaceRef face;
    if (pkgs::FaceRef::parse(chosen, face) && espos::decl::drawFace(renderer, face, plan.clock)) return true;
  }
  if (chosen == "wl" || chosen == "wp") {
    // Widgets arranged on the web (sleep.wl / sleep.wp), or the built-in widget layout.
    namespace wg = espos::widgets;
    const bool portrait = chosen == "wp";
    renderer.setOrientation(portrait ? GfxRenderer::Orientation::Portrait
                                     : GfxRenderer::Orientation::LandscapeCounterClockwise);
    renderer.clearScreen();
    const wg::Layout l = wg::load(portrait ? wg::Screen::SleepPortrait : wg::Screen::SleepLandscape);
    wg::Data data;
    wg::loadData(data, 156);
    data.showTime = plan.clock;
    // A timer wake redraws the same showing; otherwise the screen was just opened (photo widgets).
    data.showSeq = wg::showSeq(esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER);
    const auto boxes = wg::boxesFor(l);
    wg::ensureCovers(l, boxes, data);
    for (size_t i = 0; i < l.items.size(); i++) wg::draw(renderer, l.items[i], boxes[i], data, false, l.skin);
    if (l.footer) {
      drawStatusBar(renderer, plan.clock);
      drawFooter(renderer, plan, !portrait, false);
    }
    return true;
  }
  std::string raw;
  if (!Storage.readFileToString("ESPOS", DASH_FILE, DASH_FILE_CAP, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw)) {
    LOG_ERR("ESPOS", "Dashboard cache unreadable");
    return false;
  }
  raw.clear();
  raw.shrink_to_fit();

  // The local setting wins: it changes on the device before the server copy catches up.
  std::string layout = ESPOS.get("dash.layout", "desk");
  if (layout == "landscape") layout = "desk";
  const bool portrait = layout == "portrait";
  renderer.setOrientation(portrait ? GfxRenderer::Orientation::Portrait
                                   : GfxRenderer::Orientation::LandscapeCounterClockwise);
  renderer.clearScreen();
  const Now now = localNow();
  const Book book = loadBook();

  if (layout == "calendar") {
    drawCalendar(renderer, doc, plan, now, book);
  } else if (!portrait) {
    drawDesk(renderer, doc, plan, now, book);
  } else {
    const int w = renderer.getScreenWidth();
    Column col{PAD, w - PAD * 2, 20, renderer.getScreenHeight() - FOOTER_H - 12};
    drawPortraitHeader(renderer, col, doc, plan, now);
    drawPortraitWeather(renderer, col, doc);
    drawPortraitEvents(renderer, col, doc, now);
    drawPortraitReading(renderer, col, book);
    drawPortraitBottom(renderer, col, doc);
  }
  drawFooter(renderer, plan, !portrait, true);
  return true;
}

void drawPlaceholder(GfxRenderer& renderer) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int x = PAD;
  int y = 40;
  std::string wd, date, wdShort;
  if (espos::ui::dateParts(wd, date, wdShort)) {
    renderer.drawText(CAPS_FONT, x, y, wd.c_str(), true, kBold);
    y += renderer.getLineHeight(CAPS_FONT);
    renderer.drawText(ESPOS_24_FONT_ID, x, y, date.c_str(), true, kBold);
    y += renderer.getLineHeight(ESPOS_24_FONT_ID) + 6;
  } else {
    renderer.drawText(ESPOS_24_FONT_ID, x, y, "EspOS", true, kBold);
    y += renderer.getLineHeight(ESPOS_24_FONT_ID) + 6;
  }
  renderer.fillRect(x, y, w - PAD * 2, 3, true);
  y += 28;
  const bool linked = ESPOS.isLinked();
  y += espos::ui::wrap(renderer, BODY_FONT, x, y, w - PAD * 2,
                       linked ? tr(STR_EO_SLEEP_NODATA) : tr(STR_EO_SLEEP_UNLINKED), 5, true, 0, 4);
  if (!linked && !ESPOS.webUrl().empty()) {
    const int qr = 200;
    const int qy = y + 30;
    QrUtils::drawQrCode(renderer, Rect{(w - qr) / 2, qy, qr, qr}, ESPOS.webUrl());
    const std::string host = ESPOS.webHost();
    const int uw = renderer.getTextWidth(BODY_FONT, host.c_str(), kBold);
    renderer.drawText(BODY_FONT, (w - uw) / 2, qy + qr + 12, host.c_str(), true, kBold);
  }
  // Footer band like the dashboard's.
  renderer.fillRect(0, h - FOOTER_H, w, FOOTER_H, true);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(powerManager.getBatteryPercentage()));
  const int pw = renderer.getTextWidth(CAPS_FONT, pct, kBold);
  const int ty = h - FOOTER_H + (FOOTER_H - renderer.getLineHeight(CAPS_FONT)) / 2 + 1;
  drawFit(renderer, CAPS_FONT, PAD, ty, tr(STR_EO_SLEEPING), w - PAD * 2 - pw - 16, kBold, false);
  renderer.drawText(CAPS_FONT, w - PAD - pw, ty, pct, false, kBold);
}

// ---- scheduling --------------------------------------------------------------

WakePlan planWake(const bool usbPowered) {
  WakePlan p;
  if (!sleepScreenEnabled()) return p;
  const int battery = powerManager.getBatteryPercentage();
  if (!usbPowered && battery < 8) return p;
  // Below the reader's saving level: no timed wakes at all (no data, no clock, no picture
  // change) until it is charged or a button wakes it.
  if (batterySaving(usbPowered)) {
    p.saving = true;
    return p;
  }

  const int64_t interval = fetchIntervalSec(usbPowered);
  const int64_t now = EspOSStore::nowMs();
  int64_t secs = interval;
  const bool overdue = ESPOS.dashFetchedMs <= 0 || now <= 0 || (now - ESPOS.dashFetchedMs) / 1000 >= interval;
  if (overdue) {
    // Space retries after a failed download.
    const int64_t since = systemSec() - s_lastTrySec;
    const int64_t gap = retryGapSec(usbPowered);
    secs = (s_lastTrySec > 0 && since >= 0 && since < gap) ? gap - since : 15;
  } else {
    secs = interval - (now - ESPOS.dashFetchedMs) / 1000;
  }

  bool quietNow = false;
  if (now > 0) {
    const int quietFrom = minutesOfDay(ESPOS.get("dash.quiet_from", "23:00").c_str(), 23 * 60);
    const int quietTo = minutesOfDay(ESPOS.get("dash.quiet_to", "06:00").c_str(), 6 * 60);
    const int64_t localSec = now / 1000 + static_cast<int64_t>(ESPOS.tzMin) * 60;
    const int nowMin = static_cast<int>((localSec % 86400) / 60);
    const int wakeMin = static_cast<int>(((localSec + secs) % 86400) / 60);
    quietNow = inWindow(nowMin, quietFrom, quietTo);
    if (inWindow(wakeMin, quietFrom, quietTo)) {
      int untilEnd = quietTo - nowMin;
      if (untilEnd <= 0) untilEnd += 24 * 60;
      secs = static_cast<int64_t>(untilEnd) * 60 + 30 - (localSec % 60);
    }
  }
  if (secs < 15) secs = 15;
  if (secs > 24 * 3600) secs = 24 * 3600;
  if (now > 0) p.nextFetchMs = now + secs * 1000;

  // Pictures that change on a timer.
  const bool images = effectiveMode() == Mode::Images;
  if (images && !quietNow) {
    const int64_t rot = wallpapers::secondsToRotate();
    if (rot > 0 && rot < secs) secs = rot < 15 ? 15 : rot;
  }

  // Minute clock: off at night, on a low battery, or when switched off on the web.
  const char* clockKey = images ? "sleep.clock" : "dash.clock";
  // A package face asks how often it wants to be redrawn (profiles.md §4.9): never more than that.
  const std::string faceRefresh = images ? std::string() : pkgs::faceRefresh();
  const bool clockOn = now > 0 && EspOSStore::clockValid() && !quietNow && ESPOS.get(clockKey, "1") != "0" &&
                       (faceRefresh.empty() || faceRefresh == "minute");
  if (now > 0 && !quietNow && (faceRefresh == "hour" || faceRefresh == "day")) {
    const int64_t localSec = now / 1000 + static_cast<int64_t>(ESPOS.tzMin) * 60;
    const int64_t period = faceRefresh == "hour" ? 3600 : 86400;
    const int64_t toNext = (localSec / period + 1) * period - localSec + 1;
    if (toNext < secs) secs = toNext;
  }
  if (clockOn) {
    const int64_t localSec = now / 1000 + static_cast<int64_t>(ESPOS.tzMin) * 60;
    // Wake one second into the minute after the one on screen.
    const int64_t toTick = (localSec / 60 + 1) * 60 - localSec + 1;
    if (toTick < secs) secs = toTick;
    p.clock = true;
  }
  p.sleepSec = static_cast<uint32_t>(secs);
  return p;
}

uint32_t nextWakeSeconds(const bool usbPowered) { return planWake(usbPowered).sleepSec; }

int saveBelowPercent() {
  const int pct = ESPOS.getInt("dash.save_pct", 20);
  return pct < 0 ? 0 : (pct > 50 ? 50 : pct);
}

bool batterySaving(const bool usbPowered) {
  if (usbPowered) return false;
  const int below = saveBelowPercent();
  return below > 0 && powerManager.getBatteryPercentage() < below;
}

bool fetchDue(const bool usbPowered) {
  if (batterySaving(usbPowered)) return false;
  const int64_t interval = fetchIntervalSec(usbPowered);
  const int64_t now = EspOSStore::nowMs();
  const bool stale = ESPOS.dashFetchedMs <= 0 || now <= 0 || (now - ESPOS.dashFetchedMs) / 1000 >= interval - 10;
  if (!stale) return false;
  const int64_t since = systemSec() - s_lastTrySec;
  return s_lastTrySec <= 0 || since < 0 || since >= retryGapSec(usbPowered) - 5;
}

void noteFetchAttempt() { s_lastTrySec = systemSec(); }

}  // namespace espos::dashboard
