#pragma once

#include <string>

// Vietnamese lunar calendar (Ho Ngoc Duc's algorithm, the one the EspOS server uses), for
// the month and countdown widgets. Time zone in hours (Vietnam: 7).
namespace espos::lunar {

struct Date {
  int day = 0;
  int month = 0;
  int year = 0;
  bool leap = false;
};

Date fromSolar(int dd, int mm, int yy, int tz = 7);
// Solar date of a lunar date; false when it does not exist (a leap month that year lacks).
bool toSolar(const Date& lunar, int& dd, int& mm, int& yy, int tz = 7);
// "Bính Ngọ" for a lunar year.
std::string yearName(int lunarYear);
// Days from a solar date to the next lunar new year (0 on the day itself).
int daysToTet(int dd, int mm, int yy, int tz = 7);
// Julian day number of a solar date (for day differences).
int julianDay(int dd, int mm, int yy);

}  // namespace espos::lunar
