#include "DashDraw.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "espos/EspOSUi.h"

// Drawing helpers shared by the sleep-screen layouts and the widgets.
namespace espos::draw {

void fillCircle(const GfxRenderer& r, const int cx, const int cy, const int rad, const Color color) {
  if (rad <= 0) return;
  r.fillRoundedRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, color);
}

void strokeCircle(const GfxRenderer& r, const int cx, const int cy, const int rad, const int lw) {
  fillCircle(r, cx, cy, rad, Color::Black);
  fillCircle(r, cx, cy, rad - lw, Color::White);
}

// Cloud outline as the union of a rounded base and two bumps: draw the shape
// in black, then the same shape inset in white.
static void cloudShape(const GfxRenderer& r, const int cx, const int cy, const int s, const int inset,
                       const Color color) {
  const int baseW = s * 80 / 100 - inset * 2;
  const int baseH = s * 34 / 100 - inset * 2;
  const int baseX = cx - s * 40 / 100 + inset;
  const int baseY = cy + inset;
  const int radius = baseH / 2 > 0 ? baseH / 2 : 1;
  if (baseW > 0 && baseH > 0) r.fillRoundedRect(baseX, baseY, baseW, baseH, radius, color);
  fillCircle(r, cx - s * 10 / 100, cy + s * 2 / 100, s * 22 / 100 - inset, color);
  fillCircle(r, cx + s * 16 / 100, cy + s * 8 / 100, s * 15 / 100 - inset, color);
}

static void cloud(const GfxRenderer& r, const int cx, const int cy, const int s) {
  const int lw = s >= 40 ? 3 : 2;
  cloudShape(r, cx, cy, s, 0, Color::Black);
  cloudShape(r, cx, cy, s, lw, Color::White);
}

static void sun(const GfxRenderer& r, const int cx, const int cy, const int s) {
  const int rad = s * 20 / 100;
  const int lw = s >= 40 ? 3 : 2;
  strokeCircle(r, cx, cy, rad, lw);
  static const int dx[8] = {0, 7, 10, 7, 0, -7, -10, -7};
  static const int dy[8] = {-10, -7, 0, 7, 10, 7, 0, -7};
  for (int i = 0; i < 8; i++) {
    const int x1 = cx + dx[i] * rad * 14 / 100;
    const int y1 = cy + dy[i] * rad * 14 / 100;
    const int x2 = cx + dx[i] * rad * 20 / 100;
    const int y2 = cy + dy[i] * rad * 20 / 100;
    r.drawLine(x1, y1, x2, y2, lw, true);
  }
}

void drawIcon(const GfxRenderer& r, const char* icon, const int cx, const int cy, const int s) {
  if (!icon) icon = "cloud";
  const int lw = s >= 40 ? 3 : 2;
  if (strcmp(icon, "sun") == 0) {
    sun(r, cx, cy, s);
  } else if (strcmp(icon, "cloudsun") == 0) {
    sun(r, cx - s * 18 / 100, cy - s * 16 / 100, s * 70 / 100);
    cloud(r, cx + s * 6 / 100, cy - s * 4 / 100, s * 85 / 100);
  } else if (strcmp(icon, "moon") == 0) {
    const int rad = s * 28 / 100;
    fillCircle(r, cx, cy, rad, Color::Black);
    fillCircle(r, cx + rad * 55 / 100, cy - rad * 35 / 100, rad, Color::White);
  } else if (strcmp(icon, "fog") == 0) {
    for (int i = -1; i <= 1; i++)
      r.drawLine(cx - s * 35 / 100, cy + i * s / 6, cx + s * 35 / 100, cy + i * s / 6, lw, true);
  } else {
    // cloud, rain, storm, snow: a cloud with something under it.
    const bool precip = strcmp(icon, "rain") == 0 || strcmp(icon, "storm") == 0 || strcmp(icon, "snow") == 0;
    const int cloudY = precip ? cy - s * 18 / 100 : cy - s * 8 / 100;
    cloud(r, cx, cloudY, s);
    const int top = cloudY + s * 34 / 100 + s / 14;
    if (strcmp(icon, "rain") == 0) {
      for (int i = -1; i <= 1; i++) {
        const int x = cx + i * s / 5;
        r.drawLine(x, top, x - s / 12, top + s / 6, lw, true);
      }
    } else if (strcmp(icon, "storm") == 0) {
      const int x = cx;
      r.drawLine(x + s / 12, top, x - s / 20, top + s / 10, lw, true);
      r.drawLine(x - s / 20, top + s / 10, x + s / 16, top + s / 10, lw, true);
      r.drawLine(x + s / 16, top + s / 10, x - s / 12, top + s / 4, lw, true);
    } else if (strcmp(icon, "snow") == 0) {
      for (int i = -1; i <= 1; i++) fillCircle(r, cx + i * s / 5, top + s / 12, lw + 1, Color::Black);
    }
  }
}

