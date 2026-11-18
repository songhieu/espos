#include "EspOSUi.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <cstdio>
#include <ctime>
#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "espos/EspOSFonts.h"
#include "espos/EspOSStore.h"
#include "fontIds.h"

namespace espos::ui {

int fontTitle() { return NOTOSANS_14_FONT_ID; }
int fontBody() { return UI_12_FONT_ID; }
int fontSmall() { return UI_10_FONT_ID; }
int fontKicker() { return UI_10_FONT_ID; }

int battery(const GfxRenderer& r, const int rightX, const int centerY) {
  const int pct = powerManager.getBatteryPercentage();
  char txt[8];
  snprintf(txt, sizeof(txt), "%d%%", pct);
  const int tw = r.getTextWidth(fontSmall(), txt, EpdFontFamily::BOLD);
  const int bodyW = 26;
  const int bodyH = 13;
  const int x = rightX - tw - 8 - bodyW - 3;
  const int y = centerY - bodyH / 2;
  r.drawRect(x, y, bodyW, bodyH, 2, true);
  r.fillRect(x + bodyW, y + 3, 3, bodyH - 6, true);
  const int fill = (bodyW - 6) * (pct < 0 ? 0 : pct > 100 ? 100 : pct) / 100;
  if (fill > 0) r.fillRect(x + 3, y + 3, fill, bodyH - 6, true);
  r.drawText(fontSmall(), rightX - tw, centerY - r.getLineHeight(fontSmall()) / 2, txt, true, EpdFontFamily::BOLD);
  return x;
}

void header(const GfxRenderer& r, const char* title, const char* right) {
  const int w = r.getScreenWidth();
  const int lh = r.getLineHeight(fontTitle());
  const int ty = (HEADER_H - lh) / 2;
  int rightEdge = w - SIDE;
  int leftOfRight = rightEdge;
  if (right && *right) {
    const int rw = r.getTextWidth(fontBody(), right, EpdFontFamily::BOLD);
    r.drawText(fontBody(), rightEdge - rw, (HEADER_H - r.getLineHeight(fontBody())) / 2, right, true,
               EpdFontFamily::BOLD);
    leftOfRight = rightEdge - rw;
  } else {
    leftOfRight = battery(r, rightEdge, HEADER_H / 2);
  }
  fitText(r, fontTitle(), SIDE, ty, title, leftOfRight - SIDE - 16, true, EpdFontFamily::BOLD);
  r.fillRect(0, HEADER_H - 2, w, 2, true);
}

int rowHeight(const GfxRenderer& r, const bool withSubtitle) {
  const int body = r.getLineHeight(fontBody());
  return withSubtitle ? body + r.getLineHeight(fontSmall()) + 14 : body + 22;
}

int row(const GfxRenderer& r, const int x, const int y, const int w, const char* title, const char* subtitle,
        const char* right, const bool selected, const uint8_t* icon32) {
  const bool hasSub = subtitle && *subtitle;
  const int h = rowHeight(r, hasSub);
  if (selected) r.fillRoundedRect(x, y, w, h, ROW_RADIUS, Color::Black);
  const bool ink = !selected;
  int tx = x + 16;
  if (icon32) {
    // drawIcon draws black bits; on a selected (black) row draw a white plate first so the icon reads.
    if (selected) r.fillRoundedRect(tx - 2, y + (h - 36) / 2, 36, 36, 8, Color::White);
    r.drawIcon(icon32, tx, y + (h - 32) / 2, 32);
    tx += 32 + 14;
  }
  int rightW = 0;
  if (right && *right) {
    rightW = r.getTextWidth(fontSmall(), right, EpdFontFamily::BOLD) + 16;
    r.drawText(fontSmall(), x + w - rightW, y + (h - r.getLineHeight(fontSmall())) / 2, right, ink,
               EpdFontFamily::BOLD);
  }
  const int textW = x + w - rightW - tx - 12;
  const int bodyH = r.getLineHeight(fontBody());
  if (hasSub) {
    fitText(r, fontBody(), tx, y + 7, title, textW, ink, EpdFontFamily::BOLD);
    fitText(r, fontSmall(), tx, y + 7 + bodyH, subtitle, textW, ink);
  } else {
    fitText(r, fontBody(), tx, y + (h - bodyH) / 2, title, textW, ink, EpdFontFamily::BOLD);
  }
  return h;
}

void dashedRect(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int radius) {
  constexpr int dash = 7;
  constexpr int gap = 5;
  for (int i = x + radius; i < x + w - radius; i += dash + gap) {
    const int len = std::min(dash, x + w - radius - i);
    r.fillRect(i, y, len, 2, true);
    r.fillRect(i, y + h - 2, len, 2, true);
  }
  for (int j = y + radius; j < y + h - radius; j += dash + gap) {
    const int len = std::min(dash, y + h - radius - j);
    r.fillRect(x, j, 2, len, true);
    r.fillRect(x + w - 2, j, 2, len, true);
  }
  // Corners as short solid arcs keep the box from looking broken.
  r.drawArc(radius, x + radius, y + radius, -1, -1, 2, true);
  r.drawArc(radius, x + w - radius - 1, y + radius, 1, -1, 2, true);
  r.drawArc(radius, x + radius, y + h - radius - 1, -1, 1, 2, true);
  r.drawArc(radius, x + w - radius - 1, y + h - radius - 1, 1, 1, 2, true);
}

namespace {
// Splits UTF-8 text into words on ASCII spaces (Vietnamese uses spaces between syllables).
std::vector<std::string> words(const char* text) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* p = text; *p; ++p) {
    if (*p == ' ' || *p == '\n') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
      if (*p == '\n') out.emplace_back("\n");
    } else {
      cur.push_back(*p);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}
}  // namespace

