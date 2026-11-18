#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "data.h"

#include <cmath>
#include <ctime>

#include "art.h"
#include "desk.h"
#include "lunar.h"
#include "uikit.h"

namespace data {

using namespace kit;

// ---------------------------------------------------------------- time
Day today() {
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  return {l.tm_mday, l.tm_mon + 1, l.tm_year + 1900, l.tm_wday};
}

Day addDays(const Day& d, int n) {
  tm t = {};
  t.tm_mday = d.d + n;
  t.tm_mon = d.m - 1;
  t.tm_year = d.y - 1900;
  t.tm_hour = 12;
  mktime(&t);
  return {t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_wday};
}

int nowMinutes() {
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  return l.tm_hour * 60 + l.tm_min;
}

bool night() {
  const int m = nowMinutes();
  return m >= 18 * 60 || m < 6 * 60;
}

// ---------------------------------------------------------------- weather
JsonObjectConst weather() { return desk::dash()["weather"]; }
bool hasWeather() { return !weather().isNull() && !weather()["temp"].isNull(); }

const lv_image_dsc_t* wxIcon(const char* name, int px, bool atNight) {
  // Image numbers (art.h): the 20, 28, 40, 56 and 80 px pictures of a kind follow each other.
  struct Set {
    const char* n;
    uint16_t first;
  };
  static const Set sets[] = {{"sun", ART_wx_sun_20},     {"cloudsun", ART_wx_cloudsun_20}, {"cloud", ART_wx_cloud_20},
                             {"rain", ART_wx_rain_20},   {"storm", ART_wx_storm_20},       {"fog", ART_wx_fog_20},
                             {"snow", ART_wx_snow_20},   {"moon", ART_wx_moon_20},         {"cloudmoon", ART_wx_cloudmoon_20}};
  static_assert(ART_wx_sun_80 - ART_wx_sun_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_cloudsun_80 - ART_wx_cloudsun_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_cloud_80 - ART_wx_cloud_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_rain_80 - ART_wx_rain_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_storm_80 - ART_wx_storm_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_fog_80 - ART_wx_fog_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_snow_80 - ART_wx_snow_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_moon_80 - ART_wx_moon_20 == 4, "make_art.py keeps a kind's sizes together");
  static_assert(ART_wx_cloudmoon_80 - ART_wx_cloudmoon_20 == 4, "make_art.py keeps a kind's sizes together");
  String n = name ? name : "cloud";
  if (atNight && n == "sun") n = "moon";
  if (atNight && n == "cloudsun") n = "cloudmoon";
  const Set* s = &sets[2];
  for (const Set& x : sets)
    if (n == x.n) s = &x;
  return art_dsc(s->first + (px <= 22 ? 0 : px <= 32 ? 1 : px <= 46 ? 2 : px <= 66 ? 3 : 4));
}

String dayName(JsonObjectConst day, bool shortToday) {
  int dd = 0, mm = 0;
  if (sscanf(day["date"] | "", "%d/%d", &dd, &mm) != 2 || dd < 1 || mm < 1 || mm > 12) return day["d"] | "";
  const Day t = today();
  int y = t.y;
  if (mm < t.m - 6) y++;  // January's days seen in December
  else if (mm > t.m + 6) y--;
  if (dd == t.d && mm == t.m) return shortToday ? TC_("short", "Today") : T_("Today");
  tm x = {};
  x.tm_mday = dd, x.tm_mon = mm - 1, x.tm_year = y - 1900, x.tm_hour = 12;
  mktime(&x);
  return WD_SHORT[x.tm_wday];
}

bool hourIsNight(const char* h) {
  const int v = atoi(h ? h : "12");
  return v >= 18 || v < 6;
}

void sky(const char* icon, bool atNight, uint32_t& top, uint32_t& bottom) {
  const String i = icon ? icon : "";
  if (i == "rain" || i == "storm") top = atNight ? 0x1f2937 : 0x3f4c63, bottom = atNight ? 0x374151 : 0x6b7a90;
  else if (i == "fog") top = 0x4b5563, bottom = 0x8a94a6;
  else if (atNight) top = 0x1e2a6b, bottom = 0x4338ca;
  else if (i == "cloud") top = 0x4f6fa8, bottom = 0x7f9cc6;
  else top = 0x2f7ff0, bottom = 0x5fb6ff;
}

String cityShort(const char* city) {
  String c = city ? city : "";
  if (c == "TP Hồ Chí Minh" || c == "Thành phố Hồ Chí Minh" || c == "Hồ Chí Minh") return "TP HCM";
  if (c.startsWith("Thành phố ")) return "TP " + c.substring(strlen("Thành phố "));
  return c;
}

// ---------------------------------------------------------------- calendar
String isoDate(const Day& d) {
  char b[12];
  snprintf(b, sizeof b, "%04d-%02d-%02d", d.y, d.m, d.d);
  return b;
}

std::vector<Event> agenda() {
  std::vector<Event> out;
  JsonObjectConst dash = desk::dash();
  const String td = isoDate(today());
  if (!dash["agenda"].isNull()) {
    for (JsonObjectConst e : dash["agenda"].as<JsonArrayConst>()) {
      Event ev{e["date"] | "", e["time"] | "", e["title"] | ""};
      if (ev.date.length() && ev.date < td) continue;
      out.push_back(ev);
    }
  } else {
    for (JsonObjectConst e : dash["events"].as<JsonArrayConst>()) out.push_back({td, e["time"] | "", e["title"] | ""});
  }
  return out;
}

std::vector<Event> eventsOn(const Day& d) {
  std::vector<Event> out;
  const String iso = isoDate(d);
  for (const Event& e : agenda())
    if (e.date == iso) out.push_back(e);
  return out;
}

String dayLabel(const Day& d) {
  const Day t = today();
  if (d.d == t.d && d.m == t.m && d.y == t.y) return T_("Today");
  const Day n = addDays(t, 1);
  if (d.d == n.d && d.m == n.m && d.y == n.y) return T_("Tomorrow");
  return i18n::date(N_("{W}, {M} {d}"), d.d, d.m, d.y, d.wday);
}

// ---------------------------------------------------------------- lunar
// The heavenly stems and earthly branches. English names the element and the animal ("Fire Horse"): the pair still
// tells the 60 apart (a stem's parity is its branch's); a pack gives its own (vi Giáp/Tý, zh 甲/子...).
namespace {
// i18n: the ten heavenly stems in order (Giáp Ất Bính Đinh Mậu Kỷ Canh Tân Nhâm Quý; 甲乙丙丁戊己庚辛壬癸)
const char* const CAN_EN[10] = {"Wood", "Wood", "Fire", "Fire", "Earth", "Earth", "Metal", "Metal", "Water", "Water"};
// i18n: the twelve earthly branches (Tý Sửu Dần Mão Thìn Tỵ Ngọ Mùi Thân Dậu Tuất Hợi; 子丑寅卯辰巳午未申酉戌亥)
const char* const CHI_EN[12] = {"Rat", "Ox", "Tiger", "Rabbit", "Dragon", "Snake", "Horse", "Goat", "Monkey", "Rooster",
                                "Dog", "Pig"};
// i18n: the 24 solar terms starting from the spring equinox (Xuân phân, Thanh minh...; 春分 清明...)
const char* const TERM_EN[24] = {"Spring Equinox", "Clear and Bright", "Grain Rain", "Start of Summer", "Grain Buds",
                                 "Grain in Ear", "Summer Solstice", "Minor Heat", "Major Heat", "Start of Autumn",
                                 "End of Heat", "White Dew", "Autumn Equinox", "Cold Dew", "Frost Descent",
                                 "Start of Winter", "Minor Snow", "Major Snow", "Winter Solstice", "Minor Cold",
                                 "Major Cold", "Start of Spring", "Rain Water", "Insects Awaken"};
}  // namespace
const i18n::List CAN = {"@stems", CAN_EN, 10};
const i18n::List CHI = {"@branches", CHI_EN, 12};
const i18n::List SOLAR_TERM = {"@solar_terms", TERM_EN, 24};

String canChi(int stem, int branch) {
  // i18n: a stem and a branch: "Fire Horse" (vi "Bính Ngọ", zh "丙午": no space)
  return i18n::fmt(TC_("canchi", "{0} {1}"), CAN[stem % 10], CHI[branch % 12]);
}

namespace {
// The sun's ecliptic longitude in radians at Julian day jdn (lunar.cpp's formula; lunar.ts has the same).
double sunLong(double jdn) {
  const double PI_ = 3.14159265358979323846;
  const double T = (jdn - 2451545.0) / 36525, T2 = T * T, dr = PI_ / 180;
  const double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
  const double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
  double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * sin(dr * M);
  DL = DL + (0.019993 - 0.000101 * T) * sin(dr * 2 * M) + 0.000290 * sin(dr * 3 * M);
  double L = (L0 + DL) * dr;
  L = L - PI_ * 2 * floor(L / (PI_ * 2));
  return L;
}
// Giờ hoàng đạo by the day's chi (Tý and Ngọ days share a pattern, Sửu and Mùi...): hour chi 0..11.
const char* const GOOD_HOURS[6] = {"110100101100", "001101001011", "110011010010",
                                   "101100110100", "001011001101", "010010110011"};
struct SolarHoliday {
  uint8_t d, m;
  const char* name;
};
// Vietnam's holidays (the names are translated where they are shown).
const SolarHoliday SOLAR_DAYS[] = {{1, 1, N_("New Year's Day")},
                                   {14, 2, N_("Valentine's Day")},
                                   {8, 3, N_("Women's Day")},
                                   {30, 4, N_("Reunification Day")},  // i18n: Vietnam, 30 April
                                   {1, 5, N_("Labour Day")},
                                   {1, 6, N_("Children's Day")},
                                   {28, 6, N_("Vietnamese Family Day")},
                                   {27, 7, N_("War Martyrs' Day")},    // i18n: Vietnam, 27 July (Thương binh Liệt sĩ)
                                   {2, 9, N_("National Day")},         // i18n: Vietnam, 2 September
                                   {20, 10, N_("Vietnamese Women's Day")},
                                   {20, 11, N_("Teachers' Day")},       // i18n: Vietnam, 20 November
                                   {22, 12, N_("Army Day")},            // i18n: Vietnam People's Army, 22 December
                                   {24, 12, N_("Christmas Eve")}};
const SolarHoliday LUNAR_DAYS[] = {{1, 1, N_("Lunar New Year")},          // i18n: Tết Nguyên Đán, 春节
                                   {15, 1, N_("Lantern Festival")},        // i18n: first full moon (Rằm tháng Giêng)
                                   {3, 3, N_("Cold Food Festival")},       // i18n: Tết Hàn thực
                                   {10, 3, N_("Hung Kings' Day")},         // i18n: Giỗ Tổ Hùng Vương
                                   {15, 4, N_("Buddha's Birthday")},
                                   {5, 5, N_("Dragon Boat Festival")},     // i18n: Tết Đoan Ngọ
                                   {15, 7, N_("Vu Lan Festival")},         // i18n: Lễ Vu Lan, Ghost Festival
                                   {15, 8, N_("Mid-Autumn Festival")},     // i18n: Tết Trung Thu
                                   {23, 12, N_("Kitchen Gods' Day")}};     // i18n: Ông Công Ông Táo
}  // namespace

int julian(const Day& d) { return espos::lunar::julianDay(d.d, d.m, d.y); }

Day fromJulian(int jd) {
  // Gregorian from a Julian day number (the same steps as lunar.cpp's jdToDate).
  const int a = jd + 32044, b = (4 * a + 3) / 146097, c = a - (b * 146097) / 4;
  const int dd = (4 * c + 3) / 1461, e = c - (1461 * dd) / 4, m = (5 * e + 2) / 153;
  Day out;
  out.d = e - (153 * m + 2) / 5 + 1;
  out.m = m + 3 - 12 * (m / 10);
  out.y = b * 100 + dd - 4800 + m / 10;
  out.wday = (jd + 1) % 7;
  return out;
}

Day lunarToSolar(const Lunar& l, bool& ok) {
  espos::lunar::Date x;
  x.day = l.d, x.month = l.m, x.year = l.y, x.leap = l.leap;
  int dd = 0, mm = 0, yy = 0;
  ok = espos::lunar::toSolar(x, dd, mm, yy);
  return ok ? fromJulian(espos::lunar::julianDay(dd, mm, yy)) : Day{0, 0, 0, 0};
}

int lunarMonthDays(const Lunar& l) {
  bool ok;
  const Day first = lunarToSolar({1, l.m, l.y, l.leap}, ok);
  if (!ok) return 29;
  return lunar(fromJulian(julian(first) + 29)).d == 30 ? 30 : 29;
}

String canChiHour(const Day& d, int chi) {
  const int ty = (julian(d) - 1) * 2 % 10;  // the can of the day's Tý hour
  return canChi(ty + chi, chi);
}

int hourChi(int hour) { return ((hour + 1) / 2) % 12; }

String hourRange(int chi) { return String((chi * 2 + 23) % 24) + "-" + String((chi * 2 + 1) % 24); }

uint16_t goodHours(const Day& d) {
  const char* p = GOOD_HOURS[(julian(d) + 1) % 12 % 6];
  uint16_t bits = 0;
  for (int i = 0; i < 12; i++)
    if (p[i] == '1') bits |= 1 << i;
  return bits;
}

int solarTerm(const Day& d) {
  // Ho Ngoc Duc's: the term the sun is in at the end of the day (Vietnam, UTC+7).
  const double PI_ = 3.14159265358979323846;
  return (int)floor(sunLong(julian(d) + 1 - 0.5 - 7.0 / 24) / PI_ * 12) % 24;
}

Day nextSolarTerm(const Day& d, int& term) {
  const int now = solarTerm(d), jd = julian(d);
  for (int k = 1; k <= 20; k++) {
    const Day x = fromJulian(jd + k);
    if ((term = solarTerm(x)) != now) return x;
  }
  term = (now + 1) % 24;
  return fromJulian(jd + 15);
}

Lunar lunar(const Day& d) {
  const auto l = espos::lunar::fromSolar(d.d, d.m, d.y);
  return {l.day, l.month, l.year, l.leap};
}

String canChiDay(const Day& d) {
  const int jd = espos::lunar::julianDay(d.d, d.m, d.y);
  return canChi(jd + 9, jd + 1);
}

String canChiMonth(const Lunar& l) { return canChi(l.y * 12 + l.m + 3, l.m + 1); }

String canChiYear(int y) { return canChi(y + 6, y + 8); }  // lunar.cpp's yearName

String lunarText(const Lunar& l) {
  // i18n: the 15th of a lunar month (vi "Rằm tháng {0}")
  if (l.d == 15) return i18n::fmt(N_("Full moon, month {0}"), String(l.m));
  // i18n: the 1st of a lunar month (vi "Mùng 1 tháng {0}")
  if (l.d == 1) return i18n::fmt(N_("New moon, month {0}"), String(l.m));
  // i18n: a lunar day/month; {2} is " leap" for a leap month (vi "{0}/{1}{2}")
  return i18n::fmt(N_("{0}/{1}{2}"), String(l.d), String(l.m), l.leap ? T_(" leap") : "");
}

String holiday(const Day& d, const Lunar& l) {
  if (!l.leap) {
    if (l.m == 1 && l.d <= 3) return T_("Lunar New Year");
    for (const SolarHoliday& h : LUNAR_DAYS)
      if (l.m == h.m && l.d == h.d) return T_(h.name);
    if (l.m == 12 && l.d >= 29 && lunar(fromJulian(julian(d) + 1)).d == 1) return T_("Lunar New Year's Eve");
  }
  for (const SolarHoliday& h : SOLAR_DAYS)
    if (d.m == h.m && d.d == h.d) return T_(h.name);
  return "";
}

Holiday nextHoliday(const Day& d) {
  const int now = julian(d);
  Holiday best;
  best.days = 100000;
  auto take = [&](const Day& x, const char* name) {
    const int n = julian(x) - now;
    if (n >= 0 && n < best.days) best.day = x, best.name = T_(name), best.days = n;
  };
  for (const SolarHoliday& h : SOLAR_DAYS)
    for (int y = d.y; y <= d.y + 1; y++) take(fromJulian(espos::lunar::julianDay(h.d, h.m, y)), h.name);
  const Lunar l = lunar(d);
  bool ok;
  for (int y = l.y; y <= l.y + 1; y++) {
    for (const SolarHoliday& h : LUNAR_DAYS) {
      const Day x = lunarToSolar({h.d, h.m, y, false}, ok);
      if (ok) take(x, h.name);
    }
    const Day tet = lunarToSolar({1, 1, y, false}, ok);
    if (ok) take(fromJulian(julian(tet) - 1), N_("Lunar New Year's Eve"));
  }
  return best;
}

int daysToTet(const Day& d) { return espos::lunar::daysToTet(d.d, d.m, d.y); }

// A saying a day. English is built in; a language pack brings its own list ("@quotes", any length): Vietnamese has the
// ca dao and tục ngữ of web/app/src/lunar.ts in the same order.
namespace {
// i18n: sayings and proverbs of your language, one a day on the calendar widgets; any number of them, short ones
const char* const QUOTES_EN[] = {
    "Actions speak louder than words",
    "All good things come to those who wait",
    "A friend in need is a friend indeed",
    "Better late than never",
    "Practice makes perfect",
    "Where there's a will, there's a way",
    "Rome wasn't built in a day",
    "Slow and steady wins the race",
    "Every cloud has a silver lining",
    "Look before you leap",
    "An apple a day keeps the doctor away",
    "Early to bed and early to rise makes a man healthy, wealthy and wise",
    "The early bird catches the worm",
    "Honesty is the best policy",
    "Knowledge is power",
    "Time is money",
    "Don't put off until tomorrow what you can do today",
    "Little strokes fell great oaks",
    "Many hands make light work",
    "A journey of a thousand miles begins with a single step",
    "Prevention is better than cure",
    "Two heads are better than one",
    "Birds of a feather flock together",
    "Still waters run deep",
    "Laughter is the best medicine",
    "Home is where the heart is",
    "There's no place like home",
    "Where there's smoke, there's fire",
    "Make hay while the sun shines",
    "When in Rome, do as the Romans do",
    "Don't judge a book by its cover",
    "The pen is mightier than the sword",
    "Fortune favours the bold",
    "Necessity is the mother of invention",
    "A penny saved is a penny earned",
    "You reap what you sow",
    "Patience is a virtue",
    "Kind words cost nothing",
    "One good turn deserves another",
    "Great minds think alike",
    "It's never too late to learn",
    "Failure is the mother of success",
    "Constant dripping wears away the stone",
    "Learning is a treasure that follows its owner everywhere",
    "Good things take time",
    "Small steps every day",
};
}  // namespace
const i18n::List QUOTES = {"@quotes", QUOTES_EN, sizeof QUOTES_EN / sizeof QUOTES_EN[0]};

const char* quoteOfDay(const Day& d) { return QUOTES[julian(d) % QUOTES.size()]; }


// ---------------------------------------------------------------- news
JsonArrayConst newsItems() {
  JsonObjectConst n = desk::dash()["news"];
  return n["items"].isNull() ? n["top"].as<JsonArrayConst>() : n["items"].as<JsonArrayConst>();
}

String newsTitle(JsonVariantConst v) { return v.is<const char*>() ? String(v.as<const char*>()) : String(v["title"] | ""); }

// ---------------------------------------------------------------- numbers
String num(double v, int decimals) {
  char buf[32];
  snprintf(buf, sizeof buf, "%.*f", decimals, fabs(v));
  String in = buf, intPart = in, frac;
  const int dot = in.indexOf('.');
  if (dot >= 0) intPart = in.substring(0, dot), frac = in.substring(dot + 1);
  String out;
  for (int i = 0; i < (int)intPart.length(); i++) {
    if (i && (intPart.length() - i) % 3 == 0) out += i18n::groupSep();
    out += intPart[i];
  }
  if (frac.length()) out += i18n::decSep() + frac;
  return (v < 0 ? "−" : "") + out;
}

String signedNum(double v, int decimals, const char* suffix) { return (v > 0 ? "+" : "") + num(v, decimals) + suffix; }

uint32_t trendColor(double v, bool onDark) {
  if (onDark) return v > 0 ? 0x34d399 : v < 0 ? 0xf87171 : 0x94a3b8;
  return v > 0 ? 0x16a34a : v < 0 ? 0xdc2626 : 0x6b7280;
}

std::vector<String> codesOf(const String& option, const char* def) {
  String s = option.length() ? option : String(def);
  std::vector<String> out;
  int start = 0;
  while (start <= (int)s.length()) {
    int comma = s.indexOf(',', start);
    if (comma < 0) comma = s.length();
    String c = s.substring(start, comma);
    c.trim();
    if (c.length()) out.push_back(c);
    start = comma + 1;
  }
  return out;
}

std::vector<Quote> quotes(const char* kind, const String& codes) {
  std::vector<Quote> out;
  JsonArrayConst items = desk::dash()[kind]["items"];
  const String k = kind;
  auto make = [&](JsonObjectConst x) {
    Quote q;
    if (k == "gold") {
      const double s = x["s"] | 0.0, dd = x["d"] | 0.0;
      q.code = x["c"] | "";
      q.name = x["n"] | q.code.c_str();
      q.value = num(s, 1);
      q.pct = dd;
      q.change = x["d"].isNull() ? "" : signedNum(dd, 1);
      q.unit = T_("million/tael");  // i18n: gold price unit: million dong per tael (triệu/lượng)
    } else if (k == "fx") {
      const double s = x["s"] | 0.0;
      q.code = x["c"] | "";
      q.name = q.code;
      q.value = num(s, s < 1000 ? 2 : 0);
      q.pct = 0;
      q.unit = T_("selling, dong");  // i18n: exchange rate: the bank's selling price in Vietnamese dong (bán ra, đồng)
    } else {
      const double p = x["p"] | 0.0, pc = x["pc"] | 0.0;
      q.code = x["s"] | "";
      q.name = q.code == "VNINDEX" ? "VN-Index" : q.code;
      q.value = (k == "crypto" ? "$" : "") + num(p, p >= 1000 ? (k == "crypto" ? 0 : 2) : 2);
      if (k == "stocks" && p >= 1000) q.value = num(p, 1);
      q.pct = pc;
      q.change = signedNum(pc, 1, "%");
      q.unit = k == "crypto" ? "USD" : T_("points");  // i18n: a stock index's unit
    }
    q.color = trendColor(q.pct);
    return q;
  };
  if (codes.length()) {
    for (const String& c : codesOf(codes, ""))
      for (JsonObjectConst x : items) {
        const char* xc = k == "gold" || k == "fx" ? (x["c"] | "") : (x["s"] | "");
        if (c == xc) out.push_back(make(x));
      }
  } else {
    for (JsonObjectConst x : items) out.push_back(make(x));
  }
  return out;
}

// ---------------------------------------------------------------- JSON settings
bool loadJson(const char* key, JsonDocument& doc) {
  const String s = desk::setting(key, "");
  if (!s.length()) return false;
  return deserializeJson(doc, s) == DeserializationError::Ok;
}

void saveJson(const char* key, const JsonDocument& doc) {
  String s;
  serializeJson(doc, s);
  desk::setSetting(key, s);
}

String newId() {
  static uint32_t n = 0;
  char b[12];
  snprintf(b, sizeof b, "%lx%lx", (unsigned long)(time(nullptr) & 0xfffff), (unsigned long)(++n & 0xff));
  return b;
}

}  // namespace data
