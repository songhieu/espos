#pragma once
#include <lvgl.h>

// Ubuntu (the font of the EspOS web previews), pre-rendered with the Latin-1 letters; other letters (Vietnamese, CJK,
// Cyrillic) come from a language's font package (useLang).
// Text sizes: regular 12-16, 18, 20; bold 12-18, 20, 22, 24. Digit-only display sizes
// (letters come from the largest text size): bold 28, 34, 44, 56, 64, 96; regular 40, 64, 84, 96.
namespace fonts {
void begin();
const lv_font_t* get(int px, bool bold = false);   // nearest pre-rendered size of that weight
const lv_font_t* text(int px, bool bold = false);  // nearest size that has every letter
// The letters of a language the built-in fonts lack (Chinese, Japanese, Korean, Cyrillic) come from its font package
// (i18n.cpp): every text size falls back to the package's nearest size. nullptr: the built-in fonts only. false when
// the package has no readable font.
bool useLang(const char* fontPkg);
// A font file of that layout (r<px>.bin / b<px>.bin, tools/i18n.py esf) from an app's package, e.g. PawBoop's
// bubble fonts (Lexend). The bytes must stay (memory-mapped or a PSRAM copy, 4-byte aligned). Letters it lacks
// come from the built-in text size `px` (and the language's font). nullptr when the file is not one.
const lv_font_t* fromFile(const uint8_t* b, uint32_t len, int px, bool bold);
}  // namespace fonts