int smallerFont(const int font) {
  switch (font) {
    case ESPOS_SERIF_36_FONT_ID:
      return ESPOS_24_FONT_ID;
    case ESPOS_24_FONT_ID:
      return NOTOSANS_18_FONT_ID;
    case NOTOSANS_18_FONT_ID:
      return NOTOSANS_16_FONT_ID;
    case NOTOSANS_16_FONT_ID:
      return NOTOSANS_14_FONT_ID;
    case NOTOSANS_14_FONT_ID:
      return NOTOSANS_12_FONT_ID;
    case NOTOSANS_12_FONT_ID:
    case UI_12_FONT_ID:
      return UI_10_FONT_ID;
    case UI_10_FONT_ID:
      return SMALL_FONT_ID;
    default:
      return 0;  // font ids are hashes and may be negative; 0 is never one
  }
}

int fitFont(const GfxRenderer& r, const int font, const char* text, const int maxW, const int style) {
  if (!text || !*text) return font;
  const auto st = static_cast<EpdFontFamily::Style>(style);
  int f = font;
  while (r.getTextWidth(f, text, st) > maxW) {
    const int next = smallerFont(f);
    if (next == 0) break;
    f = next;
  }
  return f;
}

namespace {
// Lines of `ws` within `w` in `font`; a word wider than the line is split between characters.
std::vector<std::string> layoutLines(const GfxRenderer& r, const int font, const int w,
                                     const std::vector<std::string>& ws, const EpdFontFamily::Style st) {
  std::vector<std::string> lines;
  std::string line;
  for (const auto& word : ws) {
    if (word == "\n") {
      lines.push_back(line);
      line.clear();
      continue;
    }
    const std::string trial = line.empty() ? word : line + " " + word;
    if (r.getTextWidth(font, trial.c_str(), st) <= w) {
      line = trial;
      continue;
    }
    if (!line.empty()) lines.push_back(line);
    line.clear();
    if (r.getTextWidth(font, word.c_str(), st) <= w) {
      line = word;
      continue;
    }
    // Break inside the word on UTF-8 boundaries.
    std::string part;
    for (size_t i = 0; i < word.size();) {
      size_t n = 1;
      const auto c = static_cast<unsigned char>(word[i]);
      if (c >= 0xF0)
        n = 4;
      else if (c >= 0xE0)
        n = 3;
      else if (c >= 0xC0)
        n = 2;
      const std::string next = part + word.substr(i, n);
      if (!part.empty() && r.getTextWidth(font, next.c_str(), st) > w) {
        lines.push_back(part);
        part = word.substr(i, n);
      } else {
        part = next;
      }
      i += n;
    }
    line = part;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}
}  // namespace

// Whether every word of `ws` fits a line of width w (else lines would break inside words).
bool wordsFit(const GfxRenderer& r, const int font, const int w, const std::vector<std::string>& ws,
              const EpdFontFamily::Style st) {
  for (const auto& word : ws) {
    if (word != "\n" && r.getTextWidth(font, word.c_str(), st) > w) return false;
  }
  return true;
}

int wrap(const GfxRenderer& r, const int font, const int x, const int y, const int w, const char* text,
         const int maxLines, const bool black, const int style, const int lineGap) {
  if (!text || !*text || w <= 0) return 0;
  const auto st = static_cast<EpdFontFamily::Style>(style);
  const auto ws = words(text);
  int f = font;
  std::vector<std::string> lines = layoutLines(r, f, w, ws, st);
  // Too many lines, or a word wider than the line: a smaller font, down to the smallest (which then takes as
  // many lines as it needs).
  while ((static_cast<int>(lines.size()) > maxLines || !wordsFit(r, f, w, ws, st)) && smallerFont(f) != 0) {
    f = smallerFont(f);
    lines = layoutLines(r, f, w, ws, st);
  }
  const int lh = r.getLineHeight(f) + lineGap;
  int cy = y;
  for (const auto& l : lines) {
    r.drawText(f, x, cy, l.c_str(), black, st);
    cy += lh;
  }
  return cy - y;
}

int wrapInFont(const GfxRenderer& r, const int font, const int w, const int h, const char* text, const int style,
               int* usedH) {
  const auto st = static_cast<EpdFontFamily::Style>(style);
  const auto ws = words(text ? text : "");
  int f = font;
  while (true) {
    const int need = static_cast<int>(layoutLines(r, f, w, ws, st).size()) * r.getLineHeight(f);
    const int next = smallerFont(f);
    if ((need <= h && wordsFit(r, f, w, ws, st)) || next == 0) {
      if (usedH) *usedH = need;
      return f;
    }
    f = next;
  }
}

int wrapIn(const GfxRenderer& r, const int font, const int x, const int y, const int w, const int h, const char* text,
           const bool black, const int style) {
  if (!text || !*text || w <= 0) return 0;
  const auto st = static_cast<EpdFontFamily::Style>(style);
  const int f = wrapInFont(r, font, w, h, text, style);
  const int lh = r.getLineHeight(f);
  int cy = y;
  for (const auto& l : layoutLines(r, f, w, words(text), st)) {
    r.drawText(f, x, cy, l.c_str(), black, st);
    cy += lh;
  }
  return cy - y;
}

int wrapCentered(const GfxRenderer& r, const int font, const int x, const int y, const int w, const int h,
                 const char* text, const bool black, const int style) {
  if (!text || !*text || w <= 0) return 0;
  const auto st = static_cast<EpdFontFamily::Style>(style);
  int used = 0;
  const int f = wrapInFont(r, font, w, h, text, style, &used);
  const int lh = r.getLineHeight(f);
  int cy = y + std::max(0, (h - used) / 2);
  for (const auto& l : layoutLines(r, f, w, words(text), st)) {
    r.drawText(f, x + (w - r.getTextWidth(f, l.c_str(), st)) / 2, cy, l.c_str(), black, st);
    cy += lh;
  }
  return used;
}

int fitText(const GfxRenderer& r, const int font, const int x, const int y, const char* text, const int maxW,
            const bool black, const int style) {
  if (!text || !*text || maxW <= 0) return 0;
  const auto st = static_cast<EpdFontFamily::Style>(style);
  const int f = fitFont(r, font, text, maxW, style);
  const int tw = r.getTextWidth(f, text, st);
  if (tw <= maxW) {
    r.drawText(f, x, y + (r.getLineHeight(font) - r.getLineHeight(f)) / 2, text, black, st);
    return tw;
  }
  wrap(r, f, x, y, maxW, text, 4, black, style, 0);
  return maxW;
}

int note(const GfxRenderer& r, const int x, const int y, const int w, const char* text, const int maxLines) {
  const int pad = 14;
  // Measure by drawing into the same place; height is lines * line height.
  const int lh = r.getLineHeight(fontSmall()) + 2;
  int lines = 1;
  {
    const auto ws = words(text);
    std::string line;
    for (const auto& word : ws) {
      if (word == "\n") {
        lines++;
        line.clear();
        continue;
      }
      const std::string trial = line.empty() ? word : line + " " + word;
      if (!line.empty() && r.getTextWidth(fontSmall(), trial.c_str()) > w - pad * 2) {
        lines++;
        line = word;
      } else {
        line = trial;
      }
    }
  }
  if (lines > maxLines) lines = maxLines;
  const int h = lines * lh + pad * 2 - 2;
  dashedRect(r, x, y, w, h);
  wrap(r, fontSmall(), x + pad, y + pad, w - pad * 2, text, maxLines);
  return h;
}

void progressBar(const GfxRenderer& r, const int x, const int y, const int w, const int h, const int percent) {
  r.drawRoundedRect(x, y, w, h, 2, h / 2, true);
  const int p = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  const int fill = (w - 6) * p / 100;
  if (fill > 0) r.fillRoundedRect(x + 3, y + 3, fill, h - 6, (h - 6) / 2, Color::Black);
}

int tag(const GfxRenderer& r, const int x, const int y, const char* text, const bool inverted) {
  const int tw = r.getTextWidth(fontSmall(), text, EpdFontFamily::BOLD);
  const int w = tw + 16;
  const int h = r.getLineHeight(fontSmall()) + 4;
  if (inverted) {
    r.fillRoundedRect(x, y, w, h, 6, Color::Black);
  } else {
    r.drawRoundedRect(x, y, w, h, 2, 6, true);
  }
  r.drawText(fontSmall(), x + 8, y + 2, text, !inverted, EpdFontFamily::BOLD);
  return w;
}

void kicker(const GfxRenderer& r, const int x, const int y, const char* text, const bool black) {
  r.drawText(fontKicker(), x, y, text, black, EpdFontFamily::BOLD);
}

void check(const GfxRenderer& r, const int x, const int y, const int size) {
  r.drawLine(x, y + size / 2, x + size / 3, y + size - 2, 3, true);
  r.drawLine(x + size / 3, y + size - 2, x + size, y + 2, 3, true);
}

void hints(GfxRenderer& r, const MappedInputManager& in, const char* back, const char* ok, const char* prev,
           const char* next) {
  const auto labels = in.mapLabels(back, ok, prev, next);
  GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

namespace {
bool localTm(struct tm& out) { return localTime(out); }
}  // namespace

bool localTime(struct tm& out) {
  if (!EspOSStore::clockValid()) return false;
  const int64_t now = EspOSStore::nowMs();
  if (now <= 0) return false;
  const time_t t = static_cast<time_t>(now / 1000 + static_cast<int64_t>(ESPOS.tzMin) * 60);
  gmtime_r(&t, &out);
  return out.tm_year + 1900 >= 2024;
}

void settleMinute() {
  const int64_t now = EspOSStore::nowMs();
  if (now <= 0) return;
  // Time zones are whole minutes, so UTC and local minutes turn over together.
  const int64_t toNext = 60000 - now % 60000;
  if (toNext <= 2000) delay(static_cast<uint32_t>(toNext) + 20);
}

const char* weekdayCaps(const int wday) {
  static const StrId kCaps[7] = {StrId::STR_EO_SUN, StrId::STR_EO_MON, StrId::STR_EO_TUE, StrId::STR_EO_WED,
                                 StrId::STR_EO_THU, StrId::STR_EO_FRI, StrId::STR_EO_SAT};
  return I18N.get(kCaps[wday >= 0 && wday < 7 ? wday : 0]);
}

std::string clockText() {
  struct tm tm{};
  if (!localTm(tm)) return {};
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return buf;
}

bool dateParts(std::string& weekdayCaps, std::string& date, std::string& weekdayShort) {
  struct tm tm{};
  if (!localTm(tm)) return false;
  weekdayCaps = espos::ui::weekdayCaps(tm.tm_wday);
  weekdayShort = weekdayCaps;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
  date = buf;
  return true;
}

}  // namespace espos::ui
