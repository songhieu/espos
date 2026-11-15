#include "Lunar.h"

#include <cmath>

namespace inkdesk::lunar {
namespace {

constexpr double PI = 3.14159265358979323846;

void jdToDate(const int jd, int& dd, int& mm, int& yy) {
  int a, b, c;
  if (jd > 2299160) {
    a = jd + 32044;
    b = (4 * a + 3) / 146097;
    c = a - (b * 146097) / 4;
  } else {
    b = 0;
    c = jd + 32082;
  }
  const int d = (4 * c + 3) / 1461;
  const int e = c - (1461 * d) / 4;
  const int m = (5 * e + 2) / 153;
  dd = e - (153 * m + 2) / 5 + 1;
  mm = m + 3 - 12 * (m / 10);
  yy = b * 100 + d - 4800 + m / 10;
}

double newMoon(const int k) {
  const double T = k / 1236.85;
  const double T2 = T * T;
  const double T3 = T2 * T;
  const double dr = PI / 180;
  double jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
  jd1 += 0.00033 * std::sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
  const double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
  const double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
  const double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
  double c1 = (0.1734 - 0.000393 * T) * std::sin(M * dr) + 0.0021 * std::sin(2 * dr * M);
  c1 = c1 - 0.4068 * std::sin(Mpr * dr) + 0.0161 * std::sin(dr * 2 * Mpr);
  c1 = c1 - 0.0004 * std::sin(dr * 3 * Mpr);
  c1 = c1 + 0.0104 * std::sin(dr * 2 * F) - 0.0051 * std::sin(dr * (M + Mpr));
  c1 = c1 - 0.0074 * std::sin(dr * (M - Mpr)) + 0.0004 * std::sin(dr * (2 * F + M));
  c1 = c1 - 0.0004 * std::sin(dr * (2 * F - M)) - 0.0006 * std::sin(dr * (2 * F + Mpr));
  c1 = c1 + 0.0010 * std::sin(dr * (2 * F - Mpr)) + 0.0005 * std::sin(dr * (2 * Mpr + M));
  const double deltat = T < -11 ? 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3
                                : -0.000278 + 0.000265 * T + 0.000262 * T2;
  return jd1 + c1 - deltat;
}

int newMoonDay(const int k, const double tz) { return static_cast<int>(std::floor(newMoon(k) + 0.5 + tz / 24)); }

double sunLong(const double jdn) {
  const double T = (jdn - 2451545.0) / 36525;
  const double T2 = T * T;
  const double dr = PI / 180;
  const double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
  const double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
  double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(dr * M);
  DL = DL + (0.019993 - 0.000101 * T) * std::sin(dr * 2 * M) + 0.000290 * std::sin(dr * 3 * M);
  double L = (L0 + DL) * dr;
  L = L - PI * 2 * std::floor(L / (PI * 2));
  return L;
}

int sunLongitude(const int dayNumber, const double tz) {
  return static_cast<int>(std::floor(sunLong(dayNumber - 0.5 - tz / 24) / PI * 6));
}

int lunarMonth11(const int yy, const double tz) {
  const int off = julianDay(31, 12, yy) - 2415021;
  const int k = static_cast<int>(std::floor(off / 29.530588853));
  int nm = newMoonDay(k, tz);
  if (sunLongitude(nm, tz) >= 9) nm = newMoonDay(k - 1, tz);
  return nm;
}

int leapMonthOffset(const int a11, const double tz) {
  const int k = static_cast<int>(std::floor((a11 - 2415021.076998695) / 29.530588853 + 0.5));
  int i = 1;
  int arc = sunLongitude(newMoonDay(k + i, tz), tz);
  int last;
  do {
    last = arc;
    i++;
    arc = sunLongitude(newMoonDay(k + i, tz), tz);
  } while (arc != last && i < 14);
  return i - 1;
}

}  // namespace

int julianDay(const int dd, const int mm, const int yy) {
  const int a = (14 - mm) / 12;
  const int y = yy + 4800 - a;
  const int m = mm + 12 * a - 3;
  int jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  if (jd < 2299161) jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
  return jd;
}

Date fromSolar(const int dd, const int mm, const int yy, const int tzHours) {
  const double tz = tzHours;
  const int dayNumber = julianDay(dd, mm, yy);
  const int k = static_cast<int>(std::floor((dayNumber - 2415021.076998695) / 29.530588853));
  int monthStart = newMoonDay(k + 1, tz);
  if (monthStart > dayNumber) monthStart = newMoonDay(k, tz);
  int a11 = lunarMonth11(yy, tz);
  int b11 = a11;
  Date out;
  if (a11 >= monthStart) {
    out.year = yy;
    a11 = lunarMonth11(yy - 1, tz);
  } else {
    out.year = yy + 1;
    b11 = lunarMonth11(yy + 1, tz);
  }
  out.day = dayNumber - monthStart + 1;
  const int diff = static_cast<int>(std::floor((monthStart - a11) / 29.0));
  out.month = diff + 11;
  if (b11 - a11 > 365) {
    const int leapDiff = leapMonthOffset(a11, tz);
    if (diff >= leapDiff) {
      out.month = diff + 10;
      if (diff == leapDiff) out.leap = true;
    }
  }
  if (out.month > 12) out.month -= 12;
  if (out.month >= 11 && diff < 4) out.year--;
  return out;
}

bool toSolar(const Date& lunar, int& dd, int& mm, int& yy, const int tzHours) {
  const double tz = tzHours;
  int a11, b11;
  if (lunar.month < 11) {
    a11 = lunarMonth11(lunar.year - 1, tz);
    b11 = lunarMonth11(lunar.year, tz);
  } else {
    a11 = lunarMonth11(lunar.year, tz);
    b11 = lunarMonth11(lunar.year + 1, tz);
  }
  const int k = static_cast<int>(std::floor(0.5 + (a11 - 2415021.076998695) / 29.530588853));
  int off = lunar.month - 11;
  if (off < 0) off += 12;
  if (b11 - a11 > 365) {
    const int leapOff = leapMonthOffset(a11, tz);
    int leapMonth = leapOff - 2;
    if (leapMonth < 0) leapMonth += 12;
    if (lunar.leap && lunar.month != leapMonth) return false;
    if (lunar.leap || off >= leapOff) off += 1;
  } else if (lunar.leap) {
    return false;
  }
  const int monthStart = newMoonDay(k + off, tz);
  jdToDate(monthStart + lunar.day - 1, dd, mm, yy);
  return true;
}

std::string yearName(const int lunarYear) {
  static const char* const kCan[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
  static const char* const kChi[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
  return std::string(kCan[(lunarYear + 6) % 10]) + " " + kChi[(lunarYear + 8) % 12];
}

int daysToTet(const int dd, const int mm, const int yy, const int tz) {
  const Date today = fromSolar(dd, mm, yy, tz);
  const int now = julianDay(dd, mm, yy);
  for (int y = today.year; y <= today.year + 1; y++) {
    Date tet;
    tet.day = 1;
    tet.month = 1;
    tet.year = y;
    int td, tm, ty;
    if (!toSolar(tet, td, tm, ty, tz)) continue;
    const int diff = julianDay(td, tm, ty) - now;
    if (diff >= 0) return diff;
  }
  return -1;
}

}  // namespace inkdesk::lunar
