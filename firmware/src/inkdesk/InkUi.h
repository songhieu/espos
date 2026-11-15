#pragma once

#include <cstdint>
#include <ctime>
#include <string>

class GfxRenderer;
class MappedInputManager;

// Shared drawing kit for the InkDesk screens, so they all look like the approved mockup
// (mockups/x4-ui-mockup.html): a 64 px header with a 2 px rule, rounded rows that invert
// when selected, dashed note boxes, and the theme's four button hints at the bottom.
namespace inkdesk::ui {

constexpr int SIDE = 20;       // left/right content inset
constexpr int HEADER_H = 64;   // header band height (rule drawn at its bottom)
constexpr int ROW_RADIUS = 10;

// Font roles (all built in; see InkFonts.h for the big ones).
int fontTitle();    // header titles
int fontBody();     // row titles, body text
int fontSmall();    // subtitles, captions
int fontKicker();   // small bold labels ("ĐANG ĐỌC")

void header(const GfxRenderer& r, const char* title, const char* right = nullptr);
// Battery icon + percentage ending at `rightX`, vertically centred on `centerY`. Returns its left x.
int battery(const GfxRenderer& r, int rightX, int centerY);

// One list row. Returns the row height actually used (grows to fit a subtitle).
int row(const GfxRenderer& r, int x, int y, int w, const char* title, const char* subtitle, const char* right,
        bool selected, const uint8_t* icon32 = nullptr);
int rowHeight(const GfxRenderer& r, bool withSubtitle);

void dashedRect(const GfxRenderer& r, int x, int y, int w, int h, int radius = 10);
// Dashed box with wrapped text inside. Returns the box height.
int note(const GfxRenderer& r, int x, int y, int w, const char* text, int maxLines = 3);
void progressBar(const GfxRenderer& r, int x, int y, int w, int h, int percent);
// Small outlined label; returns its width.
int tag(const GfxRenderer& r, int x, int y, const char* text, bool inverted = false);
void kicker(const GfxRenderer& r, int x, int y, const char* text, bool black = true);
void check(const GfxRenderer& r, int x, int y, int size);  // check mark

// The next smaller text font (serif 36 → bold 24 → Noto Sans 18 … 12 → Ubuntu 10 → 8), 0 after the last.
int smallerFont(int font);
// `font`, or the first smaller one, in which `text` fits `maxW` on one line (the smallest when none does).
int fitFont(const GfxRenderer& r, int font, const char* text, int maxW, int style = 0);
// Draws `text` whole within `maxW`: the font shrinks when needed (kept centred on the original line;
// wrapped onto more lines when even the smallest is too wide). Never cuts it. Returns the width drawn.
int fitText(const GfxRenderer& r, int font, int x, int y, const char* text, int maxW, bool black = true, int style = 0);

// Word-wraps `text` into at most `maxLines` lines of width `w`; when it needs more, the font shrinks
// (InkDesk shows text whole, never with an ellipsis). Returns the height used.
int wrap(const GfxRenderer& r, int font, int x, int y, int w, const char* text, int maxLines, bool black = true,
         int style = 0, int lineGap = 2);
// Word-wraps `text` into the box w x h in `font` or the first smaller one whose lines fit the box
// (the smallest when none does). Returns the height used.
int wrapIn(const GfxRenderer& r, int font, int x, int y, int w, int h, const char* text, bool black = true,
           int style = 0);
// The font wrapIn would pick and the height its lines take.
int wrapInFont(const GfxRenderer& r, int font, int w, int h, const char* text, int style, int* usedH = nullptr);
// wrapIn with every line centred, and the lines centred in the box. Returns the height used.
int wrapCentered(const GfxRenderer& r, int font, int x, int y, int w, int h, const char* text, bool black = true,
                 int style = 0);

// Theme button hints (respects button remapping).
void hints(GfxRenderer& r, const MappedInputManager& in, const char* back, const char* ok, const char* prev,
           const char* next);

// Local time from the InkDesk clock; false when the time is unknown.
bool localTime(struct tm& out);
// A timer wake can land just before the minute it was set for: waits (at most 2 s) for that
// minute, so a clock drawn next shows it rather than the minute that is ending.
void settleMinute();
// "THỨ BẢY" for tm_wday 6.
const char* weekdayCaps(int wday);

// "10:47" from the InkDesk clock, or "" when the time is unknown.
std::string clockText();
// Vietnamese weekday + date for the InkDesk clock: {"THỨ BẢY", "14.11.2026"}; empty when unknown.
bool dateParts(std::string& weekdayCaps, std::string& date, std::string& weekdayShort);

}  // namespace inkdesk::ui
