// Made by tools/bot/gen.py from bot-art's art2 (tools/bot/art2): do not edit.
#pragma once
#include <stdint.h>

namespace art {

constexpr int FRAMES = 1939;
extern const uint8_t FRAME_DATA[];
extern const uint32_t FRAME_OFF[];  // FRAMES + 1
struct Step { uint16_t frame, ms; };
struct Scene { const Step* steps; uint16_t n; };
enum SceneId : uint8_t { SC_SLEEP, SC_LISTEN, SC_THINK, SC_HAPPY, SC_SAD, SC_IDLE, SC_TALK, SC_TALK_HAPPY, SC_TALK_SAD, SC_COUNT };
constexpr int LOOK_COUNT = 3;
extern const Scene SCENES[LOOK_COUNT][SC_COUNT];
struct Box { int8_t x0, y0, x1, y1; };
extern const Box TALK_BOX[LOOK_COUNT];  // where the reply goes (inclusive)
constexpr int TALK_LINE_H = 13;
struct DigitSet { const char* chars; const uint8_t* const* glyphs; uint8_t gap, h; };  // UTF-8 chars
enum DigitSetId : uint8_t { DS_BOLD40, DS_BOLD24, DS_BOLD16, DS_THIN40, DS_THIN20, DS_SEG40, DS_DOTS, DS_FLIP30, DS_SEG16 };
extern const DigitSet DIGIT_SETS[];
struct Glyph { uint16_t cp; uint8_t adv, w; int8_t xo; uint16_t off; };
constexpr int F11_N = 237, F11_H = 17, F11_TOP = 3;
extern const uint8_t F11_BITS[];
extern const Glyph F11_G[];
constexpr int F16B_N = 95, F16B_H = 19;
extern const uint8_t F16B_BITS[];
extern const Glyph F16B_G[];
constexpr int T5_N = 237, T5_H = 16, T5_TOP = 4;
extern const uint8_t T5_BITS[];
extern const Glyph T5_G[];
enum FieldId : uint8_t { FD_TIME, FD_SS, FD_DATE_MID, FD_TEMP, FD_DATE_LINE, FD_HH, FD_MM, FD_WEEKDAY_SHORT, FD_HANDS, FD_WEEKDAY, FD_DAY, FD_MONTH_SHORT, FD_LUNAR_SHORT, FD_MONTH_LINE, FD_LUNAR_LABEL, FD_LUNAR_DAY, FD_MONTH, FD_WEATHER, FD_CITY_HILO, FD_TEMP_RAIN, FD_RAIN_S, FD_CITY_HUM, FD_TIME_N, FD_TEMP_NIGHT, FD_HEADLINE, FD_TIME_M, FD_GREET, FD_GOLD_SELL, FD_GOLD_BUY_LINE, FD_USD_LINE, FD_GOLD_UNIT_S, FD_QUOTE, FD_TET_NAME, FD_TET_DAYS, FD_COUNT };
enum LiveKind : uint8_t { LK_DIGITS, LK_TEXT, LK_HANDS, LK_PROGRESS, LK_MONTH };
// digits: ref = digit set, x0 y0 = x y; text: ref 0 F11, 1 T5, box x0..y1; hands: p = cx cy h m s h_w*10 m_w*10;
// progress: x0 x1 at y0; month: box, p = cell_w cell_h first_row_y. align 0 l, 1 m, 2 r.
struct Live { uint8_t kind, field, ref; int8_t x0, y0, x1, y1; uint8_t align, color, upper, wrap, blink; int16_t p[7]; };
extern const Live LIVE[];
struct Standby { const char* id; const char* name; Scene scene; bool drift; int16_t ovClear, ovSet; uint8_t live0, live1; };
constexpr int STANDBY_COUNT = 21;
extern const Standby STANDBY[STANDBY_COUNT];
extern const char* const QUOTES_VI[]; constexpr int QUOTES_VI_N = 120;
extern const char* const QUOTES_EN[]; constexpr int QUOTES_EN_N = 46;

}  // namespace art
