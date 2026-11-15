#pragma once

#include <GfxRenderer.h>

#include <string>

#include "fontIds.h"
#include "inkdesk/InkFonts.h"

// Drawing helpers shared by the sleep-screen layouts (DashboardView) and the widgets.
namespace inkdesk::draw {

constexpr int BODY_FONT = NOTOSANS_14_FONT_ID;  // 29 px
constexpr int LIST_FONT = NOTOSANS_12_FONT_ID;  // 25 px: list rows
constexpr int SMALL_FONT = NOTOSANS_12_FONT_ID;
constexpr int CAPS_FONT = UI_10_FONT_ID;  // 21 px Ubuntu: labels, footer
constexpr int CAPS_BIG_FONT = UI_12_FONT_ID;
constexpr auto kBold = EpdFontFamily::BOLD;
constexpr auto kReg = EpdFontFamily::REGULAR;

void fillCircle(const GfxRenderer& r, int cx, int cy, int rad, Color color);
void strokeCircle(const GfxRenderer& r, int cx, int cy, int rad, int lw);
// Weather icon by name (sun, cloudsun, cloud, rain, storm, snow, fog, moon), centred, `s` px.
void drawIcon(const GfxRenderer& r, const char* icon, int cx, int cy, int s);
void drawRight(const GfxRenderer& r, int font, int rightX, int y, const char* text,
               EpdFontFamily::Style style = kReg, bool black = true);
// Draws `text` whole within `maxW` (smaller font, or wrapped, when it does not fit; never "…").
// Returns the drawn width.
int drawFit(const GfxRenderer& r, int font, int x, int y, const char* text, int maxW, EpdFontFamily::Style style = kReg,
            bool black = true);
void drawFitCentered(const GfxRenderer& r, int font, int cx, int y, const char* text, int maxW,
                     EpdFontFamily::Style style = kReg, bool black = true);
void drawFitRight(const GfxRenderer& r, int font, int rightX, int y, const char* text, int maxW,
                  EpdFontFamily::Style style = kReg, bool black = true);
void drawCentered(const GfxRenderer& r, int font, int cx, int y, const char* text, EpdFontFamily::Style style = kReg,
                  bool black = true);
// The hero fonts carry only digits; their glyph boxes start well below the line top.
int digitTop(int font);
int digitHeight(int font);
// Draws hero digits with their tops at `top`.
void drawDigits(const GfxRenderer& r, int font, int x, int top, const char* text, bool black = true);
// Black label with white caps text; returns its width.
int invTag(const GfxRenderer& r, int x, int y, const char* text, int h = 26);
// Rounded progress bar, black on white or white on black.
void bar(const GfxRenderer& r, int x, int y, int w, int h, int pct, bool black);
// "08:30" -> 510; `fallback` when it does not parse.
int minutesOfDay(const char* hhmm, int fallback);

}  // namespace inkdesk::draw
