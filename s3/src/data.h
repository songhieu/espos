#pragma once
// Reading the synced dashboard for widgets and apps: weather, calendar, news, markets, plus the
// Vietnamese calendar bits (lunar dates, can chi, holidays) and number formatting.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include <vector>

#include "i18n.h"

namespace data {

// ---------------------------------------------------------------- time
struct Day {
  int d, m, y, wday;  // wday 0 = Sunday
};
Day today();
Day addDays(const Day& d, int n);
int nowMinutes();      // minutes since midnight
bool night();          // 18:00..06:00

// ---------------------------------------------------------------- weather
JsonObjectConst weather();
bool hasWeather();
// Icon for a weather name (sun, cloudsun, cloud, rain, storm, fog, snow, moon, cloudmoon) at
// 20, 28, 40, 56 or 80 px (nearest). `atNight` swaps sun for moon.
const lv_image_dsc_t* wxIcon(const char* name, int px, bool atNight = false);
bool hourIsNight(const char* hLabel);  // "21h" -> true
// A forecast day's name in the device's language from its "date" ("15/11"): "Today" (short: the compact form) or the
// short weekday; the server's own "d" when there is no date.
String dayName(JsonObjectConst day, bool shortToday = false);
// Sky colours for a weather card (top, bottom) by the conditions and time of day.
void sky(const char* icon, bool atNight, uint32_t& top, uint32_t& bottom);
String cityShort(const char* city);  // "TP Hồ Chí Minh" -> "TP HCM"

// ---------------------------------------------------------------- calendar
struct Event {
  String date;  // YYYY-MM-DD ("" = today, from the old `events` block)
  String time;  // HH:MM, "" = all day
  String title;
};
// Events from today on (the 7-day `agenda` when the server sends it, else today's `events`).
std::vector<Event> agenda();
std::vector<Event> eventsOn(const Day& d);
String isoDate(const Day& d);
String dayLabel(const Day& d);  // "Today", "Tomorrow", "Tuesday, Nov 18"

// ---------------------------------------------------------------- lunar
struct Lunar {
  int d, m, y;
  bool leap;
};
Lunar lunar(const Day& d);
String canChiDay(const Day& d);
String canChiMonth(const Lunar& l);
String canChiYear(int lunarYear);
String canChi(int stem, int branch);  // "Fire Horse" (vi "Bính Ngọ")
String lunarText(const Lunar& l);     // "7/10" or "Full moon, month 10"
String holiday(const Day& d, const Lunar& l);  // "Lunar New Year", "National Day"... or "" (translated)
int daysToTet(const Day& d);
// The same maths as web/app/src/lunar.ts (checked against each other date by date).
extern const i18n::List CAN;  // 10 stems: CAN[i] (translated)
extern const i18n::List CHI;  // 12 branches
int julian(const Day& d);                 // Julian day number
Day fromJulian(int jd);
Day lunarToSolar(const Lunar& l, bool& ok);  // the solar day of a lunar date (ok false: no such date)
int lunarMonthDays(const Lunar& l);       // 29 or 30: the length of l's lunar month
String canChiHour(const Day& d, int chi);  // hour `chi` (0 Tý .. 11 Hợi) of day d: "Giáp Tý"
int hourChi(int hour);                    // 23h-1h -> 0 (Tý), 1h-3h -> 1 (Sửu) ...
String hourRange(int chi);                // "23-1", "1-3" ...
uint16_t goodHours(const Day& d);         // giờ hoàng đạo: bit i set = hour chi i is good (six of them)
int solarTerm(const Day& d);              // tiết khí of the day: 0 Xuân phân .. 23 Kinh trập
extern const i18n::List SOLAR_TERM;  // 24 names
Day nextSolarTerm(const Day& d, int& term);  // the first day of the next tiết khí (and which)
struct Holiday {
  Day day;
  String name;
  int days;  // from today (0 = today)
};
Holiday nextHoliday(const Day& d);        // the next Vietnamese holiday, solar or lunar (today counts)
// A saying a day: English built in, the language pack's own ("@quotes"; Vietnamese = lunar.ts's list and pick).
extern const i18n::List QUOTES;
const char* quoteOfDay(const Day& d);

// ---------------------------------------------------------------- news
JsonArrayConst newsItems();  // [{title, lead, id, sec, pub_ms, img, thumb}] or the old `top` strings
String newsTitle(JsonVariantConst v);

// ---------------------------------------------------------------- markets
struct Quote {
  String code, name, value, change, unit;
  uint32_t color;  // of the change
  double pct;
};
// kind: gold | stocks | fx | crypto; codes empty = all the server sent.
std::vector<Quote> quotes(const char* kind, const String& codes = "");
std::vector<String> codesOf(const String& option, const char* def);

// ---------------------------------------------------------------- numbers
String num(double v, int decimals);                          // 1,234.5 (the language's separators)
String signedNum(double v, int decimals, const char* suffix = "");
uint32_t trendColor(double v, bool onDark = true);

// ---------------------------------------------------------------- JSON settings
// Parses a JSON setting into doc (PSRAM); false when empty or bad.
bool loadJson(const char* key, JsonDocument& doc);
void saveJson(const char* key, const JsonDocument& doc);
String newId();  // short unique id for notes, widgets

}  // namespace data