void drawRight(const GfxRenderer& r, const int font, const int rightX, const int y, const char* text,
               const EpdFontFamily::Style style, const bool black) {
  if (!text || !*text) return;
  r.drawText(font, rightX - r.getTextWidth(font, text, style), y, text, black, style);
}

int drawFit(const GfxRenderer& r, const int font, const int x, const int y, const char* text, const int maxW,
            const EpdFontFamily::Style style, const bool black) {
  return espos::ui::fitText(r, font, x, y, text, maxW, black, style);
}

void drawFitCentered(const GfxRenderer& r, const int font, const int cx, const int y, const char* text, const int maxW,
                     const EpdFontFamily::Style style, const bool black) {
  if (!text || !*text) return;
  const int f = espos::ui::fitFont(r, font, text, maxW, style);
  const int tw = std::min(r.getTextWidth(f, text, style), maxW);
  espos::ui::fitText(r, font, cx - tw / 2, y, text, maxW, black, style);
}

void drawFitRight(const GfxRenderer& r, const int font, const int rightX, const int y, const char* text, const int maxW,
                  const EpdFontFamily::Style style, const bool black) {
  if (!text || !*text) return;
  const int f = espos::ui::fitFont(r, font, text, maxW, style);
  const int tw = std::min(r.getTextWidth(f, text, style), maxW);
  espos::ui::fitText(r, font, rightX - tw, y, text, maxW, black, style);
}

void drawCentered(const GfxRenderer& r, const int font, const int cx, const int y, const char* text,
                  const EpdFontFamily::Style style, const bool black) {
  r.drawText(font, cx - r.getTextWidth(font, text, style) / 2, y, text, black, style);
}

// The hero fonts carry only digits; their glyph boxes start well below the line top.
// Offset from drawText's y to the top of a digit (ascender minus digit height).
int digitTop(const int font) {
  switch (font) {
    case ESPOS_60_FONT_ID:
      return 134 - 90;
    case ESPOS_44_FONT_ID:
      return 98 - 66;
    case ESPOS_30_FONT_ID:
      return 67 - 45;
    default:
      return 0;
  }
}
int digitHeight(const int font) {
  switch (font) {
    case ESPOS_60_FONT_ID:
      return 90;
    case ESPOS_44_FONT_ID:
      return 66;
    case ESPOS_30_FONT_ID:
      return 45;
    default:
      return 0;
  }
}
// Draws hero digits with their tops at `top`.
void drawDigits(const GfxRenderer& r, const int font, const int x, const int top, const char* text, const bool black) {
  r.drawText(font, x, top - digitTop(font), text, black, kBold);
}

// Black label with white caps text; returns its width.
int invTag(const GfxRenderer& r, const int x, const int y, const char* text, const int h) {
  const int w = r.getTextWidth(CAPS_FONT, text, kBold) + 14;
  r.fillRoundedRect(x, y, w, h, 4, Color::Black);
  r.drawText(CAPS_FONT, x + 7, y + (h - r.getLineHeight(CAPS_FONT)) / 2 + 1, text, false, kBold);
  return w;
}

// Rounded progress bar, black on white or white on black.
void bar(const GfxRenderer& r, const int x, const int y, const int w, const int h, int pct, const bool black) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  r.drawRoundedRect(x, y, w, h, 2, h / 2, black);
  const int fill = (w - 4) * pct / 100;
  if (fill > 0) r.fillRoundedRect(x + 2, y + 2, fill, h - 4, (h - 4) / 2, black ? Color::Black : Color::White);
}

int minutesOfDay(const char* hhmm, const int fallback) {
  if (!hhmm || strlen(hhmm) < 4 || !isdigit(static_cast<unsigned char>(hhmm[0]))) return fallback;
  const int h = atoi(hhmm);
  const char* colon = strchr(hhmm, ':');
  const int m = colon ? atoi(colon + 1) : 0;
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

}  // namespace espos::draw
